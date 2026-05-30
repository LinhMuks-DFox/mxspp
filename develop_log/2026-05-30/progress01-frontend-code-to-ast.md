# Progress 01 — Complete the frontend: wire the source → AST path
id: 2026-05-30/progress01
date: 2026-05-30
author: human+ai
status: active
refs: []
supersedes:
commits: []
files:
  - include/mxspp/frontend/grammar.hpp      # (read-only ref; renamed from grammer.hpp)
  - include/mxspp/frontend/ast.h
  - include/mxspp/frontend/action.h
  - src/frontend/ast.cpp
  - src/frontend/CMakeLists.txt
  - src/driver/main.cpp

## Goal
Finish the frontend so that `.mxs` source parses into a complete in-memory AST
(`mxs::frontend::ast`). The deliverable is a working **code → AST** path with an inspectable
result (a `--dump-ast` mode). Codegen / execution are explicitly out of scope for this progress.

## Context / Motivation
The PEG grammar (`grammar.hpp`, 378 lines, scannerless PEGTL) already recognizes the whole language,
but it has **no semantic actions** — it validates structure without building anything. `action.h`
implements exactly one action (`integer_literal`) over a `node_stack` design and is never invoked.
`ast.h` declares ~18 node classes (narrower than the grammar) and only `IntegerLiteral` is defined
in `ast.cpp`. So today `source → AST` is ~unimplemented scaffolding. Mux chose (2026-05-30) to build
the **frontend/AST first** (foundation) before codegen. See `docs/Architecture.md` for the pipeline.

## Decisions

### D1 — Build the AST via PEGTL `parse_tree` + a transform pass (supersedes the started node_stack actions)
- Decision: Use `tao::pegtl::parse_tree::build<grammar, selector>` to produce a typed parse tree, then
  a single transform pass converts it into `mxs::frontend::ast` nodes. Retire the hand-managed
  `node_stack` approach in `action.h`.
- Why: The grammar is heavily `pegtl::list` / `pegtl::star` based — variable-arity constructs
  (`param_list`, `arg_list`, block statements, class members, enum variants) and left-flat binary-op
  chains. Reducing those from a single flat `node_stack` needs fragile sentinel/marker actions and
  precise ordering. `parse_tree` handles arbitrary arity and nesting natively, folds transparent
  wrapper rules via selectors, and gives a clean tree to walk once. Only one action was ever written,
  so almost nothing is lost.
- Impact: `action.h` becomes a **selector set** (which rules to keep / fold / store-content) plus a
  transform; the existing `ast.h` node classes are kept as the transform **target** (and extended with
  missing nodes). Grammar (`grammar.hpp`) stays unchanged.
- Alternatives considered: (a) extend `node_stack` with marker actions — closer to the started design
  but fragile for this list-heavy grammar; rejected. (b) parse the `tokenizer.h` token stream instead —
  rejected: the grammar is scannerless and complete on raw chars; the tokenizer is a separate, unused stage.

### D2 — Parse-only this progress; AST nodes are data + a dumper, no codegen
- Decision: Do not implement any `codegen()` bodies. Nodes carry data and gain a `dump()`/visitor for
  inspection; success is measured by AST shape, not IR.
- Why: Isolates and makes the frontend verifiable without LLVM/JIT. Codegen is a later progress.
- Impact: Add an AST dumper + `mxs --dump-ast <file>`; touch no backend/jit code.

### D3 — Cover the grammar incrementally; expressions + functions first (task01), breadth later
- Decision: task01 = literals, the full expression precedence ladder, `let`/`return`/expression
  statements, blocks, `func_def`, and the top level. Control-flow, class/interface/enum/type,
  match/patterns, lambdas, generics, annotations, imports follow in task02+.
- Why: Proves the `parse_tree → AST` mechanism on the trickiest part (operator precedence &
  associativity) before widening to breadth.
- Impact: Defines task ordering (see Tasks + Open/TODO).

### D4 — Fix Mux's typos encountered along the way (requested 2026-05-30)
- Decision: Correct misspellings as the frontend work touches them. Done now: `grammer.hpp` →
  `grammar.hpp` (file + 2 includes in `tokenizer.h`, `action.h`); `MatchStatment` → `MatchStatement`
  (`ast.h`). Remaining (tracked, not yet done): example identifiers in `flow_control.mxs`
  (`if_stamt`, `for_in_stamt`, `for_until_stamt`) and stray `Okey`/`True` casing in examples.
- Why: Mux asked to fix typos; the grammar-filename misspelling in particular is the most visible.
- Impact: Safe — neither `grammar.hpp` nor `action.h` is compiled by any current `.cpp`, so the rename
  cannot break the build.

## Tasks
- [x] [task01 — bootstrap parse_tree → AST for expressions, statements, and func_def](tasks/task01-bootstrap-ast-builder.md) — done & verified 2026-05-31

