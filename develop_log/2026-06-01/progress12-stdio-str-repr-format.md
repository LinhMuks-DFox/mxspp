# Progress 12 — stdlib stdio v1: str/repr split + `{}` format + variadic print
id: 2026-06-01/progress12
date: 2026-06-01
author: human+ai
status: active
refs: [2026-06-01/progress11, 2026-05-31/progress09]
supersedes:
commits: []
files:
  - include/mxspp/core/MXObject.h, src/core/MXObject.cpp     # new str() virtual (default=repr); print()→str(); mxs_str/mxs_repr
  - include/mxspp/core/MXString.h, src/core/MXString.cpp     # str()=raw bytes, repr()=quoted+escaped
  - src/core/MXFormat.cpp (NEW), include/mxspp/core/MXFormat.h (NEW)  # mxs_format engine (template + spec)
  - src/core/CMakeLists.txt                                  # add MXFormat to lib + CORE_BC_SOURCES
  - include/mxspp/frontend/grammar.hpp                       # rest-parameter `...name: type`
  - include/mxspp/frontend/ast.h, src/frontend/parser.cpp    # Parameter.is_rest
  - src/backend/codegen.cpp                                  # variadic call lowering (pack surplus args → MXArrayList)
  - src/driver/main.cpp                                      # kCorePrelude: str/repr/format binds; variadic print/println
  - src/shell/shell.cpp                                      # REPL value echo via repr()
  - docs/stdio.md (NEW)                                      # authoritative: text IO + str/repr duality + format spec
  - test/core_test.cpp, example/examples/format_basics.mxs (NEW)

## Goal
Build the **text-output foundation** of the MXScript standard library: (1) split the single
`repr()` stringification into a **`str` (Display, human) / `repr` (Debug, unambiguous)** duality;
(2) a **`format(fmt, …)` function** using a Python/Rust-style `{}` placeholder model (no printf
type specifiers — mxs is dynamically typed); (3) **variadic call** support so `print`/`println`/
`format` take N args, implemented by packing surplus args into an **`MXArrayList`** (the `MXObject*`
dynamic array). End state: `print(a, b, c)` and `format("{} + {} = {}", a, b, c)` run end-to-end
through `mxs run-core`, and `["hi"]` prints with element quotes (the str/repr fix).

Boundary (NOT in this progress): the **`import` system**, **console input**, **type-conversion**
functions, and **file IO** — those four groups Mux is specifying/scaffolding personally (the paused
work). f-strings, richer format spec (base/sign/grouping), and self-hosting `format` in mxs are
**v2** (see Open / TODO). This progress is the *output + format* slice only.

