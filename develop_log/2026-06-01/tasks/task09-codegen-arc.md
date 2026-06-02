# Task 09 — Codegen: ARC insertion (retain/release protocol)
id: 2026-06-01/task09
parent: 2026-06-01/progress11
status: done
owner: code_agent

## Objective
Insert retain/release in `compile_core` so the uniform "+1" ownership protocol holds end to end:
user `~Class()` runs deterministically, fields/elements are released, and no object leaks or
double-frees. Layered on top of task08's lowering.

## Scope
In:
- Treat every `expr()` result as **owned (+1)**; consume each exactly once (adopt / release / transfer).
- Release binding cells at scope exit (block end, function return, loop-iteration end); release operands
  after each `mxs_op_*` / method / call; release unused `ExprStatement` results and condition values.
- Handle **all** control-flow exits: `match` arms, `&&`/`||` short-circuit, `break`/`continue`, early
  `return`, `if/else` merge — release on every path.
Out:
- Runtime accessor retain + lvalue/instance/arraylist ownership (task06/07 provide these).

## Inputs (read first, priority order)
1. `develop_log/2026-06-01/progress11-…md` §"ARC protocol" + §Issues (double-free risk; gotchas list).
2. `src/backend/codegen.cpp` `CoreGen` (post-task08) — `expr`/`stmt`/`block`/`shortCircuit`/`emitFunction`
   and the `bind`/`locals` machinery + `breakT`/`continueT`.
3. `src/core/MXObject.cpp` `mxs_retain`/`mxs_release` (the ABI to emit).
4. `include/mxspp/core/MXPopulationManager.h` (count accessor for the leak test).

## Deliverables
- `CoreGen` emits `mxs_retain`/`mxs_release` per the protocol:
  - identifier reads / field reads / element reads are already +1 (task07 retains in the accessor) — the
    consumer releases them like any temporary.
  - operands to `mxs_op_*` / calls / method dispatch: release after the call returns.
  - `let`/bind, `mxs_set_attr`, `xs[i]=v`, `append`, `mxs_lvalue_update`: **adopt** (no release).
  - `ExprStatement` unused value, condition value (`if`/`until`/`do-until`/`for` test, `assert`,
    `&&`/`||`): release after use.
  - scope exit: release every binding cell created in the scope (track per-block; release on normal fall-
    through AND on `return`/`break`/`continue` that leave the scope).
  - `return e`: transfer (do not release `e`); still release the scope's bindings.
- A leak assertion: a `run-core` (or unit) path that checks `population_count()` returns to baseline.

## Steps
1. Define a small helper discipline in `CoreGen` (e.g. `consume()`/`adopt()`/`releaseScope()`), so the
   "exactly once" rule is mechanical rather than ad hoc.
2. Thread binding-creation tracking through `block`/`emitFunction`/loops/match so scope-exit release fires
   on every path (mind blocks that are `terminated()` by a return/break).
3. Insert operand/condition/unused releases; mark adopt sites as no-release.
4. Validate with the population-count leak test + re-run ALL existing demos and `ctest`.

## Acceptance criteria
- [ ] A program that binds + drops a user object runs its `~Class()` exactly once (observable, e.g. a
      destructor that appends to / prints a marker) and `population_count()` returns to baseline.
- [ ] No double-free / use-after-free across: aliasing (`let q = p;`), `return p;`, objects through
      `match` arms, short-circuited `&&`/`||`, `break`/`continue`.
- [ ] All existing demos (`core_fib`/`core_loops`/`core_types`/`core_list`/`core_iter`/`core_match`/…)
      reproduce their documented outputs; `ctest` green.

## Constraints
- If the full protocol proves too risky to land cleanly in one pass, fall back to the **D-LIFE v1 floor**
  (release binding cells + instance fields only; pure unbound temporaries may leak) — and LOG the gap
  explicitly (no silent partial coverage). Target stays the full protocol.
- Keep emission naive/explicit; LLVM inlines `mxs_retain`/`mxs_release` from core.bc (D6).

## Notes / Assumptions
- Assumption: single-threaded; rc is non-atomic (progress09 D8).
- Question: the returned value carries its own +1 (the accessor retain) so scope-exit release of its
  source binding cannot free it — verify with the `return p;` case.