## Issues / Gotchas
- **Grammar bugs found & fixed while wiring task01 (all in `grammar.hpp`):**
  1. `struct arg;` was forward-declared but never defined; `arg_list` used it → would not compile. Fixed
     to use the defined `argument`.
  2. Every binary level used `pegtl::list<Operand, Sep, Operand>` — PEGTL's 3rd `list` arg is *padding*,
     not the repeated operand, so all binary ops (`* / % + - .. < > == != && ||`) were malformed and any
     operator expression failed to parse. Fixed to 2-arg `list<Operand, Sep>`.
  3. `string_literal` used `until<'"', if_must<'\\', any>>`, which only accepts escape sequences and
     rejects ordinary characters — `"Hello"` failed. Fixed to `star<sor<\\ any, not_one<'"'>>>`.
  4. `line_comment` only matched `//`, but every `.mxs` example uses `#` (Python-style). Fixed to accept
     `#` (and keep `//`).
- AST nodes inherit `virtual core::MXObject` whose ctor needs `is_static`; the transform threads `false`
  for now. Added an explicit default ctor to each node (the virtual base has no default ctor) — kept the
  change inside the frontend (did not modify core `MXObject`).
- `codegen()` is stubbed for every instantiated node so the nodes are concrete/linkable (vtables need a
  definition); real codegen is a later progress.
- Assignment fold subtlety: `struct expression : assign_expr {}` means the *matched* rule is `expression`,
  not `assign_expr`, so `rearrange` must target `expression` (caught only by running — `y = 10` was
  dropping its RHS until fixed).
- `match` is an **expression** in the grammar (`match_expr`), but `ast.h` modeled it as a `Statement`
  (`MatchStatement`, currently an empty stub). Re-model as an expression in the task that covers match.
- Missing AST nodes vs grammar (add as tasks reach them): `FunctionDef`, `Parameter`, `ClassDef`,
  `InterfaceDef`, `TypeDef`, `EnumDef`, `Lambda`, `MatchExpr`/`CaseClause`/`Pattern`, `MemberAccess`,
  `IndexAccess`, `AssignExpr`, `RangeExpr`, error-propagation `?`, `NilLiteral`, `Annotation`,
  `ImportStmt`, `BindingStmt`, `AssertStatement`, `DeferStatement`, named call args, tuple/list literals.
- Binary-op chains are left-flat `pegtl::list`; the transform must fold them **left-associative**, while
  `assign_expr` is **right-associative**.
- The parser is wired nowhere yet; task01 also adds the first real invocation + driver flag.

## Open / TODO (carry-over)
- task02: control-flow statements (`if` / `for-in` / `loop` / `until` / `do-until` / `break` /
  `continue` / `assert` / `defer`).
- task03: definitions — `class` / `interface` / `type` / `enum` + members (ctor / dtor / method /
  operator / field / static).
- task04: `match` expression + patterns; lambdas; `block_expr`.
- task05: annotations (`@@...`), `import` / binding statements, generics (`generic_param` / `generic_inst`).
- A golden-AST check harness over `example/examples/*.mxs`; remaining example/identifier typo fixes (D4).
- Later (post-frontend): source locations on nodes; symbol table / scope (the commented-out field in
  `AstBuilderState`); then codegen (separate progress).

## Agent log
- 2026-05-30 [ai] Read `grammar.hpp` / `ast.h` / `action.h` / `ast.cpp` first-hand. Mux set the goal
  "complete frontend, code → AST". Fixed typos (D4): renamed `grammer.hpp` → `grammar.hpp` (+ includes),
  `MatchStatment` → `MatchStatement`. Authored progress01 + task01. Proposed `parse_tree`-based
  construction (D1) over the started node_stack. Verified vendored PEGTL 3.2.7 ships
  `contrib/parse_tree.hpp`. Next: implement task01 (proceeding with D1 unless Mux redirects).
- 2026-05-31 [ai] Implemented task01. New `parser.{h,cpp}` (PEGTL `parse_tree` + a `rearrange`
  left-assoc fold + transform → `ast` nodes + an AST dumper); extended `ast.h` (per-node ctors +
  `FunctionDef`/`Parameter`/`NilLiteral`); stubbed `codegen()` in `ast.cpp`; `main.cpp` now does
  `mxs --dump-ast <file>`; wired `parser.cpp` into the frontend lib and linked `frontend` into the
  driver. Found and fixed four grammar bugs (see Issues 1-4).
  VERIFICATION (this host is macOS; the project builds in a Linux Docker container — vendored LLVM is
  Linux aarch64 and `toolchain.cmake` forces Linux, so the in-container Ninja build was NOT run here):
  (a) `clang++ -std=c++23 -fsyntax-only` type-checks `parser.cpp` / `ast.cpp` / `main.cpp` clean against
  the real PEGTL + LLVM headers; (b) compiled+linked the real sources natively (LLVM codegen symbols
  left undefined via `-Wl,-undefined,dynamic_lookup`, never called by `--dump-ast`) and RAN
  `--dump-ast`. Results: 10/14 examples parse (the 4 failures are out-of-scope class/match/generics or
  intentionally-broken sources); `1 + 2 * 3` folds with correct precedence; `a = b = c` is right-assoc;
  functions/params/types/returns/lets/calls/all literal kinds/strings produce correct ASTs.
  REMAINING: run the official `python3 rebuild.py` inside the Docker dev container to confirm the
  CMake/Ninja build is green and the real `mxs` binary works (only env-orchestration unverified — the
  sources already compile + run). Then task02 (control-flow) per Open/TODO.
