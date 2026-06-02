# Task 20 — Container/string operations as OOP methods (builtin-method dispatch)
id: 2026-06-01/task20
parent: 2026-06-01/progress13
status: done           # all methods, no global len/append — implemented & verified
owner: code_agent

## Objective
Make built-in container/string operations method calls on the receiver (`xs.append(v)`), and remove the
OOP-violating free functions (`append(xs, v)`).

## Scope
In:
- A built-in method dispatch table in codegen: `recv.m(args)` with no user selector but a known built-in
  method → lower to the runtime symbol with the receiver as arg0.
- Migrate the list/string surface (`append`, `len`/`size`, subscript already works via `[]`).
- Remove `append` (and per D4, `len`) from the free-function prelude bindings.
Out:
- Giving built-ins full `MXClassInfo`/vtables (deferred; a codegen table suffices for v1).

## Inputs (read first, priority order)
1. `src/backend/codegen.cpp` — method-call path (~434-464): today requires `selectors->count(name)` and
   routes via `mxs_object_classinfo(recv)->vtable[slot]` (null for built-ins → would crash).
2. `src/core/MXArrayList.cpp/.h` — the runtime surface: `mxs_arraylist_append`, `mxs_len`,
   `mxs_index_get/set`, `concat`.
3. `src/core/MXString.cpp` — string ops surface.
4. `develop_log/2026-06-01/progress13-...md` — D4 (+ I3).
5. `example/examples/syntax_reference.mxs` (~92-98) — current free-function usage to migrate.

## Deliverables
- codegen — in the `call->receiver` branch, before the user-selector path: if `name` is a built-in
  method, emit a direct call to its runtime symbol `(recv, args…)` with the correct return type, and
  apply the existing ARC convention for built-in/runtime calls (caller-owned: callee borrows; release
  operands; the result is +1). e.g. `xs.append(v)` → `mxs_arraylist_append(xs, v)` (void → yields nil);
  `xs.len()` → `mxs_len(xs)`.
- Prelude/std — drop the `append` (and `len`, per D4) free-function bindings.
- Migrate examples/tests + update `docs/syntax.md` §3.1 (remove the "current limitation" note).

## Steps
1. **Surface (RESOLVED by Mux): all methods, no globals.** `xs.append(v)`, `xs.len()`, `"hi".len()`;
   remove BOTH `len` and `append` from the global/prelude bindings (no Python-style global `len`).
   List surface: `append`, `len`, `get`, `set`, `concat` (map to the existing `mxs_arraylist_*`/`mxs_len`/
   `mxs_index_*` runtime fns); String: `len` (+ more later).
2. **Add the dispatch table** — a static map keyed by method name (v1: receiver kind not statically
   known, so route purely by method name to the polymorphic runtime symbol; `mxs_len`/`mxs_index_*` are
   already polymorphic over list/string).
3. **Lower** built-in method calls; handle void-returning ones (yield `nil`) + ARC.
4. **Remove** the free-function bindings; migrate call sites.
5. **Build + verify** the repros.

## Acceptance criteria
- [ ] `let xs=[1,2,3]; xs.append(4); println(xs);` → `[1, 2, 3, 4]`.
- [ ] `xs.len()` / `"hi".len()` return the length.
- [ ] `append(xs, 4)` (free fn) no longer resolves (removed from surface).
- [ ] `ctest` green; migrated demos green; no ARC leak (population-count baseline).

## Constraints
- Keep built-ins zero-overhead (no MXClassInfo allocation); reuse the existing polymorphic runtime fns.
- Honor the ARC protocol for runtime/`@@foreign`-style calls (progress11).

## Notes / Assumptions
- RESOLVED (Mux): all methods, no global `len`/`append`.
- Assumption: v1 dispatches by method name to polymorphic runtime symbols (no static receiver typing).
- Coordinate with task18: the prelude no longer binds `len`/`append` at all (they become methods here).
