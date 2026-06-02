# Progress 18 — Modules as real namespaces (enable layer-2 mxs stdlib)
id: 2026-06-02/progress18
date: 2026-06-02
author: human+ai
status: active
refs: [2026-06-01/progress13, 2026-06-02/progress16, 2026-06-02/progress17]
supersedes:
commits: []
files:
  - src/frontend/imports.cpp            # merge a module as a unit (intra-module refs resolve)
  - src/backend/codegen.cpp             # module-scoped name resolution for `ns.fn` + sibling calls
  - include/mxspp/frontend/ast.h, src/frontend/parser.cpp  # (maybe) multi-module import `import a.{b, c}`

## Goal
Make `import`ed modules behave like **real namespaces** so that **layer-2 stdlib written in pure mxs**
works — a module function can call its siblings, and qualified access keeps internal calls intact.
Mux's direction: "mxs 应该可以原生实现 is_instance_of … Date 之类的也是可以原生 mxs 实现的" — rich stdlib
in mxs over thin `@@foreign` C primitives. This progress removes the blocker that stops that today.

## Context / the gap (found in progress16, reproduced)
The resolver does a **flat declaration merge**, not namespaced scoping, so a module function calling a
sibling breaks under import:
- qualified `import std.types;` renames `typeof`→`types.typeof` but does NOT rewrite the body of
  `is_instance_of` (which calls bare `typeof`) → `unknown function 'typeof'`;
- selective `import std.types.{is_instance_of}` doesn't merge `typeof` at all;
- only `import std.types.{is_instance_of, typeof}` (both selected) works today.
Result: a module's internal helpers/wrappers (the essence of a layer-2 stdlib) are unusable.

## Decisions (proposed — refine at execution)
- **D1 — a module is resolved as a unit with internal scope.** When a module is imported (any form),
  its top-level decls resolve references *among themselves* by bare name (the module's own scope),
  independent of how the importing program sees them. Concretely: merge ALL of a module's top-level
  functions so intra-module calls bind, and expose to the importing TU only what the import form
  selects — qualified → `ns.fn`; selective → the listed bare names; the rest stay module-private
  (not callable from the program, and not colliding with program names).
- **D2 — possible implementation directions (pick during design):** (a) name-mangle every module's
  decls to a unique internal prefix and rewrite intra-module call references to that prefix, exposing
  aliases per the import form; or (b) teach codegen a module-scoped symbol table so `ns.fn` and
  in-module bare calls both resolve. (a) is closer to the current flat-merge machinery.
- **D3 — multi-module import sugar (from Mux's `import std.{io, lib};` sketch): SEPARATE, optional.**
  Selecting several *modules* of a package in one statement is a grammar/resolver convenience that can
  come after the core namespace fix; tracked here but not required for layer-2 to work.

## Tasks
- [ ] task30 — modules-as-namespaces: intra-module references resolve under every import form
      (the core fix; re-verify is_instance_of works under `import std.types;` and selective-single).
- [ ] (optional, later) task — multi-module import `import std.{a, b};` sugar (D3).

## Beneficiaries (unblocked by this)
- `std.types.is_instance_of` working under qualified / selective-single import (progress16 D3).
- A pure-mxs `std.date` (Date over the `std.time` primitives) — Mux's example.
- `attributes_of` on a module/namespace (progress16 D2) if namespaces become addressable.

## Agent log
- 2026-06-02 [ai/opus] Recorded per the batch-record-first workflow. Captures the flat-merge-vs-real-
  namespaces gap found while implementing `is_instance_of` (progress16) and the design directions.
  NOT executed — part of the current requirements batch awaiting Mux's go + ordering.
