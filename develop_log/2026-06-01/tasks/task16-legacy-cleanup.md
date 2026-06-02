# Task 16 — Legacy cleanup
id: 2026-06-01/task16
parent: 2026-06-01/progress13
status: done
owner: code_agent

## Objective
Remove dead/superseded code so the tree reflects the live design only.

## Scope
In:
- Orphaned `raise_expr` rule + `K_RAISE` keyword in `grammar.hpp` (+ any parser action on them).
- Empty `src/core/builtin_func.cpp` (0 lines) + its stub header `include/mxspp/core/builtin_func.h`.
- Finalize the staged deletion of the old runtime module: `src/runtime/runtime.cpp`,
  `src/runtime/CMakeLists.txt`, `src/frontend/ast.cpp`, `test/runtime_test.cpp` — and any lingering
  CMake `add_subdirectory(runtime)` / include references.
Out:
- `std/io.mxs` reconciliation — handled by task18 (it becomes live, not deleted).

## Inputs (read first, priority order)
1. `include/mxspp/frontend/grammar.hpp` — `raise_expr` (~248), `K_RAISE` (~86); confirm neither is in
   `primary_expr` / `reserved_word`.
2. `src/frontend/parser.cpp` — confirm NO action is keyed on `raise_expr` before removing it.
3. `CMakeLists.txt`, `src/CMakeLists.txt`, `src/core/CMakeLists.txt`, `test/CMakeLists.txt` — references
   to runtime/, builtin_func.cpp, runtime_test.cpp.
4. `git status` — the runtime/ files already show as deleted (`D`); this finalizes + builds clean.

## Deliverables
- `grammar.hpp` — `raise_expr` + `K_RAISE` removed; `raise_expr` forward-decl gone.
- `builtin_func.cpp`/`.h` — removed (and dropped from `src/core/CMakeLists.txt`), OR documented if a
  symbol is actually referenced (verify `core::functions::is_instance_of` usage first).
- CMake — no dangling references to deleted files; configure + build clean.

## Steps
1. **Grep before delete** — for `raise_expr`, `K_RAISE`, `builtin_func`, `is_instance_of`, `runtime/`,
   `runtime_test` across `src include test *.txt`. Only remove what has zero live references.
2. **Remove** the confirmed-dead items; update CMake lists.
3. **Build + ctest** — must stay green; run every `example/examples/*.mxs` at its prior rc.

## Acceptance criteria
- [ ] `grep -rn 'raise_expr\|K_RAISE' include src` returns nothing.
- [ ] No empty translation units in `src/core`; CMake has no missing-file references.
- [ ] `python3 rebuild.py --clean` configures + builds clean; `ctest` green; demos unchanged.

## Constraints
- Verify-then-delete: never remove a symbol with a live reference. Keep builds green at each step.

## Notes / Assumptions
- Assumption: the runtime/ deletion is intentional (superseded by `core.bc`, per progress09/11).
