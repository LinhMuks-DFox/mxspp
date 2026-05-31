# Progress 09 — Object model: real C++ types + left/r-value (architecture)

id: 2026-05-31/progress09
date: 2026-05-31
author: human+ai
status: active
refs: [2026-05-31/progress05, 2026-05-31/progress07]
supersedes: 2026-05-31/progress05 (its "Object model" section — the flat tagged-union runtime value)
commits: []
files:
  - include/mxspp/core/*.h, src/core/*.cpp   # the real type classes (MXInteger, MXString, …)
  - src/runtime/runtime.cpp                  # becomes a thin extern "C" facade (or moves into core)
  - src/backend/codegen.cpp                  # emit calls to the per-type extern "C" ABI; left-values
  - docs/object_model.md                     # (to write) the authoritative design

## Why this exists
The object model in [progress05](./progress05-runtime-and-stdlib.md) used a **flat tagged-union**
`MXObject` (a `tag` + `union { i64; double; char*; void*; }`) living entirely in `runtime.cpp`,
disjoint from the `core::MXObject` class hierarchy (whose `MXString`/`MXNumeric`/… were empty
skeletons). That was a stand-in to get the pipeline runnable; it **cannot** express what Mux needs
(rich `MXInteger` with bignum + `int_type`, real `MXString`, C++ integration). This progress records
the canonical design and supersedes the flat model.

## Decisions (Mux, 2026-05-31)

### D1 — The type system and operation system are implemented in C++; every base type is a real class
- Every MXScript base type is a real C++ class inheriting `core::MXObject`: `MXInteger`,
  `MXString`, `MXFloat`, `MXBoolean`, `MXNil`, `MXArrayList`, … (per docs/develop_rule.md:
  RTTI, `clone()`, `unique_ptr` ownership, move, borrow). **The C++ implementations must be
  COMPLETE** (not stubs) — MXScript needs full C++ integration to grow its ecosystem.
- The flat tagged-union value is removed; the runtime value IS a `core::MXObject*` to a real typed
  subclass.

### D2 — Left-value / r-value model
- An **r-value** is an actual object instance (`MXInteger`, `MXString`, …), a `core::MXObject`.
- An identifier/binding is a **left-value** that holds the address of an r-value, and carries
  mutability. `let` ⇒ **immutable** left-value; `let mut` ⇒ mutable.
- Worked example — what `let x = 3; x += 3;` lowers to (C++ semantics):
  ```cpp
  // let x = 3;
  auto* x = mxs::make_immutable_left_value(MXInteger::from_literal("3"));
  // x += 3;  ->  update x's r-value with (x's r-value).add(3)
  x->rvalue_update( x->rvalue->add(MXInteger::from_literal("3")) );
  // Error! an immutable left value cannot update its r-value or reference.
  ```
- So the runtime has a left-value wrapper with: `rvalue` (the held `MXObject*`),
  `rvalue_update(MXObject*)` (reassign — errors on immutable), and a mutability flag. Binary
  operators (`add`, `sub`, …) are **methods on the r-value object** returning a new `MXObject`.

### D3 — Two-layer API per type: C++ API + `extern "C"` ABI
- Each type exposes a normal C++ API **and** a stable `extern "C"` ABI over it. Benefits:
  - To extend the implementation, write native C++ and surface it via `extern "C"` — stable ABI.
  - `@@foreign` lets mxs pin exactly which C ABI a call uses (docs/ffi.md direct-call convention).
  - The type implementations compile to LLVM bitcode (lib LLIR); the JIT then optimizes across the
    **mxs-generated LLIR + the lib LLIR together** (cross-module inlining of `mxs_*` calls, etc.).
- Practical shape: the `extern "C"` shims (`mxs_*`) construct/operate on the real `core::MXObject`
  subclasses. `runtime.cpp` becomes (or is replaced by) this thin facade; the per-type C++ lives in
  `src/core/`. (Exact split — facade in runtime vs. per-type `extern "C"` in core — to settle in D-impl.)

### D4 — `MXInteger`: native big-number arithmetic
- `MXInteger` natively supports arbitrary-precision ("big number") arithmetic. Fixed-width
  representations (int8/16/32/64, uint8/16/32/64) auto-promote to **`UltraInteger`** (bignum) on
  overflow or for large literals (e.g. `2 ** 256`).
- New API: `int_size()` → sizeof(`MXInteger`) + allocated storage; `int_type()` → the underlying
  representation (`int8/16/32/64`, `uint8/16/32/64`, or `UltraInteger`).
- Needs the `**` power operator (add to the grammar; not present today). Bignum backend (own
  implementation vs. a vendored library) is OPEN — see below.

### D6 — Compilation model: naive LLIR records the full call chain; LLVM does all optimization + execution — Mux, 2026-05-31
- The frontend lowers mxs → AST → **deliberately naive, redundant LLIR** that *fully records the
  internal call chain* (every `MXInteger::add`/`mxs_*` call emitted explicitly, nothing folded).
- The module that reaches LLVM is **user-mxs LLIR + standard-library LLIR together** (the lib types
  compiled to bitcode and linked in), so the call chain is complete and visible.
- **LLVM owns the rest** — it "cleans up" the LLIR (inline the `mxs_*` shims into the real
  `MXInteger::add` bodies, DCE, const-fold, etc.) and then runs it (interpret or JIT/compile). That
  optimization + execution is LLVM's "dirty work", not the frontend's.
- Consequence for codegen: keep the emitter **simple and explicit** — do NOT pre-optimize, do NOT
  special-case; emit straight calls to the type ABI and let the linked lib bitcode + LLVM passes do
  the folding. This is exactly why the two-layer `extern "C"` ABI (D3) matters: it gives LLVM real
  bodies to inline across the mxs/lib boundary.

### D8 — Runtime object ownership: reference counting — Mux, 2026-05-31
- Cross-`extern "C"`/JIT object lifetime (incl. intermediate temporaries like the result of
  `b * c` in `a + b * c`) uses **reference counting**, not an arena or tracing GC (aligns with
  docs §3.3's rc guidance). Each `MXObject` carries a count; `retain()` adds a reference,
  `release()` drops one and frees at zero. The JIT-facing ABI is `mxs_retain` / `mxs_release`.
- Convention (to finalize in step ④): a function returning a new object returns it with one
  reference (the caller owns it); arguments are borrowed (not consumed); codegen releases
  temporaries once consumed; bindings (`MXLeftValue`) and containers (`MXArrayList`) retain what
  they hold and release on overwrite/destruction.
- Reconciling with `MXObjectOwned` (unique_ptr, develop_rule.md): an `MXObjectOwned` is one
  strong reference held via RAII. To make unique_ptr participate in rc, its deleter should call
  `release()` (so dropping the unique_ptr drops a reference) — to wire in step ④; until then a
  freshly-`new`'d object has count 1 and a plain `delete`/unique_ptr is equivalent to releasing
  the sole reference (no sharing yet), so the rc mechanism added now is safe and additive.

### D7 — Libraries are an LLIR + exported-symbols contract; any such language qualifies (incl. mxs) — Mux, 2026-05-31
- The integration boundary is **LLIR + exported, callable symbols** — not C++ specifically. Any
  language that lowers to LLVM IR and exports symbols usable from IR (C, C++, Rust, …) can, in
  principle, implement an mxs library; **mxs itself qualifies** (self-hosted libraries / bootstrap).
- This is why D3's `extern "C"` ABI matters as the *stable contract*, not as a C++ implementation
  detail: the linked-in lib bitcode (D6) just has to expose the agreed symbols. C++ is the first
  implementation language for the core types because it is ergonomic + RTTI/ownership fit
  develop_rule.md — but the architecture does not bake C++ in.

## Step ④ — codegen rewiring (in progress)
- **core.bc landed.** The core object types compile to a single LLVM bitcode `core.bc`
  (per-source `-emit-llvm` + `llvm-link`), built next to `runtime.bc`. Verified to export the type
  ABI (`mxs_int_add`, `mxs_str_concat`, `mxs_float_add`, `mxs_arraylist_*`, `mxs_lvalue_*`,
  `mxs_retain`/`mxs_release`, …). This is what the JIT links in so those symbols resolve and LLVM
  can inline across the mxs/lib boundary (D6). (Removed a stray unused `llvm/IR/Instruction.h`
  include from MXObject.cpp so the core sources are LLVM-free and lower to plain bitcode.)
- **End-to-end through the new model — works.** The JIT now links `core.bc` too (`jit::run` gained
  a `coreBcPath`); `backend::compile_core` is a new codegen path that emits the typed core ABI
  (int literals→`mxs_int_from_i64`, `+ - * / %`→`mxs_int_*`, generic calls), and a polymorphic
  print (`mxs_print_object`/`mxs_println_object` over `MXObject::repr()`) bound via the `@@foreign`
  `kCorePrelude` (no hardcoding). Driver: `mxs run-core <file>`. **Verified in Docker:**
  `run-core` of `println(2+3)`/`2+3*4`/`100-58` → `5` / `14` / `42` — real `MXInteger`s arithmetic
  + bitcode-linked print, JIT'd. (`-mno-outline-atomics` on the core.bc compile so AArch64 atomic
  RMWs are inline, not compiler-rt outline-helper calls the JIT can't resolve.)
- **Shutdown:** a one-shot `run-core` flushes and `_Exit`s after the program returns — JIT'd code
  can register `__cxa_atexit` handlers whose code lives in freed JIT memory (the standard ORC
  one-shot-runner teardown hazard); the REPL path returns through `jit::run` normally. (Proper
  fix — run the platform's deinitializers before teardown — is a later cleanup.)
- NEXT in ④: grow `compile_core` to variables (as `MXLeftValue`, retain/release per D8), control
  flow, the other types/operators (float/bool/str/list, comparisons, `**`, `[...]`/subscript),
  and `match`/Error; then retire the flat object-mode runtime + native numeric slice.

## Impact / sequencing
1. Rework `runtime.cpp` from the flat union into the `extern "C"` facade over real core types.
2. Implement the core types completely (start with `MXInteger` + `MXString`, then `MXFloat`/
   `MXBoolean`/`MXNil`, then `MXArrayList`). Each: C++ API + `extern "C"` ABI + RTTI + unit tests.
3. Left-value model: `make_immutable_left_value` / mutable; `rvalue_update`; enforce `let` vs
   `let mut`. Rework codegen so identifiers are left-values and ops call the r-value methods' C ABI.
4. The in-progress `ArrayList` (progress07, built on the flat model, **not committed**) is redone as
   the real `MXArrayList` core class.
5. `**` operator in the grammar; `int_type()`/`int_size()` methods (need method-call dispatch).

### D5 — Bignum backend: own implementation — Mux, 2026-05-31
- `UltraInteger` is implemented **from scratch** (no GMP/Boost): self-contained (matches the
  LLVM/PEGTL vendoring philosophy), full control over the representation + `int_type`, no license
  concerns. Plan: magnitude as `std::vector<std::uint64_t>` limbs + sign; schoolbook add/sub/mul,
  long division; fixed-width (int8/16/32/64, uint8/16/32/64) fast path that promotes to the limb
  representation on overflow. (Performance/correctness are ours to own; tune later.)

## Open questions (to resolve within implementation)
- Exact C++ shape of the left-value wrapper and where it lives (core vs runtime); ownership across
  the `extern "C"`/JIT boundary (develop_rule.md `unique_ptr` ownership vs. raw pointers the JIT'd
  code holds — who owns/frees, and how `clone()` fits).
- How this composes with the Error model (match-based, see the diagnostics/errors progress) and with
  containers (progress07).

## Agent log
- 2026-05-31 [ai] Recorded the object-model architecture per Mux: real C++ type classes (complete,
  not stubs) inheriting `core::MXObject`; left-/r-value model with immutable-by-default left values
  and `rvalue_update`; a two-layer (C++ + `extern "C"`) API per type so the JIT optimizes across mxs
  LLIR + lib LLIR; `MXInteger` with native bignum (`UltraInteger`) + `int_type`/`int_size` + `**`.
  Supersedes the flat tagged-union object model (progress05). No code yet — pending the bignum-backend
  decision and a confirm on the design. The uncommitted flat `ArrayList` work will be redone as a
  real `MXArrayList`.
- 2026-05-31 [ai] Implemented **MXInteger** (commit 7adc606): complete C++ class + self-written
  bignum (sign + base-2^64 limbs; add/sub/mul, bit long division, square-and-multiply pow) +
  from_literal + int_type/int_size + extern "C" ABI + RTTI; div/mod-by-zero & bad-literal return
  MXError. Unit-tested (core_test, incl. 2**256). Bignum backend = own (D5).
- 2026-05-31 [ai] Implemented **MXString** (sequencing step ②): UTF-8 std::string value, immutable;
  concat / length (→MXInteger) / cmp / equals / hash / repr + extern "C" ABI (mxs_str_*) + RTTI.
  Unit-tested. Next: MXFloat / MXBoolean / MXNil, then the left-value model and codegen rewiring.
- 2026-05-31 [ai] Implemented **MXFloat / MXBoolean / MXNil** — completing the primitive scalar
  types as real C++ core::MXObject subclasses (each: value + ops + repr/equals/hash + extern "C"
  ABI + RTTI; MXFloat /0 → MXError). All in mxs::builtin. core_test now 12 cases, ctest 3/3.
  Scalar object model done. NEXT (step ③): the left-value model (make_immutable_left_value /
  rvalue_update / mutability) and codegen rewiring to emit the type ABI + link core as bitcode.
- 2026-05-31 [ai] Implemented the **left-value model** (D2): `mxs::core::MXLeftValue` (a binding
  cell owning its r-value via unique_ptr + a mutability flag) + `mxs::make_immutable_left_value` /
  `make_mutable_left_value` + extern "C" ABI (mxs_lvalue_*). `rvalue_update` on an immutable (`let`)
  binding returns an MXError; on a mutable (`let mut`) binding it frees the old value and takes the
  new (the develop_rule.md ownership default per the open Q — bindings own their r-value).
  Unit-tested (the `let x=3; x+=3` → error vs `let mut` → 6 example from D2). Also implemented
  **MXArrayList** (progress07). NEXT, the big step ④: rewire codegen to emit these type ABIs +
  represent variables as MXLeftValues + compile core to bitcode linked for JIT cross-module opt
  (D6); plus `match`/Error lowering and the `**`/`[...]` grammar. Still needs Mux's call on the
  intermediate-temporary ownership (non-binding operation results) across the JIT boundary.
