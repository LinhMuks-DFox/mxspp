#include "mxspp/backend/coregen.h"

namespace mxs::backend::codegen::detail {
    llvm::Value *CoreGen::shortCircuit(const ast::BinaryOp *bo) {
        const bool isAnd = bo->op == "&&";
        llvm::Value *l = expr(bo->left.get());
        if (!l) return nullptr;
        llvm::Value *lc = truthyTmp(l);// releases l
        auto *entryBB = B.GetInsertBlock();
        auto *rhsBB = bb(isAnd ? "and.rhs" : "or.rhs");
        auto *contBB = bb(isAnd ? "and.cont" : "or.cont");
        if (isAnd) B.CreateCondBr(lc, rhsBB, contBB);
        else
            B.CreateCondBr(lc, contBB, rhsBB);
        B.SetInsertPoint(rhsBB);
        llvm::Value *r = expr(bo->right.get());
        if (!r) return nullptr;
        llvm::Value *rc = truthyTmp(r);// releases r
        auto *rhsEnd = B.GetInsertBlock();
        B.CreateBr(contBB);
        B.SetInsertPoint(contBB);
        auto *phi = B.CreatePHI(llvm::Type::getInt1Ty(C), 2, "logic");
        phi->addIncoming(isAnd ? llvm::ConstantInt::getFalse(C)
                               : llvm::ConstantInt::getTrue(C),
                         entryBB);
        phi->addIncoming(rc, rhsEnd);
        return boolFromI1(phi);
    }

