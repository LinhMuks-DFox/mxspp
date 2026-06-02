# Progress 15 — Strengthen the REPL: line editing (linenoise) + `./` meta-commands
id: 2026-06-02/progress15
date: 2026-06-02
author: human+ai
status: done
refs: [2026-06-01/progress13, 2026-06-01/progress14]
supersedes:
commits: [d8448d1]
files:
  - project_init.py                              # vendor linenoise (LINENOISE_CONFIG + setup_linenoise)
  - lib/linenoise/linenoise.{c,h}                # NEW vendored single-file BSD line editor
  - src/shell/CMakeLists.txt                     # linenoise static lib (C99) + link into shell
  - src/core/MXOps.cpp                           # mxs_population_dump / mxs_population_dump_all (C-ABI, in core.bc)
  - src/shell/shell.cpp                          # linenoise read loop + history; `./` dispatch; eval helper

## Goal
Make the `mxs` REPL feel like a real shell: (1) **arrow-key line editing + in-session history**
(←/→ move the cursor, ↑/↓ walk history, backspace/Ctrl-A/E/U/K etc.), and (2) **`./`-prefixed
meta-commands** replacing the old colon ones: `./quit`, `./reset`, `./status`,
`./objects_population [all]`.

## Context / Motivation
After progress14 the REPL ran real programs but read input with `std::getline` (`shell.cpp:105`) — no
cursor movement, no history; a typo meant retyping. Meta-commands were colon-prefixed
(`:q`/`:quit`/`:exit`/`:reset`). Mux asked for arrow keys + a `./` command surface.

## Decisions (Mux, 2026-06-02)
- **D1 — line editing via vendoring linenoise** (antirez/linenoise, single-file C, BSD), not a
  hand-rolled termios editor. Pinned to commit `a473823…` for reproducible vendoring.
- **D2 — `./` fully replaces `:`**. The colon commands (`:q`/`:quit`/`:exit`/`:reset`) are deleted
  (no aliases) — consistent with the "delete legacy, no redundancy" principle.
- **D3 — history is in-session only** (in-memory; no `~/.mxs_history` save/load).
- **D4 — `./objects_population`** prints the live MXObject count; `./objects_population all` also
  dumps each live object (address + `repr()`), via `MXPopulationManager`.

## Key technical constraint (the dual singleton)
`MXPopulationManager` is a function-local singleton (`MXPopulationManager.cpp:8`) compiled into BOTH
the statically-linked `core` lib AND `core.bc`. The JIT (`jit.cpp:77-86`) adds `core.bc` as a module
and only uses process symbols as a fallback, so JIT'd user objects register with **core.bc's**
singleton — a different instance than a direct C++ call from `shell.cpp` would reach (the process
copy, which never sees user objects → would always report ~0). Therefore `./objects_population` is
implemented by **routing a JIT'd expression through the existing eval pipeline** (the C-ABI wrappers
`mxs_population_dump[_all]` live in `core.bc`), so the count reflects the same singleton + the current
session's replayed `let`s. A code comment at the wrappers + the dispatch site records this invariant.

## Tasks
- [x] [task23 — vendor linenoise (project_init.py + CMake)](tasks/task23-vendor-linenoise.md)
- [x] [task24 — core population C-ABI (mxs_population_dump[_all])](tasks/task24-core-population-cabi.md)
- [x] [task25 — shell linenoise read loop + `./` dispatch](tasks/task25-shell-linenoise-loop.md)
- [x] [task26 — wire `./objects_population` + verification](tasks/task26-objects-population-and-verify.md)

## Build & verify (constraint)
Build ONLY with `ninja -C build` (auto-reruns cmake to pick up the new `linenoise` target + new
`core.bc` symbols; preserves the fragile host config). NEVER `rebuild.py --clean`. `project_init.py`
(no flags) is safe — it skips LLVM and only vendors pegtl/linenoise (idempotent, skips if present).

## Open / TODO (carry-over)
- The I7 REPL limitation persists (only `let` lines are replayed; assignments / in-place mutations
  don't persist across lines) — e.g. `let mut xs=[1,2]; xs.append(3); xs.len()` → 2, not 3. A REPL
  that persists a mutable environment is the proper fix (logged since progress13/14). Out of scope.
- Persistent history file, tab-completion, hints, and multi-line editing are possible linenoise
  follow-ups, deliberately not done here (in-session history only, per D3).

## Agent log
- 2026-06-02 [ai/opus] Implemented all four tasks. Vendored linenoise at the pinned commit
  (`lib/linenoise/`, + `setup_linenoise()` in `project_init.py` so a fresh checkout reproduces it).
  Added a `linenoise` C99 static lib in `src/shell/CMakeLists.txt` (with
  `-Wno-unused-command-line-argument` to silence the harmless libc++-on-C warning) linked into
  `shell`. Added `mxs_population_dump` / `mxs_population_dump_all` to `MXOps.cpp` (already in
  `CORE_BC_SOURCES`, so they flow into `core.bc`). Rewrote the `shell.cpp` read loop to use
  `linenoise("mxs> ")` (+ `linenoiseHistoryAdd` / `linenoiseFree`, `linenoiseHistorySetMaxLen(1000)`),
  factored the parse→resolve→codegen→jit::run eval block into `eval_thunk()`, replaced the colon
  commands with a `./` dispatch (`./quit`/`./reset`/`./status`/`./objects_population [all]` + an
  unknown-command hint), and extended the auto-prelude with the two population bindings.
  **Verified**: `ninja -C build` clean; `ctest` 3/3 (frontend/core/corpus) still green; `core.bc`
  carries the new symbols. Pipe-driven: `./status` reports lets/defs/history/searchdirs;
  `./objects_population` after `let s="hi"; let n=42` prints **`live MXObjects: 2`** (the dual-singleton
  correctness assertion — it sees the user objects, not 0); `./objects_population all` dumps both with
  addresses + repr; `./bogus` → hint; `./reset` clears lets/defs; `./quit` and EOF both exit cleanly;
  non-TTY fallback (linenoise `linenoiseNoTTY`) works and EOF on empty stdin exits rc 0. Interactive
  arrow-key editing / history is provided by linenoise (proven library; standard synchronous
  `linenoise(prompt)` integration) — needs a manual TTY session to exercise the keystrokes.
  **Not committed yet** (awaiting Mux review).
