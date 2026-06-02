# Task 40 — JIT IR optimization pipeline (D0) + `@@optimize(level=N)` annotation
id: 2026-06-02/task40
parent: 2026-06-02/progress19
status: done
owner: code_agent
blocked-on: (was) progress18 — done

## Outcome (2026-06-02, commits 0069f34 + 603eba4; implemented via sub-agent + parent-verified)
DONE. (1) jit::run links user+core.bc+std.bc into ONE module (llvm::Linker; the IRMover must be scoped
to destruct before the module is moved into the JIT — a use-after-move SIGSEGV otherwise) and runs a
PassBuilder per-module pipeline; `@@optimize(level=0/1/2/3)`→O0-O3 via `TranslationUnit::optLevel`,
default O2, threaded through driver + shell, invalid level = clean diagnostic. (2) THE key unlock —
runtime bitcode emitted at -O2 (was -O0 = optnone/noinline, which blocked all primitive inlining).
Benchmark (1e6 int loop, ns/iter): baseline ~5300 → bitcode-O0 {O0 2472, O2 1141} → bitcode-O2 {O0 297,
O2 271, O3 294}. Default O2 confirmed (O3 not faster). Verified against HEAD std: ninja clean, ctest
3/3, corpus 36/36, examples 22/22; Mux WIP std restored byte-identical. Remaining ns/iter floor = the
object model (D1/D2/D3).

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
