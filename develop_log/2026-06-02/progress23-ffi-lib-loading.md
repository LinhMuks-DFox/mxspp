# Progress 23 — Implement the mandatory `@@foreign(lib=...)` (external library loading)

id: 2026-06-02/progress23
date: 2026-06-02
author: human+ai
status: active
refs: [2026-06-02/progress17, 2026-06-02/progress20]
supersedes:
commits: []
files:
  - src/frontend/parser.cpp + include/mxspp/frontend/ast.h   # parse + store the lib= arg (foreignLib)
  - src/jit/jit.cpp                                          # load the named library so its symbols resolve
  - src/driver/main.cpp / src/shell/shell.cpp                # collect + pass the set of libs to jit::run
  - docs/ffi.md                                              # reconcile (runtime.so vs bitcode; sentinel)

## Origin (Mux, 2026-06-02)
Mux: "@@foreign(lib="XXX") 这个东西，你是不是没注意到？这个必须要的." Correct — `lib` is documented as
**Mandatory** (docs/ffi.md §2.2: "`lib: string` (Mandatory)") but is **completely unimplemented**.

## Current state (grounded)
- `parse_annotation` extracts ALL `@@foreign(...)` key=value args, but `parser.cpp:689-690` consumes only
  `symbol_name`; the `lib` value is **dropped**. `ast::FunctionDef` has `isForeign` + `foreignSymbol` but
  **no `foreignLib`** field.
- The JIT (`jit.cpp:77`) resolves symbols ONLY from the current process via
  `DynamicLibrarySearchGenerator::GetForCurrentProcess` (+ the linked core.bc/std.bc). There is **no
  `dlopen` / no per-`lib` resolution**. So a binding to an EXTERNAL library
  (`@@foreign(lib="libcurl.so", symbol_name="curl_easy_init")`) silently fails to resolve at JIT time.
- Net effect: **only symbols already in the process or in the linked runtime bitcode work.** Binding any
  real external C library — the whole point of "user-defined bindings to external shared libraries"
  (ffi.md §1) — is impossible today.

## Decisions (proposed — D3/D4 are forks for Mux)
- **D1 — parse + store `lib`.** Add `std::string foreignLib;` to `ast::FunctionDef`; in `parser.cpp` store
  `kv.second` when `kv.first == "lib"`.
- **D2 — JIT loads the named library.** Collect the distinct non-sentinel `lib` paths referenced by the
  program's `@@foreign` decls; for each, add an ORC `DynamicLibrarySearchGenerator::Load(path, globalPrefix)`
  generator to the JITDylib so its symbols resolve. (Thread the lib set: codegen/resolver collects it →
  driver/shell → a new `jit::run` parameter, or attach to the module.) `dlopen`-style; resolves lazily.