    llvm::Value *CoreGen::expr(const ast::MXASTNode *n) {
        if (const auto *il = dynamic_cast<const ast::IntegerLiteral *>(n))
            return B.CreateCall(rt("mxs_int_from_i64", ptr, { i64 }),
                                { llvm::ConstantInt::get(i64, il->value, true) });
        if (const auto *fl = dynamic_cast<const ast::FloatLiteral *>(n))
            return B.CreateCall(rt("mxs_float_new", ptr, { dbl }),
                                { llvm::ConstantFP::get(dbl, fl->value) });
        if (const auto *bl = dynamic_cast<const ast::BooleanLiteral *>(n))
            return B.CreateCall(rt("mxs_bool_new", ptr, { i64 }),
                                { llvm::ConstantInt::get(i64, bl->value) });
        if (dynamic_cast<const ast::NilLiteral *>(n)) return nil();
        if (const auto *sl = dynamic_cast<const ast::StringLiteral *>(n))
            return B.CreateCall(rt("mxs_str_new", ptr, { ptr }),
                                { B.CreateGlobalStringPtr(sl->value, "str") });
        if (const auto *ll = dynamic_cast<const ast::ListLiteral *>(n)) {
            llvm::Value *list =
                    B.CreateCall(rt("mxs_arraylist_new", ptr, { }), { }, "list");
            auto *appendFn = rt("mxs_arraylist_append", voidTy, { ptr, ptr });
            for (const auto &el : ll->elements) {
                llvm::Value *v = expr(el.get());
                if (!v) return nullptr;
                B.CreateCall(appendFn, { list, v });
                releaseTmp(v);// append retained its own ref; drop ours
            }
            return list;
        }
        if (const auto *ix = dynamic_cast<const ast::IndexExpr *>(n)) {
            llvm::Value *t = expr(ix->target.get());
            llvm::Value *i = expr(ix->index.get());
            if (!t || !i) return nullptr;
            // Generic: works for ArrayList elements and String characters.
            auto *r = B.CreateCall(rt("mxs_index_get", ptr, { ptr, ptr }), { t, i });
            releaseTmp(t);
            releaseTmp(i);
            return r;
        }
        if (const auto *me = dynamic_cast<const ast::MemberExpr *>(n)) {
            llvm::Value *t = expr(me->target.get());
            if (!t) return nullptr;
            auto *r = B.CreateCall(rt("mxs_get_attr", ptr, { ptr, ptr }),
                                   { t, B.CreateGlobalStringPtr(me->name, "attr") });
            releaseTmp(t);
            return r;
        }
        if (const auto *id = dynamic_cast<const ast::Identifier *>(n)) {
            auto it = locals.find(id->name);
            if (it == locals.end()) {
                err("unknown identifier '" + id->name + "'");
                return nullptr;
            }
            auto *cell = B.CreateLoad(ptr, it->second, id->name);
            return B.CreateCall(rt("mxs_lvalue_rvalue", ptr, { ptr }), { cell }, "rv");
        }
        if (const auto *bo = dynamic_cast<const ast::BinaryOp *>(n)) {
            const std::string &op = bo->op;
            if (op == "&&" || op == "||") return shortCircuit(bo);
            if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=") {
                // Assignment yields nil (a fresh +1) as its value — uniform and ARC-safe; the
                // stored r-value is consumed by the target (adopted or retained), not returned.
                // Subscript assignment: `xs[i] = v` (and compound forms).
                if (const auto *ix =
                            dynamic_cast<const ast::IndexExpr *>(bo->left.get())) {
                    llvm::Value *tgt = expr(ix->target.get());
                    llvm::Value *idx = expr(ix->index.get());
                    llvm::Value *rhs = expr(bo->right.get());
                    if (!tgt || !idx || !rhs) return nullptr;
                    if (op != "=") {
                        auto *cur = B.CreateCall(
                                rt("mxs_arraylist_get", ptr, { ptr, ptr }), { tgt, idx });
                        auto *nr = B.CreateCall(
                                rt(core_op(op.substr(0, 1)), ptr, { ptr, ptr }),
                                { cur, rhs });
                        releaseTmp(cur);
                        releaseTmp(rhs);
                        rhs = nr;
                    }
                    // set retains its own reference (borrow convention) -> release ours.
                    B.CreateCall(rt("mxs_arraylist_set", ptr, { ptr, ptr, ptr }),
                                 { tgt, idx, rhs });
                    releaseTmp(rhs);
                    releaseTmp(tgt);
                    releaseTmp(idx);
                    return nil();
                }
                // Member assignment: `obj.field = v` / `self.field = v` (and compound forms).
                if (const auto *me =
                            dynamic_cast<const ast::MemberExpr *>(bo->left.get())) {
                    llvm::Value *obj = expr(me->target.get());
                    llvm::Value *rhs = expr(bo->right.get());
                    if (!obj || !rhs) return nullptr;
                    auto *nameP = B.CreateGlobalStringPtr(me->name, "attr");
                    if (op != "=") {
                        auto *cur = B.CreateCall(rt("mxs_get_attr", ptr, { ptr, ptr }),
                                                 { obj, nameP });
                        auto *nr = B.CreateCall(
                                rt(core_op(op.substr(0, 1)), ptr, { ptr, ptr }),
                                { cur, rhs });
                        releaseTmp(cur);
                        releaseTmp(rhs);
                        rhs = nr;
                    }
                    // mxs_set_attr ADOPTS rhs (the field takes the +1) -> do not release rhs.
                    B.CreateCall(rt("mxs_set_attr", voidTy, { ptr, ptr, ptr }),
                                 { obj, nameP, rhs });
                    releaseTmp(obj);
                    return nil();
                }
                const auto *lid = dynamic_cast<const ast::Identifier *>(bo->left.get());
                if (!lid) {
                    err("assignment target must be a variable");
                    return nullptr;
                }
                auto it = locals.find(lid->name);
                if (it == locals.end()) {
                    err("assignment to unknown variable '" + lid->name + "'");
                    return nullptr;
                }
                // Reject reassignment of an immutable `let` binding at compile time (progress13).
                // `let mut` is required to reassign. (The MXLeftValue runtime is the backstop.)
                auto mut = localMut.find(lid->name);
                if (mut != localMut.end() && !mut->second) {
                    err("cannot assign to immutable binding '" + lid->name +
                        "'; declare it with `let mut` to allow reassignment");
                    return nullptr;
                }
                llvm::Value *rhs = expr(bo->right.get());
                if (!rhs) return nullptr;
                auto *cell = B.CreateLoad(ptr, it->second, lid->name);
                if (op != "=") {
                    auto *cur = B.CreateCall(rt("mxs_lvalue_rvalue", ptr, { ptr }),
                                             { cell }, "rv");
                    auto *nr =
                            B.CreateCall(rt(core_op(op.substr(0, 1)), ptr, { ptr, ptr }),
                                         { cur, rhs });
                    releaseTmp(cur);
                    releaseTmp(rhs);
                    rhs = nr;
                }
                // mxs_lvalue_update ADOPTS rhs (releases the old value; enforces immutability,
                // returning an MXError on a `let` binding). Discard that result.
                releaseTmp(B.CreateCall(rt("mxs_lvalue_update", ptr, { ptr, ptr }),
                                        { cell, rhs }));
                return nil();
            }
            if (const char *sym = core_op(op)) {
                llvm::Value *l = expr(bo->left.get());
                llvm::Value *r = expr(bo->right.get());
                if (!l || !r) return nullptr;
                auto *res = B.CreateCall(rt(sym, ptr, { ptr, ptr }), { l, r });
                releaseTmp(l);// operands are borrowed by the op; drop the temporaries
                releaseTmp(r);
                return res;
            }
            err("unsupported binary operator '" + op + "'");
            return nullptr;
        }
        if (const auto *uo = dynamic_cast<const ast::UnaryOp *>(n)) {
            llvm::Value *v = expr(uo->operand.get());
            if (!v) return nullptr;
            if (uo->op == "-") {
                auto *res = B.CreateCall(rt("mxs_op_neg", ptr, { ptr }), { v });
                releaseTmp(v);
                return res;
            }
            if (uo->op == "!") {
                auto *res = B.CreateCall(rt("mxs_op_not", ptr, { ptr }), { v });
                releaseTmp(v);
                return res;
            }
            return v;// unary '+' yields the operand as-is
        }
        if (const auto *call = dynamic_cast<const ast::FunctionCall *>(n)) {
            // Module-qualified call `ns.fn(args)` (progress13 D2): a receiver that is a bare
            // Identifier naming a qualified-import namespace resolves to the merged function
            // `funcs[ns.fn]` as a direct call — NOT a method on a value. Checked before method
            // dispatch (a namespace identifier is not an evaluable value). The merged function
            // is itself either @@foreign or a user wrapper, so it flows through the normal
            // call path below by rewriting the lookup name.
            std::string callName = call->name;
            bool moduleQualified = false;
            if (call->receiver && moduleNamespaces) {
                if (const auto *nsId =
                            dynamic_cast<const ast::Identifier *>(call->receiver.get())) {
                    // A bound local variable shadows an imported namespace of the same name
                    // (lexical scoping): `import std.io; let io = SomeValue; io.m()` must be a
                    // method call on the value, not a module-qualified call. Only treat the
                    // receiver as a namespace when it is NOT a live local.
                    if (moduleNamespaces->count(nsId->name) &&
                        !locals.count(nsId->name)) {
                        callName = nsId->name + "." + call->name;
                        moduleQualified = true;
                    }
                }
            }
            // Method call `recv.m(args)`. Two dispatch paths (progress13 D4):
            //  (1) user-class method  -> the receiver's classinfo vtable (progress11);
            //  (2) built-in container/string method (`xs.append(v)`, `xs.len()`,
            //      `s.len()`, `xs.get(i)`) -> a direct call to the polymorphic runtime
            //      C-ABI symbol with the receiver as arg0. Built-ins have no MXClassInfo
            //      (their classinfo is null), so they MUST NOT take the vtable path.
            if (call->receiver && !moduleQualified) {
                // Built-in container/string method table (progress13 D4): name -> {runtime
                // symbol, arg count excl. receiver, returns void?}. Routed by method name to a
                // polymorphic runtime C-ABI symbol. (`set`/`concat` are not here for v1 —
                // `mxs_arraylist_set` returns a *borrow*, not a clean +1, and `concat` has two
                // non-polymorphic symbols; both need bespoke handling. See progress13/task20.)
                struct BuiltinMethod {
                    const char *symbol;
                    std::size_t arity;// args excluding the receiver
                    bool returnsVoid;
                };
                static const std::unordered_map<std::string, BuiltinMethod> kBuiltins{
                    { "append", { "mxs_arraylist_append", 1, true } },
                    { "len", { "mxs_len", 0, false } },
                    { "get", { "mxs_index_get", 1, false } },
                };
                auto bit = kBuiltins.find(call->name);
                const bool isBuiltinName = bit != kBuiltins.end();
                // A user-class method *name* (whole-program selector set). NOTE: this is
                // name-only — it does NOT mean the receiver is actually an instance, so a name
                // that is a selector still needs the runtime classinfo check below (a built-in
                // list/string has a null classinfo and MUST NOT take the vtable path).
                const bool isUserSel = selectors && selectors->count(call->name);
                if (!isUserSel && !isBuiltinName) {
                    err("call to unknown method '" + call->name + "'");
                    return nullptr;
                }
                // For a pure built-in call (no same-named user method) the arity is fixed, so
                // check it at compile time for a clear diagnostic.
                if (isBuiltinName && !isUserSel &&
                    call->args.size() != bit->second.arity) {
                    err("method '" + call->name + "' expects " +
                        std::to_string(bit->second.arity) + " argument(s), got " +
                        std::to_string(call->args.size()));
                    return nullptr;
                }
                // Evaluate the receiver + args once (shared by every dispatch path below).
                llvm::Value *recv = expr(call->receiver.get());
                if (!recv) return nullptr;
                std::vector<llvm::Value *> argv{ recv };
                for (const auto &a : call->args) {
                    llvm::Value *v = expr(a.get());
                    if (!v) return nullptr;
                    argv.push_back(v);
                }
                std::vector<llvm::Type *> argTys(argv.size(), ptr);
                // Emit a built-in runtime call with the already-evaluated argv. Foreign/borrow
                // convention: release every operand (receiver + args); a void result yields
                // nil(), a non-void result is the fresh +1 expression value.
                auto emitBuiltin = [&](const BuiltinMethod &bm) -> llvm::Value * {
                    if (bm.returnsVoid) {
                        B.CreateCall(rt(bm.symbol, voidTy, argTys), argv);
                        for (auto *a : argv) releaseTmp(a);
                        return nil();
                    }
                    auto *res = B.CreateCall(rt(bm.symbol, ptr, argTys), argv, "method");
                    for (auto *a : argv) releaseTmp(a);
                    return res;
                };
                if (!isUserSel) {
                    // Built-in name only: dispatch straight to the runtime symbol (the symbol
                    // checks the receiver kind internally; a wrong receiver yields nil / an
                    // error value, never a crash — see progress14 §6).
                    return emitBuiltin(bit->second);
                }
                // The name is a user-class selector, so the receiver MIGHT be a user instance
                // (vtable dispatch) or a non-instance value (a built-in list/string, or a type
                // error). Built-ins have a null classinfo, so we branch at runtime — a null
                // classinfo means the vtable path is invalid (it would deref null = SIGSEGV):
                //   classinfo != null -> user-class vtable[slot]; the method is callee-owned
                //                        (adopts self + args), so the caller does NOT release;
                //   classinfo == null -> fall back to the built-in symbol when the name is a
                //                        built-in with matching arity (so `xs.len()` still works
                //                        even if some class also defines `len`), otherwise a
                //                        TypeError value (mxs_method_missing) — never a deref.
                const std::int64_t slot = selectors->at(call->name);
                auto *ci = B.CreateCall(rt("mxs_object_classinfo", ptr, { ptr }),
                                        { recv }, "ci");
                auto *isInst =
                        B.CreateICmpNE(ci, llvm::ConstantPointerNull::get(ptr), "isinst");
                auto *vtBB = bb("dispatch.vtable");
                auto *fallBB = bb("dispatch.fallback");
                auto *contBB = bb("dispatch.cont");
                B.CreateCondBr(isInst, vtBB, fallBB);
                // Vtable path (user instance): callee-owned args (do NOT release here).
                B.SetInsertPoint(vtBB);
                auto *ciTy = classInfoTy();
                auto *vtSlotPtr = B.CreateStructGEP(ciTy, ci, 4, "vtable.addr");
                auto *vtable = B.CreateLoad(ptr, vtSlotPtr, "vtable");
                auto *fnPtr = B.CreateGEP(
                        ptr, vtable, { llvm::ConstantInt::get(i64, slot) }, "fn.addr");
                auto *fn = B.CreateLoad(ptr, fnPtr, "fn");
                auto *fnTy = llvm::FunctionType::get(ptr, argTys, false);
                auto *vtRes = B.CreateCall(fnTy, fn, argv, "method");
                auto *vtEnd = B.GetInsertBlock();
                B.CreateBr(contBB);
                // Fallback path (non-instance receiver): built-in symbol (if name+arity fit) or
                // a TypeError value. Both borrow operands -> release every evaluated operand.
                B.SetInsertPoint(fallBB);
                llvm::Value *fallRes = nullptr;
                if (isBuiltinName && call->args.size() == bit->second.arity) {
                    fallRes = emitBuiltin(bit->second);
                } else {
                    auto *nameP = B.CreateGlobalStringPtr(call->name, "method.name");
                    fallRes = B.CreateCall(rt("mxs_method_missing", ptr, { ptr, ptr }),
                                           { recv, nameP }, "nomethod");
                    for (auto *a : argv) releaseTmp(a);
                }
                auto *fallEnd = B.GetInsertBlock();
                B.CreateBr(contBB);
                B.SetInsertPoint(contBB);
                auto *phi = B.CreatePHI(ptr, 2, "dispatch.val");
                phi->addIncoming(vtRes, vtEnd);
                phi->addIncoming(fallRes, fallEnd);
                return phi;
            }
            auto it = funcs.find(callName);
            if (it == funcs.end()) {
                err("call to unknown function '" + callName + "'");
                return nullptr;
            }
            llvm::Function *f = it->second;
            const bool isVariadic = variadics && variadics->count(callName);
            std::vector<llvm::Value *> argv;
            if (isVariadic) {
                // Signature is (fixed…, MXArrayList* rest): the rest param occupies the last
                // LLVM slot, so #fixed = arg_size - 1. Surplus args are packed into a fresh
                // list (progress12 D-VARARG). append retains, so each appended +1 temporary is
                // released right after (the list now owns it); the list is one owned value
                // handled like any other arg below (foreign borrows → released; user adopts).
                const std::size_t fixed = f->arg_size() ? f->arg_size() - 1 : 0;
                if (call->args.size() < fixed) {
                    err("call to '" + callName + "' expects at least " +
                        std::to_string(fixed) + " argument(s), got " +
                        std::to_string(call->args.size()));
                    return nullptr;
                }
                for (std::size_t i = 0; i < fixed; ++i) {
                    llvm::Value *v = expr(call->args[i].get());
                    if (!v) return nullptr;
                    argv.push_back(v);
                }
                auto *list =
                        B.CreateCall(rt("mxs_arraylist_new", ptr, { }), { }, "varargs");
                auto *appendFn = rt("mxs_arraylist_append", voidTy, { ptr, ptr });
                for (std::size_t i = fixed; i < call->args.size(); ++i) {
                    llvm::Value *v = expr(call->args[i].get());
                    if (!v) return nullptr;
                    B.CreateCall(appendFn, { list, v });
                    releaseTmp(v);
                }
                argv.push_back(list);
            } else {
                if (call->args.size() != f->arg_size()) {
                    err("call to '" + callName + "' expects " +
                        std::to_string(f->arg_size()) + " argument(s), got " +
                        std::to_string(call->args.size()));
                    return nullptr;
                }
                for (const auto &a : call->args) {
                    llvm::Value *v = expr(a.get());
                    if (!v) return nullptr;
                    argv.push_back(v);
                }
            }
            // Foreign C callees borrow their args (caller releases); user functions/ctors are
            // callee-owned (their params adopt the args), so the caller must NOT release them.
            const bool isForeign = foreigns && foreigns->count(callName);
            if (f->getReturnType()->isVoidTy()) {
                B.CreateCall(f, argv);
                if (isForeign)
                    for (auto *a : argv) releaseTmp(a);
                return nil();
            }
            auto *res = B.CreateCall(f, argv, "call");
            if (isForeign)
                for (auto *a : argv) releaseTmp(a);
            return res;
        }
        if (const auto *m = dynamic_cast<const ast::MatchExpr *>(n)) {
            // Evaluate the subject once; try each case in order; the first matching arm's
            // body value is the match's value (nil if none matches). Type-binding patterns
            // (`x: Type`) test the runtime type — this is the match-based error model.
            llvm::Value *subj = m->subject ? expr(m->subject.get()) : nil();
            if (!subj) return nullptr;
            auto *result = allocaTy(ptr, "match.result");
            B.CreateStore(nil(), result);
            auto *mergeBB = llvm::BasicBlock::Create(C, "match.end");
            for (const auto &cs : m->cases) {
                auto *bodyBB = bb("case.body");
                auto *nextBB = llvm::BasicBlock::Create(C, "case.next");
                llvm::Value *matches = nullptr;
                if (cs.typeName) {
                    auto *t = B.CreateCall(
                            rt("mxs_is_type", i64, { ptr, ptr }),
                            { subj, B.CreateGlobalStringPtr(*cs.typeName, "ty") });
                    matches =
                            B.CreateICmpNE(t, llvm::ConstantInt::get(i64, 0), "tymatch");
                } else if (cs.literal) {
                    llvm::Value *lit = expr(cs.literal.get());
                    if (!lit) return nullptr;
                    auto *eq = B.CreateCall(rt("mxs_op_eq", ptr, { ptr, ptr }),
                                            { subj, lit });
                    matches = truthy(eq);
                    releaseTmp(eq);
                    releaseTmp(lit);
                } else {
                    matches = llvm::ConstantInt::getTrue(C);// wildcard or plain binding
                }
                B.CreateCondBr(matches, bodyBB, nextBB);
                B.SetInsertPoint(bodyBB);
                auto saved = locals;
                // The arm gets its own scope so a `case x: T =>` binding's cell is released
                // within the block that creates it (SSA dominance) at the arm's end.
                pushScope();
                if (!cs.binding.empty()) {
                    // Retain subj for the binding (released at arm-scope exit); subj's own +1
                    // is released once after the match.
                    B.CreateCall(rt("mxs_retain", voidTy, { ptr }), { subj });
                    bind(cs.binding, subj, /*mutable=*/false);
                }
                const auto *blk = dynamic_cast<const ast::Block *>(cs.body.get());
                llvm::Value *val = blk ? blockValue(blk) : expr(cs.body.get());
                if (!terminated()) {
                    popScopeRelease();// release the arm's case binding (if any)
                    if (val) {
                        // Release the slot's current contents (the initial nil placeholder, a
                        // +1) before overwriting it with this arm's value, so the placeholder
                        // is not leaked. Arms are mutually exclusive at runtime, so exactly one
                        // store runs and releases exactly the initial nil.
                        releaseTmp(B.CreateLoad(ptr, result, "match.old"));
                        B.CreateStore(val, result);
                    }
                    B.CreateBr(mergeBB);
                } else {
                    // terminated (return): releaseAllScopes already ran. Pop both stacks in
                    // lockstep (see pushScope).
                    assert(scopes.size() == scopeNames.size());
                    scopes.pop_back();
                    scopeNames.pop_back();
                }
                locals = saved;
                curFn->insert(curFn->end(), nextBB);
                B.SetInsertPoint(nextBB);
            }
            B.CreateBr(mergeBB);// no case matched -> result stays nil
            curFn->insert(curFn->end(), mergeBB);
            B.SetInsertPoint(mergeBB);
            releaseTmp(subj);// done with the subject (its own +1)
            return B.CreateLoad(ptr, result, "match.val");
        }
        err("unsupported expression");
        return nullptr;
    }
}// namespace mxs::backend::codegen::detail
