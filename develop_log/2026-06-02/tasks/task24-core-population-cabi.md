# Task 24 — Core population C-ABI (mxs_population_dump[_all])
id: 2026-06-02/task24
parent: 2026-06-02/progress15
status: done
owner: code_agent (Opus)

## Objective
Expose the live-object population through a C-ABI compiled into `core.bc`, so the REPL can query it
via the JIT path (the only path that reaches the singleton JIT'd user objects register with).

## Steps
1. `src/core/MXOps.cpp`: add `#include "mxspp/core/MXPopulationManager.h"`.
2. Inside the existing `extern "C" {}` block add:
   - `void mxs_population_dump(void)` → `printf("live MXObjects: %zu\n", population_count())`.
   - `void mxs_population_dump_all(void)` → print the count + `get_manager().repr()` (snapshot taken
     before printing). Both return void (mxs `nil`) and allocate NO MXObject (no perturbation).
3. Add a comment recording the dual-singleton invariant (must be reached via core.bc, not a direct
   C++ call from the shell).

## Acceptance
- [x] `MXOps.cpp` is in `CORE_BC_SOURCES` → the symbols land in `core.bc` automatically (verified via
      the new `#include` triggering a `core.bc` relink).
- [x] `core`/`core_test` still build; `ctest` green.
- [x] At runtime (through the JIT), `./objects_population` reports a non-zero count when user objects
      are live (the correctness assertion in task26).

## Notes
- Why not a direct C++ call from `shell.cpp`? `MXPopulationManager` is a function-local singleton
  duplicated in the static `core` lib and in `core.bc`; JIT'd objects register with core.bc's copy
  (jit.cpp uses process symbols only as a fallback). A C++ call would read the wrong (process) copy.
