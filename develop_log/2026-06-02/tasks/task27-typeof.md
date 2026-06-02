# Task 27 — typeof primitive + is_instance_of (native mxs)
id: 2026-06-02/task27
parent: 2026-06-02/progress16
status: done
owner: code_agent (Opus)

## Objective
Add `typeof(x)` (the type name of a value) and `is_instance_of(x, cls)` to a new `std.types` module.

## Steps
1. `src/core/MXOps.cpp`: add `mxs_typeof(const MXObject*) -> MXObject*` (a fresh MXString of the type
   name; instance → `class_name()`; built-ins → int/float/str/bool/nil/List/Error). The inverse of
   `mxs_is_type`. (Layer-1 C primitive — needs runtime type inspection.)
2. `std/types.mxs` (NEW): `@@foreign(symbol_name="mxs_typeof") func typeof(x: any) -> str;` plus a
   pure-mxs `is_instance_of(x, cls) -> bool { return typeof(x) == cls; }` (layer-2, per D3).

## Acceptance
- [x] `ninja -C build` clean (core.bc relinked with mxs_typeof); `ctest` 3/3.
- [x] All import forms of typeof: `typeof(p)`→`Point`, `typeof(42)`→`int`, `3.5`→`float`,
      `"hi"`→`str`, `true`→`bool`, `nil`→`nil`, `[1,2]`→`List`; `types.typeof(42)`→`int`.
- [x] `is_instance_of(p,"Point")`→`true`, `is_instance_of(42,"int")`→`true`, `(42,"str")`→`false`
      (under `import std.types.{is_instance_of, typeof}` — see the module-namespace Finding for why
      both names are needed today).

## Notes
- `mxs_typeof` currently lives in `src/core/MXOps.cpp`; it is a std-backing function and will move to
  `src/std/` under the src/std reorganization (progress17). Recorded so the placement isn't forgotten.
- An earlier `@@foreign`-backed `mxs_is_instance_of` C primitive was tried and **removed** — Mux wants
  it native (layer-2), no redundant C code (delete-legacy-no-redundancy).
