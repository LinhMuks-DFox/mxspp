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

## MAJOR FINDING (2026-06-02, verified in src/jit/jit.cpp) — the JIT runs NO IR optimization
While answering Mux's "how far does LLVM's LLIR optimization actually go / does deep nesting hurt?",
read `src/jit/jit.cpp`: the ORC `LLJIT` is built with only a custom object-linking layer (EH-frame
silencing). There is **NO IRTransformLayer / PassBuilder pipeline** — so only backend codegen runs
(isel/regalloc at the TargetMachine default), and **no mid-end IR optimization** (no inliner, no
mem2reg/SROA, no instcombine/GVN/DCE). Worse: `core.bc`, `std.bc`, and the user IR are added as **three
separate** `addIRModule` calls, so even a per-module pipeline could not inline ACROSS them.

Consequence: the whole "compile core to bitcode so LLVM inlines across the mxs/lib boundary" design
(core/CMakeLists.txt + jit.cpp:84 comments) **never actually inlines anything**. Every layer of the
`user → layer-2 mxs → layer-1 @@foreign C primitive` stack is a REAL, un-inlined runtime call; the
1e6-loop body is a chain of un-inlined `mxs_op_*` / `mxs_retain` / `mxs_int_to_i64` calls. This is a
large part of the ~5.3 µs/iter. It also means Mux's layering/nesting is currently NOT free — but it
WOULD largely flatten under inlining.

- **D0 (NEW — likely the single biggest win; do FIRST) — add an IR optimization pipeline to the JIT.**
  Link user IR + `core.bc` + `std.bc` into ONE `llvm::Module` (in-process `llvm::Linker`) so the bodies
  are co-visible, then run a `PassBuilder` default per-module **-O2/-O3 pipeline (incl. the inliner)**
  before JIT codegen. This inlines the typed fast-path primitives (`mxs_int_*`, `mxs_retain/release`)
  into hot loops, enables mem2reg/SROA/instcombine/GVN/DCE, and makes shallow layer-2→layer-1 nesting
  collapse — directly answering "does nesting hurt" (it stops hurting once passes run). Measure the
  before/after on the 1e6 loop. Caveat: inlining EXPOSES but does not by itself remove the object-model
  costs below (heap-boxed values, dynamic_cast dispatch, refcount atomics, the population mutex) — D1-D3
  still matter, but they compound with D0. **Default level O2 (aggressive by default, per Mux); a
  `@@optimize(level=1/2/3)` program directive dials O1/O2/O3 (0 = off).** The `@@`-annotation grammar
  already parses `@@optimize(...)` (it is currently ignored — only `foreign` is handled), so wiring it is
  light. This implements the long-documented design (docs/Architecture.md §39-40, §12: merged-module LTO +
  inlining). See task40.

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

## Outcome — D0 DONE (2026-06-02, commits 0069f34 + 603eba4)
D0 was TWO changes: (1) jit::run now links user+core.bc+std.bc into ONE module and runs a PassBuilder
pipeline (default O2; `@@optimize(level=N)`→O0-O3) before codegen — commit 0069f34; (2) the runtime
bitcode is emitted at **-O2 not -O0** (commit 603eba4) — the real unlock, because -O0 stamps every
`mxs_*` body `optnone/noinline` so the JIT inliner could never touch them. Together: a 1e6 integer loop
dropped from ~5300 ns/iter (original no-opt baseline) to **~271 ns/iter at the default** — and notably
**~297 ns/iter even at @@optimize(level=0)** (because the pre-optimized bitcode bodies are no longer
optnone), with level=2 (271) the best and level=3 (294) NOT better → O2 is the right default. So the
layer-2→layer-1 nesting cost has largely collapsed — Mux's bitcode/LTO design now actually pays off.
What REMAINS (the ~271 ns/iter floor) is the object model itself — D1/D2/D3 below.

## Tasks
- [x] task40 — **(D0)** JIT optimization pipeline + `@@optimize` + runtime bitcode at -O2. DONE
      (0069f34 + 603eba4). ~5300→~271 ns/iter; ctest 3/3, corpus 36/36, examples 22/22.
- [ ] task31 — measure + remove the population-manager hot-path cost (D1); re-benchmark.
- [ ] (later) task — small-int cache/unboxing (D2); type-specialized arithmetic (D3).

## Agent log
- 2026-06-02 [ai/opus] Recorded per the batch-record-first workflow, with the std.time measurement
  that answered Mux's "is mxs slow due to my design or the pipeline?" — it's the runtime object model
  (esp. the always-on population-manager mutex), not the pipeline. Optimization targets captured. NOT
  executed — part of the current requirements batch awaiting Mux's go + ordering.
