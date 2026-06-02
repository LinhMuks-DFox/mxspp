# Progress 16 — Reflection / introspection: `typeof`, and `attributes_of` (dir)
id: 2026-06-02/progress16
date: 2026-06-02
author: human+ai
status: active
refs: [2026-06-01/progress13]
supersedes:
commits: [3a5b784]
files:
  - src/core/MXOps.cpp        # mxs_typeof C-ABI primitive (in core.bc)
  - std/types.mxs             # NEW std module: typeof (layer-1) + is_instance_of (layer-2, pure mxs)

## Goal
Add runtime reflection to MXScript: (1) **`typeof(x)`** — the type name of a value; (2)
**`attributes_of(obj)`** (a `dir`-style introspection) — the members of an object. Both reached
through `import` (D2 import-gated).

## Decisions
- **D1 — `typeof` is a function in a std module — Mux, 2026-06-02.** `import std.types.{typeof};`
  then `typeof(x)` (also `import std.types;` → `types.typeof(...)`, and `as`). Returns the type name
  as a **string**: a user-class instance → its class name (MXClassInfo->name); built-ins →
  `int`/`float`/`str`/`bool`/`nil`/`List`/`Error`. Backed by a new `mxs_typeof` C-ABI in `core.bc`
  (the inverse of `mxs_is_type`'s name mapping). Chosen over a method `x.type()` (D4) or a `typeof`
  keyword — Mux wants the function form. **DONE & verified.**
- **D2 — `attributes_of(obj)` (dir): OPEN, pending design clarification.** Mux's sketch:
  `import std.{io, lib}; std.lib.attributes_of(io);` — which implies two not-yet-existing pieces:
  (a) a **multi-module selective import** `import std.{io, lib}` (today only `import std.io.{fn,…}`
  selects *functions* of one module; selecting *modules* of a package is new), and (b) **passing a
  module/namespace as a first-class value** to `attributes_of` (today namespaces are compile-time
  routing only, not values). The simpler case — `attributes_of(instance)` listing a user instance's
  fields (and/or methods) — is implementable now (MXInstance holds its fields; MXClassInfo holds the
  method/selector names). Needs Mux to scope: instances only (quick), or also modules (requires
  first-class namespaces + the new import syntax = a larger change, likely its own progress).

### D3 — `is_instance_of` is a pure-mxs (layer-2) function over `typeof` — Mux, 2026-06-02
- Decision: `is_instance_of(x, cls)` is implemented in mxs itself as `return typeof(x) == cls;` (no
  C primitive), per the two-layer design — Mux: "mxs应该可以原生实现is_instance_of…Date之类的也是可以
  原生mxs实现的." Layer-1 = thin C `@@foreign` primitives (typeof, time, …); layer-2 = rich mxs built
  on them. **DONE** as native mxs — BUT see the Finding below: it only works today under
  `import std.types.{is_instance_of, typeof};` (both selected).
- **FINDING (blocks the layer-2 vision) — modules are not real namespaces yet.** A module's mxs
  function calling a sibling fails under import, because the resolver does a *flat decl merge*, not
  namespaced scoping: qualified `import std.types;` renames `typeof`→`types.typeof` but does NOT
  rewrite `is_instance_of`'s body (which calls bare `typeof`) → "unknown function 'typeof'";
  selective `import std.types.{is_instance_of}` doesn't merge `typeof` at all. Only
  `import std.types.{is_instance_of, typeof}` works. To realize Mux's layer-2 stdlib (is_instance_of,
  Date, attributes_of, … in pure mxs over C primitives), **the import/module system must become real
  namespaces** (intra-module references resolve; a module is a unit). This is the key enabler and
  warrants its own progress.

## Tasks
- [x] [task27 — typeof primitive + is_instance_of (native mxs)](tasks/task27-typeof.md) — DONE & verified
- [ ] task28 — attributes_of / dir — BLOCKED on D2 scope clarification + the module-namespace work
- [ ] (next progress) — module system → real namespaces (enables layer-2 mxs stdlib). See Finding/D3.

## Note (perf observation, 2026-06-02 — not part of this progress's code)
While answering Mux's "is mxs slow due to my design or the pipeline?" question, measured with the
working `std.time` module: the JIT **pipeline** is a fixed ~660 ms per `run-core` (parse + codegen +
load/link core.bc + ORC materialize) — negligible for long programs. The **runtime** is the
bottleneck: a 1e6-iteration integer loop took ~5.3 µs/iteration (~10000× a native C loop). Cost is
the object model: every int/bool is a heap-allocated refcounted MXObject, every op is a
dynamic-dispatch `mxs_op_*` C call, and **every object construct/destruct takes a `MXPopulationManager`
mutex lock + unordered_set insert/erase** (an ARC-verification debug tool that is always on, even on
the hot path). Optimization targets, if Mux pursues them (own progress): gate the population manager
off in release builds; cache/unbox small integers; type-specialize arithmetic. Recorded here so the
finding isn't lost; not acted on.

## Agent log
- 2026-06-02 [ai/opus] Implemented `typeof` (D1): `mxs_typeof` in `src/core/MXOps.cpp` (returns a
  fresh MXString of the type name, instance → class_name(), built-ins → canonical names) +
  `std/types.mxs` binding it via `@@foreign`. Verified all import forms: `typeof(p)`→`Point`,
  `typeof(42)`→`int`, `3.5`→`float`, `"hi"`→`str`, `true`→`bool`, `nil`→`nil`, `[1,2]`→`List`;
  `types.typeof(42)`→`int`. `ninja -C build` clean (core.bc relinked); ctest 3/3. Surfaced the
  `attributes_of` design questions (D2) to Mux rather than guessing. Recorded the perf finding above.
