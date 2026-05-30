# Progress 03 — Complete the unlanded frontend (full AST coverage + grammar gaps)
id: 2026-05-31/progress03
date: 2026-05-31
author: human+ai
status: active
refs: [2026-05-31/progress02, 2026-05-30/progress01]
supersedes:
commits: []
files:
  - include/mxspp/frontend/grammar.hpp
  - include/mxspp/frontend/ast.h
  - include/mxspp/frontend/parser.h
  - src/frontend/parser.cpp
  - syntax.ebnf
  - (new) unit-test harness under test/ or unit_test/

## Goal
Land the parts of the syntax design that progress02 found missing: (a) extend the AST transform to
cover everything that already parses (control flow, OOP, match, lambda, annotations, imports,
generics), and (b) close the grammar gaps/bugs (G1–G8, B1–B4) and sync `syntax.ebnf`. Codegen is
explicitly out of scope (a later progress). Every change ships unit tests to 100% coverage.

## Context / Motivation
progress02 mapped the gap: parsing is broad but the AST transform is narrow (only functions + simple
statements/expressions). Completing AST coverage finishes the code→AST path for the whole language
and is the prerequisite for semantic analysis + codegen.

## Decisions

### D1 — Sequence: AST coverage of already-parseable constructs first, then grammar extensions
- Decision: do the "parses-but-no-AST" coverage (Layer 2→3) before adding new syntax (Layer 1→2).
- Why: it completes code→AST for the existing language and unblocks codegen; new syntax is additive
  and lower-risk to defer.
- Impact: task ordering below (AST tasks 03–05 before grammar-extension task 06).

### D2 — Fix the grammar bugs (B1–B4) up front
- Decision: fix whitespace-fragility (B1, B2), keyword reservation (B4), and `func_type` ordering
  (B3) first.
- Why: they silently break or misparse valid programs; cheap, high-value, and they distort the AST
  work if left in.

### D3 — Establish a unit-test harness + 100% coverage gate (NEW PROJECT RULE, set by Mux 2026-05-31)
- Decision: **the project's unit tests must reach 100% coverage.** Add a C++ unit-test harness +
  coverage measurement; every task ships tests covering all new/changed code; `before_commit.py` /
  CI enforces the gate before a change counts as done.
- Why: Mux's rule. The project currently has NO unit harness — establishing one is a prerequisite.
- Impact: recorded in `CLAUDE.md` conventions; task02 sets it up; every later task has a coverage
  acceptance criterion. Parser coverage is cleanest via table-driven snippet tests (reuse the
  progress02 matrix) + AST-shape assertions.

## Tasks
(Task files created when each is picked up.)
- [ ] task01 — fix grammar bugs B1–B4 (+ tests; sync `syntax.ebnf`)
- [ ] task02 — set up the unit-test harness + 100%-coverage gate (prerequisite for D3)
- [ ] task03 — AST coverage: control flow (if / for-in / loop / until / do-until / break / continue /
      assert / defer) + member access `.` / index `[]` / `?` / range
- [ ] task04 — AST coverage: OOP (class / interface / `type` / enum + all members) + capture generics
- [ ] task05 — AST coverage: match (+ patterns) + lambda + annotations (incl. `@@foreign` metadata) +
      import / static-dynamic binding
- [ ] task06 — grammar extensions G1–G8 (container literals/types, tuple, array type, type-binding
      match patterns, variadic `...`, positional annotation args, bodyless `@@foreign`, `!##!`
      comments) + sync `syntax.ebnf`

## Issues / Gotchas
- 100% parser coverage is easiest via table-driven tests over construct snippets (the progress02
  matrix is a ready seed) plus AST-shape assertions on the resulting tree.
- Annotation metadata (e.g. `@@foreign` lib/symbol, `@@manual_optimize_level` level) must be attached
  to the relevant AST node, not discarded — needed later by codegen/FFI.

## Open / TODO (carry-over)
- After the frontend is complete: semantic analysis (symbol tables, scopes, type checking), then
  codegen / JIT (the empty backend) — separate progresses. The `cast` / `type_of` / `is_instance`
  hybrid-dispatch machinery (designed in docs/type_system.md §7) belongs to that codegen+runtime work.

## Agent log
- 2026-05-31 [ai] Authored from progress02 findings. Tasks not yet started — pending Mux's go / review.
