# Progress 07 — Container types

id: 2026-05-31/progress07
date: 2026-05-31
author: human+ai
status: active
refs: [2026-05-31/progress05]
supersedes:
commits: []
files:
  - src/runtime/runtime.cpp          # MX_LIST tag + mxs_list_* C-ABI
  - include/mxspp/frontend/grammar.hpp # list literal
  - include/mxspp/frontend/ast.h     # ListLiteral, IndexExpr
  - src/frontend/parser.cpp          # build ListLiteral / IndexExpr
  - src/backend/codegen.cpp          # object-mode lowering for literal + subscript
  - src/driver/main.cpp              # object-mode prelude: len/push/...

## Goal
Built-in container types so MXScript can organize data (type_system §3.3). Build on the boxed
object model from [progress05](./progress05-runtime-and-stdlib.md): every container holds
`MXObject*` elements and is itself a boxed `MXObject*`.

## Decisions

### D1 — Naming: the dynamic array is `ArrayList`, not `List` — Mux, 2026-05-31
- Decision: the dynamically-sized array is named **`ArrayList`**. The doc (type_system §3.3) calls
  it `List<T>`, but "List" is easily misread as a linked list, so the user-facing name is
  `ArrayList`. (Doc drift: §3.3 still says `List<T>`; treat `ArrayList` as authoritative. Update the
  doc in a follow-up.)
- Also important per Mux: besides the dynamic array, **static (fixed-size) arrays** (`Array`, doc's
  `[N]T`) and **`Matrix`** matter. Matrix is a library *class* built atop a static array (see
  `example/examples/matrix_class.mxs`) — it lands after classes/methods, not as a primitive.

### D2 — Container roadmap / ordering
1. **ArrayList** (dynamic array) — the foundational container; literal `[a, b, c]`, subscript
   `xs[i]`, `len`/`append`. (THIS slice.)
2. **Array** (fixed-size, `[N]T`, `[v; n]`) — POD vs boxed storage strategy (§3.3).
3. **Tuple** (`(a, b, c)`, heterogeneous, immutable) and **Dict<K,V>** (`[k: v]`).
4. **Matrix** — a library class over `Array` (needs OOP/method dispatch first).

### D3 — Runtime representation (consistent with the tagged object model)
- A new tag `MX_LIST`; the `MXObject` payload points to a heap `std::vector<MXObject*>`.
- Object-mode-friendly C-ABI (everything is `MXObject*`): `mxs_list_new()`,
  `mxs_list_push(list, v)`, `mxs_list_get(list, idx)` (bounds-checked → panic on OOB),
  `mxs_list_len(list)` (boxed int). `print_obj` renders `[a, b, c]`; `mxs_op_add` concatenates two
  lists; truthy = non-empty.
- Reference counting (the doc's rc guidance) is deferred — the current object model doesn't rc yet
  (elements are boxed and currently leak, as the rest of the runtime does). Revisit with the
  object-model/RTTI reconciliation (progress05 NEXT).

### D4 — Language constructs vs builtins (upholds the no-hardcoding rule, progress05 D3)
- The list **literal** `[...]` and **subscript** `xs[i]` are language constructs with dedicated
  object-mode codegen (like arithmetic) — they lower to `mxs_list_new`/`push`/`get`.
- `len(x)` / `append(xs, v)` are generic `@@foreign` prelude bindings (free functions for now;
  method syntax `xs.append(v)` waits for OOP method dispatch). No per-function codegen hardcoding.

## Done
- **`MXArrayList`** — the dynamic array as a real `core::MXObject` subclass (mxs::builtin), on the
  new object model (progress09), not the reverted flat prototype. Ordered sequence of element
  `MXObject*` (element ownership/rc deferred, D3). C++ API: append / get / set / size / length
  (→MXInteger) / concat / repr (`[a, b, c]`) / RTTI. `extern "C"` ABI (`mxs_arraylist_*`) with
  out-of-range → `MXError`. Unit-tested (core_test). Still pending (needs codegen rewiring,
  progress09 ④): the `[...]` literal grammar + subscript `xs[i]` lowering to this ABI.

## ArrayList usable from the language (2026-05-31)
On the new object model (run-core): the list **literal** `[a, b, c]` (grammar + `ListLiteral` AST +
codegen → `mxs_arraylist_new`/`append`), **subscript** `xs[i]` (named `index_op` postfix +
`IndexExpr` AST + codegen → `mxs_arraylist_get`, out-of-range → `MXError`), and `len`/`append`
builtins (object-mode prelude). Also added the **`**` power operator** (grammar power level +
`mxs_op_pow`). Verified (`example/examples/core_list.mxs`): `2 ** 10`→1024, `[10,20,30]`,
`xs[1]`→20, `len(xs)`→3, `append` then `[10,20,30,40]`/4.

## Open / TODO
- `pop`/`extend`/`clear`/`insert`, `operator*` (repeat), negative-index semantics; subscript
  assignment `xs[i] = v`; iteration `for x in xs`.
- Static `Array` (POD vs boxed), `Tuple`, `Dict`. Then `Matrix` (after classes/methods).
- Method-call syntax (`xs.append(v)`, `xs.length()`) once OOP dispatch lands; reconcile element rc.

## Agent log
- 2026-05-31 [ai] Prototyped ArrayList on the *flat tagged-union* runtime (list literal grammar +
  `ListLiteral` AST + object-mode codegen + `mxs_list_*` + `len`/`append`/`get`/`set` prelude +
  smoke test). Reverted (not committed) when the object model was re-architected to real C++ classes
  ([progress09](./progress09-object-model-cpp.md)): the dynamic array is redone as a real
  **`MXArrayList`** core class (C++ + `extern "C"` ABI), not a `MX_LIST` union tag. The grammar list
  literal `[...]` (frontend, model-agnostic) will be reintroduced together with `MXArrayList`.
  Sequencing: containers come after the foundational types (MXInteger landed first).
