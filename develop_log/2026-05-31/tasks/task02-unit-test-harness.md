# Task 02 — Establish a unit-test harness + frontend test suite (coverage)
id: 2026-05-31/task02
parent: 2026-05-31/progress03
status: done
owner: code_agent

## Objective
Give the project its first unit-test harness (it had none) and a frontend test suite, toward
the 100%-coverage rule (D3).

## Outcome (2026-05-31)
- `test/test_framework.h` — minimal dependency-free harness (`MX_TEST` auto-registration,
  `CHECK`/`CHECK_MSG`, a runner returning non-zero on failure).
- `test/frontend_test.cpp` — 13 cases / 78 checks: a parse-acceptance matrix (incl. the
  designed-but-unparseable gaps G1–G7 asserted as failing), B1–B4 + progress01 bug
  regressions, and AST-shape assertions (functions/params/binop, the `let z = y` fix,
  multi-name/typed let, precedence + right-assoc assignment, literals, calls, unary,
  translation unit, out-of-scope skipping, parse-failure path, lambda-parses).
- CMake wiring: `test/CMakeLists.txt` (`frontend_test` linking `frontend` + `core`,
  registered with `add_test`) + `enable_testing()` / `add_subdirectory(test)` in root.

Verification: built and RAN natively (clang + `-undefined dynamic_lookup`, LLVM codegen
symbols unused by `--dump-ast`/tests) — **13/13 cases, 78/78 checks pass**. Coverage measured
with `llvm-cov`: `parser.cpp` = 96.97% region / 95.67% line / 100% function.

## Open / TODO (remaining for the 100% rule)
- Push `parser.cpp` to 100% (≈7 regions left: a defensive `<unhandled>` fallback + a couple branches).
- `ast.cpp` coverage is low because it's mostly not-yet-implemented `codegen()` stubs (uncallable
  natively without real LLVM); they get covered when codegen lands (a later progress). Decide a
  policy: exclude stubs from the gate, or cover-on-implementation.
- Wire the coverage gate into `before_commit.py` / CI (currently coverage is measured ad-hoc).
- The in-container CMake/ctest build of `frontend_test` is unverified here (macOS host); verify in Docker.
