# Progress 04 — AST → LLVM IR codegen (numeric / control-flow slice)
id: 2026-05-31/progress04
date: 2026-05-31
author: human+ai
status: active
refs: [2026-05-31/progress03]
supersedes:
commits: []
files:
  - include/mxspp/backend/codegen.h
  - src/frontend/ast.cpp        # per-node codegen() + map_type
  - src/backend/codegen.cpp     # compile() driver
  - src/driver/main.cpp         # `mxs --emit-ir`

## Goal
Stand up the AST → LLVM IR step (the empty backend). Generate real, verifiable LLVM IR from
the AST and prove it runs. First slice = a statically-typed numeric / control-flow subset; the
full object model is a later phase.

## Context / Motivation
Mux's goal: "完成 AST -> LLIR的部分". The frontend (progress03) builds an AST for functions /
control flow / OOP; codegen was entirely empty. This connects AST → IR end-to-end.

## Decisions

### D1 — First slice: a statically-typed numeric subset (int=i64, float=f64, bool=i1, nil=void)
- Decision: codegen native LLVM types for the numeric/control-flow language — NOT the
  "everything is an MXObject" model yet.
- Why: smallest path to runnable IR. The object model (boxing, dynamic dispatch, strings,
  ARC) needs the runtime, which is a separate phase. This proves the AST→IR pipeline now.
- Impact: covers functions, control flow, arithmetic, recursion; defers objects/strings/OOP/FFI.

### D2 — Per-node `codegen()` in frontend (`ast.cpp`); `compile()` driver in backend
- Decision: each AST node emits its own IR (methods in `ast.cpp`); a `compile()` driver in
  `backend/codegen.cpp` builds the module in two passes (declare all function prototypes, then
  emit bodies) so recursion / mutual calls resolve. `map_type` lives in frontend.
- Why: respects the module layout (`core`→LLVM, `frontend`→core, `backend`→frontend); keeps
  `frontend` self-linkable so the unit-test target (frontend+core, no backend) still links.
- Impact: the backend module finally has real code; the driver gains `mxs --emit-ir`.

### D3 — Verify via `verifyModule` + JIT-run with `lli`
- Decision: `compile()` runs LLVM's `verifyModule`; correctness is proven by JIT-running the
  emitted IR with `lli`.
- Why: proves IR validity + correctness without writing the ORC JIT yet (that is its own step).

## Covered (this slice)
Functions (typed params + return), `let`/assignment (+ `+=` etc.), `return`, `if`/`else-if`/`else`,
`loop`/`until`/`do-until`/`for-in` over an integer range, `break`/`continue`, int+float+bool
arithmetic / comparison / logic, unary `-`/`!`, direct & recursive calls, literals
(int/float/bool; string → global `i8*`; nil → 0 placeholder).

## Verified
`func fib(n:int)->int { if n<=1 {return n;} return fib(n-1)+fib(n-2); } func main()->int { return
fib(10); }` → `mxs --emit-ir` emits correct IR (recursion + branches), `verifyModule` passes, and
`lli fib.ll` exits **55 = fib(10)**. Frontend unit tests unchanged (21 cases / 115 checks).

## Issues / Gotchas
- colima's volume mount serves `build/bin/mxs` without the exec bit ("Permission denied"). Run by
  copying the binary + `.so`s to a container-local path with `LD_LIBRARY_PATH`, or build/run in a
  non-mounted dir. Not a code issue.
- IR is run via `lli` for now; `mxs` itself doesn't JIT-run yet.

## Open / TODO (carry-over)
- Object model + runtime: MXObject, real strings, dynamic dispatch, ARC — the big next phase
  (enables `println`, strings, the `cast`/dispatch design, and OOP method codegen).
- OOP member codegen (methods/ctors/fields/operators), `match`, FFI (`@@foreign`, `println`).
- An in-process ORC JIT so `mxs <file>` runs directly (instead of `--emit-ir | lli`).
- Remaining frontend AST gaps (progress03 task05/06): postfix `.`/`[]`/`?`, match/lambda transforms,
  annotations/import.

## Agent log
- 2026-05-31 [ai] Implemented the numeric/control-flow codegen slice (D1–D3). Extended
  CodegenContext; per-node `codegen()` in ast.cpp; `compile()` two-pass driver in backend;
  `mxs --emit-ir`. Native `-fsyntax-only` clean against real LLVM headers; clean Docker build
  succeeds; `fib(10)` emits correct verified IR and `lli` runs it → exit 55. Not yet committed.
