# Progress 06 — Diagnostics, errors, and the shell
id: 2026-05-31/progress06
date: 2026-05-31
author: human+ai
status: active
refs: [2026-05-31/progress05]
supersedes:
commits: []
files:
  - src/frontend/parser.cpp     # report_syntax_error
  - include/mxspp/frontend/grammar.hpp  # must<> points
  - (later) src/shell/shell.cpp, the value-based Error model + runtime checks

## Goal
Good error reporting (syntax errors, numerical/runtime errors) and an interactive shell (REPL).
(Mux: "接下来是 shell 和各种 Error，比如语法错误啊、Numerical 错误之类的".)

## Done
- **Syntax-error diagnostics**: `parse_to_ast` now renders `name:line:col: syntax error: <msg>`
  plus the offending source line and a caret (from PEGTL's position).
- **`must<>` anchor points**: `block` must close with `}`, `func_sig` must close with `)`. These
  are always-required closers (no backtracking lost), so common mistakes (unclosed block / param
  list / missing `;`) now get a precise position instead of collapsing to `1:1`. Verified in Docker:
  example sweep unchanged (11/14 parse); `func f( -> int {...}` reports `1:9 ... one<')'>` with a
  caret at the `(`.

- **REPL shell** (`mxs` / `mxs shell`): JIT-backed read-eval-print — one-line definitions
  accumulate; expressions are wrapped (`println(EXPR)`), recompiled with the accumulated defs +
  prelude, and JIT-run. `:q` quits. `jit::run` gained an entry-symbol parameter. Verified in Docker:
  defining `sq`/`fib` then evaluating `sq(7)`=49, `1+2*3`=7, `fib(10)`=55. (v1: single-line defs,
  int expressions; multi-line + typed eval later.)

## Syntax-error diagnostics — improved (2026-05-31)
Two complementary improvements to `parse_to_ast`, both frontend-only (no grammar surgery, so no
backtracking/regression risk):

- **Friendly messages.** `friendly_message()` translates PEGTL's raw rule-match text into readable
  errors: `parse error matching tao::pegtl::one<'}'>` → `expected '}'`; the top-level `eof` →
  `expected end of input`. Unrecognized shapes fall through to the original text.
- **Furthest-progress position.** A custom PEGTL control (`error_tracer`, passed as the 4th
  `pt::parse` template arg) records the furthest input position any rule was *attempted* at — it
  only observes, never changes what matches. PEGTL otherwise reports the position of the `must<>`
  that threw, which collapses to the *block opening* whenever the block matches zero statements
  (a missing `;` or a stray token). When the parser actually progressed beyond the throw point, the
  diagnostic now points there with `unexpected token`.
- Verified in Docker (rebuild + `ctest` 2/2, example sweep unchanged at 14/17): `return 0 }` (missing
  `;`) now reports `1:31` at the `}` (was `1:22`); `let x = ;` reports `1:30` at the `;` (was
  `1:22`); `func f( -> int` reports `1:9: expected ')'`; an unterminated block reports
  `expected '}'`; multi-line sources report the correct line:col.

## Error model — IMPLEMENTED on the new object model (2026-05-31)
`match` with **type-binding patterns** (`case x: Type => …`) is the error-handling construct, per
Mux's confirmed design (the `case {}`/`case error{}` sketch was scrapped; his thought-through
`example/` form `case res: Matrix => {…}` / `case err: Error => {…}` is what landed — option 1).
- Grammar: added the type-binding pattern `name: Type` and a named wildcard `_` (the G3 gap).
- AST: `MatchExpr` (subject + cases: type-binding / wildcard / plain-binding / literal pattern,
  body = expression or block). Parser builds it; `block_expr` arm bodies become a Block whose last
  expression is the arm's value.
- Codegen (`compile_core`): evaluates the subject once, tries cases in order; type-binding tests
  the runtime type (`mxs_is_type`), literal tests equality (`mxs_op_eq`), wildcard/plain always
  match; binds the name; the matching arm's body value is the match's value (nil if none). It is an
  expression (`let x = match (…) { … }`).