## Context / Motivation
After OOP v1 ([progress11](./progress11-oop-data-methods-operators.md)) the language can run real
programs but text IO is bare: the only console primitives are `print`/`println` (single arg, bound to
`mxs_print_object`/`mxs_println_object`), both going through `MXObject::repr()`. Mux wants the stdlib's
stdio layer, modeled on what feels best (Python's), and explicitly asked to add a `format` facility
("文字 IO 没有 format 会蛋疼很多"). In the design discussion (2026-06-01) Mux took the four decisions
below. Key realization driving the design: because mxs is **dynamically typed**, every value knows its
own type at runtime, so the format mini-language needs **no `%d/%s/%f` type letters** — `{}` means
"stringify whatever is here", a simplification statically-typed languages can't have for free.

Grounding facts verified in-tree (2026-06-01):
- `MXString::repr()` returns the **raw bytes** (comment: "the raw bytes (for print)") — so today
  `print(["hi","yo"])` would render `[hi, yo]` (ambiguous: string vs identifier). This is exactly the
  str/repr conflation Python solves; it is the concrete bug D-STR-REPR fixes.
- `grammar.hpp` `param` = `identifier_list ':' type_spec ['=' expr]` — **no** vararg/rest syntax; D-VARARG
  must add it.
- core string ops are only `mxs_str_new/concat/len/cmp/cstr` (no slice/index/char) — too thin to write
  the format engine in mxs today, so D-FORMAT puts the v1 engine in core C++ (self-hosting deferred).

## Decisions

### D-STR-REPR — split `str` (Display) from `repr` (Debug) — Mux, 2026-06-01
- Decision: introduce two stringifications. **`repr(x)`** = unambiguous developer form (REPL echo,
  container/instance elements, format `{:?}`); **`str(x)`** = human form (top-level `print`, format
  `{}`). `MXObject` gains `virtual auto str() const -> repr_t` defaulting to `repr()`. Only `MXString`
  overrides both: `str()` = raw bytes (unchanged output), `repr()` = **quoted + escaped** (`"hi\n"`).
  Containers (`MXArrayList`, `MXInstance`) keep rendering their elements/fields via element **`repr()`**
  (so `["hi"]` now shows quotes); their own `str()` stays the default (= `repr()`).
- Why: a single stringification is ambiguous for strings inside containers and at the REPL; the
  Display/Debug (Rust) ≈ `str`/`repr` (Python) duality is the standard fix and the precondition for a
  clean `format`. Splitting at the `MXObject` level (default `str()`=`repr()`) means **only strings
  change behavior**; every scalar/container is untouched.
- Impact: `mxs_print_object`/`mxs_println_object` switch to `o->str()`; add `mxs_str(MXObject*)`/
  `mxs_repr(MXObject*)` returning a fresh `MXString` (rc 1, caller-owned); bind `str(x: any)->any`,
  `repr(x: any)->any` in `kCorePrelude`; REPL echoes a result via `repr()`. `print("hi")`→`hi` and all
  numeric/bool/nil output stay identical; the only visible change is string quoting in containers / REPL
  / `{:?}`.
- Alternatives considered: keep one `repr()` (rejected — the container-ambiguity bug, and format needs
  both forms); name them `to_string`/`repr` per type_system §2 (kept `str` as the spec name; `to_string`
  stays an alias the user-class method can carry — see Defaults).

### D-VARARG — variadic functions via a rest-parameter packed into `MXArrayList` — Mux, 2026-06-01
- Decision: add a **rest parameter** `...name: type` (last param only) to function signatures. At a
  call, arguments beyond the fixed params are packed into a **fresh `MXArrayList`** (the `MXObject*`
  dynamic array) bound to `name`. A variadic `@@foreign` function's C-ABI is
  `MXObject* fn(fixed…, MXObject* rest_list)` — the rest list is one trailing `MXObject*` arg.
  `print`/`println` become `func print(...args: any)`; `format` is `func format(fmt: str, ...args: any)`.
- Why: Mux's proposal — MXList is already the runtime's owned `MXObject*` dynamic array, so it is the
  natural carrier for a variable arg pack and reuses the existing container ARC. Doing it as a real
  language feature (not a print-only hack) also gives users variadic functions for free.
- Impact: grammar gains a `rest_param` rule; `ast::Parameter` (or the param representation) gains
  `is_rest`; codegen's call path, when the callee's last param is a rest, allocates an `MXArrayList`
  (`mxs_arraylist_new`), `mxs_arraylist_append`s the surplus owned args (append retains; per ARC release
  the temporaries), and passes the list as the trailing pointer. The list is one owned value (adopted by
  the callee frame / released after a `@@foreign` borrow call per the existing two conventions).
- Alternatives considered: a `@@foreign(variadic=true)` attribute with no language syntax (rejected —
  less general, no user varargs); C-style untyped varargs / `va_list` (rejected — breaks the uniform
  `MXObject*` ABI and ARC).

### D-FORMAT — `{}` placeholder `format` function; engine in core C++ for v1 — Mux, 2026-06-01
- Decision: `format(fmt: str, ...args: any) -> str`. Template syntax: `{}` (auto-incrementing position),
  `{N}` (explicit index); `{{` / `}}` are literal braces; an optional `:spec` and conversion. Conversion
  **`{:?}` selects `repr()`**, default `{}` selects `str()` (Rust-style). Spec mini-language **v1** =
  Python subset `[[fill]align][width][.precision]`: `align` ∈ `< > ^` (left/right/center) with an
  optional preceding fill char; `width` = integer min field width; `.precision` = float decimal places.
  The v1 engine lives in **core C++**: `mxs_format(MXObject* fmt, MXObject* args_list) -> MXObject*`
  parses the template, renders each field from the arg's `str()`/`repr()` (applying float precision via a
  numeric hook), then pads/aligns to `width`.
