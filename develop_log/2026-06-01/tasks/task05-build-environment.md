# Task 05 — Build environment & green baseline
id: 2026-06-01/task05
parent: 2026-06-01/progress11
status: done
owner: code_agent

## Objective
Get a working native build on this host (vendored LLVM 20.1.8 per D-BUILD) and a green baseline
(`ctest` + the existing `run-core` demos) **before** any OOP code is verified.

## Scope
In:
- Resolve the `project_init.py` libc++ check + LLVM-20 acquisition so `lib/llvm` + `lib/PEGTL` exist.
- Ensure the C++-stdlib ABI is consistent between the compiler (Homebrew clang) and the linked libLLVM.
- `python3 rebuild.py --clean`; run `ctest` and a few `run-core` demos; record the baseline.
Out:
- Any OOP / language feature work (task06+).
- Permanent system changes needing root we cannot make (document what Mux must run via `! sudo …`).

## Inputs (read first, priority order)
1. `develop_log/2026-06-01/progress11-…md` §Issues + D-BUILD — the exact blocker.
2. `project_init.py` — `check_system_dependencies()` (ldconfig libc++ grep), `setup_prebuilt_llvm()` /
   `build_llvm_from_source()`, `main()` (`-lvd`/`-b` flags).
3. `CMakeLists.txt` (LLVM discovery: `lib/llvm` first, else system), `toolchain.cmake` (forces
   `-stdlib=libc++`), `rebuild.py`, `src/core/CMakeLists.txt` (core.bc clang invocations).

## Deliverables
- Populated `lib/llvm` (LLVM 20.1.8) + `lib/PEGTL`, ABI-compatible with `-stdlib=libc++`.
- `build/bin/mxs` + `build/bin/core.bc` + `build/bin/runtime.bc` built clean.
- A short note in the progress Agent log: how the env was resolved + the baseline result.

## Steps
1. **Diagnose ABI**: determine whether the official LLVM 20.1.8 Linux prebuilt is libstdc++- or libc++-
   linked. If libstdc++: a libc++ build is required → prefer `project_init.py -lvd -b` (build LLVM 20
   from source with `-DLLVM_ENABLE_LIBCXX=ON`, or whatever the source path supports) OR fall back.
2. **Unblock the libc++ check**: the check is a false negative (Homebrew libc++ exists, just not in the
   ld cache). Preferred: make `ldconfig -p` see it without root if possible; otherwise minimally relax
   `check_system_dependencies()` (e.g. also accept libc++ found on the compiler's search path) — keep
   the change small and clearly scoped; do not weaken other checks.
3. **Acquire deps**: run `project_init.py` with the right flags so `lib/llvm` + `lib/PEGTL` populate.
4. **Build**: `python3 rebuild.py --clean`. Fix any LLVM-20 vs Homebrew-clang-22 compile errors if they
   appear (note them).
5. **Baseline**: `ctest` (expect 3/3) and `build/bin/mxs run-core example/examples/core_fib.mxs`
   (→ 55), `core_loops` (→ 10/120/true/true), `core_types`. Record results.

## Acceptance criteria
- [ ] `lib/llvm` + `lib/PEGTL` present and ABI-consistent with the build toolchain.
- [ ] `rebuild.py --clean` succeeds; `ctest` is green (≥ the current 3 suites).
- [ ] `run-core` of `core_fib`/`core_loops`/`core_types` reproduce their documented outputs.
- [ ] The env resolution is reproducible (documented in the Agent log; any required `! sudo` noted).

## Constraints
- Keep `-stdlib=libc++` (toolchain.cmake) — do not switch the project to libstdc++.
- Minimal, reviewable changes to `project_init.py` only; no unrelated tooling churn.

## Notes / Assumptions
- Assumption: Homebrew clang 22 + its libc++ is the available compiler; vendored LLVM 20 is the library.
- Question (FALLBACK): if a clean local LLVM-20 + libc++ build is infeasible on this WSL host, fall back
  to **system Homebrew LLVM 22** (ABI-consistent all-Homebrew; expect small codegen API edits such as
  `IRBuilder::CreateGlobalStringPtr`→`CreateGlobalString`) and flag the version drift, OR hand the build
  to Mux's Docker (LLVM 20). Surface the choice to Mux before spending long on a fight.
