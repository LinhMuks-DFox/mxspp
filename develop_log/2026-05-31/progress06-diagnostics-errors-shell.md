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

## Open / TODO
- **Friendly messages**: replace raw `parse error matching tao::pegtl::one<'}'>` with `expected '}'`
  (PEGTL custom error messages via a control/`raise_message`), and add more `must<>` anchors
  (after `func`, `if`, `(` in calls, `;` where safe — must not break block_expr's trailing expr).
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
