# Task 31 — Gate the population manager off the hot path (D1) + re-benchmark
id: 2026-06-02/task31
parent: 2026-06-02/progress19
status: pending
owner: code_agent
blocked-on: nothing (independent of the std batch; can run any time)

## Objective
Remove the always-on per-object `MXPopulationManager` mutex-lock + unordered_set insert/erase from the
hot path so compute-heavy scripts are usable. Measured cost: ~5.3 µs/iteration on a 1e6 integer loop;
the population tracker fires on EVERY MXObject construct/destruct.

## Steps (progress19 D1 — biggest cheap win first)
1. Make population tracking a **compile-time / build-flag-gated** facility: a no-op in normal/release
   runs, active only under a flag (e.g. `MXS_POPULATION_TRACKING`) used by the ARC tests.
   - The register/unregister calls live in MXObject construct/destruct paths
     (src/core/MXObject.cpp + MXPopulationManager.cpp); guard them.
   - Keep `mxs_population_dump[_all]` (the REPL `./objects_population` C-ABI) working: when tracking is
     off they report tracking-disabled rather than a wrong 0. (These move to src/std/system.cpp per
     progress17 — coordinate.)
2. Re-benchmark the 1e6 integer loop via `std.time.monotonic_ns` before/after; record the delta.
3. Keep `core_test`'s ARC-baseline assertions green by building the test target with tracking ON.

## Acceptance
- [ ] `ninja -C build` clean; ctest 3/3 (core_test ARC baseline still valid with tracking ON for tests).
- [ ] The 1e6-iter loop is materially faster (target: well under the ~5.3 µs/iter baseline; report the
      measured number).
- [ ] REPL `./objects_population` still works (or clearly reports tracking-disabled in release).

## Notes
- D2 (small-int cache/unbox) and D3 (type-specialized arithmetic) are larger, separate follow-ups — not
  in this task. This task is just D1, the cheap win.
- Independent of the std reorg, but the population-dump symbols overlap with progress17's src/std/system
  move — do this AFTER or in coordination with progress17 to avoid touching the same functions twice.
