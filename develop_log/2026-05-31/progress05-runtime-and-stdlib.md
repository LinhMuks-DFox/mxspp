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

## Object model — started (2026-05-31)
- Runtime substrate landed: a tagged boxed `MXObject` + `mxs_box_int/float/bool`, dynamic-dispatch
  `mxs_op_add/sub/mul` (int+int→int, float promotion), polymorphic `mxs_obj_println`, and
  tag/unbox/truthy accessors — all `extern "C"` in runtime.cpp. Unit-tested (test/runtime_test.cpp,
  3 cases / 17 checks, pure C++). This is the §8 *dynamic-dispatch* path at the runtime level.
- **Object-mode codegen — landed (narrow slice).** `backend::compile_obj` lowers a program in
  object mode: literals box (`mxs_box_*`), `+`/`-`/`*` go through dynamic dispatch (`mxs_op_*`),
  `println` is polymorphic (`mxs_obj_println`). Driver: `mxs run-obj <file>`. Verified in Docker:
  `println(2 + 3 * 4)` → 14; `println(1.5 + 2)` → **3.5** (the same `+` promotes int→float at
  runtime — §8 dynamic dispatch in a compiled + JIT'd program). Slice = main + literals + +/-/* +
  println.

## Object-mode codegen — full programs (2026-05-31)
Extended the object model from the narrow slice to **whole programs**. Everything is a boxed
`MXObject*` (LLVM `ptr`); every mxs function takes/returns `ptr`, except `main` (returns `i64`, the
JIT entry, via `mxs_obj_as_int` on its value) and `-> nil` functions (return `void`).

- **Runtime (runtime.cpp).** Expanded the object model: added `MX_STR` (heap string, malloc/strdup)
  and `MX_NIL` tags; `mxs_box_str`/`mxs_box_nil`; `mxs_op_div`/`mod` (panic on zero), `mxs_op_neg`,
  `mxs_op_not`; the full comparison family `mxs_op_lt/le/gt/ge/eq/ne` (each returns a **boxed
  bool**; strings compare lexicographically, numbers numerically, mixing string vs number panics —
  except `eq`/`ne`, which are total and return false/true); string concatenation in `mxs_op_add`;
  and the polymorphic print symbols `mxs_print_obj`/`mxs_println_obj`/`mxs_eprint_obj`/
  `mxs_eprintln_obj` the stdlib binds to.
- **Codegen (backend/codegen.cpp).** Rewrote `compile_obj` as a full **two-pass** object-mode
  compiler (`ObjGen`): pass 1 declares every function prototype (all-`ptr` params; `ptr`/`void`/
  `i64` returns) so recursion/forward calls resolve; pass 2 emits bodies. Supports literals
  (incl. strings/nil), identifiers, arithmetic + comparison via `mxs_op_*`, `&&`/`||` with proper
  short-circuit (PHI of a boxed bool), unary `-`/`!`/`+`, plain + compound assignment, calls
  (generic resolution; void-returning calls box `nil` as their value), `let`, `return`,
  `if/else`, `loop`, `until`, `do-until`, `for-in` ranges (i64 counter, loop var re-boxed each
  iteration), `break`/`continue`, and `assert` (panics on false). Conditions go through
  `mxs_obj_truthy`.
- **NO per-function hardcoding (D3 upheld).** `println`/`print`/`eprintln`/`eprint` are now an
  **object-mode prelude** (`kObjPrelude` in driver/main.cpp) of generic `@@foreign` declarations
  binding straight to the polymorphic runtime symbols (each `void(MXObject*)`). They resolve through
  `funcs` like any other call — the compiler special-cases nothing.
- **Verified in Docker** (`python3 rebuild.py`, exit 0; clang-format clean): `ctest` 2/2 green
  (frontend + runtime, the latter now **7 cases** incl. fork-based death tests for every panic
  path). `mxs run-obj example/examples/obj_fib.mxs` → **55** (recursion + comparison + arithmetic +
  returned object). `mxs run-obj example/examples/obj_features.mxs` → `hello, world / 10 / 6 / 3.5 /
  true / true` (string concat, `for-in`, `until`, int→float promotion, comparison, logical not).
- NEXT: containers (List/Dict/Array/Tuple, type_system §3.3); the recoverable Error model
  (`raise`/`match`, plus syntax/numerical error reporting); §8 *fast-dispatch* typed ops
  (`mxs_integer_add`, …) as an optimization over `mxs_op_*`; reconcile the tagged runtime
  `MXObject` with core's `MXObject` class hierarchy + RTTI; the shell/REPL on the object-mode path.

## Open / TODO (the rest of "complete stdlib + runtime")
- **ORC JIT**: DONE — `mxs run <file>` executes (loads runtime.bc, JITs main). (commit 9958b6a)
- **Object model**: STARTED (above). Remaining: object-mode codegen + strings + containers +
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
