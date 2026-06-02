# Progress 22 — Modules as first-class objects (MXCodeModule) + attributes_of over modules

id: 2026-06-02/progress22
date: 2026-06-02
author: human+ai
status: active
refs: [2026-06-02/progress16, 2026-06-02/progress18, 2026-06-02/progress19, 2026-06-02/progress20]
supersedes:
commits: []
files:
  - include/mxspp/core/MXCodeModule.h + src/core/MXCodeModule.cpp  # NEW MXObject subtype (runtime module value)
  - src/frontend/imports.cpp / src/backend/codegen.cpp            # materialize an MXCodeModule per imported module
  - src/_std/types.cpp + std/types.mxs                           # attributes_of(x) over instances AND modules

## Origin (Mux, 2026-06-02 — answers F6)
Mux: "模块何尝不是一个 MXObject 呢？`MXCodeModule : MXObject`，`MXCodeModule::sub_modules`
`std::vector<MXCodeModule*>`（性能好的优先），这样不就方便你去搞 attributes_of 了吗？" — make a module a
first-class runtime object so reflection (`attributes_of(io)`) is natural. Answers progress16 **F6**:
`attributes_of` covers **modules too**, not just instances.

## Current state (why this is new)
Modules are a **compile-time** construct today: `resolve_imports` flat-merges a module's decls and a
"namespace" is just codegen-time name routing (`io.fn` → a direct LLVM call recorded in
`Resolution::namespaces`). There is **no runtime module object** — `MXObject` has no `MXModule` subtype,
and `io` is not a value you can pass or introspect. progress18 keeps this compile-time model (mangle +
exposure table). So a runtime module object is genuinely new machinery.

## Key decision — COMPOSE, don't replace (perf-driven)
- **D1 — calls stay compile-time; reflection/value uses MXCodeModule.** If module member ACCESS for
  calls (`io.println(x)`) went through a runtime `MXCodeModule` member-lookup + dynamic dispatch, every
  stdlib call would become a hash-lookup + indirect call instead of today's compile-time-resolved direct
  native call — a runtime regression that fights progress19's speed goal. So: **keep compile-time
  resolution for calls (progress18)**, and add `MXCodeModule` as a **runtime reflection / first-class
  value** object. Best of both: `io.println(...)` is a fast native call; `attributes_of(io)` /
  `let m = io;` use the runtime object. This is additive — it does NOT supersede progress18.
- **D2 — `MXCodeModule : MXObject`** (a core object-model type, like `MXInstance`): RTTI, `clone()`, ARC,
  `repr()` per docs/develop_rule.md. Fields: `name` (str), `sub_modules` as **`std::vector<MXCodeModule*>`**
  (vector over list — contiguous, cache-friendly, no mid-insertion; Mux: "性能好的优先"), and a member
  table (exported function/class/let names → for `attributes_of`; optionally their mangled symbols).
- **D3 — population.** The resolver already knows each imported module's decls + sub-structure; it
  materializes an `MXCodeModule` per imported module (name + member-name list + sub_modules) and binds it
  to the namespace identifier so `io` is addressable as a runtime value. This realizes the "first-class
  namespace value" progress16 D2 flagged as missing. (Implementation bridge: compile-time module info →
  a runtime descriptor constructed at module load. Exact mechanism = open, see below.)
- **D4 — `attributes_of(x)`** (in `std.types`, layer-2 over a `mxs_attributes_of` primitive): for an
  **instance**, list its fields + its class's method names (MXInstance fields + MXClassInfo selectors);
  for an **MXCodeModule**, list its member names + sub_modules. Returns an `MXArrayList` of `str`.

## Open questions (resolve at execution)
- The bridge in D3: how/where to construct the runtime `MXCodeModule` from the compile-time module
  metadata, and how `io` resolves to BOTH a compile-time call target AND a runtime value without
  ambiguity (likely: `io.fn(...)` stays compile-time routed; bare `io` as a value yields the
  MXCodeModule). Needs a small codegen rule.
- Whether sub_modules matters for v1 (the std tree is shallow: `std.io`, `std.system`, …). Could ship the
  flat member list first and add nested `sub_modules` when packages (progress21 D4) exist.

## Relationship to other progresses
- **Builds on progress18** (compile-time resolution stays the call path) — does NOT block or replace it.
- **Folds in progress16 D2/task28** (`attributes_of`): this progress supersedes the "instances vs modules"
  fork — answer is BOTH, via MXCodeModule for the module case.
- **Downstream of progress20** (needs the std tree + `mxs_attributes_of` primitive home in `src/_std`).

## Tasks
- [ ] task38 — `MXCodeModule` core type (MXObject subtype; name + vector<MXCodeModule*> sub_modules +
      member table; RTTI/clone/repr) + resolver materialization + `io`-as-value codegen rule.
- [ ] task39 — `attributes_of(x)` primitive (`mxs_attributes_of` in `src/_std/types.cpp`) over instances
      AND MXCodeModule; bind in `std/types.mxs`; tests. (Supersedes progress16/task28.)

## Agent log
- 2026-06-02 [ai/opus] Recorded per batch-record-first, from Mux's F6 answer (modules as MXObjects). Key
  call: COMPOSE with progress18 (compile-time calls) rather than route calls through a runtime module
  object (which would regress runtime speed — progress19). MXCodeModule = a runtime reflection/value
  layer enabling `attributes_of(io)` + first-class namespace values. Does not disturb the in-flight
  progress18. NOT executed — downstream of progress20.
