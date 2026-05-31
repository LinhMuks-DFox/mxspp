# Progress 10 — Native macOS (Apple Silicon) build of mxs

id: 2026-05-31/progress10
date: 2026-05-31
author: human+ai
status: active
refs: [2026-05-31/progress09]
supersedes:
commits: []
files:
  - toolchain.macos.cmake          # (new) macOS toolchain — Homebrew llvm@20
  - rebuild.py                     # auto-select macOS toolchain + build dir
  - src/core/CMakeLists.txt        # bitcode: -isysroot on Apple
  - src/runtime/CMakeLists.txt     # bitcode: -isysroot on Apple
  - src/jit/jit.cpp                # stamp user module with host data layout / triple
  - src/driver/main.cpp            # find_bc locates .bc next to the executable on macOS
  - .gitignore                     # ignore build-macos/

## Goal
Make `mxs` build and run **natively on macOS (Apple Silicon)**, not only inside the Linux/Docker
toolchain. End state: `python3 rebuild.py` produces a working `build-macos/bin/mxs`; `run-core`,
`run-obj`, and `ctest` all pass on the host.

## Context / Motivation
The build infrastructure was Linux-only (`toolchain.cmake` hard-codes `CMAKE_SYSTEM_NAME=Linux`
and a system `clang`; `lib/llvm` was a Linux-X64 prebuilt; the committed `build/` was an
aarch64-linux-gnu configure). A language whose only supported dev path is Docker is bad for its
ecosystem — contributors and users on macOS should be able to build, run, and hack on `mxs`
directly. **Decision (Mux): macOS adaptation is in scope; treat native-on-the-host as a first-class
build target.** This progress records the port and the five portability issues it surfaced.

## Decisions

### D1 — macOS uses Homebrew `llvm@20`, not the LLVM.org prebuilt
- Decision: the macOS toolchain (`toolchain.macos.cmake`) builds against **Homebrew `llvm@20`
  (20.1.8)** for the compiler, the LLVM libraries, `llvm-link`, and libc++.
- Why: the official LLVM.org `LLVM-20.1.8-macOS-ARM64` prebuilt was built with **type-aware
  allocation** (typed `operator new`) enabled. Its startup static initializers invoke typed
  `operator new` before libc++'s guard initializer runs, so the process aborts at startup
  (`libc++abi: Terminating due to typed operator new being invoked before its static initializer`).
  clang 20.1.8 has **no** flag to disable it (`-fno-typed-cxx-new-delete` is clang 21+). Homebrew's
  `llvm@20` is a sane build without that feature, and is the *same* version (20.1.8) the codebase
  targets — so the LLVM C++ API, the bitcode version (core.bc/runtime.bc), and the ORC JIT all match.
- Impact: requires `brew install llvm@20` (keg-only at `/opt/homebrew/opt/llvm@20`). `lib/llvm` (the
  LLVM.org prebuilt) is no longer used by the macOS build and can be removed to reclaim ~1.45 GB.
- Alternatives considered: vendored LLVM.org prebuilt — startup crash, no fix on clang 20. Apple
  clang + system libc++ — the prebuilt LLVM libs reference `[abi:ne200100]` libc++ symbols Apple's
  libc++ doesn't export, and Apple clang 21's bitcode is rejected by `llvm-link` 20 (newer bitcode).
  Build LLVM from source — hours; unnecessary once Homebrew's build works.

### D2 — Keep Linux untouched; macOS support is additive
- Decision: do not modify `toolchain.cmake` or the Linux flow; add a parallel `toolchain.macos.cmake`
  + `build-macos/` dir, and guard all code/CMake changes so Linux behaviour is unchanged.
- Why: the Docker/Linux build is the established CI path (ctest 3/3); the port must not regress it.
- Impact: `rebuild.py` picks `build-macos` + `toolchain.macos.cmake` on `sys.platform == "darwin"`,
  else `build` + `toolchain.cmake`. The `jit.cpp` / `main.cpp` fixes are correct on all platforms.

