# mxspp (MXScript) — Project Guide for AI Collaboration

mxspp is the **C++23 implementation of MXScript (`mxs`)** — a dynamically-typed scripting language
that compiles to native code via **LLVM (JIT)**, parsed with **PEGTL**. Like CPython is to Python,
`mxspp` is to `mxs`.

**Current state (verified 2026-05-30):** the *design* is rich and largely complete (grammar in
`include/mxspp/frontend/grammer.hpp`, the `docs/`, and 14 `.mxs` examples), but the *implementation*
is mostly skeleton. The tokenizer (`frontend/tokenizer.h`) is the only fully working frontend piece;
the only real codegen is `IntegerLiteral::codegen` in `src/frontend/ast.cpp`; `backend/`, `jit/`,
`runtime/`, and `shell/` are empty stubs. The build system (CMake+Ninja) works and already produces
the per-module `.so`, `runtime.bc`, and the `mxs` binary. **Near-term direction: implement the
language incrementally, from skeleton toward a runnable pipeline.**

## Collaboration model (development, not research)

A software-development workflow with three roles. Note the "AI assistant" and the "code agent" are
usually the same Claude Code instance wearing two hats:

- **Human (Mux)** — owns decisions, scope, direction, and conventions. The AI proposes; Mux decides.
- **AI (planner/recorder hat)** — turns Mux's intent into a `progress` note (records the decision,
  the why, and the implementation impact) and breaks it into executable `task` files.
- **AI (implementer hat / code agent)** — executes a `task`: inspects and changes the codebase.

Authority: **Mux makes the key decisions; the AI writes progress/task docs and writes code; code
changes are reviewed by Mux before they count as done.**

## Workflow

1. Mux raises an idea / problem (conversation in Chinese).
2. AI creates or updates a **progress** entry under `develop_log/<date>/` recording the decision,
   rationale, and impact.
3. AI breaks the work into one or more **task** files under `develop_log/<date>/tasks/`.
4. AI implements the task (code), keeping builds (and any tests/examples) green.
5. AI appends to the progress `Agent log`; Mux reviews.

See `develop_log/README.md` for file conventions, templates, and naming.

## Languages

- **Conversation with Mux: Chinese.**
- **All written artifacts (CLAUDE.md, progress, task, docs): English** (code-agent-facing).
- Code identifiers and comments: English.

## Where to read (don't duplicate — link)

- `AGENTS.md` — repository guidelines: module layout, code style, commit/PR conventions.
- `docs/Architecture.md` — the intended compilation pipeline (core → frontend → backend → jit).
- `docs/type_system.md` — the type system + the hybrid static/dynamic **dispatch** design (§7–§8).
- `docs/ffi.md` — the `@@foreign` FFI contract (direct-call convention, fixed vs variadic dispatch).
- `docs/develop_rule.md` — mandatory C++ runtime rules: ownership (`unique_ptr`), move, borrow
  (raw ptr/ref), deep copy via `clone()`, custom RTTI. Follow these for `src/core` and `src/runtime`.

## Build & run

- Build: `python3 rebuild.py --clean` (first build / after CMake changes) or `python3 rebuild.py`
  (incremental). Requires clang>=20, libc++, Ninja on PATH.
- Dependency vendoring (LLVM + PEGTL into `lib/`): `python3 project_init.py`.
- Pre-review checks (format/lint): `python3 before_commit.py --staged`.
- Smoke-test a script: `build/bin/mxs example/examples/hello_world.mxs`.

> Known doc drift (do not be misled): `AGENTS.md` / `README.md` still mention `download_dep.py`
> (the real vendoring script is `project_init.py`) and call the binary `mxspp` (the actual built
> target/binary is `mxs`). Use the corrected names above.

## Code conventions (summary; AGENTS.md + docs/develop_rule.md are authoritative)

- C++23. Exported types are `MX`-prefixed (`MXObject`, `MXType`); methods camelCase in namespaces.
- Style: root `.clang-format` (WebKit base, 4 spaces, 90 columns). Run clang-format / before_commit
  before requesting review.
- Runtime memory: `unique_ptr` ownership + move; borrow via raw ptr/ref; `clone()` for deep copies.
- **Testing rule — unit tests are REQUIRED and must reach 100% coverage.** Every code change ships
  unit tests covering all new/changed code; the coverage gate is enforced before a change counts as
  done. (The test harness + coverage tooling is being established in
  `develop_log/2026-05-31/progress03` — the project had none.) Keep `example/examples/*.mxs` as
  integration smoke tests on top of the unit tests.

## Current direction / active work

Implementing MXScript incrementally. See `develop_log/<date>/` for the active progress and tasks.
