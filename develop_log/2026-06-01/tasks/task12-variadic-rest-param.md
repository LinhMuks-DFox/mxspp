# Task 12 — variadic rest-parameter (frontend + codegen)
id: 2026-06-01/task12
parent: 2026-06-01/progress12
status: done
owner: code_agent

## Objective
Add a rest parameter `...name: type` to function signatures so a call's surplus arguments are packed
into a fresh `MXArrayList` and passed as one trailing `MXObject*`; make `print`/`println` variadic.

## Scope
In:
- Grammar `rest_param` rule (last param only) + parser action.
- `ast::Parameter` (or param representation) `is_rest` flag.
- Codegen call lowering: pack surplus owned args into an `MXArrayList`, pass as the final pointer.
- `kCorePrelude`: `print`/`println` become `func print(...args: any) -> nil;` over a new
  `mxs_print`/`mxs_println` that take a list.
- Tests: a variadic call end-to-end + ARC no-leak.
Out:
- `format` itself (task13 binds `format(fmt, ...args)` and relies on this).
- Keyword args / defaults-on-rest / spread at call site (`f(*xs)`).

## Inputs (read first, priority order)
1. `develop_log/2026-06-01/progress12-stdio-str-repr-format.md` — §"Variadic ABI (rest parameter)".
2. `include/mxspp/frontend/grammar.hpp` lines ~116-139 — `param` / `param_list` / `func_sig`.
3. `src/frontend/parser.cpp` — param-building actions + the call lowering (`to_postfix`, FunctionCall).
4. `include/mxspp/frontend/ast.h` — `FunctionCall`, parameter/`Parameter` representation.
5. `src/backend/codegen.cpp` — the call emission path (named call + `@@foreign`); `mxs_arraylist_new` /
   `mxs_arraylist_append` usage; the ARC release of operands.
6. `src/core/MXArrayList.cpp`, `src/driver/main.cpp` (`mxs_arraylist_*` binds, `print`/`println`).

Code to inspect/change:
- `include/mxspp/frontend/grammar.hpp`, `src/frontend/parser.cpp`, `include/mxspp/frontend/ast.h`
- `src/backend/codegen.cpp`
- `src/driver/main.cpp`; possibly `src/core/MXObject.cpp` (new `mxs_print`/`mxs_println` over a list)

## Deliverables
- Grammar: `param_list` may end with one `rest_param = '...' identifier ':' type_spec` (no default; last
  only). Reject a non-final rest and a defaulted rest in the parser.
- AST: parameter carries `bool is_rest`; a decl with a rest param is recognizable as variadic
  (fixed arity = #params − 1).
- Codegen: calling a callee whose last param `is_rest` → evaluate fixed args; build a fresh `MXArrayList`
  (`mxs_arraylist_new`), `append` each surplus owned arg (append retains → release the temporary per the
  caller-owned convention), pass the list as the trailing `MXObject*`. Zero surplus ⇒ empty (non-null)
  list.
- `mxs_print(MXObject* list)` / `mxs_println(MXObject* list)` in core: iterate the list, write each via
  `str()` (task11), single-space separated; `println` adds `\n`. Bind `print`/`println` to them as
  `func print(...args: any) -> nil;`.
- Tests: an mxs program calling `print(1, "a", true)` → `1 a true`; a user-defined variadic function if
  user funcs are supported; population-count baseline holds (no leak/double-free).

## Steps
1. **Grammar** — add `rest_param`; extend `param_list` to `seq<list<param,...>, opt<','-sep rest_param>>`
   (or `sor`), keeping the existing `param` intact. Verify `func print(...args: any)` and
   `func f(a: int, ...rest: any)` parse and `func f(...r: any, b: int)` / `...r: any = []` are rejected.
2. **AST + parser** — thread `is_rest` into the parameter node from the new rule.
3. **Codegen** — in the call path, detect the callee's trailing rest, pack surplus args into a list with
   correct ARC, pass it last. Handle the `@@foreign` variadic signature (one trailing pointer).
4. **Core + prelude** — add `mxs_print`/`mxs_println` over a list; rebind `print`/`println` variadic.
   (Keep `mxs_print_object`/`mxs_println_object` if still used elsewhere, or retire if fully replaced.)
5. **Test** — add the end-to-end variadic demo/case; run `ctest` + all examples.

## Acceptance criteria
- [ ] `func print(...args: any)` parses; non-final / defaulted rest are parse errors.
- [ ] `print(1, "a", true)` → `1 a true` and `println()` (zero args) → just a newline.
- [ ] Codegen passes an empty `MXArrayList` (not null) for zero surplus args.
- [ ] `MXPopulationManager` live count returns to baseline after a variadic-call program; `ctest` green;
      all existing examples unchanged (modulo task11's string quoting).

## Constraints
- Reuse `MXArrayList` + existing `mxs_arraylist_new`/`append`; do not invent a new arg-pack type.
- Honor the ARC conventions from progress11 (append retains; release surplus temporaries; the list is one
  owned value); release on all control-flow paths.
- 90-col, clang-format; `before_commit.py --staged` before review.

## Notes / Assumptions
- Assumption: codegen already knows each callee's declared params (it declares `funcs[name]`); the rest
  flag is available there to decide packing. If a call targets an unknown/dynamic callee, treat as
  non-variadic (current behavior).
- Question: are user-defined (non-foreign) variadic functions in scope for v1, or only the prelude
  builtins? Default: implement generally (cheap once the rest param exists), test at least the builtin
  path.
