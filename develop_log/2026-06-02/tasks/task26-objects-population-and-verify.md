# Task 26 — Wire `./objects_population` + verification
id: 2026-06-02/task26
parent: 2026-06-02/progress15
status: done
owner: code_agent (Opus)

## Objective
Implement `./objects_population [all]` on top of the core C-ABI (task24) + the eval helper (task25),
and run the full verification matrix.

## Steps
1. Extend the auto-prelude (`fullPrelude`) with `@@foreign` bindings (symbol_name form, since the mxs
   name differs from the C symbol):
   `@@foreign(symbol_name="mxs_population_dump") func __objects_population() -> nil;` and the
   `__objects_population_all()` / `mxs_population_dump_all` variant.
2. `./objects_population` → `eval_thunk(fullPrelude, defs, lets + "__objects_population();", …)`;
   `./objects_population all` → the `_all` variant. Routing through the eval pipeline (replaying the
   accumulated lets) makes the count reflect core.bc's singleton + the session's live objects.

## Acceptance (all verified via `printf … | mxs shell`)
- [x] `ninja -C build` clean; `ctest` 3/3 (frontend_test, core_test, corpus).
- [x] **Dual-singleton correctness (key assertion):** after `let s = "hi"` + `let n = 42`,
      `./objects_population` prints `live MXObjects: 2` (NON-zero — it sees the user objects; a 0
      here would mean the wrong singleton was queried).
- [x] `./objects_population all` prints the count + a dump of each live object (address + repr) —
      shows `42` and `"hi"`.
- [x] `./status` / `./reset` / `./quit` / unknown-command hint all behave; EOF exits rc 0; non-TTY
      pipe input works.

## Notes
- The count includes only objects live at the call point (the replayed lets + any live temporaries);
  the C wrappers print before allocating, so they don't inflate their own snapshot. Between evals the
  population returns to baseline (the JIT main() returns and destructs the replayed objects).
