# Task 29 — Create src/_std tree + move std-backing functions + std.bc wiring
id: 2026-06-02/task29
parent: 2026-06-02/progress17
status: done
owner: code_agent
blocked-on: nothing (Mux greenlit; borderline split resolved by the builtins taxonomy + survey)

## Outcome (2026-06-02, implemented + adversarially verified via sub-agents)
DONE. The C++ refactor is verified correct: `std.bc` defines all 14 moved symbols (T), `core.bc`
defines the new `mxs_panic` and none of the 14 (no ODR collision), the moved bodies are gone from
`src/core`, `MXFormat.{cpp,h}`+`MXTime.{cpp,h}` are deleted, and `include/mxspp/_std/` has a prototype
per symbol. **Isolation proof** (verifier, with HEAD's `std/io.mxs`): `ninja` clean, ctest 3/3, corpus
34/34, example sweep 22/22, REPL `println`+`./objects_population` green, a failing `assert` aborts via
`mxs_panic` (rc 134, not "symbol not found").
Necessary deviation: `core_test` natively calls `mxs_str`/`mxs_repr`/`mxs_format` (now in `_std`), so
`src/_std/CMakeLists.txt` also builds a `std` STATIC lib (the JIT still uses `std.bc`); `core_test`
links `core std`.
**Caveat (NOT a task29 regression):** the working tree carries Mux's WIP `std/io.mxs`
(`import std._fileio;` nested import + the `@@foreign` bindings moved out) which breaks any program
importing `std.io` until **progress18** (transitive imports) + **progress20** (`std/_file.mxs` body +
bindings re-homed to builtins/string per the new taxonomy) land. Left untouched per scope; HEAD's
`std/io.mxs` is green.

## Objective
Move the std-library C/C++ backends out of `src/core/` into a dedicated `src/_std/`, build them into a
`std.bc` the JIT links, and keep `src/core/` to the object model + runtime primitives. (Foundation for
the std-체계 batch — progress20's `mxs_sys_*` land in `src/_std/system.cpp` after this.)

## Steps (exact relocation from the 2026-06-02 survey; keep C symbol names identical)
1. **Create `src/_std/` TUs**, moving the function bodies (+ the includes they need):
   - `io.cpp` ← `mxs_print`, `mxs_println` (from MXFormat.cpp), `mxs_repl_echo` (REPL glue).
   - `string.cpp` ← `mxs_format` + **the whole format engine** (MXFormat.cpp is consumed by io.cpp +
     string.cpp and removed from core).
   - `builtins.cpp` ← `mxs_str`, `mxs_repr` (from MXString.cpp), `mxs_raise`, `mxs_exit` (from MXOps.cpp).
   - `time.cpp` ← `mxs_time_now/ms/ns` (MXTime.cpp consumed, removed from core).
   - `types.cpp` ← `mxs_typeof` (from MXOps.cpp).
   - `system.cpp` ← `mxs_population_dump`, `mxs_population_dump_all` (from MXOps.cpp). (Future home of
     progress20 `mxs_sys_*`.)
   Each is an `extern "C"` TU including the core headers it uses (`include/mxspp/core/*.h`).
2. **BUG fix (same task): define `mxs_panic`.** Codegen emits a call to `mxs_panic` for `assert`
   (codegen_stmt.cpp:230) but no definition exists in-tree → assert scripts fail JIT symbol resolution.
   Add `extern "C" void mxs_panic(const char *msg)` to `src/core/MXOps.cpp` (print to stderr + abort);
   it is codegen-emitted, so it stays in core.bc (always resolvable). Add a corpus case exercising assert.
3. **`src/_std/CMakeLists.txt`** — mirror `src/core/CMakeLists.txt:30-74`:
   - re-run `find_program(MXS_LLVM_LINK ...)` + the APPLE `-isysroot` block (non-cache vars, not visible
     cross-dir); `MXS_BC_CXX` (cache) + `BIN_DIR` (root) are visible.
   - `STD_BC_SOURCES = io.cpp string.cpp builtins.cpp time.cpp types.cpp system.cpp`; per-file
     `-emit-llvm -c … -std=c++23 -stdlib=libc++ -fPIC -mno-outline-atomics ${MXS_BC_SYSROOT} -I …/include`
     → `${BIN_DIR}/std_bc_${nm}.bc`; `llvm-link` → `${BIN_DIR}/std.bc`; `add_custom_target(std-bc ALL …)`.
   - (Optional `std` static lib for parity — NOT required for linking; symbols resolve via the JIT.)
4. **`src/core/CMakeLists.txt`** — drop `MXFormat.cpp` + `MXTime.cpp` from the static lib sources and
   from `CORE_BC_SOURCES`; remove the moved functions from `MXString.cpp` (str/repr) and `MXOps.cpp`
   (raise/exit/typeof/population_dump[_all]) — leave everything else.
5. **`src/CMakeLists.txt:5-10`** — `add_subdirectory(std)` after `core`. Root already does
   `add_subdirectory(src)`.
6. **JIT/driver/shell wiring** (reuse the dead `runtimeBcPath` slot = std.bc):
   - `jit.cpp` — no change (line 85 already links `runtimeBcPath`; line 35 no-ops on empty).
   - `driver/main.cpp` — add `std::string std_bc_path() { return find_bc("std.bc"); }`; at the run-core
     `jit::run` call (main.cpp:154) pass `std_bc_path()` instead of `""`; thread it into the repl launch.
   - `shell/shell.cpp` + `include/mxspp/shell/shell.h` — add a `stdBcPath` param to `repl()` +
     `eval_thunk()`, pass it as the `jit::run` `runtimeBc` arg (shell.cpp:110); optionally show it in
     `./status`.

## Acceptance
- [ ] `ninja -C build` clean; `build/bin/std.bc` present and `lib/llvm/bin/llvm-nm build/bin/std.bc`
      shows the moved symbols (mxs_println/print/format/str/repr/raise/exit/typeof/time_*/repl_echo/
      population_dump[_all]); `llvm-nm build/bin/core.bc` shows mxs_panic + still has the kept symbols.
- [ ] `ctest` 3/3; full `example/examples/*.mxs` sweep at expected rc; corpus all green (+ new assert
      case); a REPL pipe (`./objects_population`, `println`, an import) works — JIT resolves every moved
      symbol from std.bc.
- [ ] `grep` confirms the moved std-backing functions no longer live in `src/core/`; no `std/*.mxs`
      change needed (bindings still resolve cross-bc).

## Notes
- Delicate part: the `std.bc` bitcode build — mirror src/core exactly (same `MXS_BC_CXX`, sysroot,
  flags). Version-skew: `MXS_BC_CXX`'s LLVM major must be ≤ the JIT's LLVM or `parseIRFile` fails.
- Build ONLY with `ninja -C build` (auto-reruns cmake for the new subdir/target). Never rebuild.py --clean.
- The `std/*.mxs` text path (copied to `${BIN_DIR}/std` by driver/CMakeLists.txt:9-12) is orthogonal to
  `std.bc` — no change here (that's progress21).
