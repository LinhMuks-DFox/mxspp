# MXScript red/green corpus

A durable, CI-able body of small `.mxs` programs that pin language behavior, built in
`develop_log/2026-06-01/progress14` (task22) on top of the unit tests (`test/core_test`,
`test/frontend_test`) and the `example/examples/*.mxs` smoke tests.

- **green/** — programs that MUST run to a known output. Each `name.mxs` has a companion
  `name.out` holding the exact expected stdout; the program must exit `0`.
- **red/** — programs that MUST be rejected (a compile/parse error or a runtime panic). Each
  `name.mxs` has a companion `name.err` whose every non-empty line must appear as a substring of
  the program's combined std{out,err}; the program must exit non-zero. A red case must fail for the
  *right* reason — the diagnostic text is checked, not just the exit code.

## Running

```
test/corpus/run_corpus.sh [path-to-mxs]      # default: build/bin/mxs
```

It is also wired into ctest (`add_test(NAME corpus ...)` in `test/CMakeLists.txt`), so
`ctest --test-dir build` runs it against the freshly built `mxs`.

## Adding a case

- Green: write `green/foo.mxs`, then `build/bin/mxs run-core green/foo.mxs > green/foo.out`.
  Read the `.out` and confirm it is the *intended* result, not whatever happened to print.
- Red: write `red/foo.mxs` and `red/foo.err` with the diagnostic substring you expect. Run the
  runner and confirm it passes (rejected with that diagnostic).

## What this corpus pins (highlights)

Bindings (`let mut`, nested shadowing, immutable/redeclaration/loop-var errors), container & string
methods (`append`/`len`/`get`, subscript, `for-in`), `match` (literal / type-binding / wildcard),
operators (incl. `**`), `str`/`repr`/`format`, OOP (fields/ctor/method/`operator+`/dtor + ARC), and
the import system (qualified / `as` / selective; import-gating; duplicate-import and unresolved
errors). It also locks in the `progress14` regression fixes: built-in/user method-name collision no
longer crashes (`method_name_collision`), a local variable shadows an imported namespace
(`ns_local_shadow`), `return` inside a match arm compiles (`match_return`), the same module under two
aliases works (`import_two_aliases`), and the documented v1 wrong-receiver behavior
(`probe_wrong_receiver`, progress14 §6).
