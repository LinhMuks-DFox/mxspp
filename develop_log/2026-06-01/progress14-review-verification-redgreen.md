# Progress 14 — Review, verification & red/green hardening of the progress13 consolidation (Opus-led)
id: 2026-06-01/progress14
date: 2026-06-01
author: human+ai
status: done
refs: [2026-06-01/progress13, 2026-06-01/progress11, 2026-06-01/progress12]
supersedes:
commits: []
files:
  # The review/verification surface = everything progress13 changed (all UNCOMMITTED). See §"Change surface".
  - (review-only; this progress produces tests + fixes, see tasks)

## Goal
An **independent, high-capability review + implementation verification** of the *entire, uncommitted*
progress13 consolidation — binding semantics, legacy cleanup, OOP container methods, and the
import/stdlib restructure — plus a **red/green test corpus** that flushes hidden bugs. **Executed by
Opus in a fresh session** (the progress13 work was done by Sonnet + background sub-agents and has had no
independent review). End state: every progress13 change is verified correct (or fixed), covered by green
*and* red `.mxs` tests, the build + `ctest` + example sweep are green, and the work is ready to commit.

## Context / Motivation
Mux: this session ran on **Sonnet 4.6**; he wants **Opus** to do the review + implementation
verification. progress13 landed a large body of work — much of it via sub-agents — and **nothing is
committed**. It needs a capable, skeptical second pass before commit. Mux also asked (earlier) for a
**red/green bug hunt**: green exemplars that must run to a known output, and intentionally-wrong "red"
programs that must be rejected/panic with the *right* diagnostic. This progress is that pass.

> **Heads-up for the reviewer:** the working tree had **large pre-existing uncommitted changes before
> this session** (e.g. `src/backend/codegen.cpp`, many `src/core/*` files — the unrecorded earlier
> "progress13 code" + the macOS/build work). So a plain `git diff HEAD` is **not** a clean isolation of
> progress13's edits. Use the per-task file lists below + the progress13 Agent log as the authoritative
> "what changed and why", not the raw diff size.

## Build & verify (MANDATORY constraint)
The host `build/` is configured with a fragile recipe (vendored LLVM 20 + libc++ + an LLVM-20 bitcode
emitter via the `MXS_BC_CXX` cache var). **Rebuild ONLY with `ninja -C build`** (incremental; auto-re-runs
cmake, preserves cache vars). **NEVER** `python3 rebuild.py --clean` / reconfigure / delete `build/` — it
wipes the host config and the project won't rebuild. Smoke a program with `build/bin/mxs run-core <f.mxs>`;
REPL with `build/bin/mxs shell`; tests with `ctest --test-dir build --output-on-failure`.

## Change surface to review (all uncommitted; per progress13 task)
- **task14 (docs):** `docs/syntax.md` (NEW, authoritative), `syntax.ebnf` (v2.0), `docs/basic_syntax.md`
  (drift fixes). Verify the doc matches the *actual* grammar after all the code changes below.
- **task15 (binding semantics):** `src/backend/codegen.cpp` — per-scope `scopeNames` set; `bind()` errors
  on same-scope redeclaration; nested-block shadowing still legal. `src/shell/shell.cpp` — REPL is
  **strict** (re-`let` errors) + a `:reset` command.
- **task16 (legacy cleanup):** `include/mxspp/frontend/grammar.hpp` + `tokenizer.h` — `raise_expr`/`K_RAISE`
  removed; `src/core/builtin_func.{cpp,h}` deleted + dropped from `src/core/CMakeLists.txt`; the
  `src/runtime/*` + `src/frontend/ast.cpp` + `test/runtime_test.cpp` deletion finalized.
- **task20 (OOP container methods):** `src/backend/codegen.cpp` — built-in method dispatch table
  (`xs.append(v)`→`mxs_arraylist_append`, `xs.len()`→`mxs_len`, `xs.get(i)`→`mxs_index_get`);
  `src/driver/main.cpp` — `append`/`len` free-fn bindings removed; examples migrated; `docs/syntax.md` §3.1.
