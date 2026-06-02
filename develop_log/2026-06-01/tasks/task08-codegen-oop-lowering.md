# Task 08 — Codegen: ClassDef lowering + vtable dispatch + member-assign
id: 2026-06-01/task08
parent: 2026-06-01/progress11
status: done
owner: code_agent

## Objective
Teach `compile_core` to lower `ClassDef`: emit the class's `MXClassInfo` + vtable constant globals,
lower the constructor / methods / operators / destructor to functions, and dispatch `C(args)`,
`obj.m(args)`, `a + b` (user operator), `obj.field` read, and `self.field = v` write.

## Scope
In:
- AST/parser **method-call fix** so `obj.m(args)` keeps its receiver + method name.
- A pre-pass assigning global method-selector slots and declaring ctor/method/operator functions.
- Per-class constant globals: name, vtable (operator + method-selector slots; rest null), `MXClassInfo`
  (incl. `C$dtor` trampoline when a `~Class` exists).
- Lowering: constructor body (`self = mxs_instance_new(&C.classinfo)` … `return self`), method bodies,
  method-call dispatch (load classinfo→vtable[slot]→call), member-assignment LHS, operator routing.
Out:
- ARC retain/release insertion (task09) — write this task **without** ARC first; task09 layers it on.
- Inheritance / interfaces / statics.

## Inputs (read first, priority order)
1. `develop_log/2026-06-01/progress11-…md` §"Design contract" (lowering shapes, MXClassInfo, slots,
   method-call AST fix).
2. `src/backend/codegen.cpp` — the `CoreGen` struct (`expr`/`stmt`/`block`/`emitFunction`) + the
   top-level `compile_core` (function pre-declare loop). This is where everything lands.
3. `include/mxspp/frontend/ast.h` (`ClassDef`,`MethodDef`,`ConstructorDef`,`OperatorDef`,`DestructorDef`,
   `FieldDecl`,`MemberExpr`,`FunctionCall`); `src/frontend/parser.cpp` `to_postfix` (the receiver-drop bug).
4. `include/mxspp/core/MXClassInfo.h` (from task06 — the layout to mirror in LLVM).

## Deliverables
- `ast::FunctionCall` gains `std::unique_ptr<Expression> receiver;` (or a new `MethodCall` node); parser
  `to_postfix` sets it when a `call_args` postfix applies to a `MemberExpr` base (name = member name,
  receiver = member target).
- `compile_core`: a pre-pass that, before emitting bodies, (a) collects classes, (b) builds the global
  `selector→slot` map (slots `>= MX_SLOT_RESERVED_COUNT`), (c) declares `funcs[ClassName]` (ctor) +
  `C$<method>` + `C$op_<x>` + `C$dtor` Functions, (d) emits each class's name/vtable/classinfo globals
  (LLVM struct `{ptr,ptr,ptr,i64,ptr}`; unimplemented slots = null; `destructor` = `C$dtor` or null).
- `CoreGen` lowering:
  - constructor body: bind `self` (immutable) = `mxs_instance_new(&C.classinfo)`, bind ctor params,
    run body, `return self`.
  - method/operator bodies: bind `self` = arg0, bind params, run body.
  - `expr(FunctionCall)` with a `receiver`: evaluate receiver, `ci = mxs_object_classinfo(recv)`,
    `fn = ci->vtable[slot]`, `call fn(recv, args…)`. Without a receiver: existing named-call path.
  - assignment LHS `MemberExpr`: `mxs_set_attr(obj, name, rhs)`; compound forms via get→op→set.
  - operator overloading needs **no** codegen change at the call site (still `mxs_op_*`); just ensure the
    user `operator+` is placed at `MX_SLOT_OP_ADD` etc. in the vtable so `mxs_op_add` (task06/MXOps) routes.

## Steps
1. Fix the method-call AST/parser gap; keep `func`/`@@foreign` named calls working.
2. Add the class pre-pass (selectors, function decls, globals) to `compile_core`.
3. Lower ctor/method/operator/dtor bodies; bind `self`.
4. Implement method-call dispatch + member-assignment in `CoreGen::expr`.
5. Verify a `Point`/`Vector` demo (no ARC yet): construct, `obj.field`, `self.field=v`, `obj.method()`,
   `a + b` via `operator+`, `match (… ) { case p: Point => … }`.

## Acceptance criteria
- [ ] `obj.m(args)` parses to a method call (receiver preserved) and dispatches via the vtable.
- [ ] `C(args)` constructs an `MXInstance` with the right classinfo; `Point(3,4).x` → `3`.
- [ ] A user `operator+` is invoked for `a + b` when `a` is an instance (routed by `mxs_op_add`).
- [ ] `match` on the class type selects the right arm (`mxs_is_type` instance-aware).
- [ ] The module passes `verifyModule`; existing demos (`core_fib`/`core_loops`/…) still produce the same
      output (no regression from the pre-pass / call-path changes).

## Constraints
- Keep the emitter naive/explicit (progress09 D6) — emit straight calls; let LLVM fold const-global
  dispatch. Do not special-case "type known" by hand.
- LLVM struct for `MXClassInfo` must byte-match task06's C++ struct.

## Notes / Assumptions
- Assumption: v1 is single-class; `obj.m` resolves against the one known class, so an absent method is a
  codegen error (no `mxs_no_such_method` thunk needed yet).
- Question: emit the vtable as a private constant `GlobalVariable` with a `ConstantArray` of the Function
  pointers so loads of `vtable[const]` fold (the D6 win) once core.bc is linked.
