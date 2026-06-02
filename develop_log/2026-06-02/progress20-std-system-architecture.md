# Progress 20 — The std-library system: complete taxonomy + std.system / std.io

id: 2026-06-02/progress20
date: 2026-06-02
author: human+ai
status: active
refs: [2026-06-01/progress13, 2026-06-02/progress16, 2026-06-02/progress17, 2026-06-02/progress18, 2026-06-02/progress21]
supersedes:
commits: []
files:
  - std/{system,_file,io,string,array,builtins,net_io,types,time}.mxs  # the module taxonomy (.mxs surface)
  - src/_std/{system,io,string,array,builtins,types,time,repl}.cpp      # layer-1 C primitives (per progress17)
  - src/core/MXOps.cpp                                                 # mxs_op_add: list+list; mxs_panic def (bug)
  - src/core/MXArrayList.cpp, MXString.cpp                             # new array/string layer-1 primitives

## Goal

Build a **coherent, complete standard-library system** instead of std-backing code scattered across
`src/core`. Mux: "建立完整的 std 体系，而不是乱七八糟的分散在世界的各个角落". The whole stdlib follows
**one pipeline and two layers**:

```
user_script  ->  mxs std (layer-2, pure .mxs)  ->  @@foreign C primitives (layer-1, mxs_*)  ->  LLVM
```

- **Layer-1** = thin `extern "C"` `mxs_*` primitives (the OS/runtime boundary), compiled to `std.bc`
  (per progress17), bound by `@@foreign(symbol_name=...)`.
- **Layer-2** = rich pure-`.mxs` wrappers/classes built on layer-1 (the bulk of the stdlib; the
  self-hosting goal — Mux: "mxs 应该可以原生实现 is_instance_of … Date 之类的也是可以原生 mxs 实现的").

This progress defines the **module taxonomy** and the **std.system + std.io** core; the relocation of
the C backends is progress17, the import/namespace machinery the layer-2 needs is progress18, and how
modules are *located* on disk is progress21.

## Module taxonomy (the complete std tree)

| module | role | layer-1 primitives | layer-2 (.mxs) |
|---|---|---|---|
| **std.system** | OS interface — "io 最关键的是调用 syscall" | `mxs_sys_open/read/write/close/lseek/fsync`, `mxs_sys_const`, `abort`, `getenv`, `args` | flag/whence constants, thin wrappers |
| **std._file** | private file-stream backing for io (NOT public) | — | `FileStream` class (buffered read/write over fds) |
| **std.io** | streams + print/format I/O; stdout/stderr/stdlog are files | `mxs_print/println` (re-pointed — see D4), `mxs_repl_echo` | `stdout/stderr/stdin/stdlog`, `println/print/input/flush` over `FileStream` |
| **std.string** | ALL string ops — "拼接/format 围绕 string 展开" | `mxs_format`, `mxs_str_concat/len/cmp`, NEW `mxs_str_upper/lower/split/join/replace/contains/find/trim/slice/to_int/repeat` | `capitalize/is_empty/pad_*` over the above |
| **std.array** | ALL list ops | `mxs_arraylist_*` (existing) + NEW `pop/insert/remove/contains/index_of/slice/reverse/clear` | `is_empty/first/last/sum`; map/filter/reduce DEFERRED (F3) |
| **std.builtins** | the ONLY auto-imported module | `mxs_str/repr/arraylist_new/raise/exit` | — (thin) |
| **std.net_io** | network I/O (future placeholder) | — (future) | — |
| **std.types** | reflection | `mxs_typeof` | `is_instance_of`; `attributes_of` (progress16 D2) |
| **std.time** | clocks (already working) | `mxs_time_now/ms/ns` | (future `Date`, per Mux) |

Naming fix: `std/io.mxs` currently imports `std._fileio` but the file is `std/_file.mxs` — pick
`_file` consistently (progress18 §3).

## Decisions (proposed — F-tagged items are forks Mux should confirm)

- **D1 — fd-in-MXInteger I/O model.** A file descriptor is a small `MXInteger`; the layer-1 surface is
  raw POSIX (`mxs_sys_open/read/write/close/lseek/fsync` in `src/_std/system.cpp`), each returning a
  fresh owned `MXObject*` (an `MXInteger`/`MXString`, or an `MXError` on failure — so layer-2 can
  `match`/raise). No new core MXObject subtype (rejected `MXFile`/opaque-`FILE*` as over-built for v1).
  Matches the established `mxs_time_*` pattern.
- **D2 — stdout/stderr/stdin = fds 1/2/0; stdlog = a policy alias to stderr** (reconfigurable in mxs to
  any `FileStream`). "stdout/stderr/stdlog 本质上都是文件" → once they are `FileStream`s over fds,
  redirecting stdlog to a file is just constructing a different `FileStream`.