- Why: dynamic typing removes the need for type letters → one `{}` covers everything (the central
  insight). `format` is **pure string-building** (no IO), so it stays portable and self-hostable later;
  but core string ops are too thin to write it in mxs today, so v1 ships the engine in C++. width/align/
  precision is ~80% of real formatting needs.
- Impact: new `MXFormat.cpp`/`.h` in core (added to `CORE_BC_SOURCES` so it JIT-links); bind
  `format(fmt: str, ...args: any) -> any` in `kCorePrelude` (variadic per D-VARARG). `print` stays the
  simple path (space-join `str()` of its args, no newline; `println` adds `\n`).
- Alternatives considered: printf `%`-specifiers (rejected — type letters are pointless under dynamic
  typing); C++ `<<` streams (rejected — verbose, sticky global state); implement format in mxs now
  (deferred — needs slice/index/char string ops first).

### D-FSTRING — f-strings are sugar for `format()`, desugared in the frontend — Mux, 2026-06-01 (v2)
- Decision: `f"{name} is {age}"` is **syntactic sugar** lowered to `format("{} is {}", name, age)` (and
  `f"{x:>8}"` → `format("{:>8}", x)`), done in **preprocess/parse** — no new runtime. **Deferred to v2.**
- Why: f-strings are the most ergonomic surface, but require lexer work (recognize an `f"`-prefixed
  string, split literal vs `{expr}` runs, parse embedded expressions) that is independent of, and best
  layered on top of, a working `format`. Ship `format()` first; add the sugar once it is solid.
- Impact (when done): frontend only — the lexer/parser emits a `FunctionCall{name="format", args=[…]}`;
  the runtime/`format` engine is unchanged. No task in this progress; tracked in Open / TODO.

## Design contract (authoritative; tasks reference this)

### str / repr (the two stringifications)
- `MXObject`: `[[nodiscard]] virtual auto str() const -> repr_t { return repr(); }` (declared in the
  header so subclasses can override; default delegates to `repr()`).
- `MXString`: `str()` → `value_` (raw bytes, current behavior); `repr()` → the value **quoted and
  escaped** with `"` … `"`, escaping at least `\\ \" \n \t \r` (and `\0`). This is the only type whose
  `repr()` changes.
- Containers stay element-`repr()`: `MXArrayList::repr()` → `[<repr>, …]` (already does — now correctly
  quotes string elements); `MXInstance::repr()` → `ClassName(f=<repr>, …)`. Their `str()` = default.
- ABI: `mxs_str(MXObject*) -> MXObject*` and `mxs_repr(MXObject*) -> MXObject*` each return a **new,
  owned (+1)** `MXString` of `str()`/`repr()` (nil-safe → `"nil"`). `mxs_print_object`/
  `mxs_println_object` call `o->str()` (not `repr()`).

### Variadic ABI (rest parameter)
- Grammar: a `param_list` may end with one `rest_param` = `'...' identifier ':' type_spec`. Only the
  **last** parameter may be a rest; no default on a rest.
- AST: the parameter representation carries `bool is_rest`. A function/`@@foreign` decl with a rest param
  is "variadic"; its fixed arity = #params − 1.
- Codegen call lowering: when calling a callee whose last param `is_rest`, evaluate fixed args normally;
  collect the remaining call args into a fresh `MXArrayList` (`mxs_arraylist_new`, then
  `mxs_arraylist_append` each — append **retains**, so release each appended temporary per the
  caller-owned convention); pass the list as the final `MXObject*`. Zero surplus args ⇒ an **empty**
  list (never null). The list itself is one owned value handled by the existing ARC rules.
