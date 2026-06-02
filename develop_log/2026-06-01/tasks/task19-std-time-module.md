# Task 19 — std.time module
id: 2026-06-01/task19
parent: 2026-06-01/progress13
status: blocked        # depends on task17 (import system)
owner: code_agent

## Objective
Expose the existing C++ time primitives as an importable `std.time` module.

## Scope
In:
- `std/time.mxs` — `@@foreign` bindings to `mxs_time_now` / `mxs_time_ms` / `mxs_time_ns`, plus the
  mxs-level surface names.
- A demo + a test.
Out:
- New C++ (the `MXTime` leaves already exist); date/calendar/formatting (future).

## Inputs (read first, priority order)
1. `include/mxspp/core/MXTime.h`, `src/core/MXTime.cpp` — the implemented primitives:
   `mxs_time_now()` (epoch seconds), `mxs_time_ms()` (epoch millis), `mxs_time_ns()` (monotonic nanos).
2. `develop_log/2026-06-01/progress13-...md` — D3.
3. task17 deliverable — the import mechanism this module loads through.

## Deliverables
- `std/time.mxs` — e.g.
  `@@foreign(symbol_name="mxs_time_now") func now() -> int;` (epoch seconds),
  `@@foreign(symbol_name="mxs_time_ms") func now_ms() -> int;`,
  `@@foreign(symbol_name="mxs_time_ns") func monotonic_ns() -> int;` (final names = task design).
- `example/examples/time_basics.mxs` — `import std.time;` + a tiny elapsed-time demo.
- A unit/integration check.

## Steps
1. **Name the surface** — pick the mxs-level names (`now`/`now_ms`/`monotonic_ns` proposed).
2. **Write std/time.mxs** with the @@foreign bindings.
3. **Demo + test** — measure a small elapsed interval via `monotonic_ns`.
4. **Verify** through `import std.time;` end-to-end.

## Acceptance criteria
- [ ] `import std.time;` then `now()` returns a plausible epoch-seconds int.
- [ ] Without the import, `now()` is out of scope (D2).
- [ ] Demo runs at rc 0; test green.

## Constraints
- No new C++ unless a name/return-shape gap is found in MXTime.

## Notes / Assumptions
- Assumption: `MXTime` returns `MXInteger`; the mxs return type is `int`.