- **task17+task18+task19 (import system + retire kCorePrelude):** import AST/parser/grammar (`.{...}`
  selective + `as`)/resolver/loader; codegen module-qualified call resolution; `std/io.mxs` + `std/time.mxs`;
  **`kCorePrelude` deleted** from `src/driver/main.cpp`; ALL examples migrated to `import`; REPL auto-import.
  **Verify against the final state recorded in the progress13 Agent log** (this piece was the last to land
  and the largest — scrutinize it hardest).

## Tasks
- [x] [task21 — Code review of the progress13 diff](tasks/task21-progress13-code-review.md) — DONE (7-dimension adversarial review; every risk item has a verdict; bugs fixed — see Agent log)
- [x] [task22 — Implementation verification + red/green corpus + bug hunt](tasks/task22-verification-redgreen.md) — DONE (`test/corpus/` 19 green + 15 red, wired into ctest; all bugs fixed)

## Risk areas / review checklist (scrutinize these)
1. **ARC correctness (highest risk).** task20's built-in method dispatch and the variadic/import call
   paths must release every owned operand on *all* control-flow paths (no leak, no double-free). Verify
   via the `MXPopulationManager` live-object baseline (core_test) AND by running every demo. Re-check the
   progress11 ARC protocol (callee-owned user calls vs caller-owned foreign/runtime calls).
2. **Redeclaration `scopeNames` balance.** The set must push/pop/clear in lockstep with EVERY `scopes`
   lifecycle site (block enter/exit, function entry/exit, ctor, method, match-arm manual pops). A missed
   site → stale names → false-positive "redeclaration" across functions, or a leak. The task15 agent
   claimed it paired all 6 `scopes.clear()` sites — verify each.
3. **Import-gating actually enforced.** A program with NO `import` must NOT resolve any stdlib name
   (`println`, `now`, …). Confirm the resolver/merge can't accidentally leak names; confirm `kCorePrelude`
   is truly gone (`grep -rn kCorePrelude src` empty).
4. **Qualified vs selective imports.** Both `import std.io;`→`io.println(...)` and `import std.io.{println};`
   →`println(...)` work; `as` renames; the codegen module-namespace call path doesn't mis-route a real
   method call `value.m()` as a module call (collision if a variable shares an imported namespace name).
5. **REPL:** strict redeclaration + `:reset`; the auto-import convenience (Mux to confirm — flag it); the
   known **I7** limitation (assignments to existing bindings don't persist across REPL lines — is that
   acceptable, or fix now?).
6. **task20 routing-by-name gap:** `42.append(1)` lowers to `mxs_arraylist_append(42,1)` and **no-ops at
   runtime** (the `as_list` cast fails silently) instead of erroring. Decide: acceptable for v1, or add a
   guard/diagnostic?
7. **I8 — bare `{ … }` is not a statement.** Decide whether ad-hoc block scopes should be allowed
   (grammar change) or this is intended.
8. **I9 — `oop_vector.mxs`** reassigns an immutable `let a` → fix the example (`let mut a`) or make it a
   deliberate red case.
9. **Docs match reality.** After all code changes, re-verify `docs/syntax.md` (esp. §3.1 methods, §7
   import) and `syntax.ebnf` still describe the *implemented* behavior.

## Open / TODO (carry-over)
- REPL environment redesign (persist mutable bindings across lines — I7). `op_symbol` completeness
  (`operator**`/`[]`). `set`/`concat` container methods (deferred in task20). Static receiver-type method
  resolution (the §6 routing-by-name gap). Inheritance/generics/f-strings/file-IO (pre-existing).
- Commit strategy: once verified, propose a commit (or a small series) of the progress13 work — Mux reviews.