- `@@foreign` variadic callee sees exactly `(fixed…, MXObject* rest_list)`.

### format template + spec (v1)
- Field: `'{' [index] [':' spec] '}'`; `{{` and `}}` are literal `{` / `}`. `index` = decimal arg index
  (0-based, into `args_list`); omitted ⇒ next auto position. A field with an out-of-range index → a
  format error (`mxs_raise`-able / for now a clearly-marked error string; tie to the error model used by
  `mxs_op_*`).
- Conversion: a trailing `?` in the spec (i.e. `{:?}`) renders the arg via `repr()`; otherwise `str()`.
- spec grammar (Python subset): `[[fill]align][width][.precision][?]`
  - `align` = one of `< > ^`; if a char precedes the align it is the `fill` (default fill = space).
  - `width` = decimal min field width; shorter renderings are padded with `fill` per `align` (default
    align = left `<` for all types in v1 — note: Python right-aligns numbers; v1 keeps it uniform-left
    unless Mux wants numeric right-align).
  - `.precision` = decimal digits for floats (applied when rendering the number); ignored for non-floats
    in v1 (string truncation deferred).
- Engine: `mxs_format(fmt, args_list)` returns a new owned `MXString`. Out of v1: numeric base
  (`x/b/o`), sign (`+`/space), `0`-fill-as-zero-pad semantics for numbers, grouping (`,`/`_`), string
  truncation by precision, `{name}` keyword fields (no kwargs in the language).

## Defaults (assumed unless Mux objects)
- `print(a, b, …)` joins args with a **single space** and writes **no trailing newline**; `println`
  appends `\n` (keeps the existing print=no-newline / println=newline convention, extended to N args).
- A user class participates in `str` by defining a `str` method (the type_system `to_string` alias maps
  here); it already gets `repr` via its `repr` method / the `MXInstance` default. The format
  per-type-spec hook for user classes (a `format(spec)` method, Python `__format__`) is **deferred** —
  v1 user objects render via `str()`/`repr()` and ignore any numeric spec.
- v1 default field alignment is **left (`<`)** for every type (simpler than Python's type-dependent
  default); revisit if Mux wants numbers right-aligned.
- Format errors (bad index, malformed spec) surface through the existing error path; exact policy
  (raise vs. inline marker) is the implementer's call, matching `mxs_op_*`.

## Tasks
- [x] [task11 — str/repr split (core + prelude + REPL)](tasks/task11-str-repr-split.md)
- [x] [task12 — variadic rest-parameter (frontend + codegen)](tasks/task12-variadic-rest-param.md)
- [x] [task13 — format engine (core) + binding + docs/demo](tasks/task13-format-engine.md)

## Issues / Gotchas
- **Only strings change output.** Guard against regressions in every existing demo
  (`core_types`/`core_list`/`oop_*`): scalars/bools/nil/instances must print byte-identical; only string
  quoting inside containers / REPL / `{:?}` differs. Add/adjust expected outputs deliberately.
- **MXString::repr() escaping** must round-trip the common escapes; an unescaped `"` or `\n` in `repr()`
  breaks container output. Cover with a unit test (`repr("a\"b\n")` == `"a\"b\n"` literally).
- **Variadic ARC.** The packed `MXArrayList` and each appended element follow the existing two
  conventions (append retains → release the surplus temporaries; the list is one owned value). Watch
  release on all control-flow paths, same as progress11's ARC gotchas. Verify with the
  `MXPopulationManager` baseline assertion.
- **Rest param is last-only.** The grammar/parser must reject a rest that is not the final parameter, and
  reject a default on a rest. Codegen must pass an **empty (non-null)** list when zero surplus args.
- **Bitcode skew** (progress11 D-BUILD): `MXFormat.cpp` is a new `core.bc` source → it must be emitted by
  the `MXS_BC_CXX` (LLVM-20) compiler, not the host clang 22. `CORE_BC_SOURCES` already routes through it;
  just add the file.

