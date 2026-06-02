# Progress 17 — Consolidate std-library C/C++ implementations into `src/std/`
id: 2026-06-02/progress17
date: 2026-06-02
author: human+ai
status: active
refs: [2026-06-01/progress13, 2026-06-02/progress16]
supersedes:
commits: []
files:
  - src/std/** (NEW)                # the C/C++ backends for std.* modules
  - src/std/CMakeLists.txt (NEW)    # static lib + bitcode (std.bc) for the JIT
  - src/core/{MXOps,MXFormat,MXString,MXTime}.cpp  # remove the moved std-backing functions
  - src/core/CMakeLists.txt         # drop moved files from CORE_BC_SOURCES
  - src/jit/jit.cpp, src/driver/main.cpp, src/shell/shell.cpp  # link + locate std.bc
  - CMakeLists.txt                  # add_subdirectory(src/std)

## Goal
The C/C++ functions that *back the importable standard library* (`std.io`, `std.time`, `std.types`,
…) are currently **scattered through `src/core/`** (MXFormat.cpp, MXString.cpp, MXArrayList.cpp,
MXTime.cpp, MXOps.cpp). Mux: "C++/C 实现的 std 需要放在 `src/std` 里" — not mixed into the core
object-model/runtime. Move the std-backing implementations into a dedicated **`src/std/`** tree, one
TU per std module, leaving `src/core/` for the object model + language runtime only.

## Context / current state (grounded)
The `@@foreign` symbols referenced by `std/*.mxs` and where they live today:
- **std.io** → `mxs_println`/`mxs_print`/`mxs_format` (`src/core/MXFormat.cpp`), `mxs_str`/`mxs_repr`
  (`src/core/MXString.cpp`), `mxs_raise`/`mxs_exit` (`src/core/MXOps.cpp`), `arraylist` →
  `mxs_arraylist_new` (`src/core/MXArrayList.cpp`).
- **std.time** → `mxs_time_now`/`mxs_time_ms`/`mxs_time_ns` (`src/core/MXTime.cpp`).
- **std.types** → `mxs_typeof` (`src/core/MXOps.cpp`).
- **REPL support** (not a std module, but the same kind of glue): `mxs_repl_echo`
  (`src/core/MXFormat.cpp`), `mxs_population_dump`/`_all` (`src/core/MXOps.cpp`).

The JIT resolves these by linking **`core.bc`** (built from `CORE_BC_SOURCES` in
`src/core/CMakeLists.txt`); `jit::run` also has an UNUSED `runtimeBcPath` slot (always `""` today).

## Decisions (proposed — for Mux to confirm at execution time)
- **D1 — what moves vs stays.** MOVE the *pure std surface* (only reachable via `import`, not emitted
  by codegen): `mxs_println`/`print`/`format`, `mxs_time_*`, `mxs_typeof`, and the REPL glue
  (`mxs_repl_echo`, `mxs_population_dump[_all]`). KEEP in `src/core/` the *language runtime
  primitives* that codegen emits directly or that are fundamental type ops: `mxs_op_*`,
  `mxs_retain`/`release`, `mxs_int_from_i64`/`float_new`/`bool_new`/`str_new`/`nil_new`,
  `mxs_lvalue_*`, `mxs_object_classinfo`/`get_attr`/`set_attr`/`instance_new`/`method_missing`,
  `mxs_is_type`, `mxs_len`, `mxs_index_get`, `mxs_arraylist_*` (used by codegen for list literals,
  varargs, and the built-in container methods — runtime, not "std"). 
  - Borderline: `mxs_str`/`mxs_repr` (thin wrappers over `MXObject::str()/repr()`) and
    `mxs_raise`/`mxs_exit` (process control) — proposed to MOVE to `src/std/io.cpp` (they are only
    `std.io` bindings), but they may stay if judged core. Confirm at execution.
- **D2 — bitcode wiring.** `src/std/` emits its own **`std.bc`** (mirroring the per-file →
  `llvm-link` machinery in `src/core/CMakeLists.txt`). The JIT links it via the currently-unused
  `runtimeBcPath` slot of `jit::run` (so: `core.bc` = runtime, `std.bc` = stdlib). `driver/main.cpp`
  + `shell/shell.cpp` locate `std.bc` with the existing `find_bc()` and pass it in. (Alternative:
  fold `src/std` into `CORE_BC_SOURCES` — simpler but keeps one blob; rejected for clarity.)
- **D3 — layout.** `src/std/io.cpp`, `src/std/time.cpp`, `src/std/types.cpp`, and `src/std/repl.cpp`
  (the REPL glue), each an `extern "C"` TU implementing that module's `mxs_*`. A `src/std/CMakeLists.txt`
  builds the `std` static lib + `std.bc`; root `CMakeLists.txt` adds the subdirectory; `shell`/driver
  link `std` (replacing the transitive core symbols they used).

## Tasks
- [ ] [task29 — create src/std tree + move std-backing functions + bitcode (std.bc) wiring](tasks/task29-src-std-tree.md)

## Risk / notes
- The build is fragile (vendored LLVM 20 + libc++); the per-file bitcode emit + `llvm-link` step is
  the delicate part. Mirror `src/core/CMakeLists.txt` exactly for `std.bc`. Build with `ninja -C build`.
- Keep every `@@foreign(symbol_name=...)` in `std/*.mxs` pointing at the same symbol name after the
  move (only the .cpp location changes, not the C symbol) — so no `.mxs` change is needed.
- Verify the JIT still resolves all symbols (run the full example sweep + corpus + a REPL pipe).

## Agent log
- 2026-06-02 [ai/opus] Recorded the reorg per Mux's request (the std-backing C funcs are scattered in
  src/core today; he wants them in src/std). Mapped every std-backing symbol to its current file and
  proposed the move/keep split (D1), the `std.bc` bitcode wiring via jit::run's unused runtimeBc slot
  (D2), and the per-module layout (D3). NOT executed — this is the plan; awaiting Mux's go + the
  confirm on the borderline functions (mxs_str/repr, mxs_raise/exit). `mxs_typeof` (just added in
  progress16) is one of the functions slated to move here.
