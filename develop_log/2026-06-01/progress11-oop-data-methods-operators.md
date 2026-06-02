# Progress 11 — OOP v1: data classes + methods + operator overloading (vtable) + object lifetime (ARC)
id: 2026-06-01/progress11
date: 2026-06-01
author: human+ai
status: active
refs: [2026-05-31/progress09, 2026-05-31/progress05, 2026-05-31/progress06, 2026-05-31/progress07]
supersedes: 2026-05-31/progress09 (its "temporaries currently leak" TODO — superseded by D-LIFE's ARC protocol)
commits: []
files:
  - include/mxspp/core/MXClassInfo.h        # NEW — shared type-descriptor layout + reserved slots (D3/D7 contract)
  - include/mxspp/core/MXInstance.h, src/core/MXInstance.cpp   # instance: classinfo ptr + owned fields + dtor
  - include/mxspp/core/MXObject.h, src/core/MXObject.cpp       # release()-at-zero runs MXInstance dtor; rc stays
  - include/mxspp/core/MXLeftValue.h, src/core/MXLeftValue.cpp # binding cell owns via release-semantics
  - src/core/MXOps.cpp                       # operator routing via vtable; retain-on-return for accessors
  - src/core/MXArrayList.cpp                 # element ARC (retain on store, release on remove/destroy)
  - src/core/CMakeLists.txt                  # add MXInstance to lib + CORE_BC_SOURCES
  - include/mxspp/frontend/ast.h, src/frontend/parser.cpp     # method-call AST (receiver on FunctionCall)
  - src/backend/codegen.cpp                  # ClassDef lowering, vtable dispatch, member-assign, ARC insertion
  - docs/object_model.md                     # NEW — authoritative OOP + ARC design
  - test/core_test.cpp, example/examples/*.mxs

## Goal
Implement MXScript OOP **v1**: user classes with fields, a constructor, **methods (vtable dispatch)**,
and **operator overloading**, for a **single class (no inheritance)**; and wire **user-object lifetime
(ARC)** so `~Class()` runs and fields are released when an object's reference count hits zero. The
target is to run, end to end through `mxs run-core`, a `Point`/`Vector`-style program with methods and
`a + b` operator overloading, plus a destructor demo — on real C++ `MXInstance` objects compiled into
`core.bc` and JIT-linked (the progress09 D6 model).

Boundary (NOT in v1): inheritance, `override`, base-ctor chaining, interfaces, `static` members,
generics, `decimal`/`complex`, `Dict`/`Tuple`/static `Array`. The design below is inheritance-ready so
those are additive later.

## Context / Motivation
Picks up [progress09](../2026-05-31/progress09-object-model-cpp.md) step ④, which left the OOP
"data-class slice" started (`MXInstance.h` written) but unfinished, and recorded the dispatch decision
(**vtable**, not a registry) + the type-descriptor design. Mux confirmed the type_system design is
complete and asked what else needs a decision; the three decisions below were taken this session
(2026-06-01). The AST (`ClassDef`/`MethodDef`/`OperatorDef`/`ConstructorDef`/`MemberExpr`/…) and the
parser already build a full class AST (task04); `compile_core` currently ignores `ClassDef` entirely.

## Decisions

### D-SCOPE — v1 = data classes + methods + operator overloading; single class, no inheritance — Mux, 2026-06-01
- Decision: this implementation pass delivers, for a **single class** (no base/interface):
  fields, constructor, `obj.field` read + `self.field = v` / `obj.field = v` write, `C(args)`
  construction, `match` on the class type (`case x: C =>`), **methods** `obj.m(args)` via vtable
  dispatch, and **operator overloading** (`a + b` → user `operator+`, etc.).
- Why: substantial, demonstrable OOP in one coherent pass; the vtable design (progress09) is already
  "inheritance-ready" (override keeps the same global slot; subclass vtable = parent's copy + overrides;
  `MXClassInfo.parent` already present), so deferring inheritance costs little later. Mux flagged
  inheritance as "still thinking" — kept out of v1.
- Impact: codegen grows a `ClassDef` path; runtime gains `MXClassInfo` + a real `MXInstance`. No grammar
  change for classes (already parses); a small AST/parser change IS needed for **method calls** (below).
- Alternatives considered: "data classes only" (smaller, but methods+operators are the interesting half);
  "+ single inheritance" (Mux deferred); "everything incl. interfaces/ARC/statics" (too large, under-designed).

### D-LIFE — wire user-object lifetime (ARC) now; uniform "+1" ownership protocol — Mux, 2026-06-01
- Decision: user objects get **deterministic destruction** now. When an `MXObject`'s reference count
  reaches zero, its C++ destructor runs; for an `MXInstance` that means running the user `~Class()`
  trampoline (if defined) and **releasing its fields**. This is wired through codegen via the uniform
  ownership protocol in §"ARC protocol" below (supersedes progress09's "temporaries currently leak" TODO).
- Why: Mux chose to make destructors work this pass (over deferring). The only **memory-safe** way to make
  `~Class()` fire deterministically is a consistent retain/release protocol — a partial "adopt-without-
  retain" scheme breaks on aliasing (`let q = p;`) with a double-free. So we adopt the full protocol.
- Impact: `MXObject::release()` (already deletes at zero) now also drives `~MXInstance` → user dtor + field
  release; `MXLeftValue` switches to release-semantics; accessors that return a borrow must **retain**
  before returning; codegen inserts retain/release at the points the protocol names. Verified empirically
  by an `MXPopulationManager` live-object count returning to baseline (no leak, no double-free).
- Acceptable v1 floor (if the full codegen ARC proves too risky in one pass): release **binding cells at
  scope exit** + **instance fields at destruction** only (so `let p = C()` destructs at scope end — the
  demonstrable case — while pure unbound temporaries may still leak, as today). The leak-count test is
  relaxed accordingly and the gap is logged. Target remains the full protocol.
- Alternatives considered: "defer, leak like temporaries" (AI-recommended, Mux declined); arena/tracing GC
  (rejected in progress09 D8 in favor of rc).

### D-BUILD — build & verify locally against vendored LLVM 20.1.8 — Mux, 2026-06-01
- Decision: build and verify on this host using a locally **vendored LLVM 20.1.8** (`project_init.py`),
  matching the canonical (Docker) LLVM version, then `rebuild.py`.
- Why: keeps the toolchain/LLVM version aligned with the verified-in-Docker baseline; avoids version drift.
- Impact / BLOCKER discovered (see Issues): `project_init.py`'s `ldconfig -p` libc++ check fails on this
  WSL host even though Homebrew libc++ exists, so the LLVM download never runs; and the official LLVM 20
  Linux prebuilt is likely libstdc++-ABI while the project forces `-stdlib=libc++` (ABI-mismatch risk when
  linking libLLVM C++ APIs). task05 owns resolving this before any code is verified.
- Alternatives considered: system Homebrew LLVM 22 (fastest, ABI-consistent all-Homebrew, but needs
  codegen API tweaks e.g. `CreateGlobalStringPtr`→`CreateGlobalString` and drifts from canonical 20);
  "write code, Mux builds in Docker" (kept as fallback if task05 can't get a clean local build).

## Design contract (authoritative; tasks reference this)

### Type descriptor — `MXClassInfo` (shared header, the D3/D7 stable contract)
New `include/mxspp/core/MXClassInfo.h`, included by **both** `core.bc` sources and codegen, with a layout
both sides must agree on (LLVM `{ ptr, ptr, ptr, i64, ptr }`):
```cpp
namespace mxs::core {
    struct MXObject;
    struct MXClassInfo {
        const char*          name;        // class name (RTTI / is_type / repr)
        const MXClassInfo*   parent;      // nullptr in v1 (inheritance-ready)
        void               (*destructor)(MXObject* self); // nullable: user ~Class() trampoline
        std::int64_t         vtable_len;  // = kReservedSlots + #global-method-selectors
        void* const*         vtable;      // -> constant array of fn ptrs (opaque; caller casts)
    };
    // Reserved low slots, shared by codegen and core.bc (operators + Object virtuals).
    enum MXSlot : std::int64_t {
        MX_SLOT_OP_ADD = 0, MX_SLOT_OP_SUB, MX_SLOT_OP_MUL, MX_SLOT_OP_DIV, MX_SLOT_OP_MOD, MX_SLOT_OP_POW,
        MX_SLOT_OP_LT, MX_SLOT_OP_LE, MX_SLOT_OP_GT, MX_SLOT_OP_GE, MX_SLOT_OP_EQ, MX_SLOT_OP_NE,
        MX_SLOT_OP_NEG, MX_SLOT_OP_NOT, MX_SLOT_OP_INDEX_GET, MX_SLOT_OP_INDEX_SET,
        MX_SLOT_REPR, MX_SLOT_HASH, MX_SLOT_EQUALS,
        MX_SLOT_RESERVED_COUNT            // first user-method selector slot
    };
}
```
- **v1 slot policy:** a vtable slot is **null** when not overridden. Operator routing checks `!= null`.
  Method calls are resolved against the (single, known) class at compile time, so an absent method is a
  codegen error — no null-call crash. (A `mxs_no_such_method` thunk for truly-dynamic receivers is
  deferred with inheritance.)
- Codegen emits, per class `C`: a constant name string; a constant `@C.vtable` array
  (size `vtable_len`; operator slots filled with `C`'s operator fns, method-selector slots with `C`'s
  method fns, the rest null); a constant `@C.classinfo` (`MXClassInfo`) pointing at them, with
  `destructor` = `C$dtor` trampoline or null. Constant globals are what let LLVM fold dispatch to a
  direct call + cross-boundary inline when the receiver type is statically known (progress09 D6).

### Instance — `MXInstance`
- Holds `const MXClassInfo* classinfo_` (set by the constructor); `class_name()` = `classinfo_->name`.
- Fields: insertion-ordered, **owned** (strong refs). `set_field` adopts the value's +1 (no extra retain),
  releases the old value if overwriting; `~MXInstance` runs `classinfo_->destructor(this)` if non-null,
  then releases every field.
- ABI: `mxs_instance_new(const MXClassInfo*) -> MXObject*` (rc 1); `mxs_set_attr(MXObject* self,
  const char* name, MXObject* v) -> void` (adopts v); `mxs_get_attr` extended to return a **retained**
  (+1) field for instances; `mxs_is_type` extended so `case x: C =>` matches `classinfo_->name == "C"`;
  `mxs_object_classinfo(MXObject*) -> const MXClassInfo*` (instance's classinfo, else null) for operator
  routing. `repr()` stays `"ClassName(f=v, …)"`.

### Method / constructor / operator lowering (codegen, single class v1)
- **Pre-pass** over all `ClassDef`s: assign each distinct user **method name** a global selector slot
  `>= MX_SLOT_RESERVED_COUNT`; declare each class ctor as `funcs[ClassName]` `(N ptr args) -> ptr` so
  `C(args)` resolves as an ordinary call; declare each method `C$<name>(self, params…) -> ptr` and each
  operator `C$op_<x>(self, other) -> ptr`; emit the class globals (name/vtable/classinfo + `C$dtor`).
- **Constructor body**: `self = mxs_instance_new(&C.classinfo)`; bind `self` and the ctor params as
  immutable bindings; run the ctor body (so `self.x = x` lowers to member-assign → `mxs_set_attr`);
  `return self` (transfer per ARC).
- **Method body**: bind `self` = arg0, bind params, run body.
- **Method call** `obj.m(args)`: load `classinfo` from the receiver, load `vtable[slot_of("m")]`, call it
  as `(self, args…) -> ptr`. (Needs the AST/parser method-call fix below.)
- **Operator overloading**: codegen still lowers `a + b` to `mxs_op_add(a, b)` (unchanged). The *runtime*
  `mxs_op_add` does the routing: if `a` is an instance and `classinfo->vtable[MX_SLOT_OP_ADD] != null`,
  call that user `operator+`; else fall back to the existing builtin numeric/string logic. (Matches
  progress09 exactly: "mxs_op_add(lhs,rhs) checks lhs's vtable[OP_ADD_SLOT]".)
- **Member-assignment**: extend the assignment path in codegen `expr()` (today: `Identifier` /
  `IndexExpr` LHS) to handle a **`MemberExpr` LHS** → `mxs_set_attr(obj, name, rhs)`; compound
  (`self.x += v`) = get_attr → op → set_attr.

### Method-call AST/parser fix (prerequisite for `obj.m(args)`)
`to_postfix` currently builds a `FunctionCall` whose `name` is only set when the call base is an
`Identifier`; a call on a `MemberExpr` base **drops the receiver and method name** (the "Call self"
gotcha from task04). Fix: give `ast::FunctionCall` an optional `std::unique_ptr<Expression> receiver;`
(set to the `MemberExpr`'s target, with `name` = the member name) — or add a dedicated `MethodCall` node.
Codegen: `receiver` present ⇒ method dispatch; absent ⇒ named function/`@@foreign` call.

### ARC protocol (the ownership contract — runtime + codegen must both honor it)
Builds on progress09 D8 (`retain`/`release`, `mxs_retain`/`mxs_release`; fresh object = rc 1).
- **Producer rule** — every value `expr()` yields to codegen is **owned (+1)**:
  - constructors of fresh objects (literals, `mxs_op_*`, `mxs_int_*`, `mxs_str_new`, list literals, ctor
    calls, method calls) already return +1.
  - accessors that return an *existing* object (a borrow) must **retain before returning**:
    `mxs_lvalue_rvalue` (identifier read), `mxs_get_attr` (field read), `mxs_arraylist_get` /
    `mxs_index_get` (element read).
- **Consumer rule** — codegen consumes each owned value exactly once:
  - **adopt** (store into an owner; no retain, no release): `let`/bind, `self.x=v` (`mxs_set_attr`),
    `xs[i]=v`, `append`, `mxs_lvalue_update`. The owner releases it later.
  - **release** (operand / discarded / condition): after passing operands to `mxs_op_*` / a method / a
    call (callees **borrow**, don't consume), release each operand; release an `ExprStatement`'s unused
    result; release a condition value after the branch test.
  - **transfer** (return): `return e` hands the +1 to the caller — not released.
- **Owner-release rule**: a binding cell created in a scope is released at scope exit (block end / fn
  return / loop-iteration end); reassignment releases the old r-value; an `MXInstance`/`MXArrayList`
  releases owned fields/elements on destruction (and on overwrite).
- **Net**: every object hits rc 0 exactly once → its destructor runs once. `MXLeftValue` holds its
  r-value with release-semantics (adopt the +1 on construct/update, release on update/destroy) instead of
  plain `unique_ptr`-delete.
- **Verification lever**: `MXPopulationManager` tracks live `MXObject`s; expose a count and assert it
  returns to baseline after a scope/run (catches both leaks and double-frees).
- **Gotchas to watch**: release on *all* control-flow paths (match arms, `&&`/`||` short-circuit,
  break/continue, early return); don't release a value that was adopted; the returned value must survive
  scope-exit release of the binding it came from (it carries its own +1 via the retain in
  `mxs_lvalue_rvalue`).

## Defaults (assumed unless Mux objects)
- Fields are **dynamic** at runtime (`MXInstance` map); codegen does **not** yet enforce "declared-only"
  (static field checking deferred). Reading an unset field → `nil` (current `mxs_get_attr` behavior).
- The print/`repr` override method is named **`repr`** (matches the core + type_system §4.3.2; `to_string`
  in §2 is treated as an alias, not wired in v1).
- `public:` / `private:` access control: **not enforced** in v1 (parser already ignores `access_spec`).
- Overloadable operators in v1 = the set `mxs_op_*` already has: `+ - * / % ** < <= > >= == !=`, unary
  `-`/`!`, and `[]` / `[]=`. Bitwise operators are out (no `mxs_op` for them yet).
- `static` members, interfaces (grammar only supports a single `:` clause), generics: out of v1.

## Tasks
- [x] [task05 — Build environment & green baseline](tasks/task05-build-environment.md)
- [x] [task06 — OOP runtime substrate: MXClassInfo + MXInstance + ABI](tasks/task06-runtime-instance-classinfo.md)
- [x] [task07 — Object lifetime / ARC runtime substrate](tasks/task07-arc-runtime.md)
- [x] [task08 — Codegen: ClassDef lowering + vtable dispatch + member-assign](tasks/task08-codegen-oop-lowering.md)
- [x] [task09 — Codegen: ARC insertion (retain/release protocol)](tasks/task09-codegen-arc.md)
- [x] [task10 — Tests, demos, docs](tasks/task10-tests-demos-docs.md)

## Issues / Gotchas
- **Build env (D-BUILD):** vendoring failed — `project_init.py` `check_system_dependencies()` greps
  `ldconfig -p` for `libc++` and aborts; this WSL host has Homebrew libc++ (`/home/linuxbrew/.linuxbrew/
  lib/libc++.so`) but it is not in the system ld cache (no passwordless sudo to add it). And the official
  LLVM 20 Linux prebuilt is likely libstdc++-ABI vs. the project's forced `-stdlib=libc++` → linking
  libLLVM's C++ API could corrupt `std::string`/`std::vector`. task05 must resolve both (options: source
  build of LLVM 20 with libc++; patch/relax the check; or fall back to Docker / system LLVM 22).
- **Method call parse gap:** `obj.m(args)` loses its receiver today (task04 "Call self" gotcha) — fix in
  the AST/parser before codegen method dispatch (see Design contract).
- **MXClassInfo layout must match exactly** between the C++ struct (core.bc) and the codegen-emitted LLVM
  struct, or vtable/dtor reads corrupt. `core.bc` already rebuilds on any core header change (progress09
  build fix) — keep MXClassInfo.h in that header glob.
- **ARC double-free risk** is the highest-risk part; rely on the population-count assertion + running every
  existing demo (`core_fib`/`core_loops`/`core_types`/`core_list`/…) unchanged to catch regressions.

## Open / TODO (carry-over)
- Inheritance + `override` + base-ctor chaining (design is ready; `MXClassInfo.parent`, same-slot
  override). Interfaces (need multi-`:` grammar + interface itable-as-vtable-slots). `static` members
  (separate mutable area, progress09). `mxs_no_such_method` thunk for dynamic receivers.
- Full temporary-ARC if only the "v1 floor" lands; retire the flat run-obj/native legacy paths
  (progress09 remaining-work). `Matrix` demo (matrix_class.mxs) needs the `[float]` array + `make_array`
  (container work) on top of this — track separately.

## Agent log
- 2026-06-01 [ai] **OOP v1 COMPLETE & verified (task05–task10).** Status: `mxs run-core` runs user
  classes end to end on the real object model — data classes (fields, ctor, `obj.f`/`self.f=v`),
  methods via vtable dispatch (`obj.m(args)`), operator overloading (`a+b`→user `operator+`, routed
  in `mxs_op_*` through the reserved vtable slot), `match` on class type, and **deterministic
  destruction (ARC)**. Demos: `oop_point` (3/4/7), `oop_vector` (4/6/vec), `oop_dtor`
  (start/inner-before/inner-after/~Tag(2)/~Tag(1) — block then function scope). All prior demos
  unchanged; `ctest` 3/3 (core_test 19 cases incl. 3 new instance cases). 
  ARC details (task07/09): uniform +1 ownership protocol with two conventions — user
  function/method/ctor calls are **callee-owned** (params adopt; caller doesn't release), while
  operators / accessors-as-operands / `@@foreign` calls / container stores are **caller-owned**
  (callee borrows+retains; caller releases). Codegen tracks a scope stack and deletes binding cells
  on block/function/loop-iteration exit (this fires `~Class()`); `return` releases all scopes after
  retaining the transferred value. Two correctness fixes found by running: (1) **destructor
  reentrancy** — added a `destroying_` guard in `MXObject::release()` so retain/release inside a
  destructor can't re-enter `delete`; (2) **operator-overload double-release** — `user_binop`/
  `user_unop` retain operands across the user-operator call, since `mxs_op_*` borrows but the user
  operator (a method) adopts. `docs/object_model.md` written (authoritative). Known v1 gap:
  `break`/`continue` skip intervening scope releases (bounded leak of loop-body bindings before the
  jump). **Not committed** (awaiting Mux review).
- 2026-06-01 [ai] **task05 (build env) — resolved on this WSL host (libc++, vendored LLVM 20).**
  Journey + the host-specific recipe (committed toolchain unchanged; all host bits are configure-time
  `-D` flags or a separate bitcode emitter):
  1. `project_init.py`'s `ldconfig -p` libc++ check is a false negative here (Homebrew libc++ exists,
     not in the system ld cache; no passwordless sudo). Bypassed by calling its download helpers
     directly; vendored PEGTL (renamed `lib/PEGTL-3.2.7` → `lib/pegtl`).
  2. Homebrew clang 22 + the project's forced `-stdlib=libc++` need Homebrew's lib on the link path:
     add `-L/home/linuxbrew/.linuxbrew/lib -Wl,-rpath,...` to EXE/SHARED linker flags. Also installed
     `pkgconf` (`brew install pkgconf`) and pointed `CMAKE_PREFIX_PATH` at Homebrew `opt/{zlib,zstd,libxml2}`.
  3. **Prebuilt LLVMs are libstdc++-ABI** (official 20 *and* Homebrew 22) → can't link with our libc++
     code (LLVM APIs take `std::__1::optional<…>` etc.; libc++/libstdc++ mangle differently). So a
     libc++ LLVM is mandatory → **built LLVM 20.1.8 from source with `-DLLVM_ENABLE_LIBCXX=ON`** (drop
     RUNTIMES; X86 only; `cmake --build` capped at `CMAKE_BUILD_PARALLEL_LEVEL=10` for 15 GB RAM),
     installed to `lib/llvm`. mxspp then builds + links clean against it (LLVM 20.1.8).
  4. One LLVM 20→22 API guard: `jit.cpp` `setTargetTriple` (LLVM 21+ takes a `Triple`) — `#if
     LLVM_VERSION_MAJOR >= 21`; keeps both 20 (canonical) and 22 compiling. `CreateGlobalStringPtr` is
     only deprecated in 22, still compiles.
  5. **Bitcode version skew:** core.bc/runtime.bc were emitted by Homebrew clang 22 → LLVM-22 bitcode,
     which the JIT's LLVM 20 can't read (`Unknown attribute kind (105), Producer LLVM22 Reader LLVM20`).
     Fix: decoupled the bitcode emitter into a `MXS_BC_CXX` CMake cache var (defaults to the host
     compiler — portable; the project SHOULD express "bitcode emitter LLVM <= JIT LLVM"), and point it
     (+ `MXS_LLVM_LINK`) at an **LLVM-20 clang/llvm-link** from the official 20 prebuilt (used only as a
     bitcode emitter — its own libstdc++ ABI is irrelevant; it just emits LLVM-20 bitcode whose libc++
     calls resolve from the process at JIT time). Also made `jit.cpp` print the bitcode parse error.
  Configure recipe (host-specific, not committed): toolchain.cmake + `CMAKE_PREFIX_PATH` (Homebrew
  zlib/zstd/libxml2) + libc++ link path + `-DMXS_BC_CXX=lib/llvm20-tools/bin/clang++`
  + `-DMXS_LLVM_LINK=lib/llvm20-tools/bin/llvm-link`.
- 2026-06-01 [ai] **task06 + task08 (no-ARC) implemented & building.** Full project builds clean
  against LLVM 20 (libc++); `core_test` 19/19 (incl. 3 new instance cases: fields/repr/type,
  destructor+field-ARC no-leak via population count, operator-overload vtable dispatch). MXClassInfo.h
  (shared layout + reserved slots), real MXInstance (classinfo + owned fields + dtor), instance ABI
  (`mxs_instance_new`/`mxs_set_attr`/`mxs_object_classinfo`, instance-aware `mxs_get_attr`/`mxs_is_type`),
  operator routing in `mxs_op_*` via vtable slots; codegen ClassDef lowering (ctor/method/operator/dtor
  fns, vtable+classinfo const globals, whole-program selectors, method dispatch, member-assign) + the
  method-call AST/parser fix (FunctionCall.receiver). run-core of the OOP demos pending the bitcode-skew
  fix above. **ARC in codegen (task07/09) not yet wired — instances/temporaries leak until then.**
- 2026-06-01 [ai] Read progress09 + type_system + the full OOP substrate (grammar/parser/AST already build
  `ClassDef` and members; `compile_core` ignores classes; `MXInstance.h` exists, `.cpp` does not).
  Surfaced the remaining open decisions to Mux; Mux decided **D-SCOPE** (data+methods+operators, single
  class, no inheritance), **D-LIFE** (wire user-object ARC now), **D-BUILD** (vendor LLVM 20 locally).
  Recorded the full design contract (MXClassInfo + reserved slots, instance/ABI, lowering shapes,
  method-call AST fix, ARC ownership protocol) and broke the work into task05–task10. Hit the D-BUILD
  vendoring blocker (libc++ ldconfig check + LLVM-prebuilt ABI risk) — captured under Issues for task05.
  **No implementation code written yet** (Mux: write progress+tasks now, implement later).
