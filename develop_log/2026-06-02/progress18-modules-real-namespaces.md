# Progress 18 — Modules as real namespaces (enable layer-2 mxs stdlib)
id: 2026-06-02/progress18
date: 2026-06-02
author: human+ai
status: done (A–D; D-MODLET deferred to a follow-up)
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
  come after the core namespace fix; tracked here but not required for layer-2 to work. (Grammar note:
  the current selector is `.{ identifier_list }` of *names*, not module paths — D3 needs grammar work,
  `grammar.hpp:405-412` / `parser.cpp:477-495`.)

### Recommended implementation — name-mangle (survey 2026-06-02, grounded in imports.cpp)
The resolver (`imports.cpp::resolve_imports`, 59-200) is a flat decl-merge: it only ever changes
`fn->name` (`take_fn`, 136-143); **function bodies are never rewritten**. That is the whole bug. Fix
(Direction (a), closest to current machinery, confined to `imports.cpp` + a small codegen exposure
table):
1. Give each module a unique internal prefix (e.g. `__mod$std$types$`); rename **every** module fn to
   `prefix+name` (not just the exposed ones).
2. **NEW code — rewrite intra-module bare call references**: a recursive AST visitor over each module
   fn body; for every `FunctionCall` with no receiver whose name matches a sibling in `modFns`, rewrite
   `name -> prefix+name`. This is the missing step that breaks sibling calls today.
3. Expose per import form via an **alias/exposure table** in `struct Resolution` (`exposedName ->
   mangledSymbol`), consumed by codegen: qualified → `ns.fn`; selective → listed bare names; unlisted
   siblings stay module-private under the mangled name (non-colliding). `@@foreign` fns: mangle the
   `funcs`/`foreigns` key but keep `foreignSymbol` (the C symbol) untouched (`codegen.cpp:50,62,74`).

### D-CLASS (NEW, survey) — classes must survive import
The merge indexes/moves **only `ast::FunctionDef`** nodes; a `class FileStream {...}` defined in an
imported module is **silently dropped** → never reaches codegen. std.io's layer-2 `FileStream` (and any
class-based stdlib) is impossible until the import merge also carries `ClassDef` (and `Binding`/`let`)
nodes, with the same qualified/selective/alias + mangling semantics. **Hard prerequisite for progress20.**

### D-TRANSITIVE (NEW, survey) — nested/transitive imports
Nested imports are hard-rejected (`imports.cpp:114-126`): a module containing `import …` fails fast.
But `std.io` must `import std.system;` to reach the primitives. Replace the rejection with **recursive
resolution**: resolve each module's own imports first (each with its own prefix), depth-first, with cycle
detection. This is what makes the `user_script -> std.io -> std.system -> @@foreign` pipeline possible.
Also fixes the dead `import std._fileio;` in `std/io.mxs` (and the `_file`/`_fileio` name mismatch, §3).

### D-MODLET (NEW, survey) — module-level `let` singletons
std.io wants `let stdout = FileStream(1, …)` at module scope (evaluated once at load, shared). The
`obj_*` examples only construct instances *inside* `main`, not at top level — verify the binding/codegen
path supports module-scope `let` of a class instance; fallback = an init-on-first-use accessor
(`func stdout() -> FileStream`). Verify during task30; may split to its own task if codegen-heavy.

## Tasks
- [ ] task30 — modules-as-namespaces: (a) name-mangle + intra-module call rewrite so sibling calls
      resolve under every import form; (b) classes (+ bindings) survive import (D-CLASS); (c) transitive
      imports via recursion + cycle detection (D-TRANSITIVE) + `_file`/`_fileio` name fix; (d) verify
      module-level `let` singletons (D-MODLET). Re-verify `is_instance_of` under `import std.types;` and
      selective-single; verify a `class` in a module is importable and a module can import a sibling.
- [ ] (optional, later) task — multi-module import `import std.{a, b};` sugar (D3).

## Beneficiaries (unblocked by this)
- `std.types.is_instance_of` working under qualified / selective-single import (progress16 D3).
- A pure-mxs `std.date` (Date over the `std.time` primitives) — Mux's example.
- `attributes_of` on a module/namespace (progress16 D2) if namespaces become addressable.

## Agent log
- 2026-06-02 [ai/opus] Recorded per the batch-record-first workflow. Captures the flat-merge-vs-real-
  namespaces gap found while implementing `is_instance_of` (progress16) and the design directions.
  NOT executed — part of the current requirements batch awaiting Mux's go + ordering.
- 2026-06-02 [ai/opus] IMPLEMENTED (sub-agent) + independently re-verified (parent). A–D landed
  (name-mangle + RefRewriter intra-module rewrite + exposure table threaded to codegen + classes survive
  import + transitive imports with cycle detection). D-MODLET (module-level `let` singletons) deferred —
  it is blocked below the resolver (top-level binding is a parse error / dropped in AST conversion; no
  module-scope global in codegen) → needs a parser+codegen follow-up task (the accessor-function fallback
  works today). The sub-agent's adversarial verifier died mid-run on an API socket error, so the parent
  re-verified independently against HEAD std: ninja clean, ctest 3/3, corpus 36/36 (+2 new import_sibling
  cases), examples clean, the headline sibling-call fix works, a class-in-imported-module + transitive +
  in-method cross-module call probe → 105, and a cyclic import is diagnosed (no hang). Committed (code +
  the 2 corpus cases; Mux's WIP std/*.mxs left untouched). See task30 Outcome.
- 2026-06-02 [ai/opus] EXPANDED after the std-architecture survey. The layer-2 std.io vision
  (progress20) needs the import system fully real, not just function sibling calls: added D-CLASS
  (classes/bindings must survive import — the merge drops everything but `FunctionDef`), D-TRANSITIVE
  (nested imports must recurse, currently hard-rejected), D-MODLET (module-level `let` singletons), and
  the grounded name-mangle implementation direction (recursive AST call-rewrite + exposure table). This
  is now the central enabler for the whole std-체계 batch (progress20).
