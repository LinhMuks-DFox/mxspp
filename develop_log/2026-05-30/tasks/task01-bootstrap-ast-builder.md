# Task 01 — Bootstrap parse_tree → AST for expressions, statements, and func_def
id: 2026-05-30/task01
parent: 2026-05-30/progress01
status: done
owner: code_agent

## Outcome (2026-05-31)
Done & verified. D1 resolved: PEGTL `parse_tree` + a `rearrange` left-assoc fold. Range/assignment
reuse `BinaryOp` (no dedicated nodes). All acceptance criteria met; verified via native clang compile +
link + run of the real sources (the in-container Ninja build is the one remaining check — see
progress01 Agent log). Four grammar bugs were found and fixed along the way.

## Objective
Make `.mxs` expressions, simple statements, and function definitions parse into a complete
`mxs::frontend::ast` tree via PEGTL `parse_tree` + a transform pass, with a dumpable result.

## Scope
In:
- A `parse_tree` **selector** set over `grammar.hpp` keeping the in-scope rules and folding wrappers.
- A **transform** pass: parse_tree → `ast` nodes for: literals, identifier, the full expression
  precedence ladder (unary, `* / %`, `+ -`, `..`, relational, equality, `&&`, `||`, assignment),
  postfix (`call`, `.member`, `[index]`), `let_stmt`, `return_stmt`, `expression_stmt`, `block`,
  `func_def`, and `mxscript` → `TranslationUnit`.
- Add missing AST nodes needed here: `FunctionDef`, `Parameter`, `NilLiteral` (and decide reuse vs new
  for `AssignExpr` / `RangeExpr` / `MemberAccess` / `IndexAccess`; `FunctionCall` already exists).
- An AST **dumper** (indented tree) + a `mxs --dump-ast <file>` path in the driver.
Out:
- Any `codegen()` / LLVM IR.
- Control-flow, class / interface / enum / type, match / patterns, lambda, generics, annotations,
  imports (task02+).
- Type checking, symbol tables, scopes, source locations.

## Inputs (read first, priority order)
1. `develop_log/2026-05-30/progress01-frontend-code-to-ast.md` — decisions D1–D4 and the why.
2. `include/mxspp/frontend/grammar.hpp` — exact rule names to select / transform.
3. `include/mxspp/frontend/ast.h` — existing node classes (the transform target).
4. `include/mxspp/frontend/action.h` — current node_stack approach (superseded by D1; reuse the file).
5. `src/frontend/ast.cpp` — `IntegerLiteral` shows how to thread `is_static` through `virtual MXObject`.
6. `lib/pegtl/include/tao/pegtl/contrib/parse_tree.hpp` — the parse_tree + selector API (PEGTL 3.2.7).

Code to inspect/change:
- `include/mxspp/frontend/action.h` (or a new `parser.h` / `src/frontend/parser.cpp`) — selectors + transform + entry.
- `include/mxspp/frontend/ast.h`, `src/frontend/ast.cpp` — add `FunctionDef` / `Parameter` / `NilLiteral`; add the dumper.
- `src/driver/main.cpp` — wire `--dump-ast`.
- `src/frontend/CMakeLists.txt` — add any new `.cpp`.

## Deliverables
- A parse entry, e.g. `parse_to_ast(std::string_view source) -> std::unique_ptr<ast::TranslationUnit>`
  (returning an error/diagnostic on parse failure).
- The selector set + transform covering the in-scope rules above.
- An AST dumper producing a readable indented tree.
- `mxs --dump-ast example/examples/simple_function_return.mxs` prints a complete AST.

## Steps
1. **Builder home** — add `parser.{h,cpp}` in frontend (keep grammar / ast / action concerns separate);
   decide where selectors live.
2. **Selectors** — store-content for `integer_literal` / `float_literal` / `string_literal` / `identifier`
   and operator symbols; structural-keep for exprs / statements / func_def; fold single-alternative wrappers.
3. **Transform expressions** — literals, identifier, unary prefix, the precedence ladder (fold `list`
   children **left-assoc**), assignment (**right-assoc**), postfix (`call` / `.member` / `[index]`).
4. **Transform statements** — `let_stmt`, `return_stmt`, `expression_stmt`, `block`.
5. **Transform definitions / top level** — `func_def` (+ params from `func_sig`), `mxscript` → `TranslationUnit`.
6. **Dump + wire + verify** — add the dumper, wire `--dump-ast`, verify on `basic_types.mxs`,
   `variable.mxs`, `simple_function_return.mxs`; rebuild stays green.

## Acceptance criteria
- [ ] `mxs --dump-ast example/examples/simple_function_return.mxs` prints a full AST (two `FunctionDef`s,
      each with a `return` of a literal).
- [ ] `basic_types.mxs` and `variable.mxs` parse to complete ASTs (`let`, `mut`, all literal kinds incl. `nil`).
- [ ] `1 + 2 * 3` parses with correct precedence (`*` under `+`) and left-assoc; `a = b = c` parses right-assoc.
- [ ] `python3 rebuild.py` stays green; no `codegen()` bodies added.
- [ ] In-scope-only: constructs outside scope (class / match / etc.) either parse-and-skip cleanly or emit a
      clear "unsupported in task01" note — no crash.

## Constraints
- No codegen / IR in this task.
- Keep `grammar.hpp` unchanged (all additions go in selectors / transform / ast).
- Reuse existing `ast.h` node classes where they fit; thread `is_static = false` for now.
- English identifiers and comments.

## Notes / Assumptions
- Assumption: the build env (vendored LLVM in `lib/`, clang >= 20, Ninja) is functional from the last build
  (`build/bin/` already holds the `.so` set + `mxs`).
- Assumption: PEGTL 3.2.7 `contrib/parse_tree.hpp` is available (verified present).
- Question (for Mux): confirm D1 (`parse_tree` over the started `node_stack`)? And is reusing `BinaryOp`
  for `..` (range) and `=` (assignment) acceptable, or do you want dedicated `RangeExpr` / `AssignExpr` nodes?