- **D3 (RESOLVED — Mux, 2026-06-02) — `lib=` uniformly names the providing artifact; NO special-casing of
  std.** Mux: "我不喜欢搞特殊…用户完全可以不用 std 里的任何东西，也就不应该对 std 有任何特殊对待." The loader
  dispatches by file type:
  - **`.so` / `.dylib` / `.dll`** (any C-ABI shared library) → **native dynamic load** (ORC
    `DynamicLibrarySearchGenerator::Load`); the symbol is resolved at runtime (called, not inlined).
  - **`.bc`** (LLVM bitcode) → **IR-link** into the merged module (and thus optimized/inlined by D0).
  - **The stdlib declares `lib="std.bc"`** (std's C impl IS compiled to bitcode — progress17:
    `src/_std/*.cpp → std.bc`); symbols that stayed in core declare **`lib="core.bc"`**. std.bc is treated
    EXACTLY like any user-named lib — no sentinel, no exemption.
  - A program that names no `lib="std.bc"` does NOT get std.bc linked → **the user can opt out of std
    entirely.** (`core.bc` is the exception, but it is the *compiler runtime* — it holds codegen-EMITTED
    symbols `mxs_op_*`/`mxs_retain`/… that are not `@@foreign` — so it is always linked. That is the
    compiler's own runtime, NOT a "std" special-case.)
- **D4 (RESOLVED) — JIT module loading becomes `lib=`-annotation-driven.** Collect the distinct `lib=`
  values from the program's (transitively-resolved) `@@foreign` decls; for each `.bc` → parse + `llvm::Linker`
  into the merged module (the D0 module); for each `.so/.dylib/.dll` → add a
  `DynamicLibrarySearchGenerator::Load(path)`. `core.bc` is always linked (compiler runtime). This SUBSUMES
  today's hardcoded "always link core.bc + std.bc" — std.bc becomes "linked iff some binding names it".
- **D5 — soname or path.** For native libs accept a bare soname (`"libm.so.6"`, `"libcurl.so"`,
  `"libfoo.dylib"`, `"foo.dll"`) resolved by the platform loader, or an absolute path. For `.bc`, resolve via
  the same `find_bc()` search the runtime bitcode already uses (next to the binary, build dirs).
- **D6 — `lib` is mandatory** (per docs/ffi.md); a `@@foreign` missing `lib` is a clean diagnostic. No
  exemptions (std carries `lib="std.bc"`/`"core.bc"` like everyone else).

### Interim coupling with D0 (progress19/task40)
D0 (in flight now) keeps the current hardcoded "link core.bc + std.bc into the merged+optimized module".
That stays correct as an interim; progress23 then refactors jit.cpp so module loading is `lib=`-driven
(D4) — which preserves D0's merge/optimize for `.bc` libs and ADDS native `.so/.dylib/.dll` loading + the
"unused std isn't linked" opt-out. progress23 lands AFTER D0 (both touch jit.cpp; no concurrent edits).

## Relationship to other work
- **Orthogonal to D0** (progress19): external-lib symbols are CALLED (resolved at JIT time), not inlined;
  the bitcode runtime symbols are the ones D0 inlines. No conflict.
- **Touches the std/*.mxs form** (D3): if the sentinel is adopted, every std binding gains `lib="std"`,
  and progress20's new modules (system/io/string/array/net_io) declare it from the start. net_io in
  particular will bind real external/system libraries via `lib`.

## Tasks
- [ ] task41 — parse+store `lib` on FunctionDef (D1); make JIT loading `lib=`-driven (D4): `.bc` →
      Linker-merge (into D0's module), `.so/.dylib/.dll` → `DynamicLibrarySearchGenerator::Load`; `lib`
      mandatory + clean diagnostic (D6); update every `std/*.mxs` binding to `lib="std.bc"` (or
      `lib="core.bc"` for core-resident symbols). Tests: (a) bind a real native symbol, e.g.
      `@@foreign(lib="libm.so.6", symbol_name="sqrt")` and call it; (b) std still works with `lib="std.bc"`;
      (c) a program using no std does not link std.bc; (d) a missing `lib` errors cleanly.

## Agent log
- 2026-06-02 [ai/opus] Recorded from Mux's catch that `@@foreign(lib=...)` (mandatory per docs/ffi.md) is
  parsed-but-discarded and entirely unused — external-library FFI is impossible today.
- 2026-06-02 [ai/opus] Mux RESOLVED the design fork (rejected my sentinel idea): `lib=` uniformly names the
  providing artifact, std gets NO special treatment — `.so/.dylib/.dll` → native dynamic load (C ABI), `.bc`
  → IR-link/merge/inline, std uses `lib="std.bc"`, core-resident symbols `lib="core.bc"`; module loading
  becomes `lib=`-annotation-driven so a program that doesn't name std.bc doesn't link it (core.bc stays
  always-linked as the compiler runtime, not a std special-case). `lib` mandatory. See D3-D6. Lands after
  D0 (shared jit.cpp). NOT executed.
