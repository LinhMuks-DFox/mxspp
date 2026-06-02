# Task 23 — Vendor linenoise (project_init.py + CMake)
id: 2026-06-02/task23
parent: 2026-06-02/progress15
status: done
owner: code_agent (Opus)

## Objective
Make the linenoise line editor available to the build as a vendored dependency, mirroring how PEGTL
is vendored, and link it into the `shell` static library.

## Steps
1. `project_init.py`: add `LINENOISE_COMMIT` (pinned full SHA `a473823…`) + `LINENOISE_CONFIG`
   (GitHub archive zip → `lib/linenoise/`) next to `PEGTL_CONFIG`; add `setup_linenoise()` mirroring
   `setup_pegtl()` (skips if `lib/linenoise/` exists); call it from `main()` after `setup_pegtl()`.
2. Vendor the files: `lib/linenoise/linenoise.{c,h}` (+ `LICENSE`) at the pinned commit.
3. `src/shell/CMakeLists.txt`: `add_library(linenoise STATIC lib/linenoise/linenoise.c)` with its
   include dir, `C_STANDARD 99`, and `-Wno-unused-command-line-argument` (silences the harmless
   "argument unused" warning from the global `-stdlib=libc++` on a C TU — there is no `-Werror`).
   Add `linenoise` to `target_link_libraries(shell PUBLIC jit backend linenoise)`.

## Acceptance
- [x] `lib/linenoise/linenoise.{c,h}` present; `project_init.py` reproduces them (idempotent).
- [x] `ninja -C build` builds `bin/liblinenoise.a` and links it into `mxs`. (Top-level CMake already
      declares `LANGUAGES C CXX`, so the .c compiles as C with no extra setup.)

## Notes
- The build is fragile (vendored LLVM 20 + libc++); use `ninja -C build` only. The `.c`-under-libc++
  warning is non-fatal (no `-Werror` anywhere) and is silenced per-target.
