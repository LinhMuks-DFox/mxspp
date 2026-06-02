# stdio — text output, `str`/`repr`, and `format` (v1)

Authoritative reference for MXScript's text-output standard-library slice (progress12). Scope: the
console **output** path (`print`/`println`), the **`str`/`repr`** stringification duality, and the
**`format`** function. Console **input**, **type conversion**, **file IO**, and the **`import`**
system are out of scope here (separate work). f-strings are planned sugar (v2, see end).

> Design rationale + decisions: `develop_log/2026-06-01/progress12-stdio-str-repr-format.md`
> (D-STR-REPR, D-VARARG, D-FORMAT, D-FSTRING). This file documents the resulting surface.

## str vs repr — two stringifications

Every value has two textual forms:

| form | who calls it | strings render as |
|------|--------------|-------------------|
| **`str(x)`** — Display, human | top-level `print`/`println`, format `{}` | raw bytes: `hi` |
| **`repr(x)`** — Debug, unambiguous | container/instance elements, the REPL prompt, format `{:?}` | quoted + escaped: `"hi\n"` |

`str()` defaults to `repr()`, so the two coincide for every type **except strings** (and any user
class that defines its own `str`). The split is what makes `print("hi")` show `hi` while
`print(["hi"])` shows `["hi"]` — containers render their elements with `repr()`, so a string inside
a list is unambiguous.

```mxs
print("hi")          // hi
repr("hi")           // "hi"      (a string whose bytes are  "hi"  with the quotes)
println(["a", "b"])  // ["a", "b"]
```

Builtins: `str(x: any) -> str` and `repr(x: any) -> str` each return a new string. A user class
participates in `str` by defining a `str` method; it already has `repr` via the object model.

## print / println

```mxs
print(...args: any) -> nil      // space-joins str() of each arg; NO trailing newline
println(...args: any) -> nil    // same, then a newline
```

Both are **variadic** (progress12 D-VARARG): the surplus arguments are packed into a list and
handed to the runtime. `println()` with no args prints just a newline.

```mxs
print("x", 1, true)   // x 1 true
println()             // (blank line)
```

## format

```mxs
format(fmt: str, ...args: any) -> str
```

A template with `{}` placeholders. Because MXScript is dynamically typed, there are **no `printf`
type letters** — `{}` stringifies whatever value lands there. `format` builds a string only (it does
no IO); pair it with `print`/`println` to emit.

### Fields

| field | meaning |
|-------|---------|
| `{}` | next positional argument (auto-incrementing) |
| `{N}` | argument at 0-based index `N` |
| `{{`, `}}` | a literal `{` / `}` |
| `{:spec}` | apply a format spec (below) |
| `{:?}` | render via `repr()` instead of `str()` |

A field index out of range, or a malformed spec, yields an error value (the match-based error
model) rather than crashing.

### Spec (v1)

```
[[fill]align][width][.precision][?]
```

- **align** — `<` left · `>` right · `^` center. A character before the align is the **fill**
  (default fill is a space). v1 default alignment is **left** for every type.
- **width** — minimum field width; shorter renderings are padded with the fill per the alignment.
- **.precision** — number of decimal places for a **float** (ignored for other types in v1).
- **`?`** — a trailing `?` selects the `repr()` form for that field.

```mxs
format("{} + {} = {}", 1, 2, 3)   // 1 + 2 = 3
format("{0} {1} {0}", "a", "b")   // a b a
format("[{:>8}]", "hi")           // [      hi]
format("[{:*^8}]", "hi")          // [***hi***]
format("pi = {:.2}", 3.14159)     // pi = 3.14
format("{:?}", "x")               // "x"
format("{{}}")                    // {}
```

## What's deferred (v2)

- **f-strings**: `f"{name} is {age}"` lowered to `format("{} is {}", name, age)` at parse time
  (D-FSTRING) — sugar over this same `format`, no runtime change.
- **Spec**: numeric base (`x`/`b`/`o`), sign, zero-pad, grouping; string truncation by precision;
  type-dependent default alignment; a user-class `format(spec)` hook.
- **Self-hosting**: re-implement `format` in MXScript once the string library (slice/index/char)
  exists — it is pure string-building, the natural first self-hosted stdlib piece.
