# Task 22 — Implementation verification + red/green corpus + bug hunt
id: 2026-06-01/task22
parent: 2026-06-01/progress14
status: done
owner: code_agent (Opus)

## Objective
Prove the progress13 consolidation actually works end-to-end, and build a durable **red/green** test
corpus (green = must run to a known output; red = must be rejected/panic with the *right* diagnostic)
that flushes hidden bugs. Fix bugs found.

## Scope
In:
- Build + `ctest` + full example sweep.
- A green corpus (every new feature) and a red corpus (every intentional-error path), each with expected
  output, runnable repeatably.
- Bug hunt + fixes (incl. the known I7/I8/I9 + the §6 routing-by-name gap from progress14).
Out:
- Re-doing task21's static review (but consume its findings — turn each "needs-test" into a case here).

## Inputs (read first)
1. `develop_log/2026-06-01/progress14-...md` (risk checklist, build rule) + `progress13-...md` (the spec
   each test asserts against).
2. `example/examples/*.mxs` (existing demos — the seed green set) + `test/core_test.cpp`,
   `test/frontend_test*` (the unit harness; extend if useful).
3. `docs/syntax.md` (the behavior each test pins).

## Deliverables
- A **green corpus** + a **red corpus** of `.mxs` programs with documented expected results, plus a way to
  run them repeatably (a small runner script under `test/`, or `example/examples/{green,red}/` with a
  checked expected-output list — your call; keep it CI-able and obvious).
- Bug fixes for anything that fails, OR a precise logged repro if a fix is out of scope (escalate to Mux).
- A verification report in the progress14 Agent log: build/ctest/example status + corpus pass rates +
  bugs found/fixed.

## Steps
1. **Baseline:** `ninja -C build`; `ctest --test-dir build --output-on-failure`; run EVERY
   `example/examples/*.mxs` via `build/bin/mxs run-core` and record each rc + key output.
2. **GREEN corpus** — each must run rc 0 with the documented output:
   - bindings: `let mut` reassign; nested-block shadowing (`let a=1; if(true){let a=2; …} …`).
   - container methods: `xs.append(v)`, `xs.len()`, `xs.get(i)`, `"hi".len()`; list literal; `for x in xs`;
     `xs[i]`; `for i in 0..n`.
   - match (literal / type-binding `case e: Error` / wildcard); operators incl. `**`; str/repr; `format`
     (`{}`,`{N}`,`{:spec}`,`{:?}`).
   - OOP: class fields/ctor/method/`operator+`/dtor (ARC — no leak).
   - imports: `import std.io.{println};` then `println(...)`; `import std.io;` then `io.println(...)`;
     `import std.io as o;` then `o.println(...)`; `import std.time.{now, monotonic_ns};` elapsed-time.
3. **RED corpus** — each must be REJECTED (compile error or panic) with a CLEAR, CORRECT message, never a
   crash or a silent pass:
   - immutable reassign: `let a=1; a=2;` → "cannot assign to immutable binding".
   - same-scope redeclaration: `let a=1; let mut a=2;` → "redeclaration of 'a' in the same scope".
   - import-gating: `println(1)` with NO import → unknown function (stdlib not in scope).
   - removed free fns: `append(xs,4)` / `len(xs)` → unknown function (they're methods now).
   - unresolved import: `import std.nope.{x};` → clear resolver error, non-zero rc.
   - (probe) `42.append(1)` — does it error or silently no-op? (progress14 §6 — decide + test the chosen
     behavior). bare-block statement `{ let a=2; }` (I8). `operator**` in a class (op_symbol gap).
4. **Bug hunt + fix:** I9 `oop_vector.mxs` (fix `let mut a` or make it a red case); I8 bare-block (decide);
   the §6 routing-by-name no-op; ARC edge cases (run the population baseline under each green case); REPL
   edges (`:reset`, strict redecl, the I7 mutation-non-persistence — fix or document). Fix what you can;
   log the rest with repros.
5. **Wire it up** so the corpus is re-runnable (script or ctest entries) and document expected outputs.

## Acceptance criteria
- [ ] `ninja -C build` clean; `ctest` green; full example sweep at expected rc (fix or reclassify any that aren't).
- [ ] GREEN corpus: all rc 0 with documented output.
- [ ] RED corpus: every case rejected with a correct, clear diagnostic (no crash, no silent pass).
- [ ] Every bug found is fixed or logged with a minimal repro; no ARC leak/double-free (population baseline).
- [ ] Corpus is repeatable (script/ctest) with checked-in expected outputs; report in progress14 Agent log.

## Constraints
- `ninja -C build` only (never `rebuild.py --clean`). No commits unless Mux asks.
- A red case must fail for the RIGHT reason — verify the diagnostic text, not just a non-zero rc.

## Notes / Assumptions
- The red corpus is the high-value part (it's how "hidden small bugs" surface — Mux's ask). Prefer many
  small, single-purpose programs over a few big ones.
