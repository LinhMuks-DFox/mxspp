# Progress 19 — Runtime performance (make script execution fast)
id: 2026-06-02/progress19
date: 2026-06-02
author: human+ai
status: active
refs: [2026-06-01/progress11]
supersedes:
commits: []
files:
  - src/core/MXPopulationManager.* + src/core/MXObject.cpp  # gate the population tracking off the hot path
  - src/core/MXInteger.* (small-int cache / unboxing)       # avoid per-int heap alloc
  - src/core/MXOps.cpp (mxs_op_*)                           # type-specialize arithmetic

## Goal
Mux: "shell 慢无所谓，真正运行脚本的时候不慢就行" — the JIT *pipeline* warmup doesn't matter; **script
execution** speed does. Bring per-operation runtime cost down so compute-heavy scripts are usable.

## Context / measurement (2026-06-02, std.time)
Measured with the working `std.time` module:
- **Pipeline** (trivial program, total `run-core`): ~660 ms fixed — parse + codegen + load/link
  `core.bc` + ORC materialize. One-time; negligible for long programs. NOT the bottleneck.
- **Runtime** (1e6-iteration integer loop, timed inside via `monotonic_ns`): **~5.3 µs/iteration**
  (~5.26 s total). For a JIT-compiled-to-native language this is very slow — order ~10000× a native C
  loop and ~100–1000× CPython — i.e. the per-op runtime overhead is negating the JIT.

Diagnosis: it is the **runtime object model**, NOT the pipeline and NOT (necessarily) the language
*semantics*. Each operation pays: a heap allocation per value (every int/bool is a boxed refcounted
MXObject), a dynamic-dispatch C call (`mxs_op_*` inspects operand types at runtime), retain/release
refcounting, and — on EVERY object construct/destruct — a `MXPopulationManager` **mutex lock +
unordered_set insert/erase** (an ARC-verification debug tool that is always on, even on the hot path).

## Decisions (proposed targets, in rough impact order — refine at execution)
- **D1 — gate the population manager off the hot path.** It is a debug/test lever (the ARC baseline in
  core_test). Compile it out (or make it a no-op) in normal/release runs — a mutex+hash per object
  lifecycle is pure overhead. Likely the biggest cheap win. Keep it available behind a build flag for
  the ARC tests.
- **D2 — small-integer cache / unboxing.** Avoid a heap MXInteger per arithmetic result (cache small
  ints, or carry ints unboxed where the type is known). Removes most allocations in numeric loops.
- **D3 — type-specialized arithmetic.** Reduce `mxs_op_*` dynamic dispatch (fast paths / inline
  guards) so int loops don't re-dispatch every op.
These keep the language semantics intact — they are runtime optimizations, not a redesign.

## Tasks
- [ ] task31 — measure + remove the population-manager hot-path cost (D1); re-benchmark.
- [ ] (later) task — small-int cache/unboxing (D2); type-specialized arithmetic (D3).

## Agent log
- 2026-06-02 [ai/opus] Recorded per the batch-record-first workflow, with the std.time measurement
  that answered Mux's "is mxs slow due to my design or the pipeline?" — it's the runtime object model
  (esp. the always-on population-manager mutex), not the pipeline. Optimization targets captured. NOT
  executed — part of the current requirements batch awaiting Mux's go + ordering.
