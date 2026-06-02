# Task 14 — Authoritative syntax docs + refresh syntax.ebnf + fix basic_syntax drift
id: 2026-06-01/task14
parent: 2026-06-01/progress13
status: done
owner: code_agent

## Objective
Produce a single trustworthy syntax reference that matches the implemented grammar, and reconcile the
two drifted companion files.

## Scope
In:
- `docs/syntax.md` — authoritative reference incl. a divergence section vs the original `syntax.ebnf`.
- `syntax.ebnf` — refresh to match the implementation.
- `docs/basic_syntax.md` — correct the points it gets wrong.
Out:
- Any grammar/parser code change (the orphaned `raise_expr` removal is task16).

## Inputs (read first, priority order)
1. `include/mxspp/frontend/grammar.hpp` — the implemented grammar (source of truth).
2. `example/examples/syntax_reference.mxs` — what actually runs end-to-end.
3. `syntax.ebnf` (v1.0) — the original design to diff against.

## Deliverables
- `docs/syntax.md` — lexical / operators+precedence / expressions / statements / types / definitions /
  top-level / annotations; every construct tagged `[parses + runs]` vs `[parses only]`; a §9
  "Divergence from the original syntax.ebnf"; a §10 "Doc drift" for basic_syntax.
- `syntax.ebnf` — v2.0 header pointing at docs/syntax.md; list literals, `**`, `...rest`, bodyless
  `func … ;`, `raise`-expression removed.
- `docs/basic_syntax.md` — fix: multi-line comment is `/* … */` (not `!##!`); `raise` is a function, not
  a keyword/statement; list typing is `List<T>`.

## Steps
1. **Author docs/syntax.md** from grammar.hpp; cross-check against syntax_reference.mxs. (DONE)
2. **Refresh syntax.ebnf** to v2.0. (DONE)
3. **Fix basic_syntax.md drift** — the three points above; leave the tutorial structure intact.

## Acceptance criteria
- [x] docs/syntax.md exists and lists every grammar.hpp construct with a run/parse tag.
- [x] docs/syntax.md §9 enumerates every divergence from syntax.ebnf v1.0.
- [x] syntax.ebnf parses as ISO-14977-style EBNF and matches the implemented rules.
- [x] basic_syntax.md no longer claims `!##!` comments, `raise` keyword, or `[] List<T>` typing.

## Constraints
- Docs only. Do not change behavior.

## Notes / Assumptions
- Assumption: docs/syntax.md wins over basic_syntax.md and (a drifted) syntax.ebnf on any conflict.
