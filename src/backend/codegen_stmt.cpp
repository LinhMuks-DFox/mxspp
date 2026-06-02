#include "mxspp/backend/coregen.h"

namespace mxs::backend::codegen::detail {
    void CoreGen::stmt(const ast::MXASTNode *n) {
        if (const auto *blk = dynamic_cast<const ast::Block *>(n)) {
            block(blk);
            return;
        }
        if (const auto *ls = dynamic_cast<const ast::LetStatement *>(n)) {
            llvm::Value *v = ls->value ? expr(ls->value.get()) : nil();
            if (!v) return;
            // `v` is a +1 the first binding adopts; each additional name needs its own
            // reference (retain) so every binding cell owns exactly one (no double-free).
            bool first = true;
            for (const auto &nm : ls->names) {
                if (!first) B.CreateCall(rt("mxs_retain", voidTy, { ptr }), { v });
                bind(nm, v, ls->isMut);
                first = false;
            }
            return;
        }
        if (const auto *es = dynamic_cast<const ast::ExprStatement *>(n)) {
            if (es->expr) releaseTmp(expr(es->expr.get()));// discard the value
            return;
        }
        if (const auto *rs = dynamic_cast<const ast::ReturnStatement *>(n)) {
            if (inMain) {
                if (rs->value) {
                    llvm::Value *v = expr(rs->value.get());
                    if (!v) return;
                    auto *code = B.CreateCall(rt("mxs_int_to_i64", i64, { ptr }), { v });
                    releaseTmp(v);
                    releaseAllScopes();
                    B.CreateRet(code);
                } else {
                    releaseAllScopes();
                    B.CreateRet(llvm::ConstantInt::get(i64, 0));
                }
                return;
            }
            if (curFn->getReturnType()->isVoidTy()) {
                if (rs->value) releaseTmp(expr(rs->value.get()));
                releaseAllScopes();
                B.CreateRetVoid();
            } else {
                // Evaluate the value (a +1) BEFORE releasing scopes, so if it was read out of a
                // binding it survives that binding's release; then transfer it to the caller.
                llvm::Value *v = rs->value ? expr(rs->value.get()) : nil();
                if (!v) return;
                releaseAllScopes();
                B.CreateRet(v);
            }
            return;
        }
        if (const auto *is = dynamic_cast<const ast::IfStatement *>(n)) {
            llvm::Value *cv = is->condition ? expr(is->condition.get()) : nullptr;
            llvm::Value *cond = cv ? truthyTmp(cv) : llvm::ConstantInt::getFalse(C);
            auto *thenBB = bb("then");
            auto *mergeBB = llvm::BasicBlock::Create(C, "ifcont");
            auto *elseBB = is->elseBranch ? llvm::BasicBlock::Create(C, "else") : mergeBB;
            B.CreateCondBr(cond, thenBB, elseBB);
            B.SetInsertPoint(thenBB);
            if (is->thenBlock) block(is->thenBlock.get());
            if (!terminated()) B.CreateBr(mergeBB);
            if (is->elseBranch) {
                curFn->insert(curFn->end(), elseBB);
                B.SetInsertPoint(elseBB);
                stmt(is->elseBranch.get());
                if (!terminated()) B.CreateBr(mergeBB);
            }
            curFn->insert(curFn->end(), mergeBB);
            B.SetInsertPoint(mergeBB);
            return;
        }
        if (const auto *lp = dynamic_cast<const ast::LoopStatement *>(n)) {
            auto *bodyBB = bb("loop");
            auto *afterBB = llvm::BasicBlock::Create(C, "loopend");
            B.CreateBr(bodyBB);
            B.SetInsertPoint(bodyBB);
            continueT.push_back(bodyBB);
            breakT.push_back(afterBB);
            if (lp->body) block(lp->body.get());
            if (!terminated()) B.CreateBr(bodyBB);
            continueT.pop_back();
            breakT.pop_back();
            curFn->insert(curFn->end(), afterBB);
            B.SetInsertPoint(afterBB);
            return;
        }
        if (const auto *us = dynamic_cast<const ast::UntilStatement *>(n)) {
            auto *condBB = bb("until.cond");
            auto *bodyBB = llvm::BasicBlock::Create(C, "until.body");
            auto *afterBB = llvm::BasicBlock::Create(C, "until.end");
            B.CreateBr(condBB);
            B.SetInsertPoint(condBB);
            llvm::Value *cv = us->condition ? expr(us->condition.get()) : nullptr;
            B.CreateCondBr(cv ? truthyTmp(cv) : llvm::ConstantInt::getTrue(C), afterBB,
                           bodyBB);
            curFn->insert(curFn->end(), bodyBB);
            B.SetInsertPoint(bodyBB);
            continueT.push_back(condBB);
            breakT.push_back(afterBB);
            if (us->body) block(us->body.get());
            if (!terminated()) B.CreateBr(condBB);
            continueT.pop_back();
            breakT.pop_back();
            curFn->insert(curFn->end(), afterBB);
            B.SetInsertPoint(afterBB);
            return;
        }
        if (const auto *ds = dynamic_cast<const ast::DoUntilStatement *>(n)) {
            auto *bodyBB = bb("do.body");
            auto *condBB = llvm::BasicBlock::Create(C, "do.cond");
            auto *afterBB = llvm::BasicBlock::Create(C, "do.end");
            B.CreateBr(bodyBB);
            B.SetInsertPoint(bodyBB);
            continueT.push_back(condBB);
            breakT.push_back(afterBB);
            if (ds->body) block(ds->body.get());
            if (!terminated()) B.CreateBr(condBB);
            continueT.pop_back();
            breakT.pop_back();
            curFn->insert(curFn->end(), condBB);
            B.SetInsertPoint(condBB);
            llvm::Value *cv = ds->condition ? expr(ds->condition.get()) : nullptr;
            B.CreateCondBr(cv ? truthyTmp(cv) : llvm::ConstantInt::getTrue(C), afterBB,
                           bodyBB);
            curFn->insert(curFn->end(), afterBB);
            B.SetInsertPoint(afterBB);
            return;
        }
        if (const auto *fs = dynamic_cast<const ast::ForInStatement *>(n)) {
            // `for v in lo..hi` (integer range) or `for v in xs` (iterate a container by
            // index). Both run a hidden i64 counter; the loop var is rebound each iteration.
            const auto *range = dynamic_cast<const ast::BinaryOp *>(fs->iterable.get());
            const bool isRange = range && range->op == "..";
            auto *toI64 = rt("mxs_int_to_i64", i64, { ptr });
            llvm::Value *lo = nullptr, *hi = nullptr, *listObj = nullptr;
            if (isRange) {
                llvm::Value *loObj = expr(range->left.get());
                llvm::Value *hiObj = expr(range->right.get());
                if (!loObj || !hiObj) return;
                lo = B.CreateCall(toI64, { loObj });
                hi = B.CreateCall(toI64, { hiObj });
                releaseTmp(loObj);// bounds consumed
                releaseTmp(hiObj);
            } else {
                listObj = expr(fs->iterable.get());
                if (!listObj) return;
                lo = llvm::ConstantInt::get(i64, 0);
                auto *lenObj = B.CreateCall(rt("mxs_len", ptr, { ptr }), { listObj });
                hi = B.CreateCall(toI64, { lenObj });
                releaseTmp(lenObj);
            }
            auto *ctr = allocaTy(i64, fs->var + ".i");
            B.CreateStore(lo, ctr);
            auto saved = locals;
            auto savedMut = localMut;
            auto *box = allocaTy(ptr, fs->var);// the loop var's binding cell
            locals[fs->var] = box;
            // Track the loop var's mutability (progress13 D5): `for v` is immutable (assigning
            // to it is a compile error), `for mut v` is reassignable. Without this the var was
            // absent from localMut, so the compile-time check passed and the runtime immutable
            // backstop was silently discarded — `v = …` no-oped with no diagnostic.
            localMut[fs->var] = fs->isMut;
            auto *condBB = bb("for.cond");
            auto *bodyBB = llvm::BasicBlock::Create(C, "for.body");
            auto *incrBB = llvm::BasicBlock::Create(C, "for.incr");
            auto *afterBB = llvm::BasicBlock::Create(C, "for.end");
            B.CreateBr(condBB);
            B.SetInsertPoint(condBB);
            llvm::Value *cur = B.CreateLoad(i64, ctr, fs->var + ".i");
            B.CreateCondBr(B.CreateICmpSLT(cur, hi, "forcmp"), bodyBB, afterBB);
            curFn->insert(curFn->end(), bodyBB);
            B.SetInsertPoint(bodyBB);
            // The element: the counter itself (range) or xs[counter] (container).
            auto *ctrObj = B.CreateCall(rt("mxs_int_from_i64", ptr, { i64 }), { cur });
            llvm::Value *iv = nullptr;
            if (isRange) {
                iv = ctrObj;// the loop var's cell adopts the counter object
            } else {
                iv = B.CreateCall(rt("mxs_index_get", ptr, { ptr, ptr }),
                                  { listObj, ctrObj });
                releaseTmp(ctrObj);// the index object was just a temporary
            }
            auto *cell = B.CreateCall(rt("mxs_lvalue_new", ptr, { ptr, i64 }),
                                      { iv, llvm::ConstantInt::get(i64, fs->isMut) });
            B.CreateStore(cell, box);
            continueT.push_back(incrBB);
            breakT.push_back(afterBB);
            if (fs->body) block(fs->body.get());
            if (!terminated()) {
                emitDeleteCell(cell);// release this iteration's loop var (and its value)
                B.CreateBr(incrBB);
            }
            continueT.pop_back();
            breakT.pop_back();
            curFn->insert(curFn->end(), incrBB);
            B.SetInsertPoint(incrBB);
            B.CreateStore(B.CreateAdd(B.CreateLoad(i64, ctr),
                                      llvm::ConstantInt::get(i64, 1), "inc"),
                          ctr);
            B.CreateBr(condBB);
            curFn->insert(curFn->end(), afterBB);
            B.SetInsertPoint(afterBB);
            if (listObj) releaseTmp(listObj);// the iterable, done after the loop
            locals = saved;
            localMut = savedMut;
            return;
        }
        if (dynamic_cast<const ast::BreakStatement *>(n)) {
            if (!breakT.empty()) B.CreateBr(breakT.back());
            else
                err("'break' outside a loop");
            return;
        }
        if (dynamic_cast<const ast::ContinueStatement *>(n)) {
            if (!continueT.empty()) B.CreateBr(continueT.back());
            else
                err("'continue' outside a loop");
            return;
        }
        if (const auto *as = dynamic_cast<const ast::AssertStatement *>(n)) {
            llvm::Value *v = as->expr ? expr(as->expr.get()) : nullptr;
            if (!v) return;
            auto *failBB = bb("assert.fail");
            auto *okBB = bb("assert.ok");
            B.CreateCondBr(truthyTmp(v), okBB, failBB);
            B.SetInsertPoint(failBB);
            B.CreateCall(rt("mxs_panic", voidTy, { ptr }),
                         { B.CreateGlobalStringPtr("assertion failed") });
            B.CreateUnreachable();
            B.SetInsertPoint(okBB);
            return;
        }
        err("unsupported statement");
    }