## Agent log
- 2026-06-01 [ai/sonnet] Wrote this progress + task21/task22 as the **handoff for an Opus session** to
  review + verify the progress13 consolidation (Mux: this session is Sonnet; Opus does the review). Snapshot
  of progress13 status at handoff: task14/15/16/20 DONE & verified (by Sonnet + sub-agents); task17+18+19
  (import system + kCorePrelude retirement) was dispatched to a background sub-agent and is the last piece
  landing — its final state is in the progress13 Agent log. Nothing committed. Build was green at last check.
- 2026-06-02 [ai/opus] **task21 + task22 executed (Opus). Review + verification complete; all confirmed
  bugs fixed; corpus added; ready to commit.**

  *Method.* First confirmed the import subsystem (task17/18/19) actually landed and works: all three
  forms (`import std.io;` → `io.println`, `import std.io as o;`, `import std.io.{println};`) run; import-
  gating holds; `kCorePrelude` gone; unresolved/selective-missing imports give clean errors. Then ran a
  **7-dimension adversarial code review as a background workflow** (22 sub-agents: a reviewer per
  dimension — ARC, scopeNames balance, import-gating/routing, import internals, binding/REPL,
  built-in methods, docs — each finding then independently verified by a skeptic that tried to refute it
  with a fresh repro). I read the hot code paths (`codegen.cpp` dispatch/ARC/scopes, `imports.cpp`,
  `main.cpp`, `shell.cpp`) myself to judge the findings, then fixed serially keeping `ninja -C build` +
  `ctest` green.

  *Risk-checklist verdicts (progress14 §1–9).*
  1. **ARC** — normal paths correct (built-in dispatch, variadic, user-method, ctor/dtor, scope-exit
     all release exactly once — re-verified post-fix with a dtor-counting program). Two small leaks
     found + fixed (below). break/continue non-local-exit leak stays a documented v1 gap.
  2. **scopeNames balance** — balanced at every `scopes` lifecycle site. Hardened: the defensive
     `!scopeNames.empty()` guard asymmetry (could mask a future desync) replaced with a lockstep
     `assert(scopes.size()==scopeNames.size())` in pushScope/popScopeRelease/match-pop.
  3. **Import-gating** — real (no stdlib name without `import`); `kCorePrelude` truly gone. ✓
  4. **Qualified vs method routing** — **BUG (fixed):** a local var named like a namespace was misrouted.
  5. **REPL** — strict redecl + `:reset` correct; auto-import is documented ergonomics (kept; flagged).
     I7 confirmed broader than documented (plain `a=10` and `xs.append()` also not persisted) — kept as
     the documented REPL-redesign carry-over (a fix = persist an environment, out of scope).
  6. **§6 routing-by-name** — `42.append(1)` is a safe silent no-op (no UB); `42.len()`/`42.get()` return
     a TypeError *value*. Decision: **accept for v1 + pin with a test** (`green/probe_wrong_receiver`).
     Static receiver typing remains the proper fix (carry-over).
  7. **I8 bare-block** — kept disallowed for v1 (no grammar change; "no new features" boundary); pinned
     as `red/bare_block`. Candidate feature for Mux.
  8. **I9 oop_vector** — fixed the example (`let mut a`) and corrected its expected-output comment.
  9. **Docs vs reality** — fixed the stale bits (below).

  *Bugs found + fixed (all confirmed by independent verification + a repro).*
  - **CRITICAL — method-name collision → null-vtable deref → SIGSEGV.** Dispatch chose the user-vtable
    path by method *name* alone (`selectors->count(name)`), a whole-program untyped set. If ANY class
    defined `len`/`get`/`append` (or any name) and that name was called on a built-in list/string (or
    any non-instance, e.g. `42.userMethod()`), codegen took the vtable path and dereferenced a null
    classinfo. **Fix** (`codegen.cpp`): evaluate the receiver once, then for a selector name branch at
    runtime on `mxs_object_classinfo(recv) != null` — instance → vtable; non-instance → the built-in
    symbol if the name+arity fit (so `xs.len()` still works even when a class also defines `len`), else
    a clean `TypeError` via the new runtime `mxs_method_missing`. Pinned by `green/method_name_collision`.
  - **HIGH — `return` inside a match-arm block emitted IR after the terminator** (verifier failure /
    uncompilable). **Fix:** `blockValue()` now `break`s the statement loop once the block is terminated,
    so no release/nil is emitted after a `ret`. Pinned by `green/match_return`.
  - **HIGH — local variable not shadowing an imported namespace:** `import std.io; let io = X; io.m()`
    misrouted `io.m` to a (nonexistent) module function. **Fix:** the module-routing guard now also
    requires `!locals.count(name)` — a bound local shadows the namespace. Pinned by `green/ns_local_shadow`.
  - **HIGH — duplicate `@@foreign` symbol → `mxs_println.1` JIT failure** (double import, or one module
    under two aliases). **Fix:** the compile_core decl pre-pass dedups external declarations by symbol
    (`foreignBySym`), so all bindings to one C symbol share one LLVM declaration. `import_two_aliases`
    now works (green). Resolver also now **rejects re-binding a namespace** (`import std.io; import
    std.io;` or two modules `as x`) with a clear diagnostic — `red/double_import`.
  - **HIGH — `for` loop variable ignored mutability:** `for v` did not reject assignment (silent no-op)
    and `for mut v` did not work. **Fix:** the loop var is tracked in `localMut` and the cell is created
    with `fs->isMut`. `green/for_mut` + `red/for_immutable`.
  - **MEDIUM — non-recursive import silently dropped a module's own `import`** → misleading "unknown
    function". **Fix:** the resolver now errors clearly that transitive imports are unsupported.
  - **LOW — match leaked its initial `nil()` placeholder** when an arm matched. **Fix:** release the
    slot's old value before storing the arm value.
  - **LOW — field assignment on a non-instance leaked the rhs.** **Fix:** `mxs_set_attr` releases `v`
    on the non-instance branch (the field never adopted it).

  *Refuted / accepted as-is:* break/continue scope leak (documented v1 gap, not progress13's); REPL
  auto-import (documented ergonomics); whitespace-around-fqdn-dots (accept); the "unknown method" vs
  "unknown namespace" diagnostic nicety (accept).

  *Red/green corpus (task22 deliverable).* `test/corpus/` — **19 green** (must run to a known `.out`,
  rc 0) + **15 red** (must be rejected with the diagnostic substring in `.err`, rc≠0), driven by
  `test/corpus/run_corpus.sh` and wired into ctest as the `corpus` test. Covers bindings, container/
  string methods, match, operators, str/repr/format, OOP+ARC, the full import system, and every fix
  above as a regression.

  *End state.* `ninja -C build` clean; `ctest` 3/3 (frontend_test, core_test, **corpus 34/34**); 22/22
  `example/examples/*.mxs` at expected rc (`oop_vector` fixed → now passes; `core_raise` rc 2 panic
  demo); clang-format clean over changed lines; ARC population baseline (core_test) green + dtor-count
  spot check clean. Files touched: `src/backend/codegen.cpp`, `src/core/MXOps.cpp`,
  `src/frontend/imports.cpp`, `docs/syntax.md`, `syntax.ebnf`, `example/examples/oop_vector.mxs`,
  `test/CMakeLists.txt`, `test/corpus/**` (new).

  *Escalations / decisions for Mux (none blocking the commit):* (a) I8 bare-block scopes — allow via a
  grammar rule, or keep disallowed (current)? (b) §6 — make `42.append` raise a TypeError to match
  `len`/`get`, or keep the documented no-op until static receiver typing lands? (c) REPL I7 redesign
  (persist a mutable environment). All left as carry-over per the "no new features" boundary.
