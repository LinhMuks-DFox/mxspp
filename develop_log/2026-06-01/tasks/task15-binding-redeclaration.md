# Task 15 — Binding semantics: reject same-scope redeclaration (+ REPL policy)
id: 2026-06-01/task15
parent: 2026-06-01/progress13
status: done
owner: code_agent

## Objective
Make declaring a name twice in the same scope a compile-time error (C++-like), while keeping nested-
block shadowing legal and the REPL usable.

## Scope
In:
- Same-scope redeclaration check in codegen.
- REPL redefinition policy = **strict error + `:reset`** (Mux, RESOLVED): re-`let` of an existing name
  errors (the codegen check already produces this via the replayed accumulated `let`s — do NOT add
  replace-prior); add a `:reset` command that clears accumulated `lets`/`defs` so the user can redefine.
Out:
- Immutable-assignment enforcement (already done — Issue I1; just keep green).
- REPL mutation-persistence redesign (Open/TODO).

## Inputs (read first, priority order)
1. `src/backend/codegen.cpp` — `bind()` (~170-180), `block()` save/restore (~185-194), the `scopes`
   stack, the LetStatement handler (~600-611), param/for-var binding (~835-845, ~749-780).
2. `src/shell/shell.cpp` — REPL `lets` accumulation (~92-134).
3. `develop_log/2026-06-01/progress13-...md` — D5 + Issue I2/I7.

## Deliverables
- `src/backend/codegen.cpp` — a per-scope declared-names set (parallel to `scopes`); `bind()` (or the
  Let handler) errors on a name already declared *in the current scope*; nested-block shadowing still
  allowed (push/pop with the scope). Error text e.g. `redeclaration of 'a' in the same scope`.
- `src/shell/shell.cpp` — a `:reset` command clearing accumulated `lets`/`defs`. Do NOT dedup/replace:
  re-`let` SHOULD hit the new redeclaration error (strict, per Mux). The error arises naturally once the
  codegen check lands (the replayed `let a` + new `let a` collide in one `main()` scope).

## Steps
1. **REPL policy = strict + `:reset`** (RESOLVED — no confirmation needed).
2. **Add per-scope name tracking** — e.g. `std::vector<std::unordered_set<std::string>> scopeNames;`
   pushed/popped alongside `pushScope()`/`popScopeRelease()`; function params + for-loop var seed the
   right scope. Watch: `bind()` is called for params and loop vars too — don't false-positive.
3. **Error on same-scope dup** in the Let path; allow shadow across a nested `block()`.
4. **REPL** — implement the chosen policy so re-`let` at the prompt stays coherent.
5. **Build + verify** the repros below.

## Acceptance criteria
- [ ] `func main(){ let a=4; let mut a=4; return 0; }` → compile error (same-scope redeclaration).
- [ ] Nested shadowing still compiles: `let a=1; { let a=2; println(a); } println(a);` → `2` then `1`.
- [ ] `let a=3; a=6;` still errors (immutable — regression guard for I1).
- [ ] REPL: re-`let` of an existing name ERRORS (strict); `:reset` clears state so it can be redefined.
- [ ] `ctest` green; all `example/examples/*.mxs` demos at their prior rc.

## Constraints
- Compile-time check; do not regress the immutability error.
- Don't break function parameters or `for`-loop variables (also bound via `bind()`).

## Notes / Assumptions
- RESOLVED (Mux): REPL redefinition = strict error + `:reset`. NOTE: the first dispatch of this task
  used replace-prior; a follow-up corrects it to strict + `:reset`.
