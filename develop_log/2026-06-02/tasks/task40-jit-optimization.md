# Task 40 — JIT IR optimization pipeline (D0) + `@@optimize(level=N)` annotation
id: 2026-06-02/task40
parent: 2026-06-02/progress19
status: pending
owner: code_agent
blocked-on: progress18 must finish first (shared build/ dir — cannot run ninja concurrently)

## Objective
Make the JIT actually optimize (it runs NO mid-end IR passes today — progress19 D0). Implement the
documented-but-unbuilt architecture (docs/Architecture.md §39-40, §12): merge user IR + core.bc + std.bc
into ONE module and run an LLVM optimization pipeline (default O2, with the inliner) before codegen, so
cross-module inlining flattens the layer-2 → layer-1 → C-primitive nesting. Add a `@@optimize(level=N)`
program directive to dial the level. Mux: "jit 执行的时候直接就有 LLIR 最激进优化好了" + "@@optimize(level=1/2/3)".

## Steps
1. **Single-module link (the crux).** Today `src/jit/jit.cpp` parses core.bc/std.bc each in its OWN
   LLVMContext and `addIRModule`s three SEPARATE modules — so the inliner can never see across them.
   Restructure `jit::run`: parse core.bc + std.bc into the SAME `context` as the user module, then
   `llvm::Linker::linkModules` all three into one Module. (link_bitcode must stop making fresh contexts.)
2. **Optimization pipeline.** On the merged module, run a `PassBuilder` default per-module pipeline at the
   chosen `OptimizationLevel` (register LAM/FAM/CGAM/MAM, `buildPerModuleDefaultPipeline(level)`), before
   `addIRModule` + lookup. Stamp data layout/triple first (as today). Keep the NoEHFrame memory manager.
3. **`@@optimize(level=N)` annotation.** The `@@`-annotation grammar is generic and already PARSES
   `@@optimize(level=3)` — it is just ignored (parser.cpp:686 only handles `name=="foreign"`). Handle
   `name=="optimize"`: read the `level` arg, store a program-level opt level on the TranslationUnit (e.g.
   `ast::TranslationUnit::optLevel`, default = unset). Treat it as MODULE-scope (place on `main` or at the
   top; last/any wins). Thread TU.optLevel → driver → a new `optLevel` param of `jit::run` (and the shell
   path). Map level 1/2/3 → O1/O2/O3; 0 → O0 (off, debug). Default when unset = **O2**.
4. **Benchmark.** 1e6-iteration integer loop via `std.time.monotonic_ns`, measured at O0 vs O2 vs O3;
   record the deltas in progress19. Confirm cross-module inlining happened (e.g. dump the optimized IR for
   `main` and confirm the hot `mxs_int_*` / `mxs_retain` calls are inlined, or just the perf delta).

## Acceptance
- [ ] `ninja -C build` clean; ctest 3/3, corpus all green, example sweep all pass (against HEAD std).
- [ ] The 1e6 loop is materially faster at the default than before (report the number); `@@optimize(level=0)`
      vs `level=3` shows a clear, measurable difference (proves the knob + pipeline work).
- [ ] Evidence of cross-module inlining (IR dump or the perf delta) — the layering is no longer per-call.
- [ ] `@@optimize(level=N)` parses, is honored, and an unknown/missing level is a clean diagnostic (not UB).

## Notes
- O2 default is deliberate (JIT sweet spot; O3's extra unroll/vectorize rarely helps a heap-boxed object
  model and lengthens JIT warmup — but it is one annotation away). Confirm with Mux if he wants O3 default.
- This is independent of the std batch (touches jit.cpp + parser/ast + driver/shell) but MUST run after
  progress18 (shared build/ dir; no concurrent ninja). It exposes — but does not remove — the object-model
  costs (D1 population gating, D2 small-int unbox, D3 typed arithmetic), which compound on top.
- docs/Architecture.md §39-40 already specifies this — update those docs if behavior drifts.