- Errors flow as `MXError` objects (a fallible op like `/0` returns one); `case e: Error => …`
  catches them — "all errors handled via match". Verified in Docker (`example/examples/core_match.mxs`):
  `10/2` → `case v:int` → `5`; `10/0` → `case e:Error` → "caught division error";
  `match(7){case 1=>… case _=>…}` → "other".
- `raise(...)` / `exit(...)` are now **functions** (not keywords): `mxs_raise` prints the error and
  exits(1); `mxs_exit(code)` exits with the integer code (both flush + `_Exit`, skipping the ORC
  teardown hazard). Removed `K_RAISE` from the reserved words and `raise_expr` from the grammar, so
  `raise` is an ordinary identifier/function. Verified (`example/examples/core_raise.mxs`):
  `exit(2)` ends the program with code 2. (Still TODO: `err.msg` needs member access; exhaustiveness.)

## Error model — design (Mux, 2026-05-31): `match`-based, no `raise` keyword
Supersedes the docs §6 `raise`/`match` sketch and progress05's note.

- **No `raise` keyword/statement.** (The grammar still has `K_RAISE`/`raise_expr` — remove them.)
  `raise` exists only as a **function**: a special form of `exit` — *exit with error*.
- **All errors must be handled.** A fallible operation returns a *result-or-error*; the caller is
  required to handle it via `match`. The error path is just the match's `error` case; an **empty**
  `error` case means the default "exit with error code" (i.e. `raise` is a special case of `match`).
  ```mxs
  let x = match (something_returning_result_or_error()) {
      case { ... }        # ok value
      case error { }      # empty -> default: exit with error code
  }
  ```
- Implications: `match` becomes the core construct (currently `MatchStatement` is a no-op stub, and
  `match_expr` exists in the grammar); it must work as an **expression** (binds a value, as above).
  Needs the object model (an `Error` object) and result-or-error returns. Lands with/after
  [progress09](./progress09-object-model-cpp.md). Drop `K_RAISE`/`raise_expr`/`raise` keyword from
  the grammar; provide `raise(...)`/`exit(...)` as functions.

## Open / TODO
- **More precise messages / anchors**: `unexpected token` is generic; deriving "expected X" at the
  furthest point needs grammar introspection. Adding more `must<>` anchors (after `func`, `if`, call
  `(`, `;` where safe) would let the must<>-message path cover more cases — must not break
  block_expr's trailing expr.
- **Numerical / runtime errors**: DONE for unrecoverable panics — `mxs_panic(msg)` runtime
  (stderr + exit 1) + a codegen guard on integer `/` and `%` (panics "division by zero").
  Verified: `10/2`->5, `10/0`->panic+exit1. STILL TODO: the *recoverable* value-based Error model
  (docs §6 — `raise`/`match`/`Error` object), which needs the object model (overlaps progress05);
  plus more runtime checks (overflow, array bounds) once containers exist.
- **Shell (REPL)**: implement the empty `shell` module — read-eval-print on top of the JIT (eval
  expressions, accumulate definitions). `mxs` with no args → interactive prompt.

## Agent log
- 2026-05-31 [ai] Added located syntax-error diagnostics (line:col + source line + caret) and two
  safe `must<>` anchors (block `}`, func_sig `)`). Verified in Docker: no sweep regression; broken
  programs report a located error with a caret. Next: friendlier messages + more anchors, then the
  Error model (raise/panic/match + runtime checks) and the REPL shell.
- 2026-05-31 [ai] Friendly messages (`expected '}'`/`')'` instead of raw PEGTL text) +
  furthest-progress position tracking (custom `error_tracer` control) so a missing `;` / stray token
  points at the real spot instead of the block opening. Frontend-only, no grammar change; `ctest`
  2/2, example sweep unchanged (14/17). Next: the recoverable Error model (raise/match) — overlaps
  the object model in progress05 — plus runtime checks once containers exist.
