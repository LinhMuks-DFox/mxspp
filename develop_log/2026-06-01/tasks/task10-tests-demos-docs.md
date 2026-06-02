# Task 10 — Tests, demos, docs
id: 2026-06-01/task10
parent: 2026-06-01/progress11
status: done
owner: code_agent

## Objective
Lock the OOP v1 + ARC work behind unit tests (incl. leak/double-free assertions), runnable `.mxs`
integration demos, and an authoritative `docs/object_model.md`; update the progress Agent log.

## Scope
In:
- `core_test` cases for `MXClassInfo`/`MXInstance`/ABI/dtor/field-ARC and the population-count leak check.
- `.mxs` demos run via `mxs run-core`: data class + methods + `operator+` + a destructor demo.
- `docs/object_model.md`: the authoritative OOP + ARC design (the progress contract, promoted to docs).
- Progress11 Agent log + task status updates; mark superseded progress09 TODO.
Out:
- New language features beyond v1 scope.

## Inputs (read first, priority order)
1. `develop_log/2026-06-01/progress11-…md` (everything — promote the design contract into docs).
2. `test/core_test.cpp` + `test/test_framework.h` (`MX_TEST`/`CHECK`; `main` runs `mxtest::run_all`).
3. `example/examples/core_*.mxs` (style of the existing run-core demos) + `example/examples/README.md`.
4. `src/driver/main.cpp` `kCorePrelude` (the `@@foreign` prelude available to run-core programs).
5. `docs/type_system.md` §4 (the spec the docs must stay consistent with), `CLAUDE.md` testing rule.

## Deliverables
- `test/core_test.cpp` additions:
  - build an `MXClassInfo` (+ a destructor fn that bumps a counter), `mxs_instance_new`, set/get fields,
    `repr()`, `mxs_is_type` true/false, classinfo round-trip via `mxs_object_classinfo`.
  - field ARC: setting/overwriting/destroying an instance releases field objects (population count).
  - dtor fires exactly once at rc 0; leak/double-free: a build→bind→drop sequence returns the count to
    baseline.
- `example/examples/` demos (run-core), e.g.:
  - `oop_point.mxs` — `class Point { Point(x,y){…} let x; let y; func dist2()->int{…} }`, construct,
    read fields, call a method.
  - `oop_vector.mxs` — `operator+` returning a new instance; `match` on the result type.
  - `oop_dtor.mxs` — a `~Class()` that prints a marker, proving deterministic destruction at scope exit.
- `docs/object_model.md` — the OOP object model + ARC protocol (sections: MXClassInfo/vtable, MXInstance,
  lowering, ARC ownership), cross-linking type_system.md and progress11.
- Progress11: Agent log entries; flip task05–task10 checkboxes as they complete; note any v1-floor gap.

## Steps
1. Add `core_test` cases (instance/classinfo/dtor/leak). Keep/upgrade existing ownership-sensitive cases.
2. Write the `.mxs` demos; verify each via `build/bin/mxs run-core …` reproduces expected stdout.
3. Write `docs/object_model.md`.
4. Update progress + example/README; run `before_commit.py --staged` (format/lint) before review.

## Acceptance criteria
- [ ] `ctest` green incl. the new instance/dtor/leak cases (CLAUDE.md: unit tests required, aim 100% of
      new/changed code).
- [ ] Each demo runs through `run-core` with the documented output; `oop_dtor` shows the destructor firing.
- [ ] `docs/object_model.md` exists and matches the implemented behavior (no doc drift).
- [ ] Progress11 reflects final state; superseded progress09 TODO marked.

## Constraints
- Demos must be in the v1 subset (single class, no inheritance/interfaces/statics) so they actually run.
- English for all docs/tests/comments (CLAUDE.md).

## Notes / Assumptions
- Assumption: `run-core` is the verification path (the flat run-obj/native paths are legacy).
- Question: if matrix_class.mxs can't run yet (needs `[float]` array + `make_array`), keep it as a known-
  pending demo and add a smaller `oop_vector.mxs` that exercises `operator+` within v1.