- **D3 — std.io layer-2 is a `FileStream` class in pure mxs** (mxs has working classes). Buffered
  `write/flush/read/close` over `std.system` fds; `println/print/input` sit on top. `std._file` holds
  `FileStream`; `std.io` re-exports the streams + the print surface.
- **D4 (F1 — FORK) — single stdout writer.** Today `mxs_print`/`mxs_println` write libc `stdout`
  directly (`MXFormat.cpp:191-207`); a layer-2 `FileStream` over fd 1 buffers separately → interleave
  hazard. Options: (a) re-point `mxs_print/println` to `mxs_sys_write(1, …)` so there is one path;
  (b) keep the C fast path and have layer-2 `print` defer to it. **Recommend (a)** for a coherent
  self-hosted io. Confirm.
- **D5 (F2) — OS constants via `mxs_sys_const(name) -> int`** (portable) rather than hardcoding Linux
  `O_*`/`SEEK_*` numbers in `.mxs`. Recommend the accessor.
- **D6 — builtins is auto-imported.** `std.builtins` (`str/repr/arraylist/raise/exit`) is injected into
  every program's scope without an `import` (the one exception to import-gating, progress13 D2). The
  driver/shell prepend it; everything else stays import-gated.
- **D7 (F3 — FORK, DEFER) — map/filter/reduce need first-class function values**, which the language
  lacks (functions resolve by name in codegen `funcs`, not as values). EXCLUDE from v1 std.array;
  revisit when higher-order functions land. v1 std.array = pop/insert/remove/contains/index_of/slice/
  reverse/clear + first/last/sum/is_empty.
- **D8 — list `+` gap.** `mxs_op_add` handles int/str/num but NOT list+list; `mxs_arraylist_concat`
  exists unbound. Either extend `mxs_op_add` to dispatch list+list, or expose `array.concat`. Recommend
  both (operator parity + the named function).

## Hard dependencies (this progress is blocked until these land)

1. **progress17** — `src/_std/` C tree + `std.bc`. std.system's `mxs_sys_*` and std.io's primitives live
   here; the reorg must exist first.
2. **progress18** — the import system must become *real* for the layer-2 vision. Three blockers found
   in the survey, ALL required by std.io:
   - function sibling calls resolve under every import form (progress18 D1, original);
   - **classes survive import** (the resolver merges only `FunctionDef`; a `class FileStream` in a
     module is silently dropped) — NEW, see progress18 D-CLASS;
   - **transitive/nested imports** (`std.io` must `import std.system;`; today nested import is
     rejected) — NEW, see progress18 D-TRANSITIVE;
   - module-level `let` singletons (`let stdout = FileStream(1,…)`) evaluated once at module load —
     verify, see progress18 D-MODLET.
3. **progress21** — module location (binary-relative `std/`, `--std-dir`, Python-like search) so the
   grown std tree resolves robustly when installed beside the binary.

## Open forks for Mux

- **F1 (D4)** single-stdout-writer: re-point `mxs_println` through `sys_write` vs keep libc.
- **F3 (D7)** map/filter/reduce deferred until first-class functions — confirm OK.
- **F6** `attributes_of` scope (instances vs modules) — carried from progress16 D2.

## Tasks (deferred until 17 + 18 + 21 land; recorded now per batch-record-first)

- [ ] task32 — std.system layer-1 (`src/_std/system.cpp`: `mxs_sys_*` + const) + `std/system.mxs`.
- [ ] task33 — std.io / std._file layer-2 (`FileStream` class; stdout/stderr/stdlog; println/input).
- [ ] task34 — std.string surface (new `mxs_str_*` primitives + `std/string.mxs` wrappers).
- [ ] task35 — std.array surface (new `mxs_arraylist_*` primitives + `std/array.mxs`; D7/D8).
- [ ] task36 — std.builtins auto-import (D6) + the `_file`/`_fileio` name fix.
(Tasks fully specced after the foundation lands; the C-primitive inventory + design live in the
2026-06-02 survey — `develop_log/2026-06-02/` agent log below.)

## Agent log
- 2026-06-02 [ai/opus] Recorded per batch-record-first. Grounded by a 5-agent parallel survey (C-ABI
  primitive inventory + relocation map; core.bc/JIT bitcode recipe; std.system/io design; string/array
  op inventory; import/namespace gap). Centerpiece of the std-체계 batch; sits on progress17 (C tree) +
  progress18 (real imports) + progress21 (location). Forks F1/F3/F6 surfaced to Mux, not guessed.
  NOT executed — foundation (17→18→21) implements first.
