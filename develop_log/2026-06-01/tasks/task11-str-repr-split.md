# Task 11 — str/repr split (core + prelude + REPL)
id: 2026-06-01/task11
parent: 2026-06-01/progress12
status: done
owner: code_agent

## Objective
Introduce a `str()` (Display, human) stringification distinct from `repr()` (Debug, unambiguous), so
top-level `print` and format `{}` use `str()` while containers / REPL / `{:?}` use `repr()`, and strings
gain a quoted+escaped `repr()`.

## Scope
In:
- `MXObject::str()` virtual defaulting to `repr()`.
- `MXString`: `str()` = raw bytes; `repr()` = quoted + escaped.
- `mxs_str` / `mxs_repr` C primitives; `str`/`repr` prelude builtins.
- `print`/`println` switch to `str()`; REPL value echo via `repr()`.
- Unit tests for the new behavior + a non-regression check on scalars.
Out:
- format engine (task13), variadic args (task12), any new container types.

## Inputs (read first, priority order)
1. `develop_log/2026-06-01/progress12-stdio-str-repr-format.md` — §"str / repr" design contract.
2. `src/core/MXObject.cpp` (`repr()`, `mxs_print_object`, `mxs_println_object`) + `include/mxspp/core/MXObject.h`.
3. `src/core/MXString.cpp` / `include/mxspp/core/MXString.h` — current `repr()` = raw bytes.
4. `src/driver/main.cpp` `kCorePrelude` — where `print`/`println` are bound.
5. `src/shell/shell.cpp` — REPL result handling.

Code to inspect/change:
- `include/mxspp/core/MXObject.h`, `src/core/MXObject.cpp`
- `include/mxspp/core/MXString.h`, `src/core/MXString.cpp`
- `src/driver/main.cpp`, `src/shell/shell.cpp`
- `test/core_test.cpp`

## Deliverables
- `MXObject::str()` — `[[nodiscard]] virtual auto str() const -> repr_t { return repr(); }` in the header.
- `MXString::str()` returns `value_`; `MXString::repr()` returns the value wrapped in `"` and escaping at
  least `\\ \" \n \t \r \0`.
- `mxs_str(MXObject*) -> MXObject*` and `mxs_repr(MXObject*) -> MXObject*`: new owned (+1) `MXString` of
  `str()`/`repr()`; nil → `"nil"`. Declared `extern "C"` like the other `mxs_*` primitives.
- `mxs_print_object`/`mxs_println_object` use `o->str()`.
- `kCorePrelude`: `@@foreign(symbol_name="mxs_str") func str(x: any) -> any;` and likewise `repr`.
- REPL: a computed result value is echoed using `repr()` (so strings show quotes at the prompt).
- `test/core_test.cpp`: cases for (a) `MXString` str vs repr (quoting+escaping), (b) `["hi","yo"]` repr
  shows quoted elements, (c) a scalar/bool/nil regression (str == repr, unchanged bytes).

## Steps
1. **MXObject** — add the `str()` virtual (default → `repr()`) in the header; add `mxs_str`/`mxs_repr`
   in `MXObject.cpp` (allocate `mxs_str_new(... )`-style new `MXString`; +1 owned); switch the two print
   primitives to `str()`.
2. **MXString** — split: `str()` = bytes; rewrite `repr()` to quote + escape. Keep `mxs_str_*` helpers
   untouched.
3. **Prelude/REPL** — bind `str`/`repr`; make the REPL echo path use `repr()`.
4. **Tests** — add the three cases above; run `ctest` and every `example/examples/*.mxs` to confirm only
   string quoting (containers/REPL) changed.

## Acceptance criteria
- [ ] Build clean (LLVM 20 / libc++) incl. `core.bc` rebuild.
- [ ] `print("hi")` → `hi`; `repr("hi")` (via builtin) → `"hi"`; `print(["hi","yo"])` → `["hi", "yo"]`.
- [ ] `repr("a\"b\n")` renders the escapes (`"a\"b\n"`), covered by a unit test.
- [ ] Scalars/bool/nil/instances print byte-identical to before across all demos; `ctest` green.

## Constraints
- Only `MXString::repr()` may change observable output; every other type's output stays identical
  (default `str()` = `repr()`).
- Follow `docs/develop_rule.md` ownership rules; `mxs_str`/`mxs_repr` return +1 (caller-owned).
- 90-col, clang-format; run `before_commit.py --staged` before requesting review.

## Notes / Assumptions
- Assumption: `repr_t` is the existing return type of `repr()`; mirror it for `str()`.
- Question: confirm the exact REPL echo site in `shell.cpp` (it builds a `func main()` thunk — the echo
  may be in the driver, not the shell; wire `repr()` wherever the interactive result is shown).