## Open / TODO (carry-over)
- **D-FSTRING (v2):** f-string sugar → `format()` in the frontend (lexer `f"…"` + embedded-expr parse).
- **format spec v2:** numeric base/sign/zero-pad/grouping, string truncation by precision, type-dependent
  default alignment, a user-class `format(spec)` hook.
- **Self-hosting format:** re-implement `mxs_format` in mxs once string ops (slice/index/char) exist —
  `format` is pure string-building, the natural first self-hosted stdlib piece.
- **The other stdlib groups (Mux-owned, paused):** `import` system (AST node + parser + module loader +
  `io.println` namespacing), console **input**, **type-conversion**, **file IO** primitives. Separate
  progress when Mux resumes the scaffolding.

## Agent log
- 2026-06-01 [ai] **stdio v1 COMPLETE & verified (task11–13).** Implemented and built green on the
  WSL host (vendored LLVM 20 / libc++; `MXS_BC_CXX`=lib/llvm20-tools clang for bitcode). What landed:
  - **D-STR-REPR:** `MXObject::str()` virtual (default → `repr()`); `MXString::str()` = raw bytes,
    `repr()` = quoted + escaped (`\\ \" \n \t \r \0`). `mxs_str`/`mxs_repr` builtins (+1 owned
    MXString, nil→"nil"); `print`/`println` now go through `str()`; the REPL echoes via `repr()`.
    Net visible change is string-only: containers of strings show quotes (`["hi", "yo"]`).
  - **D-VARARG:** grammar `rest_param` (`...name: type`, last-only) + `ast::Parameter.isRest` +
    `collect_params` handling; codegen packs surplus call args into a fresh `MXArrayList`
    (append retains → release each temp; empty non-null list for zero surplus), passed as the
    trailing pointer. A `variadics` set in `compile_core` drives it. User-defined variadic functions
    work too (the rest param binds to the packed list; verified `count(...xs)` → len).
  - **D-FORMAT:** `MXFormat.cpp` — `mxs_format(fmt, args)` template engine (`{}`/`{N}`, `{{`/`}}`,
    spec `[[fill]align][width][.precision]`, `{:?}`→repr), returns +1 MXString or an MXError on a
    bad index/spec. `print`/`println` variadic primitives live here too. Added to the lib +
    `CORE_BC_SOURCES`. Bound in `kCorePrelude`: variadic `print`/`println`, `str`/`repr`,
    `format(fmt, ...args)`.
  Verification: `ctest` 2/2 (21 cases — added `string_str_repr_split`, `stdio_format`; fixed two
  string-repr assertions for the new quoting). `format_basics.mxs` runs through `mxs run-core` with
  the documented output. Regression sweep: all previously-working demos (hello_world, core_*, oop_*,
  obj_*) unchanged at rc 0; the rc≠0 demos all fail on pre-existing unimplemented features
  (default-param values, `@@template`, kwargs, `raise`-expr, `cast`, file IO / `import`), none from
  this change. clang-format clean (`git clang-format` over the changed lines; whole uncommitted delta
  is now format-clean). `docs/stdio.md` written (authoritative). **Not committed** (awaiting Mux
  review). Build note: the host `build/` cache was stale (Make generator, system compiler); had to
  reconfigure fresh with the progress11 host recipe (Ninja + toolchain.cmake + Homebrew prefix +
  libc++ link path + LLVM-20 `MXS_BC_CXX`/`MXS_LLVM_LINK`).
- 2026-06-01 [ai] Design discussion with Mux on stdio paradigms + format; Mux decided **D-STR-REPR**
  (split str/repr), **D-VARARG** (varargs via MXArrayList rest-param), **D-FORMAT** (`{}` placeholder,
  core-C++ engine, width/align/precision), **D-FSTRING** (sugar for format, v2). Verified in-tree facts
  (single-`repr()` conflation incl. the string-in-container bug; grammar has no rest syntax; core string
  ops too thin for an mxs-side format engine). Recorded this progress + the design contract and broke the
  work into task11–13. **No implementation code written yet** (recording the decision first).
