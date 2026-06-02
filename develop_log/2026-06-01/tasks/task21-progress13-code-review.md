# Task 21 — Code review of the progress13 consolidation diff
id: 2026-06-01/task21
parent: 2026-06-01/progress14
status: done
owner: code_agent (Opus)

## Objective
Independently review every (uncommitted) change made under progress13 for correctness, memory safety
(ARC), edge cases, design adherence, and style — and FIX defects you find (or log them precisely for
task22 if they need a test to pin down).

## Scope
In:
- All code/doc changes listed in progress14 §"Change surface" (task14/15/16/20 + the import work
  task17/18/19).
- Authority to edit/fix; keep the build green.
Out:
- Writing the test corpus (that's task22 — but flag every case worth a test).
- New features beyond fixing what's there.

## Inputs (read first, priority order)
1. `develop_log/2026-06-01/progress13-...md` — the SPEC: its Decisions (D1–D6 + RESOLVED notes) are what
   the code must satisfy; its Agent log is the per-piece "what/why" + the final import state.
2. `develop_log/2026-06-01/progress14-...md` — §"Risk areas / review checklist" (1–9) and the build rule.
3. `docs/develop_rule.md` — the mandatory C++ ownership/move/borrow/`clone()` rules (judge `src/core` +
   codegen ARC against these). `docs/object_model.md` — the OOP + ARC protocol. `AGENTS.md` — style.
4. The changed files themselves (per progress14 §"Change surface").

## Steps
1. **Reconstruct the diff intent.** Don't rely on `git diff HEAD` alone (pre-existing uncommitted changes
   muddy it — see progress14 heads-up). Use the per-task file lists + progress13 Agent log to know what
   each edit was meant to do, then read the current code.
2. **Walk the risk checklist (progress14 §Risk 1–9)** item by item. For each, read the relevant code and
   judge: is it correct on all control-flow paths? The top ones:
   - ARC in `codegen.cpp` built-in method dispatch (task20) + import/variadic call paths — operands
     released on every path, results owned once; matches the progress11 callee-owned/caller-owned split.
   - `scopeNames` push/pop/clear pairs with EVERY `scopes` lifecycle site (no stale names, no missed pop).
   - Import-gating is real (no stdlib name without `import`); `kCorePrelude` truly gone.
   - Module-qualified call routing can't be confused with a method call on a same-named value.
3. **Style/convention:** clang-format clean over changed lines (`python3 before_commit.py --staged` or
   `git clang-format`); `MX`-prefix / naming / 90-col per AGENTS.md.
4. **Docs vs reality:** confirm `docs/syntax.md` (§3.1 methods, §7 import) + `syntax.ebnf` describe the
   actually-implemented behavior after all edits.
5. **Fix** defects you can fix safely (keep `ninja -C build` + `ctest` green after each). For anything
   that needs a reproducer to be sure, write it down for task22.

## Acceptance criteria
- [ ] Every risk-checklist item (progress14 §1–9) has an explicit verdict (correct / fixed / needs-test).
- [ ] No ARC leak or double-free: `core_test` population baseline green AND a full example sweep is clean.
- [ ] `ninja -C build` clean; `ctest` green; clang-format clean over changed lines.
- [ ] A concise written review (findings + fixes + anything escalated to Mux) in the progress14 Agent log.

## Constraints
- `ninja -C build` only (never `rebuild.py --clean` — see progress14). No commits unless Mux asks.
- Verify-then-change; keep builds green at each step.

## Notes / Assumptions
- This is an adversarial review: assume the sub-agents made plausible-but-subtly-wrong choices and try to
  break them. ARC and the import name-resolution are the likeliest places for hidden bugs.
