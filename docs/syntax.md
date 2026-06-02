# MXScript Syntax (authoritative)

**Status:** reflects the *implemented* grammar in `include/mxspp/frontend/grammar.hpp` as of
2026-06-01 (verified against the parser and `example/examples/syntax_reference.mxs`). This file is
the single source of truth for syntax. Two companion files exist and are deliberately *subordinate*
to this one:

- `syntax.ebnf` (repo root) — the formal grammar. It was the **original design**; it has since
  drifted from the implementation. The "Divergence" section below enumerates every difference, and
  `syntax.ebnf` has been refreshed to match (see its header).
- `docs/basic_syntax.md` — a prose *tutorial* (examples, intent). Where it disagrees with the
  grammar, this file and `grammar.hpp` win; `basic_syntax.md`'s known drift is noted under
  "Doc drift" below.

Legend used throughout: **[parses + runs]** = accepted by the parser and lowered by
`backend/codegen.cpp` (`mxs run-core`); **[parses only]** = accepted by the grammar but not yet
wired in codegen (a syntactically-valid construct that won't run end-to-end yet).

---

## 1. Lexical structure

### 1.1 Identifiers & reserved words
```
identifier      = (alpha | "_") , { alnum | "_" } ,  — but NOT a reserved word
```
An identifier may not be a reserved keyword (a `not_at<reserved_word>` guard precedes it; this
prevents `let`/`func`/… from being misparsed as identifiers).

**Reserved words** (cannot be used as identifiers):
```
as assert break case class continue defer do dynamic else enum export for func if
import in interface let loop match mut operator override private public return static
type until
```
**Notably NOT reserved:** `raise`, `exit`, `nil`, `true`, `false`, `public`/`private` are keywords
but `raise`/`exit` are *ordinary identifiers* — they are plain functions (see §7, §8), not syntax.
`nil`/`true`/`false` are matched as literals.

### 1.2 Literals
```
integer_literal = digit , { digit }
float_literal   = digit , { digit } , "." , digit , { digit }       (* both sides required *)
string_literal  = '"' , { '\' any | not('"') } , '"'                (* backslash escapes any char *)
bool_literal    = "true" | "false"
nil_literal     = "nil"
literal         = float_literal | integer_literal | string_literal | bool_literal | nil_literal
```
Order matters (PEG ordered choice): `float_literal` is tried **before** `integer_literal` so `3.5`
is one float, not `3` then `.5`. There is no negative literal — `-3` is unary minus applied to `3`.

### 1.3 Comments & whitespace
```
line_comment  = ("#" | "//") , … to end of line
block_comment = "/*" , … , "*/"                         (* not nestable *)
```
Comments and whitespace are subsumed into a single skip rule (`ignored`) consumed between tokens;
they are **not** AST nodes. `#` is the idiomatic line comment in every example; `//` and `/* */`
are also accepted.

---

## 2. Operators & precedence

From lowest to highest binding. Every binary level is **left-associative** (built with PEGTL
`list`, i.e. `lhs { op rhs }`); assignment is **right-associative**.

| Level | Operators | Assoc | Notes |
|------:|-----------|-------|-------|
| assignment | `=` `+=` `-=` `*=` `/=` | right | An *expression* (yields `nil`); see §2.1 |
| logical or | `\|\|` | left | |
| logical and | `&&` | left | |
| equality | `==` `!=` | left | |
| relational | `<` `<=` `>` `>=` | left | chains like `a < b < c` parse (left-assoc) |
| range | `..` | left | exclusive; only meaningful in `for … in lo..hi` |
| additive | `+` `-` | left | `+` also concatenates strings/lists |
| multiplicative | `*` `/` `%` | left | |
| power | `**` | left | NOTE: left-assoc here (math convention is right) |
| unary (prefix) | `!` `+` `-` | — | applies to a postfix expression |
| postfix | `.name`  `[i]`  `<T,…>`  `(args)`  `?` | left | member / index / generic / call / try |

### 2.1 Assignment is an expression
```
assign_expr = logic_or_expr , [ assign_op , expression ]
```
`a = b = c` is `a = (b = c)`. Assignment to an **immutable** binding (`let` without `mut`) is a
**compile-time error** (`backend/codegen.cpp`); `let mut` is required to reassign. Compound forms
(`+= -= *= /=`) desugar to `lhs = lhs <op> rhs`. Valid assignment targets: a variable, an index
`xs[i] = v`, or a member `obj.field = v` / `self.field = v`.

---

## 3. Expressions

```
primary_expr  = literal
              | list_literal
              | "(" , expression , ")"
              | block_expr
              | match_expr
              | lambda_expr
              | identifier                       (* tried LAST: keywords win first *)

postfix_op    = "." identifier                   (* member access            *) [parses+runs]
              | "[" expression "]"               (* subscript                *) [parses+runs]
              | generic_inst                     (* List<int> as a value     *) [parses only]
              | call_args                        (* f(a, b)                   *) [parses+runs]
              | "?"                              (* error propagation         *) [parses only]
postfix_expr  = primary_expr , { postfix_op }

unary_expr    = [ "!" | "+" | "-" ] , postfix_expr
…             (the precedence ladder of §2) …
expression    = assign_expr
```

### 3.1 Calls and method calls
```
call_args = "(" , [ arg_list ] , ")"
arg_list  = argument , { "," , argument }
argument  = identifier "=" expression            (* keyword arg — [parses only] *)
          | expression
```
A call whose base is a bare `identifier` → a **function call** (resolved against declared funcs /
`@@foreign` bindings). A call whose base is a `member` (`recv.m(args)`) → a **method call**. Method
dispatch has two paths: a *user-class* selector dispatches through the receiver's class vtable; a
*built-in* container/string method (`xs.append(v)`, `xs.len()`, `"hi".len()`, `xs.get(i)`) lowers to
its polymorphic runtime symbol with the receiver as arg0 (a static codegen table — built-ins carry no
vtable). Built-in container/string operations are **methods only**: there is no global `len`/`append`
free function (per `develop_log/2026-06-01/progress13` D4 — they would violate the OOP model).
Keyword arguments parse but are not yet lowered.

### 3.2 List literal  [parses + runs]
```
list_literal = "[" , [ expression , { "," , expression } ] , "]"
```
e.g. `[]`, `[1, 2, 3]`. Builds an `MXArrayList`. (Not present in the original `syntax.ebnf`.)

### 3.3 Lambda, block-expression, match  [match runs; lambda/block-expr parse]
```
lambda_expr = func_sig , "=>" , ( expression | block )
block_expr  = "{" , { statement } , [ expression ] , "}"    (* trailing expr is the value *)
match_expr  = "match" , "(" , expression , ")" , "{" , { case_clause } , "}"
case_clause = "case" , pattern , "=>" , ( expression | block ) , [ "," ]
pattern     = literal
            | bind_pattern                                  (* name ":" type  — e.g. e: Error *)
            | "_"                                           (* wildcard *)
            | identifier [ "(" pattern_list ")" ]           (* ctor / enum-variant destructure *)
            | "(" pattern_list ")"                          (* tuple *)
bind_pattern = identifier ":" type_spec
```
`match` runs end-to-end (literal, type-binding `case e: Error =>`, and wildcard arms are the wired
subset). Lambdas and standalone block-expressions parse but are not fully lowered.

> There is **no** `raise` expression. The original design had `raise_expr = "raise" expression` as a
> primary expression; the language dropped it (error model moved to a value-returning `raise(…)`
> function, see `docs/basic_syntax.md` §6 and `develop_log/2026-05-31/progress06`).

---

## 4. Statements

```
statement       = let_stmt | control_stmt | expression_stmt | assert_stmt | defer_stmt
let_stmt        = "let" , [ "mut" ] , identifier_list , [ ":" type_spec ] , [ "=" expression ] , ";"
expression_stmt = expression , ";"
assert_stmt     = "assert" , expression , ";"                          [codegen'd; needs runtime mxs_panic]
defer_stmt      = "defer" , block                                      [parses only]

control_stmt    = if_stmt | for_in_stmt | loop_stmt | do_until_stmt
                | until_stmt | break_stmt | continue_stmt | return_stmt
if_stmt         = "if" , expression , block , [ "else" , ( if_stmt | block ) ]
for_in_stmt     = "for" , [ "mut" ] , identifier , "in" , expression , block
loop_stmt       = "loop" , block
do_until_stmt   = "do" , block , "until" , "(" , expression , ")" , ";"
until_stmt      = "until" , "(" , expression , ")" , block
break_stmt      = "break" , ";"
continue_stmt   = "continue" , ";"
return_stmt     = "return" , [ expression ] , ";"
```
Notes:
- `if` takes a **bare** expression (no parentheses); `until` / `do-until` take a
  **parenthesized** condition. `until (c)` runs the body *while c is false* (pre-test);
  `do … until (c)` is the post-test form.
- `for v in lo..hi` iterates an integer range (exclusive); `for v in xs` iterates a list or string
  by index. Both run today.
- `let` with two-plus names (`let a, b = …`) parses via `identifier_list`; the wired path is a
  single name. `let` defaults to **immutable**; `let mut` is reassignable.

---

## 5. Type expressions
```
type_spec    = single_type , { "|" , single_type }          (* union: int | Error *)
single_type  = ( fqdn , [ generic_inst ] ) | func_type      (* List<int>, func(int)->int *)
func_type    = "func" , "(" , [ type_spec , { "," type_spec } ] , ")" , [ "->" , type_spec ]
generic_inst = "<" , type_spec , { "," , type_spec } , ">"
fqdn         = identifier , { "." , identifier }
```
Types are parsed everywhere a `: Type` annotation or `-> Type` appears. The codegen is dynamically
typed (values are `MXObject*`); annotations are currently **not** statically checked — the
`type_spec` after `case x: T` *is* used (runtime type test in `match`).

---

## 6. Definitions (class / interface / type / enum / func)

### 6.1 Functions  [parses + runs]
```
func_def  = "func" , identifier , [ generic_param ] , func_sig , ( block | ";" )
func_sig  = "(" , [ param_list ] , ")" , [ "->" , type_spec ]
param_list = param , { "," , param } , [ "," , rest_param ]   |   rest_param
param     = identifier_list , ":" , type_spec , [ "=" , expression ]
rest_param = "..." , identifier , ":" , type_spec             (* variadic; last only, no default *)
generic_param = "<" , identifier_list , ">"                   [parses only]
```
- A function body is a **block** `{ … }` **or** a lone `;`. The `;` form is a *bodyless declaration*
  — the mechanism behind `@@foreign` FFI bindings (§8). (The original `syntax.ebnf` required a
  block.)
- **Variadics** (`...name: type`, last parameter only): surplus call arguments are packed into a
  fresh `MXArrayList` bound to `name`. Default values on a parameter parse but are **not** applied
  at call sites yet.

### 6.2 Classes  [parses + runs: fields, ctor, methods, operators, dtor; single class, no inheritance]
```
class_def     = "class" , identifier , [ generic_param ] , [ ":" , type_spec ] ,
                "{" , { class_member } , "}"
class_member  = access_spec | constructor_def | destructor_def | static_member
              | method_def | operator_def | field_def_class
access_spec      = ( "public" | "private" ) , ":"             (* parsed, NOT enforced *)
field_def_class  = let_stmt                                   (* let x: int; *)
constructor_def  = identifier , func_sig , [ ":" identifier call_args ] , block
destructor_def   = "~" , identifier , "(" , ")" , [ ":" "~" identifier ] , block
method_def       = [ "override" ] , "func" , identifier , [ generic_param ] , func_sig , block
operator_def     = [ "override" ] , "operator" , op_symbol , func_sig , block
static_member    = "static" , ( method_def | field_def_class )         [parses only]
op_symbol        = "+=" | "-=" | "*=" | "/=" | "==" | "!=" | "<=" | ">="
                 | "+" | "-" | "!" | "*" | "/" | "%" | "<" | ">"
```
Wired today: a **single** class with fields, a constructor (`self.f = v`), methods (`obj.m(args)`
via vtable), operator overloading (routed through reserved vtable slots), `match` on the class type,
and a destructor (deterministic, ARC). Inheritance (`: Base`), `override`, base-ctor chaining
(`: Base(args)`), `static` members, and generics parse but are not lowered.

> **op_symbol gap:** the overloadable-operator set in the grammar does **not** include `**` (power),
> `[]`/`[]=` (index), or the unary/relational completeness the runtime reserves slots for
> (`MXClassInfo` has `OP_POW`, `OP_INDEX_GET`, `OP_INDEX_SET`). So `operator**` / `operator[]`
> cannot be *written* yet even though the dispatch machinery has slots for them. Tracked.

### 6.3 Interface / type / enum  [parses only]
```
interface_def    = "interface" , identifier , [ generic_param ] , [ ":" , type_spec ] ,
                   "{" , { interface_member } , "}"
interface_member = "func" , identifier , [ generic_param ] , func_sig , [ block ] , ";"
type_def         = "type" , identifier , "{" , { field_decl } , "}"
field_decl       = identifier_list , ":" , type_spec , ";"
enum_def         = "enum" , identifier , [ generic_param ] , "{" , enum_variant , { "," , enum_variant } , "}"
enum_variant     = identifier , [ "(" , param_list , ")" ]
```
All three parse into AST but have no codegen yet.

---

## 7. Top level
```
mxscript         = { top_level_decl }
top_level_decl   = [ "export" ] , ( import_stmt | binding_stmt
                                  | annotation , annotatable_decl | annotatable_decl )
annotatable_decl = func_def | class_def | interface_def | type_def | enum_def
import_stmt      = "import" , fqdn , [ import_tail ] , ";"                  [parses + runs]
import_tail      = ( "." , "{" , identifier_list , "}" ) | ( "as" , identifier )
binding_stmt     = ( "static" | "dynamic" ) , "let" , identifier , "=" , expression , ";"   [parses only]
```
A file is a sequence of top-level **declarations** — *not* bare statements. (You cannot write
`let x = 1;` at file scope; put it inside a function.) `export` and `static/dynamic let` parse but are
not lowered. The program entry point is `func main() -> int`.

**Import design (progress13 D2).** The stdlib is *import-gated* — nothing is in scope
without an `import` (even `print`/`println`). Three forms:
- `import std.io;` — qualified: the module's last segment becomes a namespace → `io.println(...)`.
- `import std.io as o;` — renames that namespace → `o.println(...)`.
- `import std.io.{println, format};` — selective: the listed names enter scope **unqualified**.

The module resolver (`src/frontend/imports.cpp`) locates `std/<path>.mxs` on a filesystem search path —
CWD/`std` first, then the executable's directory — and all three forms run end-to-end. A namespace name
must be bound by exactly one import (importing the same module twice, or two modules under one alias, is
an error — use `as`); a local variable shadows a same-named imported namespace; transitive imports (a
module that itself `import`s) are not supported and are rejected with a diagnostic.

## 8. Annotations & `@@foreign`
```
annotation     = "@@" , identifier , [ "(" , [ annotation_arg , { "," , annotation_arg } ] , ")" ]
annotation_arg = identifier , "=" , expression
```
An annotation precedes an `annotatable_decl`. The wired one is **`@@foreign`** on a *bodyless*
function declaration, which binds an mxs name to a C-ABI runtime symbol:
```mxs
@@foreign(symbol_name="mxs_arraylist_append") func append(xs: any, v: any) -> nil;
```
See `docs/ffi.md` for the calling convention. `@@template(T)` (generics) is designed but not wired.

---

## 9. Divergence from the original `syntax.ebnf`

`syntax.ebnf` was the **original design** and is the file to answer the question "is this still my
original grammar?" — the answer is **no, it had drifted**. Every structural difference between that
original and the implemented grammar:

**Added in the implementation (not in the original EBNF):**
1. **List literals** `[a, b, c]` — a new `primary_expr` (container support,
   `develop_log/2026-05-31/progress07`).
2. **Power operator `**`** — a whole new precedence level (`power_expr`) between unary and
   multiplicative. The original had no power operator at all.
3. **Variadic rest parameter** `...name: type` in `param_list` (`progress12`).
4. **Bodyless function declarations** (`func f(...) -> T ;`) — the original `func_def` always
   required a block. The `;` form enables `@@foreign` FFI bindings (§8).
5. **`reserved_word` guard** on `identifier` — a PEG necessity (keywords were silently misparsing as
   identifiers before this guard).

**Removed / changed vs. the original EBNF:**
6. **`raise` expression dropped.** The original had `raise_expr = "raise" expression` as a
   `primary_expr`. The error model moved to a value-returning `raise(…)` *function* (`progress06`);
   `raise` is no longer reserved/syntax (the legacy `raise_expr` rule + `K_RAISE` keyword were
   removed from `grammar.hpp`/`tokenizer.h` in `progress13` task16).
7. **Comments are not grammar nodes.** The original listed `comment` as a `statement`/`top_level`
   alternative; the implementation folds comments into whitespace skipping (`ignored`).

**PEG ordered-choice semantics (the implementation is an ordered PEG; the EBNF was unordered):**
8. `primary_expr` tries `identifier` **last** (so keywords match first); `literal` tries
   `float_literal` **before** `integer_literal`; `**` is matched before `*`. These orderings are
   load-bearing in a PEG and have no analogue in the order-free EBNF.

**Operator-set mismatches:**
9. `op_symbol` (operator overloading) cannot express `operator**`, `operator[]`, or `operator[]=`
   even though the runtime reserves vtable slots for them (see §6.2 note).

## 10. Doc drift (basic_syntax.md)
`docs/basic_syntax.md` is a tutorial and predates several decisions. Known inaccuracy there
(this file is correct):
- Its §6.2 still documents `raise` as a keyword/statement; `raise` is a plain function now (§1.1, §6
  above). (A corrective note at the top of that section flags it.)

(The earlier comment-syntax and `List<string>` typing drifts were fixed in `progress13` task14.)
