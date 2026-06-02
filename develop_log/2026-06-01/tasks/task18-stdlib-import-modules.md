# Task 18 — Stdlib restructure: retire kCorePrelude → import-gated std modules
id: 2026-06-01/task18
parent: 2026-06-01/progress13
status: blocked        # D2/D4 RESOLVED; still depends on task17 (import loader) landing first
owner: code_agent

## Objective
Move the `@@foreign` stdlib bindings out of the hardcoded C++ `kCorePrelude` string into real
`std/*.mxs` modules, reachable only via `import`.

## Scope
In:
- `std/io.mxs` (and any split, e.g. `std/builtins`) holding the `@@foreign` bindings + mxs wrappers for
  the surface that stays free-function (print/println/format/str/repr/raise/exit/…).
- Remove `kCorePrelude` from `main.cpp`; update every example/test to `import` what it uses.
Out:
- The import loader itself (task17).
- `time` module (task19); container methods (task20).

## Inputs (read first, priority order)
1. `src/driver/main.cpp` — `kCorePrelude` (~34-45): the current binding set to relocate.
2. `std/io.mxs` — the existing (dead) two-layer example to grow into the real module.
3. `develop_log/2026-06-01/progress13-...md` — D2 (import-gated) + D4 (what becomes a method, not a fn).
4. `example/examples/*.mxs`, `test/` — call sites that assume implicit `println`/etc.

## Deliverables
- `std/io.mxs` (+ siblings) — `@@foreign(symbol_name="mxs_*")` bindings + any mxs-level wrappers
  (e.g. stream-selecting `println`/`eprintln`), per the two-layer model.
- `main.cpp` — no `kCorePrelude`; stdlib reaches programs only through task17's importer.
- Updated examples/tests with explicit `import std.io;` (etc.).

## Steps
1. **Partition the current prelude** per D4: which bindings become methods (task20 — `append`, `len`?)
   vs stay free functions in a module (`print`/`println`/`format`/`str`/`repr`/`raise`/`exit`).
2. **Write the std module(s)** with the @@foreign markers + wrappers.
3. **Delete kCorePrelude**; route through the importer.
4. **Update all examples/tests** to import their stdlib; adjust the demo runner expectations.
5. **Build + verify** every demo + ctest.

## Acceptance criteria
- [ ] `main.cpp` no longer contains a hardcoded prelude string.
- [ ] A demo that uses `println` declares `import std.io;` and runs; without it, it fails to resolve.
- [ ] `ctest` green; demos green (post-import-update).

## Constraints
- Two-layer model only (docs/ffi.md): no per-function special-casing in the compiler.

## Notes / Assumptions
- Assumption: depends on task17 landing first. Surface split coordinated with task20.
