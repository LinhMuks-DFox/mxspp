# Progress 21 — Module location & resolution (binary-relative std, `--std-dir`, Python-like search)

id: 2026-06-02/progress21
date: 2026-06-02
author: human+ai
status: active
refs: [2026-06-01/progress13, 2026-06-02/progress18, 2026-06-02/progress20]
supersedes:
commits: []
files:
  - src/driver/main.cpp                 # std_search_dirs(): --std-dir flag, MXSPATH env, script-dir, binary-relative std
  - src/frontend/imports.cpp            # (maybe) package dirs / __init style; search-path semantics
  - include/mxspp/frontend/imports.h    # search-dir contract
  - docs/ (module resolution doc)       # document the final search order

## Goal
Make `import` resolution **robust and predictable**, like Python's. Mux: "mxs 的 std 应当放在 mxs
解释器的二进制文件同目录下的 std 里。除非 mxs --std-dir 的方式传入 std 的位置。然后 mxs 寻找 import
用的模块/包的方式应当和 python 一样." Two parts: (1) a canonical, install-relative home for the stdlib;
(2) a Python-style search path for finding any module/package.

## Context / current state (grounded — `src/driver/main.cpp:105-116`, `imports.cpp:27-91`)
- `std_search_dirs()` returns `["", exe_dir(), "build/bin", "bin"]`; a module `std.io` is sought at
  `<base>/std/io.mxs` for each base in order, **first hit wins**. CWD (`""`) is checked FIRST.
- The std `.mxs` modules ARE copied next to the binary at build (`src/driver/CMakeLists.txt:9-12`,
  `copy_directory std -> ${BIN_DIR}/std`), so `exe_dir()` already finds them — but only AFTER the CWD,
  and there is no override and no env var.
- A dotted path maps to a relative file: `std.io -> std/io.mxs` (`imports.cpp:29-36`). There is **no
  package concept** (no dir-as-package, no `__init__`-style module), no `MXSPATH` env, no `--std-dir`.

## Problems with today's behavior
- **CWD-first is fragile**: running from a directory that happens to contain a `std/` shadows the real
  stdlib. The canonical stdlib should be the one beside the binary (or `--std-dir`), not whatever `./std`
  the CWD has.
- **No override**: can't point at an alternate stdlib for testing/packaging.
- **Not Python-like**: a user's own modules next to their *script* aren't found (the script's own
  directory isn't on the path); no env-var path; no packages.

## Decisions (proposed — refine at execution)
- **D1 — canonical std home = binary-relative `std/`.** The stdlib that ships with the interpreter is
  `<dir-of-mxs-binary>/std/`. This is the authoritative `std.*` source. Resolve `std.*` here, not from a
  coincidental `./std`.
- **D2 — `--std-dir <path>` override.** A CLI flag (parsed in `main.cpp` before the subcommand) sets the
  stdlib root explicitly, taking precedence over the binary-relative default. (Also accept an env var,
  e.g. `MXS_STD_DIR`, as the non-flag form.)
- **D3 — Python-like search path for *user* modules.** Model on `sys.path`: search order =
  (1) the **importing script's own directory** (so a program can `import mymod` sitting beside it);
  (2) directories from an env var **`MXSPATH`** (colon-separated, like `PYTHONPATH`);
  (3) the **std root** (D1/D2) for `std.*`. First hit wins. The REPL uses CWD as the "script dir".
- **D4 — packages as directories (Python-like).** A dotted path `a.b.c` resolves to either
  `a/b/c.mxs` (module) or `a/b/c/` (package). Decide whether a package needs an explicit entry file
  (`a/b/c/__init__.mxs`-equivalent, e.g. `c/c.mxs` or a `mod.mxs`) or whether a bare directory with
  submodules suffices. (mxs has no `__init__` notion yet — this is the part that most needs design.)
  Minimal v1: keep the flat `a.b.c -> a/b/c.mxs` mapping but layer the search *path* (D1-D3) under it;
  full package dirs can be a follow-up.

## Relationship to other progresses
- **Orthogonal to progress18** (which fixes how a *found* module is *scoped/merged* — namespaces,
  classes, transitive). progress21 is purely "how the file is *located*". They compose: progress18's
  transitive resolution will call back into progress21's path search for each nested import.
- **Enables progress20**: a grown std tree installed beside the binary resolves robustly regardless of
  CWD.

## Tasks
- [ ] task37 — search-path overhaul: `--std-dir` flag + `MXS_STD_DIR`/`MXSPATH` env + script-dir-first
      + binary-relative std default; update `std_search_dirs()` and thread a richer search spec into
      `resolve_imports`. Document the final order. (Package dirs D4 = optional follow-up.)

## Agent log
- 2026-06-02 [ai/opus] Recorded per batch-record-first, from Mux's "std beside the binary / --std-dir /
  Python-like import" requirement. Grounded the current `std_search_dirs()` + dotted-path mapping
  (CWD-first, no override, no packages). Kept orthogonal to progress18 (location vs scoping). NOT
  executed — part of the std-체계 batch; lands after progress17/18, before/with progress20.
