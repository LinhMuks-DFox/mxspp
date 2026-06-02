# Progress 13 — Consolidation: authoritative syntax, import-gated stdlib, OOP container methods, binding correctness, legacy cleanup
id: 2026-06-01/progress13
date: 2026-06-01
author: human+ai
status: done
refs: [2026-06-01/progress11, 2026-06-01/progress12, 2026-05-31/progress05, 2026-05-31/progress06, 2026-05-31/progress07]
supersedes:
commits: []
files:
  - docs/syntax.md (NEW)                       # authoritative syntax + divergence analysis
  - syntax.ebnf                                # refreshed v2.0 to match the implementation
  - docs/basic_syntax.md                       # fix tutorial drift (comments / raise / list typing)
  - include/mxspp/frontend/grammar.hpp         # remove orphaned raise_expr + K_RAISE (legacy)
  - src/backend/codegen.cpp                    # same-scope redeclaration error; builtin-method dispatch
  - src/shell/shell.cpp                        # REPL redefinition policy
  - src/driver/main.cpp                        # retire kCorePrelude → import-gated std modules
  - std/io.mxs, std/time.mxs, std/...          # the std modules (C++ impl + @@foreign markers)
  - include/mxspp/core/MXTime.{h}, src/core/MXTime.cpp   # already present; wire as std.time
  - src/core/builtin_func.{h,cpp}              # remove empty stub
  - src/frontend/ast.cpp, src/runtime/*, test/runtime_test.cpp  # finalize legacy deletion

## Goal
A correctness + structure consolidation pass over MXScript, driven by problems Mux found while
exercising the language. Five threads: (1) make the **syntax documentation authoritative** and
reconcile the drifted `syntax.ebnf`; (2) restructure the **standard library to be import-gated**
(C++ implementation + `@@foreign` markers living in `std/*.mxs`, reachable only via `import` — nothing
in scope without it); (3) add a **`time`** std module; (4) move **container operations to OOP methods**
(`xs.append(v)`, not the free function `append(xs, v)`); (5) fix **binding-semantics** holes
(immutable reassignment, same-scope redeclaration). Plus a legacy-code sweep. Boundary: this is a
consolidation pass — no new language features beyond what these fixes require (the import system is the
one large new subsystem; f-strings, generics, inheritance remain out).

## Context / Motivation
After OOP v1 (progress11) and stdio v1 (progress12) the language runs real programs, and Mux began
using it in earnest. That surfaced a cluster of design-vs-implementation gaps and correctness bugs,
recorded as Issues below. Mux's directive: **encode the findings as this progress + issues, break them
into tasks, then execute the tasks** — the standard bug-handling workflow, not ad-hoc fixes.

Meta-finding: the uncommitted working tree already contained un-recorded work tagged "(progress13)" in
code comments — per-binding mutability enforcement (`codegen.cpp` `localMut` + the immutable-assignment
error) and the `MXTime` C++ primitives — written without a progress doc. This progress retroactively
records that work and folds in the newly-found problems.

Grounding facts verified in-tree (2026-06-01, against a fresh `ninja -C build`):
- `let a = 3; a = 6;` now **correctly errors** (`cannot assign to immutable binding 'a'`) — the fix was
  already in `codegen.cpp:380-387` but the previously-shipped binary was stale; rebuilt + verified.
- `let a = 4; let mut a = 4;` in one scope is **silently accepted** (`bind()` overwrites
  `locals`/`localMut`, no same-scope check) — Issue I2.
- `xs.append(4)` → `core-codegen: call to unknown method 'append'`; only `append(xs, 4)` (free function)
  works — Issue I3.
- The `@@foreign` stdlib bindings are a hardcoded C++ raw string `kCorePrelude` in `main.cpp:34-45`,
  auto-prepended to every program; `std/io.mxs` exists but is **not loaded** by any path — Issue I4.
- `MXTime.cpp/.h` (`mxs_time_now/ms/ns`) are compiled into `core` + `core.bc` but unbound/unused — I5.
- `grammar.hpp` still defines `raise_expr` (line ~248) + keyword `K_RAISE` (line ~86), neither reachable
  (not in `primary_expr`, not in `reserved_word`) — dead code, Issue I6. `builtin_func.cpp` is empty.

## Decisions

### D1 — `docs/syntax.md` is the authoritative syntax reference; `syntax.ebnf` is refreshed to match — Mux, 2026-06-01
- Decision: write `docs/syntax.md` from the *implemented* grammar (`grammar.hpp`), including a section
  that answers "is `syntax.ebnf` still my original design?" by enumerating every divergence. Refresh
  `syntax.ebnf` to v2.0 (matching the implementation, with a header pointing at `docs/syntax.md` as
  authoritative). Fix the known drift in the `docs/basic_syntax.md` tutorial.
- Why: Mux observed the implementation had diverged from the original design and wanted a single
  trustworthy reference. The original `syntax.ebnf` (v1.0) had drifted (list literals, `**`, variadics,
  bodyless `func`, dropped `raise` expression).
- Impact: docs only (+ the legacy `raise_expr` removal is D6). `docs/syntax.md` marks each construct
  `[parses + runs]` vs `[parses only]` so the doc doubles as an implementation-status map.
- Divergences recorded (see `docs/syntax.md` §9): added list literals, `**`, `...rest` params, bodyless
  `func … ;`, the `reserved_word` guard; removed the `raise` expression; PEG ordered-choice semantics;
  `op_symbol` cannot express `operator**`/`operator[]` though the runtime reserves those vtable slots.

### D2 — The standard library is import-gated; nothing is in scope without `import` — Mux, 2026-06-01
- Decision: stdlib functions are NOT globally available. A program reaches them only via `import`
  (e.g. `import std.io;`). The two-layer model stands (docs/ffi.md, type_system §8): C++ implements the
  fast-dispatch `mxs_*` C-ABI; an `std/*.mxs` module declares the `@@foreign` bindings + any mxs-level
  wrappers. The hardcoded `kCorePrelude` in `main.cpp` is **retired** in favor of real `std/*.mxs`
  modules loaded by an import system.
- Why: Mux: "stdio 是一个需要 import 的模块，我不希望没有 import 的东西出现在代码可用范围里." Implicit-global
  builtins violate that; they also make the stdlib's structure invisible and un-versionable. Putting the
  `@@foreign` markers in `std/*.mxs` is exactly the structure Mux wants (C++ impl ⇄ mxs binding).
- Impact: requires an **import/module subsystem** (resolve `fqdn` → an `std/<path>.mxs` file, parse it,
  merge its top-level decls into the compilation unit, with name scoping). Touches the parser (an
  `Import` AST node + driver wiring), the driver (drop `kCorePrelude`; resolve a std search path), and
  every example/test that relied on implicit `println`/etc. (they gain `import std.io;`).
- OPEN sub-decisions for Mux (flagged; see Issues I-OPEN): (a) **resolution mechanism** — discover
  `std/*.mxs` on a filesystem search path next to the binary, vs. baking std sources into the binary;
  (b) **namespacing** — does `import std.io;` bind `println` unqualified, or as `io.println` (with
  `import std.io as io;` / selective `import std.io.{println};`)?; (c) which primitives, if any, are
  true always-in-scope builtins (Mux's stance implies *none* — even `print` needs an import).
- **RESOLVED (Mux, 2026-06-01):** (b) namespacing = **qualified-by-default + selective-unqualified**
  ("1+3"). `import std.io;` binds the module namespace `io` → call as `io.println(...)`; `import std.io
  as foo;` renames it; `import std.io.{println, format};` brings *those* names into scope unqualified
  (needs a grammar extension for the `.{ id_list }` form). Both forms coexist. (c) **No always-in-scope
  builtins** — even `print`/`println` require an import. (a) resolution = **filesystem search path**
  next to the binary (the existing `find_bc()` pattern), unless Mux later objects. Note: qualified
  `io.println(...)` needs the frontend to tell a *module-qualified call* apart from a *method call* on a
  value — task17 owns that.
- Alternatives considered: keep `kCorePrelude` but document it (rejected — violates "no implicit
  globals"); a `prelude` module auto-imported unless opted out (rejected — still implicit).

### D3 — `time` standard module (`std.time`) over the existing C++ `MXTime` — Mux, 2026-06-01
- Decision: ship `std/time.mxs` exposing the timestamp primitives already implemented in C++
  (`mxs_time_now`/`mxs_time_ms`/`mxs_time_ns`) as `@@foreign` bindings, reachable via `import std.time;`.
- Why: Mux asked for a `time` module; the C++ leaves exist and are unused — this is the smallest real
  module to validate D2's import structure end-to-end.
- Impact: depends on D2 (the import loader). `std/time.mxs` + a demo + a test; no new C++ (MXTime stays).
  Naming of the mxs surface (`now()`/`monotonic()`/…) is part of task design.

### D4 — Container/string operations are OOP methods, not free functions — Mux, 2026-06-01
- Decision: `xs.append(v)`, `xs.len()` / `len(xs)`-policy, `s.len()`, etc. — built-in container and
  string operations are invoked as **methods on the receiver**, not as free functions in global scope.
  `append(xs, v)` as a free function is removed from the surface (it violates the OOP model).
- Why: Mux: "list 的方法，比如 append，居然是直接暴露在公共空间的？ … 严重违背了我的 oop 的设计." Methods belong
  to the type.
- Impact: codegen's method-call path (`codegen.cpp:434-464`) currently dispatches `recv.m(args)` only
  through a *user-class* vtable (`mxs_object_classinfo(recv)` is null for built-ins → it would crash).
  Add a **built-in method dispatch table**: when `recv.m(args)` has no user selector but `m` is a known
  built-in method for the receiver's kind, lower it to the existing runtime symbol with the receiver as
  arg0 (`xs.append(v)` → `mxs_arraylist_append(xs, v)`; `xs.len()` → `mxs_len(xs)`). Remove `append`
  (and per the surface decision, `len`) from the stdlib's free-function bindings.
- OPEN sub-decision for Mux: the exact **method surface** per type (List: `append`/`len`or`size`/`get`/
  `set`/`concat`/…; String: `len`/…) and whether `len(x)` survives as a global builtin (Python keeps it)
  or becomes `x.len()` only. Default proposal: `x.len()` method form, no global `len`/`append`.
- **RESOLVED (Mux, 2026-06-01):** **all methods, no globals.** `xs.append(v)`, `xs.len()`, `"hi".len()`;
  the global space exposes neither `len` nor `append`. (List surface still to detail in task20:
  `append`/`len`/`get`/`set`/`concat`; String: `len`/….)
- Alternatives considered: give built-ins real `MXClassInfo`/vtables so the existing dispatch "just
  works" (heavier; deferred — a static codegen table is enough for v1 and keeps built-ins zero-overhead).

### D5 — Binding-semantics correctness: enforce immutability + reject same-scope redeclaration — Mux, 2026-06-01
- Decision: (a) assigning to an immutable `let` binding is an error — **done** (compile-time, verified).
  (b) Declaring a name already declared **in the same scope** (`let a …; let mut a …;`) is an error,
  mirroring C++ (`const int a; int a;` in one scope is illegal). Shadowing in a *nested* block stays
  legal.
- Why: Mux found both: `let a=3; a=6;` ran silently (stale binary; fix was already in source), and
  `let a=4; let mut a=4;` was silently accepted. Both break the language's stated immutable-by-default,
  single-declaration semantics.
- Impact: (b) needs a per-scope "declared names" set in `codegen.cpp` (`bind()` currently overwrites
  `locals`/`localMut` with no same-scope check). The flat `locals` map can't tell same-scope from
  outer-scope, so track names per active scope (parallel to the `scopes` cell stack).
- OPEN sub-decision for Mux (REPL policy): the REPL replays accumulated `let`s in one `main()` scope, so
  a same-scope-redecl error would make re-`let` at the prompt illegal. Options: (i) REPL **replaces** a
  prior `let` of the same name (dedup — standard REPL UX, stays usable); (ii) REPL errors like a real
  scope (matches Mux's C++ analogy literally; redefinition needs a `:reset`). Default proposal: (i).
- **RESOLVED (Mux, 2026-06-01):** (ii) **strict — the REPL errors on re-`let` like a real scope.** With
  the codegen redeclaration check, the REPL's replayed accumulated `let`s already produce the error for
  free (no replace-prior). Add a **`:reset`** REPL command that clears accumulated `lets` (and `defs`)
  so the user can redefine after a deliberate reset.
- Alternatives considered: enforce redeclaration only at runtime (rejected — silent today; compile-time
  is the right layer, matching the immutability check).

### D6 — Legacy cleanup — Mux, 2026-06-01
- Decision: remove dead/superseded code: the orphaned `raise_expr` rule + `K_RAISE` keyword in
  `grammar.hpp`; the empty `builtin_func.cpp` (0 lines) + its stub header; finalize the already-staged
  deletion of the old `src/runtime/` module + `src/frontend/ast.cpp` + `test/runtime_test.cpp` (clean up
  any lingering CMake/include references); reconcile the dead `std/io.mxs` (it becomes live under D2).
- Why: progress11/12 left "retire the legacy paths" as carry-over; the grammar carries unreachable
  rules; an empty translation unit is noise. Mux: "清理 legacy 代码."
- Impact: build-system + grammar/parser touch; must keep every demo + `ctest` green. Verify nothing
  references the removed symbols before deleting (esp. parser actions on `raise_expr`).

## Tasks
- [x] [task14 — Syntax docs + syntax.ebnf refresh + basic_syntax drift](tasks/task14-syntax-docs-and-ebnf.md) — done (docs/syntax.md, syntax.ebnf v2.0, basic_syntax drift fixed)
- [x] [task15 — Binding semantics: same-scope redeclaration error (+ REPL policy)](tasks/task15-binding-redeclaration.md) — DONE & verified (codegen `scopeNames` per-scope check; REPL strict + `:reset`)
- [x] [task16 — Legacy cleanup (raise_expr, builtin_func, runtime/ deletion, dead std/io)](tasks/task16-legacy-cleanup.md) — DONE & verified (raise_expr/K_RAISE gone incl. tokenizer.h; builtin_func removed; runtime/ deletion clean)
- [x] [task17 — Import / module system (resolve+load std/*.mxs)](tasks/task17-import-module-system.md) — DONE & verified (Import AST/parser/grammar `.{…}`+`as`; `imports.cpp` resolver; codegen module-qualified routing; all 3 forms run end-to-end)
- [x] [task18 — Stdlib restructure: retire kCorePrelude → import-gated std modules](tasks/task18-stdlib-import-modules.md) — DONE & verified (`kCorePrelude` deleted; `std/io.mxs` live; import-gating enforced; examples migrated; REPL auto-import)
- [x] [task19 — std.time module](tasks/task19-std-time-module.md) — DONE & verified (`std/time.mxs` → `now`/`now_ms`/`monotonic_ns` over `MXTime`; corpus `import_time`)
- [x] [task20 — Container/string ops as OOP methods (builtin-method dispatch)](tasks/task20-oop-container-methods.md) — DONE & verified (`xs.append(v)`/`xs.len()`/`"hi".len()`/`xs.get(i)`; free `append`/`len` removed; `set`/`concat` deferred)

## Issues / Gotchas
- **I1 (resolved-in-tree) — immutable reassignment ran silently.** `let a=3; a=6;` executed (printed 3)
  on the shipped binary. Root cause: the compile-time check (`codegen.cpp:380-387`) was present in
  source but the binary predated it (built 20:29 vs source 20:43). Rebuilt → now errors. Backstop:
  `MXLeftValue::rvalue_update` returns an `MXError` on an immutable cell, but codegen *discards* it
  (`releaseTmp(... update ...)`), so a runtime-only path would still be silent — fine while the
  compile-time check covers the static case; revisit if dynamic rebinding lands.
- **I2 — same-scope redeclaration accepted.** `let a=4; let mut a=4;` → no error; `bind()` overwrites.
  Fixed by task15. (Surfaced via the REPL, where it also caused incoherent state — see I7.)
- **I3 — built-in methods undispatched.** `xs.append(4)` → "call to unknown method 'append'"; the
  method-call path only knows user-class selectors and would null-deref on a built-in's classinfo.
  Fixed by task20.
- **I4 — stdlib is implicit-global + a dead std file.** `@@foreign` bindings live in the C++
  `kCorePrelude` string (`main.cpp:34-45`), auto-injected; `std/io.mxs` is never loaded. Addressed by
  task17/task18.
- **I5 — `time` C++ exists but unwired.** `MXTime.cpp/.h` compiled into core/core.bc, no mxs binding.
  Addressed by task19.
- **I6 — legacy/dead code.** Orphaned `raise_expr` + `K_RAISE` in `grammar.hpp`; empty
  `builtin_func.cpp`; staged-but-unfinished `src/runtime/` deletion. Addressed by task16.
- **I7 — REPL state is incoherent under rebinding/mutation.** The REPL persists only `let` lines
  (replayed in a fresh `main()` each eval); assignments to existing bindings do NOT persist, so
  `let mut a=4; a*=a;` then `a` prints `4` (the `*=` was a non-persisted statement). And accumulated
  duplicate `let`s stack in one scope (see I2). The redeclaration fix (task15) forces a REPL policy
  decision (D5 OPEN); the deeper "mutation doesn't persist" limitation is logged in Open/TODO (a REPL
  redesign — persist an environment instead of replaying `let`s — is out of this progress).
- **I-OPEN — RESOLVED (Mux, 2026-06-01):** (a) resolution = filesystem search path; (b) namespacing =
  qualified-default + selective `.{...}` ("1+3"); (c) all-methods, no global `len`/`append`; (d) REPL =
  strict redeclaration error + `:reset`. See the RESOLVED notes under D2/D4/D5. task17/18/19/20 unblocked.
- **I8 (found by task15 agent) — a bare `{ … }` block is NOT a statement.** `statement` =
  let/control/expr/assert/defer; a block only attaches to control-flow (`if`/`for`/…) or is a
  `block_expr` in expression position. So you cannot open an ad-hoc nested scope with bare braces
  (`{ let a = 2; }` at statement level → parse error). Candidate item for the progress14 red/green pass
  (decide whether bare-block scopes should be allowed).
- **I9 (found by task16 agent) — `oop_vector.mxs` is a buggy (untracked) example.** It reassigns an
  immutable `let a` (line ~23 `a = Vec2(5,5)` vs `let a` at ~20), so it now correctly FAILS the
  immutability check (I1) — a latent bug the rebuilt enforcement surfaced (good). 21/22 examples pass;
  fix the example (`let mut a`) or mark it a deliberate red case in progress14.

## Open / TODO (carry-over)
- **Next progress (progress14) — red/green test corpus + bug hunt (Mux, 2026-06-01).** Write a body of
  mxs programs: positive ("green") exemplars that must run to a known output, and negative ("red")
  programs that are *deliberately wrong* and must be rejected/panic with the right diagnostic. Use the
  pair to flush out hidden small bugs. This is its own progress once the consolidation tasks land.
- REPL redesign: persist a mutable binding environment across evals (so assignments stick), rather than
  replaying accumulated `let` text. Larger than this progress.
- `op_symbol` completeness: allow `operator**` / `operator[]` / `operator[]=` (grammar) to match the
  reserved vtable slots (`MXClassInfo`). Tracked from docs/syntax.md §6.2.
- Carry-over from progress11/12 unaffected here: inheritance, generics, f-strings, file IO, console
  input, type-conversion.

## Agent log
- 2026-06-01 [ai] Investigated the five threads; verified each Issue against a fresh build. Wrote
  `docs/syntax.md` (authoritative, with the full divergence analysis answering Mux's "is syntax.ebnf
  still my design?" — no, drifted) and refreshed `syntax.ebnf` to v2.0. Confirmed I1 is already fixed in
  source (rebuilt + verified). Recorded D1–D6 + Issues I1–I7 + the open decisions. Next: write task14–20,
  then execute the unambiguous ones (task14 docs, task15 redeclaration, task16 legacy) while surfacing
  the OPEN design decisions (D2/D4/D5 sub-decisions) to Mux before the import/stdlib/method tasks.
- 2026-06-01 [ai] Wrote task14–task20. **task14 DONE** (docs/syntax.md authoritative + divergence §9;
  syntax.ebnf refreshed to v2.0; basic_syntax.md drift fixed: `/* */` comments, `raise`-is-a-function
  banner, `List<string>` typing). **task15 + task16 dispatched to a background sub-agent** (per Mux:
  "启动 sub-agent 去完成这些任务") — it edits codegen.cpp/shell.cpp/grammar.hpp/CMake only, builds with
  `ninja -C build` (host config preserved), and verifies the repros; the orchestrator owns develop_log
  bookkeeping. task17–20 held BLOCKED pending Mux's D2/D4/D5 OPEN decisions (import resolution +
  namespacing; built-in method surface + fate of global `len`; REPL redefinition policy) — surfaced to
  Mux now so they can be dispatched next.
- 2026-06-01 [ai] **task15 + task16 DONE & verified** (background sub-agent + orchestrator). task15:
  added a per-scope `scopeNames` set in codegen `CoreGen` (lockstep with the `scopes` stack); `bind()`
  errors on a same-scope duplicate, nested-block shadowing stays legal. Verified: `let a=4; let mut a=4;`
  → redeclaration error; nested shadow (`if (true){ let a=2; …}`) → 2 then 1; `let a=3; a=6;` → still
  the immutability error; `ctest` 2/2; 21/22 examples at expected rc. task16: removed `raise_expr` +
  `K_RAISE` from grammar.hpp AND `tokenizer.h` (the agent found a dangling tokenizer ref — justified
  extra edit; `raise` now lexes as a plain identifier), deleted empty `builtin_func.{cpp,h}` + its
  CMake line, confirmed the staged `runtime/` deletion links clean. `grep raise_expr|K_RAISE|builtin_func`
  → empty; `ninja -C build` clean. **REPL policy corrected by the orchestrator** from the agent's
  default (replace-prior) to Mux's choice (**strict + `:reset`**): dropped the dedup helpers, the REPL
  now lets the redeclaration error fire on re-`let`, and `:reset` clears `lets`/`defs`. Verified:
  `let a=1; let a=2;` at the prompt → redeclaration error, `a`→1; `:reset` then `let a=2;` → `a`→2.
  Found I8 (bare-block-not-a-statement) and I9 (`oop_vector.mxs` buggy) — logged for progress14.
  **Not committed** (awaiting Mux review). Next: dispatch task17 (import system) → task18/19 → task20,
  serially (shared build/), per the now-RESOLVED D2/D4 design.
- 2026-06-01 [ai] **task20 DONE & verified** (background sub-agent). codegen method-call branch now has
  two paths: user-class selector → vtable (unchanged); else a static built-in-method table → direct call
  to the polymorphic runtime symbol with the receiver as arg0. Implemented `append`→`mxs_arraylist_append`
  (void→nil), `len`→`mxs_len` (+1), `get`→`mxs_index_get` (+1); ARC mirrors the foreign-call path
  (release every evaluated operand incl. receiver). **Deferred:** `set` (only `mxs_arraylist_set` exists —
  list-only, returns a borrow, doesn't fit the generic foreign-ARC model) and `concat` (two
  non-polymorphic symbols, unroutable by name in v1) — documented in a code comment. Removed the `append`
  and `len` `@@foreign` free-fn bindings from `kCorePrelude`; migrated `core_list.mxs`/`core_string.mxs`/
  `syntax_reference.mxs` to method form; updated `docs/syntax.md` §3.1. Verified: `xs.append(4)`→
  `[1,2,3,4]`, `xs.len()`→4, `"hi".len()`→2, `xs.get(1)`→20; free `append(xs,4)`/`len(xs)` now error
  ("unknown function"); for-in + `xs[i]` unaffected (they call the runtime directly); `ctest` 2/2;
  22 examples at expected rc; ARC population baseline green. Note: dispatch routes by method NAME only
  (no static receiver typing) — `42.append(1)` lowers to `mxs_arraylist_append` and no-ops at runtime
  rather than erroring (static method-resolution is out of v1 scope). `xs.append(v)` needs no `let mut`
  (mutates contents, not the binding). **Not committed.**
- 2026-06-01 [ai] **Principle from Mux (recorded):** "历史的错误应该被删除，不应该冗余进代码里" — when a
  design is superseded, DELETE the old construct in the same effort; don't keep it as a transitional
  fallback. This **corrects** the earlier "increment-1: don't touch the prelude" plan: `kCorePrelude` is a
  historical mistake (implicit-global stdlib) and is **deleted** as part of the import work, not preserved.
  So task17 + task18 now execute together toward that end state (import machinery → then retire
  `kCorePrelude` + migrate all examples + the REPL). Also a **decision-boundary correction**: rollout
  strategy (incremental vs big-bang) is an *execution* call I should own myself in auto mode, not put to
  Mux as a "decision" — only genuine design/scope forks (the `1+3` namespacing, all-methods, REPL policy)
  are his. (Memories: delete-legacy-no-redundancy, decision-boundary-automode.) An earlier import sub-agent
  came back read-only (writes/build denied — a transient permission state during an interrupt); Mux
  restored permissions, and the full import work (task17+task18, deleting `kCorePrelude`) was re-dispatched
  to a background sub-agent (two phases: machinery + std.io/std.time, then retire the prelude + migrate).
- 2026-06-02 [ai/opus] **task17/18/19 confirmed DONE & verified; progress13 closed.** The import
  subsystem landed (`include/mxspp/frontend/ast.h` `Import` node, parser, grammar `.{…}`+`as`,
  `src/frontend/imports.cpp` resolver, `src/driver/main.cpp` search path, codegen module-qualified
  routing). Verified all three forms run end-to-end, import-gating holds (no stdlib name without an
  `import`), `kCorePrelude` is gone, and `std.time` works. The independent Opus review +
  red/green verification of the **entire** progress13 consolidation is recorded in
  [progress14](./progress14-review-verification-redgreen.md) — it found + fixed a CRITICAL crash and
  several other bugs in the consolidation; see that Agent log. Build + `ctest` (now incl. the corpus)
  + 22/22 examples green.