    void CoreGen::block(const ast::Block *blk) {
        auto saved = locals;
        auto savedMut = localMut;
        pushScope();
        for (const auto &s : blk->statements) {
            if (terminated()) break;
            stmt(s.get());
        }
        popScopeRelease();
        locals = saved;
        localMut = savedMut;
    }

    llvm::Value *CoreGen::blockValue(const ast::Block *blk) {
        auto saved = locals;
        auto savedMut = localMut;
        pushScope();
        llvm::Value *val = nil();
        for (const auto &s : blk->statements) {
            if (terminated()) break;
            if (const auto *es = dynamic_cast<const ast::ExprStatement *>(s.get())) {
                releaseTmp(val);// drop the previous (non-final) arm value
                val = es->expr ? expr(es->expr.get()) : nil();
            } else {
                stmt(s.get());
                // A `return`/`break`/`continue` inside the arm terminates this block; do
                // NOT emit the release/nil after the terminator (that would put
                // instructions after a `ret` -> invalid IR / verifier failure).
                if (terminated()) break;
                releaseTmp(val);
                val = nil();
            }
        }
        popScopeRelease();// the arm value `val` is separately owned and survives this
        locals = saved;
        localMut = savedMut;
        return val;
    }
}// namespace mxs::backend::codegen::detail
