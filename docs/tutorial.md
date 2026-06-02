# MXScript Tutorial (current build)

A hands-on guide to the **subset of MXScript that runs today**. Everything shown under "✅ Works"
has been executed with the current `mxs` binary; features that only parse (or aren't built yet) are
collected — and clearly labelled — in [§14 Not yet implemented](#14-not-yet-implemented).

- The **authoritative grammar reference** is [`docs/syntax.md`](./syntax.md) (each construct is tagged
  `[parses + runs]` vs `[parses only]`). This tutorial is the *task-oriented* companion.
- For the object model / ARC see [`docs/object_model.md`](./object_model.md); for the FFI contract
  see [`docs/ffi.md`](./ffi.md); for the type/dispatch design see [`docs/type_system.md`](./type_system.md).

> Status: the language runs real programs (arithmetic, strings, lists, control flow, functions,
> `match`, single-class OOP with ARC, an import-gated standard library), but it is still an early
> build — see §14 for the boundaries.

---

## 1. Build & run

```bash
python3 project_init.py        # one-time: vendor LLVM + PEGTL into lib/
python3 rebuild.py --clean     # first build (or after CMake changes)
python3 rebuild.py             # incremental builds afterwards
```

This produces the `mxs` binary and the runtime bitcode `core.bc` under `build/bin/`.

The CLI has four modes:

```bash
mxs run-core <file.mxs>     # JIT-compile and run main()
mxs check    <file.mxs>     # parse-only lint (syntax + import errors), prints "ok"
mxs --dump-ast <file.mxs>   # parse and print the AST
mxs                         # or: mxs shell  — interactive REPL
```

Every program's entry point is `func main() -> int`; the returned int is the process exit code.

---

## 2. Hello, world — and the import rule

```mxs
import std.io.{println};

func main() -> int {
    println("Hello, World!");
    return 0;
}
```

```bash
$ mxs run-core hello.mxs
Hello, World!
```

**The standard library is import-gated.** Nothing — not even `println` — is in scope without an
`import`. A program that calls `println` with no import is rejected:

```
core-codegen: call to unknown function 'println'
```

See [§11 Modules & imports](#11-modules--imports) for the three import forms.

---

## 3. Values & variables

MXScript is **dynamically typed** (values carry their type at runtime) but **statically scoped**.
Bindings are introduced with `let`, and are **immutable by default**:

```mxs
import std.io.{println};

func main() -> int {
    let a = 10;          # immutable
    let mut b = 1;       # mutable — reassignable
    b = b + a;
    b += 5;              # compound assignment works on `let mut`
    println(b);          # 16

    let name = "mxs";    # strings
    let flag = true;     # booleans
    let nothing = nil;   # nil
    let pi = 3.14;       # floats
    println(name);
    return 0;
}
```

Three binding rules are enforced **at compile time**:

| You write | Result |
|---|---|
| `let a = 1; a = 2;` | error: `cannot assign to immutable binding 'a'` |
| `let a = 1; let mut a = 2;` (same scope) | error: `redeclaration of 'a' in the same scope` |
| nested block re-using a name | **allowed** — inner binding shadows the outer one |

Shadowing in a nested block is legal and the outer binding is restored on exit:

```mxs
import std.io.{println};

func main() -> int {
    let a = 1;
    if true {
        let a = 2;       # shadows the outer `a` inside this block
        println(a);      # 2
    }
    println(a);          # 1
    return 0;
}
```

> A **type annotation** may be written (`let a: int = 1;`) but it is currently advisory — codegen is
> dynamically typed and does not check or enforce it.

---

## 4. Operators

```mxs
import std.io.{println};

func main() -> int {
    println(2 + 3 * 4);    # 14   (* binds tighter than +)
    println(2 ** 10);      # 1024 (power)
    println(2 ** 3 ** 2);  # 64   (** is LEFT-associative here: (2**3)**2)
    println(7 / 2);        # 3    (integer division)
    println(7 % 3);        # 1
    println(-5 + 3);       # -2
    println(3 < 5 && 5 >= 5);  # true   (&&, ||, ! short-circuit)
    println(!false);       # true
    println("a" + "b");    # ab   (+ concatenates strings)
    return 0;
}
```

Comparisons (`< <= > >= == !=`) and logic (`&& || !`) yield booleans. Arithmetic is dynamic: `int op
int` stays exact, a float operand promotes to float, `string + string` concatenates.

> Note `**` is **left**-associative in this build (math convention is right). Tracked in
> `docs/syntax.md` §2.

---

## 5. Strings & lists

Lists are written with `[...]`, indexed with `[i]`, and iterated with `for x in xs`:

```mxs
import std.io.{println, print};

func main() -> int {
    let xs = [10, 20, 30];
    xs.append(40);          # method, mutates the list contents
    println(xs.len());      # 4
    println(xs.get(1));     # 20
    println(xs[2]);         # 30  (subscript)
    for v in xs { print(v); print(" "); }   # 10 20 30 40
    println("");

    println("hello".len()); # 5  (strings have .len() too)
    return 0;
}
```

**Container and string operations are methods on the receiver, not free functions** — `xs.append(v)`,
`xs.len()`, `xs.get(i)`, `"s".len()`. There is no global `len(...)` or `append(...)`; calling those
is an error (`call to unknown function 'len'`).

> `xs.append(v)` does **not** require `let mut` — it mutates the list's *contents*, not the binding.

---

## 6. Control flow

MXScript has `if`, `for … in`, `loop`, `until`, and `do … until`. **There is no `while` — by
design.** The loop construct is `until`, read as *"keep going **until** the condition becomes true"*
(the exit condition is stated positively). `until (c)` runs the body **while `c` is false** and stops
the moment `c` becomes true.

```mxs
import std.io.{println};

func main() -> int {
    # if / else if / else — the condition is a BARE expression (no parentheses required)
    let x = 5;
    if x > 10 { println("big"); }
    else if x > 3 { println("mid"); }   # prints: mid
    else { println("small"); }

    # for over an exclusive integer range, and over a list
    for i in 0..3 { println(i); }       # 0 1 2
    for v in [7, 8] { println(v); }     # 7 8

    # until: pre-test. Runs UNTIL the condition holds.
    let mut n = 0;
    until (n >= 3) { println(n); n = n + 1; }   # 0 1 2

    # do … until: post-test (body runs at least once)
    let mut m = 0;
    do { println(m); m = m + 1; } until (m >= 2);   # 0 1

    # loop + break / continue
    let mut k = 0;
    loop {
        if k >= 2 { break; }
        println(k);                      # 0 1
        k = k + 1;
    }
    return 0;
}
```

Summary of the loops:

| Form | Meaning |
|---|---|
| `for v in lo..hi { … }` | iterate the exclusive integer range `[lo, hi)` |
| `for v in xs { … }` | iterate a list (or string) by element |
| `until (c) { … }` | pre-test: run the body until `c` becomes true |
| `do { … } until (c);` | post-test: run once, then repeat until `c` becomes true |
| `loop { … }` | run forever; exit with `break` |

`break` and `continue` work inside any loop. The loop variable of a `for` is immutable (`for v`);
write `for mut v` to reassign it in the body.

---

## 7. Functions

```mxs
import std.io.{println};

func add(a: int, b: int) -> int { return a + b; }

func fib(n: int) -> int {
    if n < 2 { return n; }
    return fib(n - 1) + fib(n - 2);     # recursion
}

func main() -> int {
    println(add(2, 3));   # 5
    println(fib(10));     # 55
    return 0;
}
```

**Variadic functions** collect surplus arguments into a list via a trailing `...rest` parameter:

```mxs
import std.io.{println};

func count(first: int, ...rest: any) -> int {
    return 1 + rest.len();   # `rest` is a list of the extra args
}

func main() -> int {
    println(count(10));           # 1
    println(count(10, 20, 30));   # 3
    return 0;
}
```

(`println`, `print`, and `format` are themselves variadic — that is how `println(a, b, c)` works.)

---

## 8. `match` and the error model

`match` is an **expression**: it evaluates to the matching arm's value. Arms can be a literal, a
**type-binding** pattern (`name: Type`), or the wildcard `_`.

```mxs
import std.io.{println};

func describe(n: int) -> any {
    return match (n) {
        case 0 => "zero"
        case 1 => "one"
        case _ => "many"
    };
}

func main() -> int {
    println(describe(0));   # zero
    println(describe(9));   # many
    return 0;
}
```

Errors are **values** (not exceptions). An operation that fails returns an `Error`, and you
discriminate it with a type-binding `match` arm:

```mxs
import std.io.{println};

func main() -> int {
    # 1/0 yields an Error value rather than throwing
    let result = match (1 / 0) {
        case e: Error => "caught a division error"
        case v: int   => "got a value"
        case _        => "other"
    };
    println(result);        # caught a division error
    return 0;
}
```

`raise(...)` and `exit(code)` are ordinary **functions** imported from `std.io` (there is no `raise`
keyword); `raise` prints the error and terminates the process.

---

## 9. Object-oriented programming

A `class` has fields, a constructor named after the class, methods (dispatched through a per-class
vtable), operator overloads, and a destructor `~ClassName()`. Memory is managed by **ARC** (automatic
reference counting): when the last reference goes away the destructor runs deterministically.

```mxs
import std.io.{println};

class Point {
    Point(x: int, y: int) {        # constructor (same name as the class)
        self.x = x;
        self.y = y;
    }
    func sum() -> int { return self.x + self.y; }      # method
    operator+(o: Point) -> Point {                     # operator overload
        return Point(self.x + o.x, self.y + o.y);
    }
    ~Point() { println("dropping a Point"); }          # destructor (ARC)
    let x: int;
    let y: int;
}

func main() -> int {
    let p = Point(3, 4);
    println(p.x);          # 3       (field access)
    println(p.sum());      # 7       (method call via vtable)
    let q = p + Point(1, 1);   # operator+
    println(q.sum());      # 9
    return 0;              # ~Point runs here for each live instance
}
```

Notes:
- The constructor is the method whose name equals the class name; call it as `Point(3, 4)`.
- Optional `public:` / `private:` access labels may precede members (access is not enforced yet).
- Overloadable operators include `+ - * / % ** < <= > >= == != !`. (`operator**` and `operator[]`
  cannot be *written* yet even though the runtime reserves vtable slots — see §14.)
- **No inheritance yet** — classes are standalone (see §14).

---

## 10. Formatting, `str`, and `repr`

```mxs
import std.io.{println, str, repr, format};

func main() -> int {
    println(str(42));                 # 42      (human form)
    println(repr("hi"));              # "hi"    (debug form — strings are quoted)
    println(format("{} + {} = {}", 2, 3, 5));     # 2 + 3 = 5   (positional)
    println(format("{0} {0} {1}", "a", "b"));     # a a b       (indexed)
    println(format("[{:5}][{:<5}][{:>5}]", 1, 2, 3));  # [1    ][2    ][    3]  ({:N} defaults to left)
    println(format("{:?}", "q"));     # "q"     (debug spec)
    return 0;
}
```

`format` supports positional `{}`, indexed `{N}`, width/alignment specs `{:5}` / `{:<5}` / `{:>5}`,
and the debug spec `{:?}`.

---

## 11. Modules & imports

The stdlib lives in `std/*.mxs` (e.g. `std/io.mxs`, `std/time.mxs`) and is reached only through
`import`. There are three forms:

```mxs
# (a) qualified — the module's last segment becomes a namespace:
import std.io;
# ... io.println("hi");

# (b) aliased — rename that namespace:
import std.io as o;
# ... o.println("hi");

# (c) selective — bring the listed names into scope UNQUALIFIED:
import std.io.{println, format};
# ... println("hi");
```

A complete program using each:

```mxs
import std.io;
import std.time.{now, monotonic_ns};

func main() -> int {
    let t0 = monotonic_ns();
    let t1 = monotonic_ns();
    io.println(t1 - t0 >= 0);   # true  (monotonic clock never goes backwards)
    io.println(now() > 0);      # true  (wall-clock seconds since the epoch)
    return 0;
}
```

Rules:
- A namespace must be bound by **exactly one** import. Importing the same module twice, or two
  modules under one alias, is an error — use `as` to give a distinct name.
- A **local variable shadows** a same-named imported namespace (`let io = …;` then `io.m()` is a
  method call on the local, not a module call).
- **Transitive imports are not supported**: a module that itself `import`s is rejected with a
  diagnostic.

`std.io` exports `println, print, str, repr, format, raise, exit` (+ `arraylist`). `std.time` exports
`now, now_ms, monotonic_ns`.

---

## 12. The REPL

```bash
$ mxs shell
mxs> let x = 21
mxs> x * 2
42
mxs> 1 + 2 * 3
7
mxs> :reset      # clear accumulated lets/defs so names can be redefined
mxs> :q          # quit
```

The REPL accumulates `let` bindings and `func`/`class` definitions across lines and re-runs them each
evaluation, so `let`s persist. For interactive convenience it auto-imports
`std.io.{println, print, str, repr, format}` — so those names work unqualified at the prompt without
an explicit `import` (this is REPL-only ergonomics; **program files still require the import**).

> Known REPL limitation: **assignments and in-place mutations to an existing binding do not persist**
> across lines (only `let` lines are replayed). `let mut a = 4` then `a = 10` then `a` still prints
> `4`. A REPL that persists a mutable environment is planned; for now use `:reset` and re-`let`.

---

## 13. A slightly larger example

```mxs
import std.io.{println};

class Counter {
    Counter(start: int) { self.n = start; }
    func bump() -> int { self.n = self.n + 1; return self.n; }
    let n: int;
}

func sum_to(n: int) -> int {
    let mut total = 0;
    for i in 1..n { total = total + i; }   # 1 + 2 + … + (n-1)
    return total;
}

func main() -> int {
    let c = Counter(10);
    println(c.bump());     # 11
    println(c.bump());     # 12
    println(sum_to(5));    # 10  (1+2+3+4)

    let nums = [3, 1, 2];
    nums.append(9);
    println(nums.len());   # 4

    let label = match (nums.len()) {
        case 4 => "four items"
        case _ => "some items"
    };
    println(label);        # four items
    return 0;
}
```

---

## 14. Not yet implemented

These are tracked so you don't reach for them by mistake. Two categories:

### Deliberately absent (by design)
- **`while`** — there is no `while` loop, on purpose. Use `until (c)` (pre-test, "run until `c`
  becomes true") or `do … until (c)` (post-test). See §6.
- **Global `len` / `append`** — container ops are methods (`xs.len()`), never free functions.
- **Implicit stdlib** — no name is available without an `import` (no auto-prelude).

### Parses but does **not** run yet `[parses only]`
The grammar accepts these, but `mxs run-core` will not execute them (you'll hit a codegen/JIT error):
- **`interface` / `type` / `enum`** definitions.
- **Generics** — `func f<T>(...)` and `List<int>` as a value annotation parse only; there is no
  generic instantiation.
- **`lambda` expressions** and standalone **block-expressions**.
- **Keyword arguments** `f(a=5)` and **default parameter values** `func g(a = 7)`.
- **Error-propagation `?`** postfix operator.
- **`static` / `dynamic let`** top-level bindings and **`export`**.
- **`assert`** — it is lowered by codegen but the runtime symbol `mxs_panic` is not provided yet, so
  it fails at JIT link.
- **`defer`** — parses only.
- **`static` members** inside a class.

### Not built yet
- **Inheritance** — a class cannot extend another; classes are standalone.
- **`operator**` / `operator[]` / `operator[]=`** — the runtime reserves vtable slots, but the
  operator-symbol grammar can't express them yet.
- **`set` / `concat`** list methods (only `append` / `len` / `get` are wired).
- **f-strings, file I/O, console input, explicit type conversion.**

### Sharp edges (current build)
- A built-in method name on a wrong-typed receiver does not crash but is inconsistent: `42.append(1)`
  is a silent no-op, while `42.len()` returns a `TypeError` *value* (it is not raised). This is the
  documented v1 behavior pending static receiver typing.
- A type annotation on `let`/params is parsed but not enforced (codegen is dynamically typed).

---

*This tutorial reflects the build current as of the `progress14` review. When a feature graduates
from §14, move it into the body and add a runnable example. The grammar tags in `docs/syntax.md` are
the source of truth for what runs.*
