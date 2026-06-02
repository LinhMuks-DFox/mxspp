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
- **D3 (FORK) — how do the STDLIB's own `mxs_*` bindings declare `lib`?** They live in core.bc/std.bc
  (JIT-linked, NOT an external .so), and shipping the runtime as bitcode is deliberate (enables D0
  cross-module inlining — progress19). Options: (a) a reserved **sentinel** `lib="std"` / `lib="core"` (or
  `lib="@runtime"`) that means "resolve from the linked runtime bitcode / process" — no dlopen; external
  bindings use a real path/soname. (b) keep stdlib bindings `lib`-less and require `lib` only for external
  libs (contradicts "Mandatory"). (c) ship the runtime as `runtime.so` and use `lib="runtime.so"`
  (contradicts the bitcode/inlining design). **Recommend (a)** — keeps `lib` mandatory + honest while
  preserving the bitcode runtime. Then update all `std/*.mxs` to `lib="std"` (or "core") + the new progress20
  modules follow suit. Confirm with Mux.
- **D4 (FORK) — enforcement.** Make `lib` required (a clear diagnostic if a `@@foreign` omits it), per the
  spec? Couples to D3 (the stdlib must then carry the sentinel). Recommend: required, with the sentinel for
  runtime symbols.
- **D5 — soname vs path.** Accept both a bare soname (`"libm.so.6"` / `"libcurl.so"`, resolved by the
  loader's search path) and an absolute path. ORC's `Load` takes a filename the dynamic loader resolves.

## Relationship to other work
- **Orthogonal to D0** (progress19): external-lib symbols are CALLED (resolved at JIT time), not inlined;
  the bitcode runtime symbols are the ones D0 inlines. No conflict.
- **Touches the std/*.mxs form** (D3): if the sentinel is adopted, every std binding gains `lib="std"`,
  and progress20's new modules (system/io/string/array/net_io) declare it from the start. net_io in
  particular will bind real external/system libraries via `lib`.

## Tasks
- [ ] task41 — parse+store `lib` (D1) + JIT external-library loading (D2) + the runtime sentinel (D3) +
      enforcement (D4); test by binding a real external symbol (e.g. `libm` `pow`/`sqrt` via
      `@@foreign(lib="libm.so.6", symbol_name="sqrt")`) and calling it, plus the sentinel path for stdlib.

## Agent log
- 2026-06-02 [ai/opus] Recorded from Mux's catch that `@@foreign(lib=...)` (mandatory per docs/ffi.md) is
  parsed-but-discarded and entirely unused — external-library FFI is impossible today. Captured the
  parse+store+JIT-load plan and the stdlib-sentinel design fork (D3/D4). Orthogonal to the std batch and
  to D0; slot per Mux's priority. NOT executed.
