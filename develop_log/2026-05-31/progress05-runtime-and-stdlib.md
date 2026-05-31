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

### D2 — (SUPERSEDED by D3) Built-in print/println hardcoded in codegen — interim only
- Was a hardcoded `if name=="println"` fast path; removed in favor of generic @@foreign (D3).

### D3 — Standard library binds via generic @@foreign; NO per-function special-casing — Mux, 2026-05-31
- Decision: a `@@foreign(symbol_name=...)` function is a bodyless declaration that codegen lowers
  to an external symbol; a call to it emits a direct `call` to that symbol. The compiler does ZERO
  per-function hardcoding — `print`/`println`/everything resolves through `ctx.functions` like any
  other call. The stdlib is mxs `@@foreign` declarations + the runtime fast-dispatch symbols.
- Why: Mux's design (docs/ffi.md "Direct Call with a Type Convention"). Hardcoding specific names
  in codegen was wrong.
- Impact: grammar gained bodyless `func ... ;` decls (G6); `@@foreign` annotation is parsed and
  attached to the FunctionDef (isForeign + foreignSymbol); `compile()` declares the external symbol
  under the mxs name; the hardcoded print path is deleted.

### D4 — Module libraries are STATIC (single LLVM copy)
- Decision: build core/frontend/backend/jit/shell as STATIC; `mxs` links one copy of LLVM.
- Why: the vendored LLVM has only static component archives (no shared libLLVM); each SHARED module
  embedded its own LLVM copy, so once `native`/`IRReader` pulled option-registration code, LLVM
  aborted with "cl::opt registered more than once". Static = one copy in the final binary.
- Revisit: if LLVM is rebuilt with `LLVM_BUILD_LLVM_DYLIB`, modules could go back to SHARED.

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
  Stream routing (stdout/stderr/stdlog) verified. (committed 24e729e)
- 2026-05-31 [ai] Mux corrected: NO hardcoded print — use generic @@foreign (D3). Removed the
  hardcode; added bodyless func decls (grammar), @@foreign annotation -> AST, and external-symbol
  codegen. Also implemented the ORC JIT (`mxs run <file>`): loads runtime.bc + user IR, resolves
  libc from the process, runs main. Fixed the LLVM duplicate-static-init crash by making the
  module libs STATIC (D4). Verified in Docker: `mxs run` a program whose print is a @@foreign
  binding (`putln` -> mxs_println_int) prints fib(10)=55. The language now runs end-to-end:
  source -> AST -> IR -> JIT(+runtime.bc) -> output. Next: object model + fast-dispatch arithmetic
  on MXObject; then the real std.io.mxs.
