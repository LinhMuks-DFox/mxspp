# MXScript Object Model & ARC (v1)

Authoritative design for MXScript's user-defined-object runtime and its automatic reference
counting, as implemented in progress11 (data classes + methods + operator overloading, single class,
no inheritance). This complements `type_system.md` (§4 classes) and records the *implementation*
contract between the backend codegen and the C++ runtime compiled into `core.bc`.

## 1. Everything is an `MXObject`

Every runtime value is a `core::MXObject*` to a real C++ subclass: `MXInteger` (bignum), `MXString`,
`MXFloat`, `MXBoolean`, `MXNil`, `MXArrayList`, `MXError`, and — for user classes — `MXInstance`.
`MXObject` carries a virtual `repr()`/`is_truthy()`/`equals()`/`get_hash_code()`, a reference count,
and a destruction guard (see §4).

## 2. Type descriptor — `MXClassInfo`

Each user class has a per-class **type descriptor**, `core::MXClassInfo` (`include/mxspp/core/
MXClassInfo.h`), the stable layout contract shared by `core.bc` and codegen
(LLVM `{ ptr, ptr, ptr, i64, ptr }`):

```cpp
struct MXClassInfo {
    const char*        name;        // class name (RTTI / is_type / repr)
    const MXClassInfo* parent;      // base class; nullptr in v1 (inheritance-ready)
    void             (*destructor)(MXObject* self); // user ~Class() trampoline, or nullptr
    std::int64_t       vtable_len;  // = MX_SLOT_RESERVED_COUNT + #whole-program method selectors
    void* const*       vtable;      // -> constant array of fn ptrs (opaque; caller casts)
};
```

Codegen emits, per class `C`, **constant globals**: the name string, the `vtable` array, and the
`MXClassInfo` pointing at them. Because they are constants, when a receiver's runtime type is known
at a call site, LLVM folds `load classinfo → load vtable → load vtable[slot]` to the concrete
function and inlines across the mxs/`core.bc` boundary (the progress09 D6 win).

### Vtable slots
A vtable slot is **null** when the class does not override it. Low slots are **reserved**
(`enum MXSlot`) for the builtin operators and `Object` virtuals; user method names get
whole-program selector slots `>= MX_SLOT_RESERVED_COUNT` (one global map for all classes, feasible
because `compile_core` sees the whole program).

## 3. Instances, dispatch, operators

- **`MXInstance`** holds `const MXClassInfo* classinfo_` and insertion-ordered, **owned** fields.
  `class_name()`/`is_type`/`repr()` read the classinfo.
- **Construction** `C(args)` lowers to a normal call of the class-named constructor function, which
  does `self = mxs_instance_new(&C.classinfo)`, binds `self` + ctor params, runs the body, and
  returns `self`.
- **Field access** `obj.f` → `mxs_get_attr` (returns the field, retained); `obj.f = v` /
  `self.f = v` → `mxs_set_attr` (the instance adopts `v`). Unset field reads as `nil`.
- **Method call** `obj.m(args)` → load `mxs_object_classinfo(obj)->vtable[slot_of("m")]` and call
  `(self, args…)`. Methods are emitted as `C$m(self, params…)`.
- **Operator overloading**: codegen still lowers `a + b` to `mxs_op_add(a, b)`; the *runtime*
  `mxs_op_add` checks whether `a` is an instance whose class overrides `MX_SLOT_OP_ADD` and, if so,
  dispatches to the user `operator+` (`C$op<slot>`); otherwise it falls back to the builtin
  numeric/string logic. Same for `- * / % ** < <= > >= == != ` and unary `-` / `!`.
- **`match` on class type** (`case x: C =>`): `mxs_is_type` matches `instance.classinfo->name`.

## 4. Automatic Reference Counting (ARC)

Object lifetime is reference-counted (`MXObject::retain`/`release`; the JIT ABI is
`mxs_retain`/`mxs_release`). A freshly constructed object has count 1. When the count reaches zero
the C++ destructor runs; for `MXInstance` that runs the user `~Class()` trampoline (while the fields
are still alive) and then releases the fields.

**Destruction guard.** `release()` sets a `destroying_` flag before `delete this`, and ignores
further releases while set — so retain/release performed *inside* a destructor (e.g. a user
`~Class()` reading `self`, or the teardown of `self`'s binding cell) cannot drive the count back to
zero and re-enter destruction.

### Ownership protocol (codegen ⇄ runtime)
Every value `expr()` yields to codegen is **owned (+1)**. There are two calling conventions:

- **Callee-owned** — calls to user mxs functions / methods / constructors. Their parameter bindings
  *adopt* the arguments (and release them at the callee's scope exit), so the **caller does not
  release** the args.
- **Caller-owned** — operators (`mxs_op_*`), accessors used as operands, `@@foreign` C calls, and
  container element stores. The callee *borrows* (retaining its own reference if it stores the
  value), so the **caller releases** the argument after the call.

Producers of fresh values return +1; accessors that hand back an existing object
(`mxs_lvalue_rvalue`, `mxs_get_attr`, `mxs_index_get`, `mxs_arraylist_get`) **retain** before
returning. Owners hold strong references: a binding cell (`MXLeftValue`) adopts its r-value and
releases it on update/destruction; an `MXInstance`/`MXArrayList` retains stored elements and releases
them on overwrite/destruction.

**Scopes.** Codegen keeps a stack of lexical scopes; each binding cell is recorded in its scope and
**deleted on scope exit** (releasing its r-value — this is what fires destructors deterministically
at block / function / loop-iteration boundaries). A `return` releases every active scope's cells
after evaluating (and retaining, by transfer) the return value.

### Verification
- C++ unit test `instance_destructor_and_field_arc` asserts the live-object count
  (`MXPopulationManager::population_count()`) returns to its baseline after building and dropping an
  instance — no leak, no double-free; and that `~Class()` fires exactly once.
- Integration: `example/examples/oop_dtor.mxs` shows deterministic destruction order across block
  and function scopes; a loop that binds + drops an instance per iteration destroys each one at the
  iteration's scope exit (no accumulation).

### Known v1 gaps
- `break` / `continue` jump out without running the intervening scope releases, so binding cells
  created in a loop body *before* a `break`/`continue` are not released (a bounded leak; revisit
  when adding non-local-exit scope cleanup).
- Inheritance, interfaces, `static` members, and a `mxs_no_such_method` thunk for truly-dynamic
  receivers are out of v1 (the design is inheritance-ready: `MXClassInfo.parent` + same-slot
  overrides).
