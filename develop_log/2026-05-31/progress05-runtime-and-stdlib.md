# Progress 05 — Runtime + standard library
id: 2026-05-31/progress05
date: 2026-05-31
author: human+ai
status: active
refs: [2026-05-31/progress04]
supersedes:
commits: []
files:
  - src/runtime/runtime.cpp
  - src/frontend/ast.cpp     # FunctionCall: built-in print/println -> runtime
  - (later) src/jit/jit.cpp, the object model in src/core/, std/*.mxs

## Goal
Build the runtime + standard library so MXScript programs actually run and do I/O. Standard
library has two layers (per docs/type_system.md §8, docs/ffi.md): an **mxs-side** API and the
**runtime fast-dispatch** C-ABI it binds to.

## Decisions

### D1 — Runtime I/O on the standard streams via C stdio (not C++ iostreams) — Mux, 2026-05-31
- Decision: runtime I/O uses `fprintf`/`FILE*` over stdout/stderr/stdlog (stream ids 0/1/2),
  not `std::cout`/`clog`.
- Why: lighter for a JIT/LTO runtime (no iostream static-init / locale machinery); maps cleanly
  to mxs's `io.stdout` / `io.stderr` / `io.stdlog` (file_io.mxs already uses `io.stderr`).
- Impact: print fast-dispatch functions take a leading `i64 stream` arg.

### D2 — Built-in print/println wired straight to the runtime fast-dispatch (interim)
- Decision: codegen recognizes `print`/`println` and emits a direct call to `mxs_(print|println)_*`
  chosen by the argument's LLVM type (int/float/bool/str), passing stream 0 (stdout).
- Why: gives real output now, before the `@@foreign` + import + stdlib-resolution layer exists.
  When std.io lands, these resolve through it instead.

## Done (this increment)
- `runtime.cpp`: stream-aware fast-dispatch I/O — `mxs_print_int/float/bool/str`, the `println_*`
  variants, `mxs_println` (all `extern "C"`, `(i64 stream, value)`).
- Codegen: `print`/`println` → runtime calls.
- **A program RUNS and PRINTS** (AOT-linked the emitted IR with runtime.cpp in Docker):
  `println(fib(10))` → `55`, plus `7` / `3.14` / `true`. Stream routing verified (0→stdout,
  1/2→stderr).

## Open / TODO (the rest of "complete stdlib + runtime")
- **ORC JIT**: `mxs run <file>` executes directly (load runtime symbols, JIT `main`) — the
  README's headline feature; removes the external clang/lli step.
- **Object model**: `MXObject` + `MXInteger`/`MXFloat`/`MXBoolean`/`MXString` (core/ is stubbed),
  then the §8 fast-dispatch arithmetic (`mxs_integer_add`, …) + dynamic dispatch (`mxs_op_add`,
  `op_from`/`cast`). NOTE: this needs an **object-model codegen pivot** — current codegen is
  native-numeric (i64/f64). Sequencing decision for Mux when we get there.
- Real strings; containers (List/Dict/Array/Tuple, type_system §3.3).
- **mxs-side stdlib** (`std/io.mxs`, …) binding via `@@foreign` — needs FFI codegen + imports
  (progress03 task05/06 + a codegen pass).

## Agent log
- 2026-05-31 [ai] Implemented the runtime I/O fast-dispatch layer (stream-aware, C stdio per
  Mux's call) + codegen print wiring. First program with real output (fib(10)=55) via AOT link.
  Stream routing (stdout/stderr/stdlog) verified. Not yet committed. Next: ORC JIT so `mxs run`
  executes directly, then the object model + fast-dispatch arithmetic.
