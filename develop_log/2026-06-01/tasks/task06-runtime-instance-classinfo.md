# Task 06 — OOP runtime substrate: MXClassInfo + MXInstance + ABI
id: 2026-06-01/task06
parent: 2026-06-01/progress11
status: done
owner: code_agent

## Objective
Provide the real C++ runtime for user objects: the `MXClassInfo` type descriptor (shared layout +
reserved slots) and a complete `MXInstance` (classinfo pointer + owned fields), with the `extern "C"`
ABI codegen targets, compiled into `core.bc`.

## Scope
In:
- `MXClassInfo.h` (NEW): the struct layout + `MXSlot` reserved-slot enum (the D3/D7 contract).
- `MXInstance`: store `const MXClassInfo* classinfo_`; insertion-ordered **owned** fields; `repr()`.
- ABI: `mxs_instance_new`, `mxs_set_attr`, `mxs_object_classinfo`; extend `mxs_get_attr` + `mxs_is_type`
  for instances. Add to `core` lib + `CORE_BC_SOURCES`.
Out:
- Field ARC details that belong to the lifetime contract are shared with task07 (retain/release of
  fields). Implement field ownership here per the protocol; task07 owns the cross-cutting accessor fixes.
- Codegen / vtable emission (task08); inheritance (`parent` stays nullptr).

## Inputs (read first, priority order)
1. `develop_log/2026-06-01/progress11-…md` §"Design contract" (MXClassInfo, Instance, slots) + §"ARC protocol".
2. `include/mxspp/core/MXInstance.h` (current WIP), `include/mxspp/core/MXError.h` + `src/core/MXError.cpp`
   (a complete type to mirror: ctor/repr/get_rtti pattern), `src/core/MXOps.cpp` (`mxs_get_attr`,
   `mxs_is_type` to extend), `include/mxspp/core/MXObject.h` (rc API), `include/mxspp/core/MXType.h`.
3. `src/core/CMakeLists.txt` (lib sources + `CORE_BC_SOURCES` + header glob).

## Deliverables
- `include/mxspp/core/MXClassInfo.h` — `struct MXClassInfo { const char* name; const MXClassInfo* parent;
  void(*destructor)(MXObject*); std::int64_t vtable_len; void* const* vtable; }` + `enum MXSlot` exactly
  as in the progress contract. No LLVM headers (must lower to plain bitcode).
- `include/mxspp/core/MXInstance.h` + `src/core/MXInstance.cpp`:
  - ctor `MXInstance(const MXClassInfo* ci, bool is_static=false)`; `class_name()` → `ci->name`;
    `classinfo()` accessor; `set_field`/`get_field`; owned fields (adopt on set, release old on overwrite);
    `~MXInstance()` runs `classinfo_->destructor(this)` if non-null, then releases all fields;
    `repr()` = `"ClassName(f=v, …)"`; `get_rtti()` (parent = `MXObject::get_rtti()`).
- `src/core/MXOps.cpp` (+ wherever fits): `extern "C"`
  - `MXObject* mxs_instance_new(const MXClassInfo* ci)` (rc 1),
  - `void mxs_set_attr(MXObject* self, const char* name, MXObject* v)` (adopts v),
  - `const MXClassInfo* mxs_object_classinfo(const MXObject* o)` (instance → its classinfo, else null),
  - extend `mxs_get_attr` so an `MXInstance` returns the named field **retained (+1)** (unset → nil),
  - extend `mxs_is_type` so `case x: C =>` matches `instance.classinfo->name == "C"`.
- `src/core/CMakeLists.txt`: add `MXInstance.cpp` to the `core` lib and to `CORE_BC_SOURCES`.

## Steps
1. Write `MXClassInfo.h` (pure C++/no LLVM). Confirm the 64-bit layout = `{ptr,ptr,ptr,i64,ptr}`.
2. Rework `MXInstance` to hold `classinfo_`; implement owned-field semantics + dtor trampoline + repr + RTTI.
3. Implement the ABI shims; extend `mxs_get_attr`/`mxs_is_type`; add `mxs_object_classinfo`.
4. Wire CMake (lib + bitcode source).

## Acceptance criteria
- [ ] `core` lib + `core.bc` build; `core.bc` exports `mxs_instance_new`/`mxs_set_attr`/`mxs_get_attr`
      (instance-aware)/`mxs_is_type` (instance-aware)/`mxs_object_classinfo`.
- [ ] Unit test (task10 will own the assertions) can: build an `MXClassInfo`, `mxs_instance_new` it,
      `mxs_set_attr` two fields, read them back via `mxs_get_attr`, see `repr()` = `"C(x=…, y=…)"`,
      and `mxs_is_type(inst,"C")` == 1 / `mxs_is_type(inst,"D")` == 0.
- [ ] A non-null `destructor` set in the classinfo is invoked exactly once when the instance is released.

## Constraints
- Follow develop_rule.md (RTTI, ownership, move, no LLVM headers in core.bc sources).
- `MXClassInfo` layout is a hard contract with codegen (task08) — do not reorder fields.

## Notes / Assumptions
- Assumption: `parent` is always null in v1 (no inheritance).
- Question: field storage is a `vector<pair<string,MXObject*>>` (ordered) — keep ordered for stable repr.
