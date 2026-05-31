# Progress 08 — `LinearAlgebra` standard-library module (planned)

id: 2026-05-31/progress08
date: 2026-05-31
author: human+ai
status: active
refs: [2026-05-31/progress07]
supersedes:
commits: []
files:
  - (later) std/linalg.mxs               # mxs-side LinearAlgebra API
  - (later) src/runtime/linalg.cpp       # runtime kernels / BLAS bindings
  - (later) docs/linear_algebra.md       # module design

## Status
**Record-only for now — implement later (Mux, 2026-05-31).** This progress captures the decision
and intended shape so it isn't lost; no code yet. Depends on containers
([progress07](./progress07-containers.md)) — at least `Array` and the `Matrix` class.

## Goal
A `LinearAlgebra` standard-library module providing the linear-algebra computations MXScript needs
(vector/matrix arithmetic, products, decompositions, solves, etc.).

## Decisions

### D1 — Backend: BLAS (e.g. OpenBLAS) or a built-in implementation — Mux, 2026-05-31
- Decision: the module's kernels may **call into a BLAS backend (OpenBLAS)** for performance, **or**
  ship a **self-contained implementation**. Both are acceptable; the choice (vendor a BLAS vs. roll
  our own, and whether it is optional/pluggable) is deferred to implementation time.
- Why: linear algebra is performance-critical; BLAS gives tuned kernels, but a built-in path keeps
  the dependency footprint small and the build self-contained (matches how LLVM/PEGTL are vendored).
- Open: dependency strategy (vendor OpenBLAS under `lib/` vs. system/optional link vs. own kernels),
  the FFI surface (this is a natural client of the `@@foreign` direct-call convention, docs/ffi.md),
  threading, and the float precision/dtype set.

## Intended shape (to refine when scheduled)
- Built on the container types (progress07): a `Matrix` class over a fixed/`POD` `Array<float>`
  (see `example/examples/matrix_class.mxs`), plus vector types.
- Likely surface: matrix/vector construction, element access, `+`/`-`/`*` (incl. matmul),
  transpose, dot/cross, norms; later `solve`, `inv`, `det`, decompositions (LU/QR/SVD), eigenvalues.
- Errors via the recoverable Error model (shape mismatch -> `Error`, as the matrix example shows).

## Open / TODO
- Everything (deferred). Prerequisites first: containers (`Array`, `Matrix`/classes), the Error
  model, and the FFI/codegen maturity to bind BLAS via `@@foreign`. Then: pick the backend strategy,
  write `docs/linear_algebra.md`, and break into tasks.

## Agent log
- 2026-05-31 [ai] Recorded the module decision per Mux ("标准库还需要一个 LinearAlgebra … 底下可以
  call OpenBLAS，也可以自己实现 … 先做成 progress，以后再实现"). Record-only; no code. Revisit
  after containers + classes + the Error model land.
