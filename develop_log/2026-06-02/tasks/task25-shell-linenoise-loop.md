# Task 25 — Shell linenoise read loop + `./` dispatch
id: 2026-06-02/task25
parent: 2026-06-02/progress15
status: done
owner: code_agent (Opus)

## Objective
Replace the `std::getline` read loop with linenoise (arrow keys + in-session history) and replace the
colon meta-commands with `./`-prefixed ones.

## Steps
1. `src/shell/shell.cpp`: `extern "C" { #include "linenoise.h" }`; `#include <algorithm>`.
2. Read loop: `while ((raw = linenoise("mxs> ")) != nullptr)` — `nullptr` (EOF/Ctrl-D/Ctrl-C) ends the
   session. For each non-empty trimmed line: `linenoiseHistoryAdd(raw)` + bump a history counter;
   `linenoiseFree(raw)`. Delete all manual `std::cout << "mxs> "` prompts (linenoise owns the prompt);
   call `linenoiseHistorySetMaxLen(1000)` once at start. NEVER save/load history (in-session only).
3. Factor the parse→resolve_imports→compile_core→jit::run block into `eval_thunk(fullPrelude, defs,
   body, searchDirs, coreBcPath) -> bool` (true iff it ran). Used by the normal path and task26.
4. Replace the colon block with a `./` dispatch (a line starting with `./`):
   `./quit` → break; `./reset` → clear lets/defs + print `(reset)`; `./status` → print accumulated
   lets/defs counts, history count, core.bc path, module search dirs; `./objects_population[ all]` →
   task26; otherwise → unknown-command hint. Delete `:q`/`:quit`/`:exit`/`:reset`.
5. Update the banner to advertise the `./` commands.

## Acceptance
- [x] Interactive (manual): ↑/↓ history, ←/→ cursor, line editing work (linenoise).
- [x] `./quit` exits; EOF (Ctrl-D / empty stdin) exits rc 0; non-TTY pipe input works (linenoise
      `linenoiseNoTTY` fallback).
- [x] `./reset` clears state; `./status` prints the expected fields; `./bogus` → hint.
- [x] Normal eval unchanged: expressions echo (`x*2`→42), `let` persists across lines, definitions
      accumulate. `ctest` 3/3 green.

## Notes
- `linenoiseHistoryLen` is not public API — `./status`'s history count is tracked in-shell.
- The I7 limitation is unchanged (only `let` lines persist; mutations don't).
