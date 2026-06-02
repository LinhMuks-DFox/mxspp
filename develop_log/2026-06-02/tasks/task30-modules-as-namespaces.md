# Task 30 — Modules as real namespaces (the layer-2 enabler)
id: 2026-06-02/task30
parent: 2026-06-02/progress18
status: done (A–D landed; E/D-MODLET deferred with reason)
owner: code_agent
blocked-on: (was) progress17 — now done

## Outcome (2026-06-02, implemented via sub-agent + independently re-verified by the parent)
A–D DONE. Mechanism: each module gets a unique prefix `__mod$<fqdn>$`; a new recursive `RefRewriter`
rewrites intra-module bare refs (sibling fn calls, sibling `C(...)` ctors, bare identifiers, match
type-binding names, and own-nested-import `ns.fn` calls) to the mangled name; an exposure table
`Resolution::exposed` (surface→mangled) is threaded into codegen (compile_core 5th param; CoreGen.exposed;
translated at the call site in codegen_expr.cpp) so qualified `ns.fn`/`ns.C` and selective bare names map
to mangled `funcs` keys, with unlisted siblings staying module-private. ClassDef nodes now survive import
(D-CLASS). The nested-import rejection is replaced by depth-first transitive recursion with an `inProgress`
cycle-detection set (D-TRANSITIVE). `@@foreign`: only the funcs key is mangled, `foreignSymbol` untouched.
The one wiring bug (compile_core's default `exposed={}` meant the table never reached codegen until the
driver/shell passed `imp.exposed`) was fixed.
**E / D-MODLET DEFERRED (with reason):** module-level `let` is blocked *below* the resolver — a top-level
binding is a parse error / silently dropped in parse-tree→AST, and codegen has no module-scope global. Needs
a parser + codegen follow-up (its own task); the accessor-function fallback works today.
**Independent re-verification (parent, against HEAD std — the sub-agent's adversarial verifier died on an
API error, so I re-ran it):** ninja clean; ctest 3/3; corpus 36/36 (incl. 2 new import_sibling cases);
example sweep no symbol/link failures; `import std.types; types.is_instance_of(42,"int")` → true/false (the
headline sibling-call fix); a combined probe (main→pmid→pleaf, class `Box` imported, `pleaf.leaf_val()`
called inside `Box.get`) → 105; a cyclic import → clean `cyclic import detected` diagnostic, no hang.
Mux's WIP std restored byte-identical.

## Objective
Make `import`ed modules behave like real namespaces so a pure-mxs layer-2 stdlib works: intra-module
references bind under every import form, classes (and bindings) survive import, and a module can import
a sibling module (transitive). This unblocks progress20's `FileStream`-in-mxs std.io.

## Steps (Direction (a) name-mangle; all confined to src/frontend/imports.cpp + a small codegen read)
1. **Mangle every module fn** to a unique prefix (e.g. `__mod$std$types$` from the fqdn) — all of them,
   not just exposed ones (`take_fn`, imports.cpp:136-143).
2. **Intra-module call rewrite (new)**: a recursive AST visitor over each module fn body; rewrite every
   no-receiver `FunctionCall` whose name is a sibling in `modFns` to `prefix+name`. (No visitor exists in
   imports.cpp today.)
3. **Exposure table**: add `exposedName -> mangledSymbol` to `struct Resolution`; codegen resolves
   `ns.fn` (qualified) and selective bare names through it (`codegen.cpp:50,62,74`,
   `codegen_expr.cpp:208-222,346-350`). Unlisted siblings stay private under the mangled name.
   `@@foreign`: mangle the `funcs`/`foreigns` key, keep `foreignSymbol` untouched.
4. **D-CLASS**: extend the merge to carry `ast::ClassDef` (and top-level `let`/binding) nodes, with the
   same qualified/selective/alias semantics (today only `FunctionDef` is merged → classes dropped).
5. **D-TRANSITIVE**: replace the nested-import rejection (imports.cpp:114-126) with recursive resolution
   (resolve a module's own imports first, depth-first, each with its own prefix) + cycle detection. Fix
   the `std/io.mxs` `import std._fileio;` → `std._file` name mismatch (std/_file.mxs).
6. **D-MODLET**: verify module-level `let` of a class instance evaluates once at load and is shared;
   fallback to an accessor function if codegen needs it.

## Acceptance
- [ ] `import std.types;` then `types.is_instance_of(42, "int")` → true (sibling `typeof` resolves).
- [ ] `import std.types.{is_instance_of};` (single) works (typeof merged privately).
- [ ] A module defining `class C {...}` is importable and `C` is constructible after import.
- [ ] A module can `import` a sibling module; a 2-level chain (io → system) resolves; a cycle is
      diagnosed, not infinite-looped.
- [ ] `ninja -C build` clean; ctest 3/3; corpus + examples green; a new corpus case per the above.

## Notes
- Direction (b) (module-scoped symbol table in codegen) rejected as more invasive — see progress18.
- D-CLASS + D-MODLET may split into their own task if codegen changes are large; keep task30 to the
  resolver core (mangle + rewrite + transitive) if it grows.
