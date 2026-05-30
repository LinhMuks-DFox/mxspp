# Progress 02 — Investigation: design vs frontend-implementation gap audit
id: 2026-05-31/progress02
date: 2026-05-31
author: human+ai
status: done
refs: [2026-05-30/progress01]
supersedes:
commits: []
files:
  - include/mxspp/frontend/grammar.hpp
  - include/mxspp/frontend/ast.h
  - src/frontend/parser.cpp
  - syntax.ebnf
  - docs/type_system.md
  - docs/ffi.md
  - docs/basic_syntax.md

## Goal
Map how much of the MXScript syntax design is actually landed in the frontend. For every construct,
record its status across three layers — (1) DESIGNED (EBNF + docs), (2) PARSES (grammar.hpp),
(3) BUILDS AST (parser.cpp + ast.h) — and enumerate the gaps. Output is this findings doc;
the implementation work is progress03.

## Context / Motivation
After progress01 the code→AST path works for a subset. Mux asked for a precise map of what remains
before we build out / implement the rest.

## Method
Empirical, not by reading: drove ~55 minimal constructs through the real grammar with a native parse
harness (PEGTL `parse_tree` over `grammar.hpp`), recording PASS/FAIL, then cross-referenced
`parser.cpp` for which constructs the transform turns into AST nodes. Five initial hypotheses were
wrong — the harness corrected them (see Issues).

## Findings

### Layer 1 → 2: PARSES today (grammar accepts)
Comments (`#`, `//`, `/* */`); scalar literals (int/float/string/bool/nil); full expression
precedence ladder; unary; range `..`; assignment ops (`= += -= *= /=`); member `.`; index `[]`;
call; named call args (`k=v`); error-propagation `?`; generic call `g<int>()`; `raise` expr; lambda
(expr & block); types: explicit, union `A|B`, generic `List<int>`; statements: if/else-if/else,
for-in, loop/break/continue, until, do-until, assert, defer, let, return, expr-stmt; functions:
generics `<T>`, default params, param grouping; OOP: class (fields/ctor/dtor/method/static/operator/
private), inheritance + override + base-ctor, generic class, `type` struct, enum (plain + data
variants); match: literal / wildcard / constructor `Some(y)` patterns; annotations: no-arg &
`key=value`; `@@foreign` on a func WITH a body; top-level: import (as/plain), static/dynamic binding,
export.

### Layer 1 but NOT Layer 2 — missing syntax (designed, won't parse)
- **G1** Container literals: list `[1,2,3]`, dict `["k":v]`, tuple `(1,2)`.
- **G2** Compound types: array `[N]T`, tuple `(A,B)`.
- **G3** Type-binding match patterns `case x: Type =>` — the canonical match usage (docs + matrix_class). **High value.**
- **G4** Variadic params / spread `...`.
- **G5** Positional annotation args, e.g. `@@template(T)` (args must currently be `key=value`).
- **G6** Bodyless declarations: `@@foreign(...) func f(...) -> R;` (func_def requires a block).
- **G7** Designed multi-line comment `!##! ... !##!` (only `#`, `//`, `/* */` implemented).
- **G8** Tuple destructuring `let (a,b) = ...` — does NOT truly parse; silently MISPARSES (see B4).

### Grammar BUGS (feature is "in" but broken)
- **B1** `block_expr` whitespace-fragile: `{ 1; 2 }` fails, `{1;2}` works — missing `ignored` between statements / around the trailing expr.
- **B2** `interface_member` whitespace-fragile: `func b()->T {} ;` (space before `;`) fails — missing `ignored` before the trailing `;`.
- **B3** `func_type` unreachable as a type: `func(int)->int` fails — `single_type` tries `fqdn` first and `func` matches as a bare identifier.
- **B4** Keywords not reserved against `identifier`: `let`, `func`, … can match as identifiers in expr/type context (root cause of G8 & B3); needs a keyword guard. (`let (a,b)=g()` silently misparses as `let(a,b) = g()`.)
- (Already fixed in progress01: undefined `arg`/`arg_list`, binary `list` 3-arg, `string_literal`, `#` comments.)

### Layer 2 but NOT Layer 3 — parses but builds NO AST (the dominant gap)
`parser.cpp` transforms only: TranslationUnit, FunctionDef (+ Parameter, return type), Block,
LetStatement, ReturnStatement, ExprStatement, Identifier, Integer/Float/Boolean/String/Nil literals,
BinaryOp, UnaryOp, FunctionCall. EVERYTHING ELSE parses but is dropped (`to_stmt` returns nullptr):
- **Control flow**: if / for-in / loop / until / do-until / break / continue / assert / defer.
- **OOP**: class / interface / `type` / enum and all members (ctor/dtor/method/operator/static/field).
- **Expressions**: match, lambda, member access `.`, index `[]`, generic instantiation, `?`.
- **Top-level**: import, static/dynamic binding, annotations (parsed then discarded — metadata lost), generics (params not captured).

## Verdict — "how much isn't landed"
- **PARSE layer: broad.** Most of the language parses. Remaining = 8 missing-syntax gaps (G1–G8) + 4 grammar bugs (B1–B4).
- **AST layer: narrow.** Only functions + simple statements/expressions/calls. The entire OOP system,
  all control flow, match, lambda, annotations, imports and generics parse but produce no AST. **This
  is the dominant gap and the prerequisite for codegen.**

## Issues / Gotchas (empirical corrections — why testing beat reading)
- `block_expr` & `interface` "fails" were whitespace bugs (B1, B2), not missing features.
- `func_type` "fail" is a rule-ordering bug (B3), not missing.
- `tuple_destructure` "passes" — but as a MISPARSE (B4/G8), not real support.
- match constructor patterns DO parse; the initial "fail" was a malformed test (a bare
  `match(){}` statement needs a trailing `;`). Only type-binding `case x:T =>` truly fails (G3).

## Open / TODO
Implementation tracked in [progress03](./progress03-complete-frontend.md).

## Agent log
- 2026-05-31 [ai] Built a ~55-construct empirical parse matrix (native harness over `grammar.hpp`) +
  cross-referenced `parser.cpp`. Produced the 3-layer map; corrected 5 wrong hypotheses by testing.
  Findings feed progress03.
