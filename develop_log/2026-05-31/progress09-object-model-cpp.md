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

### D7 — Libraries are an LLIR + exported-symbols contract; any such language qualifies (incl. mxs) — Mux, 2026-05-31
- The integration boundary is **LLIR + exported, callable symbols** — not C++ specifically. Any
  language that lowers to LLVM IR and exports symbols usable from IR (C, C++, Rust, …) can, in
  principle, implement an mxs library; **mxs itself qualifies** (self-hosted libraries / bootstrap).
- This is why D3's `extern "C"` ABI matters as the *stable contract*, not as a C++ implementation
  detail: the linked-in lib bitcode (D6) just has to expose the agreed symbols. C++ is the first
  implementation language for the core types because it is ergonomic + RTTI/ownership fit
  develop_rule.md — but the architecture does not bake C++ in.

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
