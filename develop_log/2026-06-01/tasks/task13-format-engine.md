# Task 13 — format engine (core) + binding + docs/demo
id: 2026-06-01/task13
parent: 2026-06-01/progress12
status: done
owner: code_agent

## Objective
Implement `format(fmt, …) -> str` with a `{}`-placeholder template and a `[[fill]align][width]
[.precision][?]` spec, as a core C++ engine, bound variadically; document the stdio/format design and
add an integration demo.

## Scope
In:
- `mxs_format(MXObject* fmt, MXObject* args_list) -> MXObject*` engine in core (`MXFormat.cpp/.h`).
- Template: `{}` auto-position, `{N}` index, `{{`/`}}` literals, `:spec`, `{:?}` → `repr`.
- Spec v1: `align` `< > ^` (+ optional fill char), `width`, `.precision` (float decimals).
- Prelude binding `format(fmt: str, ...args: any) -> any` (uses task12 varargs).
- `docs/stdio.md` (authoritative) + `example/examples/format_basics.mxs` + unit tests.
Out:
- f-strings (v2), numeric base/sign/grouping, string truncation, `{name}` kwargs, user-class
  `format(spec)` hook.

## Inputs (read first, priority order)
1. `develop_log/2026-06-01/progress12-stdio-str-repr-format.md` — §"format template + spec (v1)" + Defaults.
2. Task11 (`str`/`repr` available) and task12 (variadic packing) — **dependencies; do after both**.
3. `src/core/MXString.cpp` (`mxs_str_new`, owned `MXString`), `src/core/MXFloat.cpp` (float rendering for
   precision), `src/core/CMakeLists.txt` (`CORE_BC_SOURCES`, `MXS_BC_CXX`).
4. `src/driver/main.cpp` `kCorePrelude`.

Code to inspect/change:
- `include/mxspp/core/MXFormat.h` (NEW), `src/core/MXFormat.cpp` (NEW)
- `src/core/CMakeLists.txt`
- `src/driver/main.cpp`
- `docs/stdio.md` (NEW), `example/examples/format_basics.mxs` (NEW), `test/core_test.cpp`

## Deliverables
- `mxs_format(fmt, args_list)`: parse `fmt`'s bytes; emit literal runs; for each field resolve the arg
  (auto/indexed) from `args_list`, render via `repr()` if the spec ends in `?` else `str()` (floats apply
  `.precision` when rendering the number), then pad/align to `width` with `fill`. Returns a new owned
  (+1) `MXString`. Out-of-range index / malformed spec → an error consistent with `mxs_op_*`.
- `CORE_BC_SOURCES` includes `MXFormat.cpp` (emitted by `MXS_BC_CXX`, the LLVM-20 compiler).
- Prelude: `@@foreign(symbol_name="mxs_format") func format(fmt: str, ...args: any) -> any;`.
- `docs/stdio.md`: text IO surface (`print`/`println`/`str`/`repr`/`format`), the str/repr duality, the
  full `{}`/spec grammar, what is v1 vs v2; link from `progress12`.
- `example/examples/format_basics.mxs`: positional/indexed, `{:>8}`, `{:.2}` on a float, `{:?}` on a
  string and a list — with expected output in a comment for the smoke test.
- Unit tests: positional vs indexed; `{{`/`}}`; width+align (`<`/`>`/`^`, custom fill); float precision;
  `{:?}` repr vs default str; bad-index error.

## Steps
1. **Engine** — write the template/spec parser + renderer in `MXFormat.cpp`; keep it dependency-light
   (uses `str()`/`repr()` from task11 and a float-with-precision render). Add the header.
2. **Build** — add `MXFormat.cpp` to the core lib target and `CORE_BC_SOURCES`; rebuild `core.bc`.
3. **Bind** — add the `format` `@@foreign` line to `kCorePrelude`.
4. **Docs + demo + tests** — write `docs/stdio.md`, the demo, and the unit cases; run `ctest` + the demo
   via `mxs run-core`.

## Acceptance criteria
- [ ] `format("{} + {} = {}", 1, 2, 3)` → `1 + 2 = 3`; `format("{0} {1} {0}", "a", "b")` → `a b a`.
- [ ] `format("{:>8}", "hi")` → `"      hi"`; `format("{:.2}", 3.14159)` → `"3.14"`;
      `format("{:?}", "x")` → `"\"x\""`; `format("{{}}")` → `"{}"`.
- [ ] Bad index (`format("{5}", 1)`) errors cleanly (no crash), matching the project error path.
- [ ] `format_basics.mxs` runs through `mxs run-core` with the documented output; `ctest` green;
      `MXFormat.cpp` builds via `MXS_BC_CXX` with no bitcode-version skew.

## Constraints
- v1 engine in core C++ (string ops too thin to self-host yet); pure string-building, no IO.
- Dynamic typing ⇒ no type letters — `{}` stringifies any value; do not add `%d`-style specifiers.
- Returned `MXString` is +1 owned; follow `docs/develop_rule.md`. 90-col, clang-format; `before_commit.py`.

## Notes / Assumptions
- Assumption: v1 default alignment is left (`<`) for all types (per progress12 Defaults); width pads to
  the right with the fill char.
- Assumption: float precision uses the same numeric formatting as `MXFloat` (e.g. `std::format("{:.Nf}")`)
  — confirm against `MXFloat::repr()` so default (no-precision) output is unchanged.
- Question: error policy — raise via `mxs_raise` vs. return a marked error string. Default: match whatever
  `mxs_op_*` does for a type error, for consistency.
