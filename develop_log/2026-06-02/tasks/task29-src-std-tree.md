# Task 29 — Create src/std tree + move std-backing functions + std.bc wiring
id: 2026-06-02/task29
parent: 2026-06-02/progress17
status: blocked
owner: code_agent
blocked-on: Mux's go + confirmation of the D1 borderline split (mxs_str/repr, mxs_raise/exit)

## Objective
Move the std-library C/C++ backends out of `src/core/` into a dedicated `src/std/`, build them into a
`std.bc` the JIT links, and keep `src/core/` to the object model + runtime primitives.

## Steps (per progress17 D1–D3)
1. Create `src/std/` with `io.cpp` (mxs_println/print/format/str/repr/raise/exit + arraylist surface),
   `time.cpp` (mxs_time_*), `types.cpp` (mxs_typeof), `repl.cpp` (mxs_repl_echo, mxs_population_dump[_all]).
   Move the function bodies from `src/core/{MXFormat,MXString,MXTime,MXOps}.cpp`; keep the C symbol
   names identical (so `std/*.mxs` @@foreign bindings are unchanged). Keep runtime primitives in core.
2. `src/std/CMakeLists.txt`: a `std` static lib + a `std.bc` bitcode target, mirroring the per-file
   `-emit-llvm` + `llvm-link` machinery in `src/core/CMakeLists.txt`. Copy `std.bc` to `build/bin`.
3. `src/core/CMakeLists.txt`: drop the moved files from the static lib + `CORE_BC_SOURCES`.
4. `src/jit/jit.cpp`: link `std.bc` (use the existing unused `runtimeBcPath` slot, or add one).
   `src/driver/main.cpp` + `src/shell/shell.cpp`: locate `std.bc` via `find_bc("std.bc")` and pass it.
5. Root `CMakeLists.txt`: `add_subdirectory(src/std)`; link `std` where needed.

## Acceptance
- [ ] `ninja -C build` clean; `build/bin/std.bc` present with the moved symbols (`llvm-nm`).
- [ ] `ctest` 3/3; full `example/examples/*.mxs` sweep at expected rc; corpus 34/34; a REPL pipe
      (`./objects_population`, `println`) works — i.e. the JIT still resolves every moved symbol.
- [ ] `grep` confirms no std-backing function remains in `src/core/`; no `std/*.mxs` change needed.

## Notes
- Delicate part: the `std.bc` bitcode build (mirror src/core exactly — the host uses `MXS_BC_CXX`).
- Decide the borderline functions with Mux first (D1): mxs_str/repr (thin type wrappers) and
  mxs_raise/exit (process control) — move to src/std/io or keep in core.
