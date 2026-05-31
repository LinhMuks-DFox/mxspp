# Task 04 — AST coverage: OOP definitions (class / interface / enum / type)
id: 2026-05-31/task04
parent: 2026-05-31/progress03
status: done
owner: code_agent

## Objective
Make the parse→AST transform cover OOP definitions and their members.

## Outcome (2026-05-31)
Done & verified natively.
- `ast.h`: added `ClassDef`, `FieldDecl`, `MethodDef`, `ConstructorDef`, `DestructorDef`,
  `OperatorDef`, `InterfaceDef`, `InterfaceMethod`, `EnumDef`, `EnumVariant`, `TypeDef`,
  `TypeField` (+ ctors / codegen decls / stubs).
- `parser.cpp`: selected the OOP rule nodes (incl. `field_def_class`, `op_symbol`, `access_spec`,
  `K_OVERRIDE`, `K_STATIC`); refactored `parse_sig` → shared `collect_params` + `parse_params`;
  added `to_member`/`to_class`/`to_interface`/`to_enum`/`to_typedef` + `to_stmt` branches + dumper
  cases. (`field_def_class` inherits `let_stmt`, so the matched rule is `field_def_class` — it must
  be selected directly, same inheritance gotcha as `expression`/`func_def`.)
- Captures: base class, static, override, ctor base-name, method/operator signatures, enum variant
  fields, struct fields.
- Tests: +5 cases (class+members, interface, enum, type, generic class). Suite **21 cases /
  115 checks, all pass**. `universal_test.mxs` now dumps a complete top-level AST (10 functions +
  6 classes + interface + enum×2 + type).

## Deferred (follow-ups)
- Postfix `.member` / `[index]` / `?` are not dedicated AST nodes yet (need named member/index/error
  sub-rules in the grammar — `self.x` currently mis-transforms to `Call self`). Highest-value next.
- Member visibility (public/private) not tracked; generic params on class/func captured only as the
  bare name (not the `<T,...>` list); constructor base-call arguments dropped (base name kept).
- `match` / lambda transforms, annotations, import/binding → task05.
