# Task 07 — Object lifetime / ARC runtime substrate
id: 2026-06-01/task07
parent: 2026-06-01/progress11
status: done
owner: code_agent

## Objective
Make the runtime side of the ARC ownership protocol real: binding cells own with release-semantics,
`release()` at zero drives destruction (incl. `MXInstance` user dtor + field release), and every
accessor that returns a borrow retains first — so codegen (task09) can rely on a uniform "+1" world.

## Scope
In:
- `MXLeftValue`: hold its r-value with **release-semantics** (adopt the +1 on construct/update; release
  the old on update; release on cell destruction) instead of `unique_ptr`-delete.
- Accessor **retain-on-return**: `mxs_lvalue_rvalue`, `mxs_get_attr`, `mxs_arraylist_get` /
  `mxs_index_get` return a **retained (+1)** object.
- `MXArrayList`: elements are **owned** (retain on append/set, release old on overwrite, release all on
  destroy) — align with the progress §"ARC protocol" owner-release rule.
- Confirm `MXObject::release()`→`delete this`→subclass dtor chain is the single destruction path; expose
  a live-object **count** on `MXPopulationManager` for leak/double-free assertions.
Out:
- Codegen retain/release insertion (task09).
- Instance field ownership mechanics already implemented in task06 (this task only adds the accessor
  retain + arraylist element ARC + lvalue release-semantics + the count accessor).

## Inputs (read first, priority order)
1. `develop_log/2026-06-01/progress11-…md` §"ARC protocol" (the authoritative contract).
2. `develop_log/2026-05-31/progress09-…md` D8 (retain/release, fresh = rc 1, unique_ptr-deleter note).
3. `src/core/MXLeftValue.cpp` + `.h`; `src/core/MXObject.cpp` (`retain`/`release`/`use_count`);
   `src/core/MXOps.cpp` (`mxs_get_attr`, `mxs_index_get`); `src/core/MXArrayList.cpp` + `.h`;
   `src/core/MXPopulationManager.cpp` + `.h`.
4. `test/core_test.cpp` `refcounting` + `left_value_*` + `arraylist_basics` cases (must stay green or be
   updated to the new ownership semantics).

## Deliverables
- `MXLeftValue` storing `MXObject* value_` (or `unique_ptr` with a release-deleter): construct adopts the
  +1, `rvalue_update` releases old + adopts new, destructor releases. `mxs_lvalue_new` adopts; existing
  `mxs_lvalue_rvalue` now **retains** before returning the borrow; `mxs_lvalue_delete` releases.
- `mxs_get_attr` / `mxs_index_get` (+ `mxs_arraylist_get`) retain the returned object.
- `MXArrayList`: owned elements (retain/release) + `~MXArrayList` releases all; `concat` retains shared
  elements; `set` releases the replaced element.
- `MXPopulationManager::population_count()` (or similar) — live `MXObject` count for tests.
- Updated `core_test` ownership-sensitive cases (coordinate with task10) so they reflect +1 returns.

## Steps
1. Switch `MXLeftValue` to release-semantics; update its ABI (`new`/`rvalue`/`update`/`delete`).
2. Add retain-on-return to the named accessors.
3. Give `MXArrayList` owned-element semantics; update `~MXArrayList`/`set`/`concat`.
4. Add the population count accessor.
5. Re-run / update `core_test` ownership cases; assert no leak via the count.

## Acceptance criteria
- [ ] A construct→bind→scope-end sequence leaves `population_count()` at its pre-sequence baseline
      (no leak, no double-free) — provable in a unit test.
- [ ] `mxs_lvalue_rvalue`/`mxs_get_attr`/`mxs_index_get`/`mxs_arraylist_get` each return rc≥+1 caller-owned.
- [ ] `core_test` builds and passes with the new semantics (cases updated where the +1 changes accounting).
- [ ] Releasing the last reference to an `MXArrayList` releases all its elements (count returns to baseline).

## Constraints
- Single-threaded rc is fine (progress09 D8); keep the existing `mxs_retain`/`mxs_release` ABI.
- Do not change the *meaning* of existing demos' output — only memory accounting.

## Notes / Assumptions
- Assumption: the producer rule (fresh objects already +1) holds for all current `mxs_*` constructors.
- Question: confirm `MXLeftValue` is NOT an `MXObject` (it isn't) so it doesn't perturb the population
  count — only the r-values it owns are counted.