## Issues / Gotchas (the five portability bugs found)
1. **Bitcode compile lacked `-isysroot`.** The `core.bc`/`runtime.bc` `add_custom_command`s are raw
   `clang++` calls that bypass CMake's automatic SDK-sysroot injection, so libc++'s `<wchar.h>` /
   `mbstate_t` wrappers failed (`"We don't know how to get the definition of mbstate_t…"`). Fixed by
   adding `-isysroot <sdk>` on Apple, with the SDK path from `CMAKE_OSX_SYSROOT` or, when empty
   (Command Line Tools, no full Xcode), `xcrun --show-sdk-path`.
2. **`-lc++abi` not auto-added (split libc++).** Initially manifested before switching to Homebrew:
   the std exception classes (`runtime_error::what`, `length_error::~`, `format_error::~`) live in
   libc++abi, but macOS expands `-stdlib=libc++` to `-lc++` only. (Moot under Homebrew llvm@20, whose
   libc++ resolves it; recorded because it bites any split-libc++ link.)
3. **typed `operator new` startup abort** — see D1.
4. **User IR module had no data layout / triple.** `codegen` builds the module target-agnostically;
   on macOS it inherited an AArch64-**ELF** layout, which ORC rejected against the Mach-O JIT
   (`Added modules have incompatible data layouts: …m:e… vs …m:o…`). Fixed in `jit.cpp` by stamping
   the user module with `jit->getDataLayout()` / `jit->getTargetTriple()` before `addIRModule`
   (the canonical LLJIT pattern — correct on every platform).
5. **`find_bc` picked the stale Linux `build/bin/core.bc`.** The executable-relative lookup was
   `#if defined(__linux__)` only (`/proc/self/exe`); on macOS it fell through to a CWD-relative
   `build/bin/` search and loaded the old aarch64-linux-gnu (ELF) `core.bc` → the layout mismatch in
   (4). Fixed by adding a `__APPLE__` branch using `_NSGetExecutablePath`, so `mxs` finds the
   bitcode next to itself (`build-macos/bin/`) regardless of CWD.

## Verification
- `python3 rebuild.py` → `build-macos/bin/mxs` (Mach-O arm64). `--clean` also works.
- `run-core` examples all correct: `core_fib`→55, `core_arith`→5/14/42, `core_loops`→10/120/true/true,
  `core_types`→hello world/3.5/2.5/2/true/false, `core_string`, `core_list`, `core_iter`,
  `core_match`, `core_errmsg`, `core_raise` (exit 2).
- `run-obj obj_features` correct; **ctest 3/3** (frontend/runtime/core).
- Harmless: `ld: warning … built for newer 'macOS' version (26.0)` (Homebrew lib deployment-target
  skew); editor LSP "file not found" red lines (no compile context) — the CMake build is clean.

## Open / TODO (carry-over)
- macOS prerequisite is `brew install llvm@20`; not yet auto-checked by `project_init.py`
  (which only vendors the LLVM.org prebuilt). A macOS branch there could `brew install llvm@20`
  (or skip vendoring) for a one-command setup.
- AGENTS.md / README still describe only the Linux flow; add the macOS path (`brew install llvm@20`
  → `python3 rebuild.py`).
- Deployment-target warnings could be silenced with `CMAKE_OSX_DEPLOYMENT_TARGET` if noisy.

## Agent log
- 2026-05-31 [ai] Ported the build to native macOS/arm64. Re-vendored then abandoned the LLVM.org
  prebuilt (typed-new startup abort), switched to Homebrew `llvm@20`; added `toolchain.macos.cmake`;
  fixed five portability issues (bitcode `-isysroot`, split-libc++ `-lc++abi`, typed-new, user-module
  data layout/triple in `jit.cpp`, `find_bc` exe-relative lookup on Apple); taught `rebuild.py` to
  auto-select the macOS toolchain + `build-macos`. Verified: run-core/run-obj examples correct,
  ctest 3/3. Linux flow untouched (additive).
