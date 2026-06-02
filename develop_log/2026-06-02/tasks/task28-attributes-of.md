# Task 28 — attributes_of(obj) / dir
id: 2026-06-02/task28
parent: 2026-06-02/progress16
status: blocked
owner: code_agent
blocked-on: scope decision (instances vs modules) + progress18 (real namespaces) for the module case

## Objective
A `dir`-style introspection that lists the members of an object. Mux's sketch:
`import std.{io, lib}; std.lib.attributes_of(io);`.

## Open design (needs Mux) — two scopes, very different effort
1. **`attributes_of(instance)` — IMPLEMENTABLE NOW.** List a user-class instance's field names (and/or
   method names from its MXClassInfo). A layer-1 C primitive `mxs_attributes_of(obj) -> MXArrayList`
   reading MXInstance's fields + MXClassInfo's selectors, bound in a std module (`std.types` or the
   `std.lib` Mux named). Returns a list of strings.
2. **`attributes_of(module/namespace)` — Mux's actual example (`attributes_of(io)`) — LARGER.** Needs
   namespaces to be **first-class values** you can pass to a function (today they are compile-time
   routing only, not values), AND likely the multi-module import `import std.{io, lib}` sugar. Depends
   on progress18 (modules as real namespaces).

## Steps (once scoped)
- If instances-only: add `mxs_attributes_of` to the std backend (src/std per progress17), bind in the
  chosen std module, return the field/method name list. Tests for a class with fields + methods.
- If modules-too: first land progress18 (first-class namespaces) + the multi-module import, then a
  reflection over a namespace's merged decls.

## Acceptance
- [ ] (instances) `attributes_of(p)` for a `class Point { let x; let y; func sum(){...} }` lists the
      members; documented + tested.
- [ ] (modules, if chosen) `attributes_of(io)` lists io's exported names.

## Notes
- Decide the home module (Mux's sketch used `std.lib`; typeof/is_instance_of are in `std.types`).
- This is part of the reflection theme (progress16) but the module case ties into progress18.
