# Task 17 — Import / module system (resolve + load std/*.mxs)
id: 2026-06-01/task17
parent: 2026-06-01/progress13
status: active         # D2 OPEN RESOLVED (Mux): qualified-default + selective `.{...}`; filesystem resolution
owner: code_agent

## Objective
Let a program reach stdlib only via `import` — resolve an `fqdn` to an `std/<path>.mxs` module, parse it,
and make its declarations available to the importing unit.

## Scope
In:
- An `Import` AST node + parser action for `import fqdn [as name];` (grammar already parses it).
- A module resolver (fqdn → file) + loader that parses the module and merges its top-level decls.
- Name scoping per the chosen namespacing policy.
Out:
- Generic module caching / circular-import graphs beyond what std needs (keep minimal).
- The actual std module contents (task18/task19).

## Inputs (read first, priority order)
1. `develop_log/2026-06-01/progress13-...md` — D2 + I-OPEN (resolution + namespacing decisions).
2. `src/driver/main.cpp` — current `kCorePrelude` injection + `find_bc()` path discovery (the model for
   locating files next to the binary).
3. `src/frontend/parser.cpp`, `include/mxspp/frontend/ast.h` — where to add the Import node/action.
4. `include/mxspp/frontend/grammar.hpp` — `import_stmt` (~403).
5. `docs/ffi.md`, `docs/type_system.md` §8 — the two-layer std model.

## Deliverables
- `import` AST node + parser wiring.
- A resolver: `std.io` → `<std-dir>/io.mxs` on a search path (mechanism per D2(a)).
- A loader: parse the module, merge decls into the compilation unit with scoping per D2(b).
- Errors: unresolved module, parse error in a module, name clash — clear diagnostics.

## Steps
1. **Design (RESOLVED by Mux — "1+3"):** (a) resolution = **filesystem search path** for `std/*.mxs`
   next to the binary (reuse the `find_bc()` pattern; e.g. `<exe-dir>/std/`, then build-relative
   fallbacks). (b) namespacing = **qualified-by-default + selective-unqualified**:
   - `import std.io;` → binds the module namespace `io`; call `io.println(...)`.
   - `import std.io as foo;` → namespace `foo`; `foo.println(...)`.
   - `import std.io.{println, format};` → those names in scope **unqualified** (extend the grammar's
     `import_stmt` with an optional `. "{" identifier_list "}"` selector form).
   FRONTEND NOTE: qualified `io.println(args)` must be told apart from a method call `value.m(args)` —
   resolve a leading identifier that names an imported module to a module-qualified function, not a
   `MemberExpr` on a value. No always-in-scope builtins (even `print` needs an import).
2. **AST + parser** — Import node; collect fqdn + optional alias.
3. **Resolver + loader** — locate, parse, merge; respect the namespacing policy.
4. **Wire the driver** — replace `kCorePrelude` auto-injection with import resolution (coordinate with
   task18, which moves the prelude content into std/*.mxs).
5. **Tests** — import resolves; missing import → the symbol is NOT in scope (the core requirement).

## Acceptance criteria
- [ ] A program with no `import` cannot call `println` (or any stdlib fn) — it is out of scope.
- [ ] `import std.io;` then `println(...)` runs (per the chosen namespacing).
- [ ] Unresolved `import std.nope;` → a clear error, non-zero exit.
- [ ] `ctest` green.

## Constraints
- Honor D2: nothing in scope without an import. No implicit prelude.

## Notes / Assumptions
- RESOLVED (Mux): filesystem resolution; qualified-default + selective `.{...}` namespacing ("1+3").
- This is the largest task; coordinate the driver hand-off with task18 (which moves the prelude content
  into std/*.mxs). Consider landing the import loader + a tiny std module first, then task18 migrates
  the rest.
