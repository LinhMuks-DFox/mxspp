# Task 01 — Fix grammar bugs (B1–B4) and the parser let-value bug
id: 2026-05-31/task01
parent: 2026-05-31/progress03
status: done
owner: code_agent

## Objective
Fix the grammar bugs B1–B4 from progress02 and the parser bug where a bare-identifier
initializer (`let z = y;`) was dropped.

## Outcome (2026-05-31)
Done & verified (native parse harness + unit tests).
- **B1** `block_expr`: added `ignored` between statements and around the trailing expr
  (`{ 1; 2 }` now parses).
- **B2** `interface_member`: added `ignored` before the trailing `;` (`func b()->T {} ;` parses).
- **B4** keyword reservation: `identifier` now has `not_at<reserved_word>` (a `sor` of all
  31 `K_*` keywords). `let`/`func`/… can no longer be parsed as identifiers, so
  `let (a,b)=g();` cleanly fails instead of silently misparsing as `let(a,b)=g()`.
- **B3** `func_type` as a type: fixed *as a consequence of B4* — `fqdn` can no longer
  swallow `func` as an identifier, so `func(int)->int` reaches `func_type`.
- **Parser** (`parser.cpp`): selected `identifier_list` as a node so names are grouped and a
  bare-identifier initializer is a distinct child; rewrote `to_let` and `parse_sig`.

Verification: example sweep unchanged (11/14 parse; the 3 fails are known out-of-scope);
B1/B2/B3 now parse, B4 misparse now errors, keyword-prefixed identifiers (`format`,
`internal`) still parse; `let z = y;` now keeps the value (`Let z -> Identifier y`).

## Notes
- The previously-fixed progress01 grammar bugs (undefined `arg`, binary `list` 3-arg,
  `string_literal`, `#` comments) are also covered by regression tests.
