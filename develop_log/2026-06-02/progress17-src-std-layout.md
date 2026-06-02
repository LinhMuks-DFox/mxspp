# Progress 17 — Consolidate std-library C/C++ implementations into `src/_std/`
id: 2026-06-02/progress17
date: 2026-06-02
author: human+ai
status: done
refs: [2026-06-01/progress13, 2026-06-02/progress16]
supersedes:
commits: []
files:
  - src/_std/** (NEW)                # the C/C++ backends for std.* modules
  - src/_std/CMakeLists.txt (NEW)    # static lib + bitcode (std.bc) for the JIT
  - src/core/{MXOps,MXFormat,MXString,MXTime}.cpp  # remove the moved std-backing functions
  - src/core/CMakeLists.txt         # drop moved files from CORE_BC_SOURCES
  - src/jit/jit.cpp, src/driver/main.cpp, src/shell/shell.cpp  # link + locate std.bc
  - CMakeLists.txt                  # add_subdirectory(src/_std)

## Goal
The C/C++ functions that *back the importable standard library* (`std.io`, `std.time`, `std.types`,
…) are currently **scattered through `src/core/`** (MXFormat.cpp, MXString.cpp, MXArrayList.cpp,
MXTime.cpp, MXOps.cpp). Mux: "C++/C 实现的 std 需要放在 `src/_std` 里" — not mixed into the core
object-model/runtime. Move the std-backing implementations into a dedicated **`src/_std/`** tree, one
TU per std module, leaving `src/core/` for the object model + language runtime only.

## Context / current state (grounded)
The `@@foreign` symbols referenced by `std/*.mxs` and where they live today:
- **std.io** → `mxs_println`/`mxs_print`/`mxs_format` (`src/core/MXFormat.cpp`), `mxs_str`/`mxs_repr`
  (`src/core/MXString.cpp`), `mxs_raise`/`mxs_exit` (`src/core/MXOps.cpp`), `arraylist` →
  `mxs_arraylist_new` (`src/core/MXArrayList.cpp`).
- **std.time** → `mxs_time_now`/`mxs_time_ms`/`mxs_time_ns` (`src/core/MXTime.cpp`).
- **std.types** → `mxs_typeof` (`src/core/MXOps.cpp`).
- **REPL support** (not a std module, but the same kind of glue): `mxs_repl_echo`
  (`src/core/MXFormat.cpp`), `mxs_population_dump`/`_all` (`src/core/MXOps.cpp`).

The JIT resolves these by linking **`core.bc`** (built from `CORE_BC_SOURCES` in
`src/core/CMakeLists.txt`); `jit::run` also has an UNUSED `runtimeBcPath` slot (always `""` today).

## Decisions (proposed — for Mux to confirm at execution time)
- **D1 — what moves vs stays.** MOVE the *pure std surface* (only reachable via `import`, not emitted
  by codegen): `mxs_println`/`print`/`format`, `mxs_time_*`, `mxs_typeof`, and the REPL glue
  (`mxs_repl_echo`, `mxs_population_dump[_all]`). KEEP in `src/core/` the *language runtime
  primitives* that codegen emits directly or that are fundamental type ops: `mxs_op_*`,
  `mxs_retain`/`release`, `mxs_int_from_i64`/`float_new`/`bool_new`/`str_new`/`nil_new`,
  `mxs_lvalue_*`, `mxs_object_classinfo`/`get_attr`/`set_attr`/`instance_new`/`method_missing`,
  `mxs_is_type`, `mxs_len`, `mxs_index_get`, `mxs_arraylist_*` (used by codegen for list literals,
  varargs, and the built-in container methods — runtime, not "std"). 
  - Borderline: `mxs_str`/`mxs_repr` (thin wrappers over `MXObject::str()/repr()`) and
    `mxs_raise`/`mxs_exit` (process control) — proposed to MOVE to `src/_std/io.cpp` (they are only
    `std.io` bindings), but they may stay if judged core. Confirm at execution.
- **D2 — bitcode wiring.** `src/_std/` emits its own **`std.bc`** (mirroring the per-file →
  `llvm-link` machinery in `src/core/CMakeLists.txt`). The JIT links it via the currently-unused
  `runtimeBcPath` slot of `jit::run` (so: `core.bc` = runtime, `std.bc` = stdlib). `driver/main.cpp`
  + `shell/shell.cpp` locate `std.bc` with the existing `find_bc()` and pass it in. (Alternative:
  fold `src/_std` into `CORE_BC_SOURCES` — simpler but keeps one blob; rejected for clarity.)
- **D3 — layout.** `src/_std/io.cpp`, `src/_std/time.cpp`, `src/_std/types.cpp`, and `src/_std/repl.cpp`
  (the REPL glue), each an `extern "C"` TU implementing that module's `mxs_*`. A `src/_std/CMakeLists.txt`
  builds the `std` static lib + `std.bc`; root `CMakeLists.txt` adds the subdirectory; `shell`/driver
  link `std` (replacing the transitive core symbols they used).

## Survey refinement (2026-06-02 — supersedes the D1 borderline questions)
A 5-agent survey produced the exact relocation map + the copy-ready `std.bc` recipe. Decisions, now
concrete (Mux's new builtins taxonomy resolves the borderline cases):

- **MOVE to `src/_std/` (the pure std surface + REPL glue), keeping C symbol names identical:**
  - `src/_std/io.cpp` ← `mxs_print`, `mxs_println` (from MXFormat.cpp) + `mxs_repl_echo` (REPL glue).
  - `src/_std/string.cpp` ← `mxs_format` **and the whole self-contained format engine** (MXFormat.cpp).
    → MXFormat.cpp is fully consumed (split into io.cpp + string.cpp) and dropped from core.
  - `src/_std/builtins.cpp` ← `mxs_str`, `mxs_repr` (from MXString.cpp), `mxs_raise`, `mxs_exit`
    (from MXOps.cpp). (Per Mux's taxonomy, str/repr/raise/exit are the auto-imported **builtins**.)
  - `src/_std/time.cpp` ← `mxs_time_now/ms/ns` (MXTime.cpp consumed, dropped from core).
  - `src/_std/types.cpp` ← `mxs_typeof` (from MXOps.cpp).
  - `src/_std/system.cpp` ← `mxs_population_dump`, `mxs_population_dump_all` (REPL diag, from MXOps.cpp);
    **and is the future home for progress20's `mxs_sys_*`.**
- **KEEP in `src/core/` (codegen-emitted or object-model — must stay always-resolvable):** all
  `mxs_op_*`, `mxs_int_*`, `mxs_str_new`, `mxs_arraylist_*` (emitted for list literals / varargs /
  `.append`/`.get`), `mxs_str_concat/len/cmp/cstr` (string-type primitives; relocate only when
  std.string surfaces them, progress20/task34), `mxs_retain/release/object_truthy/obj_delete`,
  `mxs_get_attr/set_attr/instance_new/object_classinfo/method_missing/is_type/index_get/len`.
  - **Key insight:** a std module's `@@foreign(symbol_name="X")` binds to symbol X wherever it lives —
    `std.bc` AND `core.bc` are both JIT-linked. So `std/builtins.mxs`'s `arraylist()` keeps binding
    `mxs_arraylist_new` even though that symbol stays in `core.bc`. The "move" is about *source-file
    organization*, not about which `.bc` a bound symbol must sit in.
- **BUG found (fold into task29): `mxs_panic` is referenced by codegen (assert lowering,
  codegen_stmt.cpp:230) but DEFINED NOWHERE.** Any script using `assert` would fail JIT symbol
  resolution. Define it — since it is codegen-emitted (like `mxs_op_*`), put `mxs_panic(const char*)`
  in `src/core/MXOps.cpp` (prints + `abort`/`_Exit`), staying always-resolvable in core.bc.
- **std.bc recipe (copy `src/core/CMakeLists.txt:30-74`):** `src/_std/CMakeLists.txt` mirrors the
  per-file `-emit-llvm` + `llvm-link` machinery (rename `core_bc_*`→`std_bc_*`, output `${BIN_DIR}/std.bc`).
  `src/_std` must re-run its own `find_program(MXS_LLVM_LINK)` + APPLE-sysroot block (those are
  non-cache vars, not visible cross-dir); `MXS_BC_CXX` (cache) + `BIN_DIR` (root) ARE visible.
  `jit::run` already links the path passed as its `runtimeBcPath` slot (jit.cpp:85) — pass
  `std_bc_path()` instead of `""` (driver run-core main.cpp:154 + thread through shell). No jit.cpp
  change. The `std` static lib is NOT needed for linking (symbols resolve via the JIT from std.bc);
  the critical artifact is `build/bin/std.bc` via an `add_custom_target(std-bc ALL ...)`.

## Tasks
- [ ] [task29 — create src/_std tree + move std-backing functions + bitcode (std.bc) wiring](tasks/task29-src-std-tree.md)

## Risk / notes
- The build is fragile (vendored LLVM 20 + libc++); the per-file bitcode emit + `llvm-link` step is
  the delicate part. Mirror `src/core/CMakeLists.txt` exactly for `std.bc`. Build with `ninja -C build`.
- Keep every `@@foreign(symbol_name=...)` in `std/*.mxs` pointing at the same symbol name after the
  move (only the .cpp location changes, not the C symbol) — so no `.mxs` change is needed.
- Verify the JIT still resolves all symbols (run the full example sweep + corpus + a REPL pipe).

## Agent log
- 2026-06-02 [ai/opus] IMPLEMENTED + adversarially verified via sub-agents (Opus 4.8). Created the
  `src/_std/` tree + `include/mxspp/_std/` headers (io/string/builtins/time/types/system, one
  header+cpp pair each), a `src/_std/CMakeLists.txt` that builds `std.bc` (mirroring core's bitcode
  recipe) AND a `std` static lib; dropped `MXFormat`/`MXTime` from core; removed str/repr/typeof/
  raise/exit/population_dump[_all] from MXString.cpp/MXOps.cpp; **fixed the `mxs_panic` missing-symbol
  bug** (added to core; assert now aborts cleanly); threaded `std.bc` through `jit::run`'s reused
  `runtimeBcPath` slot (driver `std_bc_path()` + shell `stdBcPath`). Verified: std.bc has all 14 moved
  symbols, core.bc has mxs_panic + none of the 14 (no ODR collision), ctest 3/3, corpus 34/34, examples
  22/22, REPL green — all against HEAD's std/io.mxs. The working-tree std/io.mxs WIP (Mux's, broken
  nested import) is out of scope and awaits progress18/20. See task29 Outcome. Committed (C++ only;
  std/*.mxs WIP left untouched).
- 2026-06-02 [ai/opus] Recorded the reorg per Mux's request (the std-backing C funcs are scattered in
  src/core today; he wants them in src/_std). Mapped every std-backing symbol to its current file and
  proposed the move/keep split (D1), the `std.bc` bitcode wiring via jit::run's unused runtimeBc slot
  (D2), and the per-module layout (D3). NOT executed — this is the plan; awaiting Mux's go + the
  confirm on the borderline functions (mxs_str/repr, mxs_raise/exit). `mxs_typeof` (just added in
  progress16) is one of the functions slated to move here.
