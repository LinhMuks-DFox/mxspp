#include "mxspp/backend/codegen.h"
#include "mxspp/frontend/ast.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>

namespace mxs::backend::codegen {
    namespace ast = mxs::frontend::ast;

    std::unique_ptr<llvm::Module> compile(const ast::TranslationUnit &tu,
                                          llvm::LLVMContext &llvmContext,
                                          const std::string &moduleName) {
        auto module = std::make_unique<llvm::Module>(moduleName, llvmContext);
        llvm::IRBuilder<> builder(llvmContext);

        CodegenContext ctx{
            llvmContext, module.get(), &builder, {}, {}, nullptr, {}, {},
        };

        // Pass 1: declare every top-level function prototype, so recursion and
        // forward / mutual calls resolve before any body is emitted.
        for (const auto &s : tu.statements) {
            const auto *fn = dynamic_cast<const ast::FunctionDef *>(s.get());
            if (!fn) continue;
            std::vector<llvm::Type *> argTys;
            argTys.reserve(fn->params.size());
            for (const auto &p : fn->params)
                argTys.push_back(map_type(llvmContext, p->typeName.value_or("int")));
            llvm::Type *retTy = fn->returnTypeName
                                        ? map_type(llvmContext, *fn->returnTypeName)
                                        : llvm::Type::getVoidTy(llvmContext);
            auto *fty = llvm::FunctionType::get(retTy, argTys, /*isVarArg=*/false);
            // @@foreign binds the function to an external symbol (its body is a declaration).
            const std::string symbol =
                    fn->isForeign
                            ? (fn->foreignSymbol.empty() ? fn->name : fn->foreignSymbol)
                            : fn->name;
            auto *f = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, symbol,
                                             module.get());
            unsigned i = 0;
            for (auto &arg : f->args())
                if (i < fn->params.size()) arg.setName(fn->params[i++]->name);
            ctx.functions[fn->name] = f;// resolve calls by the mxs-level name
        }

        // Pass 2: emit bodies. (class/interface/enum/type need the object model — skipped.)
        for (const auto &s : tu.statements)
            if (const auto *fn = dynamic_cast<const ast::FunctionDef *>(s.get()))
                fn->codegen(ctx);

        std::string err;
        llvm::raw_string_ostream os(err);
        if (llvm::verifyModule(*module, &os)) {
            std::cerr << "codegen: LLVM module verification failed:\n" << os.str();
            return nullptr;
        }
        return module;
    }

    // ===================== object-mode lowering =====================
    // "Everything is an object": every value is a boxed MXObject* (LLVM `ptr`). Literals box
    // (mxs_box_*), operators go through dynamic dispatch (mxs_op_*), and the stdlib (println,
    // …) resolves through ordinary @@foreign bindings — NO per-function special-casing. Every
    // mxs function takes/returns ptr; `main` is the exception (returns i64, the JIT entry).
    namespace {
        using ast::MXASTNode;

        struct ObjGen {
            llvm::Module *M;
            llvm::IRBuilder<> &B;
            llvm::LLVMContext &C;
            llvm::Type *i64, *dbl, *voidTy;
            llvm::PointerType *ptr;
            const std::unordered_map<std::string, llvm::Function *> &funcs;
            std::unordered_map<std::string, llvm::AllocaInst *>
                    locals;// name -> alloca<ptr>
            llvm::Function *curFn = nullptr;
            bool inMain = false;
            std::vector<llvm::BasicBlock *> breakT, continueT;
            bool ok = true;

            void err(const std::string &m) {
                std::cerr << "object-mode: " << m << "\n";
                ok = false;
            }
            bool terminated() {
                auto *bb = B.GetInsertBlock();
                return bb && bb->getTerminator();
            }
            llvm::Function *rt(const char *name, llvm::Type *ret,
                               llvm::ArrayRef<llvm::Type *> args) {
                if (auto *f = M->getFunction(name)) return f;
                return llvm::Function::Create(llvm::FunctionType::get(ret, args, false),
                                              llvm::Function::ExternalLinkage, name, M);
            }
            llvm::BasicBlock *bb(const char *n) {
                return llvm::BasicBlock::Create(C, n, curFn);
            }
            llvm::AllocaInst *allocaTy(llvm::Type *t, const std::string &nm) {
                llvm::IRBuilder<> tmp(&curFn->getEntryBlock(),
                                      curFn->getEntryBlock().begin());
                return tmp.CreateAlloca(t, nullptr, nm);
            }
            // Box helpers.
            llvm::Value *boxBoolI1(llvm::Value *c) {
                return B.CreateCall(rt("mxs_box_bool", ptr, { i64 }),
                                    { B.CreateZExt(c, i64, "b") });
            }
            llvm::Value *boxNil() { return B.CreateCall(rt("mxs_box_nil", ptr, {}), {}); }
            // Coerce a boxed object to an i1 branch condition (mxs_obj_truthy != 0).
            llvm::Value *truthy(llvm::Value *o) {
                auto *t = B.CreateCall(rt("mxs_obj_truthy", i64, { ptr }), { o }, "t");
                return B.CreateICmpNE(t, llvm::ConstantInt::get(i64, 0), "tobool");
            }

            llvm::Value *expr(const MXASTNode *n);
            llvm::Value *shortCircuit(const ast::BinaryOp *bo);// && / ||
            void stmt(const MXASTNode *n);
            void block(const ast::Block *blk) {
                auto saved = locals;
                for (const auto &s : blk->statements) {
                    if (terminated()) break;
                    stmt(s.get());
                }
                locals = saved;// block scope ends
            }
            void emitFunction(const ast::FunctionDef *fn);
        };

        // Map an mxs binary operator to its dynamic-dispatch runtime symbol.
        const char *op_symbol(const std::string &op) {
            if (op == "+") return "mxs_op_add";
            if (op == "-") return "mxs_op_sub";
            if (op == "*") return "mxs_op_mul";
            if (op == "/") return "mxs_op_div";
            if (op == "%") return "mxs_op_mod";
            if (op == "<") return "mxs_op_lt";
            if (op == "<=") return "mxs_op_le";
            if (op == ">") return "mxs_op_gt";
            if (op == ">=") return "mxs_op_ge";
            if (op == "==") return "mxs_op_eq";
            if (op == "!=") return "mxs_op_ne";
            return nullptr;
        }

        llvm::Value *ObjGen::expr(const MXASTNode *n) {
            if (const auto *il = dynamic_cast<const ast::IntegerLiteral *>(n))
                return B.CreateCall(rt("mxs_box_int", ptr, { i64 }),
                                    { llvm::ConstantInt::get(i64, il->value, true) });
            if (const auto *fl = dynamic_cast<const ast::FloatLiteral *>(n))
                return B.CreateCall(rt("mxs_box_float", ptr, { dbl }),
                                    { llvm::ConstantFP::get(dbl, fl->value) });
            if (const auto *bl = dynamic_cast<const ast::BooleanLiteral *>(n))
                return B.CreateCall(rt("mxs_box_bool", ptr, { i64 }),
                                    { llvm::ConstantInt::get(i64, bl->value) });
            if (dynamic_cast<const ast::NilLiteral *>(n)) return boxNil();
            if (const auto *sl = dynamic_cast<const ast::StringLiteral *>(n))
                return B.CreateCall(rt("mxs_box_str", ptr, { ptr }),
                                    { B.CreateGlobalStringPtr(sl->value, "str") });
            if (const auto *id = dynamic_cast<const ast::Identifier *>(n)) {
                auto it = locals.find(id->name);
                if (it == locals.end()) {
                    err("unknown identifier '" + id->name + "'");
                    return nullptr;
                }
                return B.CreateLoad(ptr, it->second, id->name);
            }
            if (const auto *bo = dynamic_cast<const ast::BinaryOp *>(n)) {
                const std::string &op = bo->op;
                if (op == "&&" || op == "||") return shortCircuit(bo);
                // Assignment (plain or compound) — target must be a bound variable.
                if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=") {
                    const auto *lid =
                            dynamic_cast<const ast::Identifier *>(bo->left.get());
                    if (!lid) {
                        err("assignment target must be a variable");
                        return nullptr;
                    }
                    auto it = locals.find(lid->name);
                    if (it == locals.end()) {
                        err("assignment to unknown variable '" + lid->name + "'");
                        return nullptr;
                    }
                    llvm::Value *rhs = expr(bo->right.get());
                    if (!rhs) return nullptr;
                    if (op != "=") {
                        llvm::Value *cur = B.CreateLoad(ptr, it->second);
                        rhs = B.CreateCall(
                                rt(op_symbol(op.substr(0, 1)), ptr, { ptr, ptr }),
                                { cur, rhs });
                    }
                    B.CreateStore(rhs, it->second);
                    return rhs;
                }
                if (const char *sym = op_symbol(op)) {
                    llvm::Value *l = expr(bo->left.get());
                    llvm::Value *r = expr(bo->right.get());
                    if (!l || !r) return nullptr;
                    return B.CreateCall(rt(sym, ptr, { ptr, ptr }), { l, r });
                }
                err("unsupported binary operator '" + op + "'");
                return nullptr;
            }
            if (const auto *uo = dynamic_cast<const ast::UnaryOp *>(n)) {
                llvm::Value *v = expr(uo->operand.get());
                if (!v) return nullptr;
                if (uo->op == "-")
                    return B.CreateCall(rt("mxs_op_neg", ptr, { ptr }), { v });
                if (uo->op == "!")
                    return B.CreateCall(rt("mxs_op_not", ptr, { ptr }), { v });
                return v;// unary '+'
            }
            if (const auto *call = dynamic_cast<const ast::FunctionCall *>(n)) {
                auto it = funcs.find(call->name);
                if (it == funcs.end()) {
                    err("call to unknown function '" + call->name + "'");
                    return nullptr;
                }
                llvm::Function *f = it->second;
                if (call->args.size() != f->arg_size()) {
                    err("call to '" + call->name + "' expects " +
                        std::to_string(f->arg_size()) + " argument(s), got " +
                        std::to_string(call->args.size()));
                    return nullptr;
                }
                std::vector<llvm::Value *> argv;
                argv.reserve(call->args.size());
                for (const auto &a : call->args) {
                    llvm::Value *v = expr(a.get());
                    if (!v) return nullptr;
                    argv.push_back(v);
                }
                if (f->getReturnType()->isVoidTy()) {
                    B.CreateCall(f, argv);
                    return boxNil();// every expression yields an object
                }
                return B.CreateCall(f, argv, "call");
            }
            err("unsupported expression");
            return nullptr;
        }

        // Short-circuit && / || producing a boxed bool.
        llvm::Value *ObjGen::shortCircuit(const ast::BinaryOp *bo) {
            const bool isAnd = bo->op == "&&";
            llvm::Value *l = expr(bo->left.get());
            if (!l) return nullptr;
            llvm::Value *lc = truthy(l);
            auto *entryBB = B.GetInsertBlock();
            auto *rhsBB = bb(isAnd ? "and.rhs" : "or.rhs");
            auto *contBB = bb(isAnd ? "and.cont" : "or.cont");
            // &&: if l false short to cont; ||: if l true short to cont.
            if (isAnd) B.CreateCondBr(lc, rhsBB, contBB);
            else
                B.CreateCondBr(lc, contBB, rhsBB);
            B.SetInsertPoint(rhsBB);
            llvm::Value *r = expr(bo->right.get());
            if (!r) return nullptr;
            llvm::Value *rc = truthy(r);
            auto *rhsEnd = B.GetInsertBlock();
            B.CreateBr(contBB);
            B.SetInsertPoint(contBB);
            auto *phi = B.CreatePHI(llvm::Type::getInt1Ty(C), 2, "logic");
            phi->addIncoming(isAnd ? llvm::ConstantInt::getFalse(C)
                                   : llvm::ConstantInt::getTrue(C),
                             entryBB);
            phi->addIncoming(rc, rhsEnd);
            return boxBoolI1(phi);
        }

        void ObjGen::stmt(const MXASTNode *n) {
            if (const auto *blk = dynamic_cast<const ast::Block *>(n)) {
                block(blk);
                return;
            }
            if (const auto *ls = dynamic_cast<const ast::LetStatement *>(n)) {
                llvm::Value *v = ls->value ? expr(ls->value.get()) : boxNil();
                if (!v) return;
                for (const auto &nm : ls->names) {
                    auto *a = allocaTy(ptr, nm);
                    B.CreateStore(v, a);
                    locals[nm] = a;
                }
                return;
            }
            if (const auto *es = dynamic_cast<const ast::ExprStatement *>(n)) {
                if (es->expr) expr(es->expr.get());
                return;
            }
            if (const auto *rs = dynamic_cast<const ast::ReturnStatement *>(n)) {
                if (inMain) {
                    if (rs->value) {
                        llvm::Value *v = expr(rs->value.get());
                        if (!v) return;
                        B.CreateRet(
                                B.CreateCall(rt("mxs_obj_as_int", i64, { ptr }), { v }));
                    } else {
                        B.CreateRet(llvm::ConstantInt::get(i64, 0));
                    }
                    return;
                }
                if (curFn->getReturnType()->isVoidTy()) {
                    if (rs->value) expr(rs->value.get());// side effects only
                    B.CreateRetVoid();
                } else {
                    llvm::Value *v = rs->value ? expr(rs->value.get()) : boxNil();
                    if (v) B.CreateRet(v);
                }
                return;
            }
            if (const auto *is = dynamic_cast<const ast::IfStatement *>(n)) {
                llvm::Value *cv = is->condition ? expr(is->condition.get()) : nullptr;
                llvm::Value *cond = cv ? truthy(cv) : llvm::ConstantInt::getFalse(C);
                auto *thenBB = bb("then");
                auto *mergeBB = llvm::BasicBlock::Create(C, "ifcont");
                auto *elseBB =
                        is->elseBranch ? llvm::BasicBlock::Create(C, "else") : mergeBB;
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
                B.CreateCondBr(cv ? truthy(cv) : llvm::ConstantInt::getTrue(C), afterBB,
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
                B.CreateCondBr(cv ? truthy(cv) : llvm::ConstantInt::getTrue(C), afterBB,
                               bodyBB);
                curFn->insert(curFn->end(), afterBB);
                B.SetInsertPoint(afterBB);
                return;
            }
            if (const auto *fs = dynamic_cast<const ast::ForInStatement *>(n)) {
                const auto *range =
                        dynamic_cast<const ast::BinaryOp *>(fs->iterable.get());
                if (!range || range->op != "..") {
                    err("for-in only supports integer ranges (lo..hi)");
                    return;
                }
                llvm::Value *loObj = expr(range->left.get());
                llvm::Value *hiObj = expr(range->right.get());
                if (!loObj || !hiObj) return;
                auto *asInt = rt("mxs_obj_as_int", i64, { ptr });
                llvm::Value *lo = B.CreateCall(asInt, { loObj });
                llvm::Value *hi = B.CreateCall(asInt, { hiObj });
                auto *ctr = allocaTy(i64, fs->var + ".i");
                auto *box = allocaTy(ptr, fs->var);// the object loop variable
                B.CreateStore(lo, ctr);
                // The loop variable is scoped to the for-in; snapshot so it neither leaks
                // out nor clobbers a same-named binding in the enclosing scope.
                auto saved = locals;
                locals[fs->var] = box;
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
                B.CreateStore(B.CreateCall(rt("mxs_box_int", ptr, { i64 }), { cur }),
                              box);
                continueT.push_back(incrBB);
                breakT.push_back(afterBB);
                if (fs->body) block(fs->body.get());
                if (!terminated()) B.CreateBr(incrBB);
                continueT.pop_back();
                breakT.pop_back();
                curFn->insert(curFn->end(), incrBB);
                B.SetInsertPoint(incrBB);
                llvm::Value *next = B.CreateAdd(B.CreateLoad(i64, ctr),
                                                llvm::ConstantInt::get(i64, 1), "inc");
                B.CreateStore(next, ctr);
                B.CreateBr(condBB);
                curFn->insert(curFn->end(), afterBB);
                B.SetInsertPoint(afterBB);
                locals = saved;// loop variable goes out of scope
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
                B.CreateCondBr(truthy(v), okBB, failBB);
                B.SetInsertPoint(failBB);
                B.CreateCall(rt("mxs_panic", voidTy, { ptr }),
                             { B.CreateGlobalStringPtr("assertion failed") });
                B.CreateUnreachable();
                B.SetInsertPoint(okBB);
                return;
            }
            err("unsupported statement in object mode");
        }

        void ObjGen::emitFunction(const ast::FunctionDef *fn) {
            curFn = funcs.at(fn->name);
            inMain = fn->name == "main";
            locals.clear();
            B.SetInsertPoint(llvm::BasicBlock::Create(C, "entry", curFn));
            unsigned i = 0;
            for (auto &arg : curFn->args()) {
                if (i >= fn->params.size()) break;
                auto *a = allocaTy(ptr, fn->params[i]->name);
                B.CreateStore(&arg, a);
                locals[fn->params[i]->name] = a;
                ++i;
            }
            if (fn->body) block(fn->body.get());
            if (!terminated()) {
                if (inMain) B.CreateRet(llvm::ConstantInt::get(i64, 0));
                else if (curFn->getReturnType()->isVoidTy())
                    B.CreateRetVoid();
                else
                    B.CreateRet(boxNil());
            }
        }
    }// namespace

    std::unique_ptr<llvm::Module> compile_obj(const ast::TranslationUnit &tu,
                                              llvm::LLVMContext &llvmContext,
                                              const std::string &moduleName) {
        auto module = std::make_unique<llvm::Module>(moduleName, llvmContext);
        llvm::IRBuilder<> B(llvmContext);
        auto *i64 = llvm::Type::getInt64Ty(llvmContext);
        auto *voidTy = llvm::Type::getVoidTy(llvmContext);
        auto *ptr = llvm::PointerType::get(llvmContext, 0);

        std::unordered_map<std::string, llvm::Function *> funcs;
        bool hasMain = false;

        // Pass 1: declare every function prototype. In object mode every parameter is a boxed
        // MXObject* (ptr); the return is ptr too, except `main` (i64 — the JIT entry) and
        // explicit `-> nil` functions (void). @@foreign binds to its external C-ABI symbol.
        for (const auto &s : tu.statements) {
            const auto *fn = dynamic_cast<const ast::FunctionDef *>(s.get());
            if (!fn) continue;
            std::vector<llvm::Type *> argTys(fn->params.size(), ptr);
            llvm::Type *retTy = fn->name == "main" ? i64
                                : (fn->returnTypeName && *fn->returnTypeName == "nil")
                                        ? voidTy
                                        : ptr;
            auto *fty = llvm::FunctionType::get(retTy, argTys, false);
            const std::string symbol =
                    fn->isForeign
                            ? (fn->foreignSymbol.empty() ? fn->name : fn->foreignSymbol)
                            : fn->name;
            auto *f = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, symbol,
                                             module.get());
            unsigned i = 0;
            for (auto &arg : f->args())
                if (i < fn->params.size()) arg.setName(fn->params[i++]->name);
            funcs[fn->name] = f;
            if (fn->name == "main") hasMain = true;
        }
        if (!hasMain) {
            std::cerr << "object-mode: program has no main()\n";
            return nullptr;
        }

        // Pass 2: emit bodies (foreign declarations have none).
        ObjGen g{
            module.get(), B,   llvmContext, i64, llvm::Type::getDoubleTy(llvmContext),
            voidTy,       ptr, funcs
        };
        for (const auto &s : tu.statements)
            if (const auto *fn = dynamic_cast<const ast::FunctionDef *>(s.get()))
                if (!fn->isForeign) g.emitFunction(fn);
        if (!g.ok) return nullptr;

        std::string err;
        llvm::raw_string_ostream os(err);
        if (llvm::verifyModule(*module, &os)) {
            std::cerr << "object-mode codegen: verification failed:\n" << os.str();
            return nullptr;
        }
        return module;
    }

    // ===================== new object-model lowering (core types) =====================
    // The rewired codegen (progress09 ④). Values are real core::MXObject* and operators emit the
    // typed core ABI (mxs_int_*, …) defined in core.bc, which the JIT links in so LLVM can inline
    // across the boundary (D6). Variables are MXLeftValue binding cells (`let` immutable,
    // `let mut` mutable; assignment goes through mxs_lvalue_update). The stdlib (println, …)
    // resolves via @@foreign — no per-function hardcoding (D3). Arithmetic/compare currently lower
    // to the integer ABI (full cross-type dynamic dispatch is a later step); strings/floats/bools/
    // nil literals and containers are constructed via their own ABIs.
    namespace {
        // Binary operator -> dynamic-dispatch core ABI symbol (docs §8): these inspect operand
        // types at runtime (int/float/string), so mixed-type arithmetic and comparisons work.
        const char *core_op(const std::string &op) {
            if (op == "+") return "mxs_op_add";
            if (op == "-") return "mxs_op_sub";
            if (op == "*") return "mxs_op_mul";
            if (op == "**") return "mxs_op_pow";
            if (op == "/") return "mxs_op_div";
            if (op == "%") return "mxs_op_mod";
            if (op == "<") return "mxs_op_lt";
            if (op == "<=") return "mxs_op_le";
            if (op == ">") return "mxs_op_gt";
            if (op == ">=") return "mxs_op_ge";
            if (op == "==") return "mxs_op_eq";
            if (op == "!=") return "mxs_op_ne";
            return nullptr;
        }

        struct CoreGen {
            llvm::Module *M;
            llvm::IRBuilder<> &B;
            llvm::LLVMContext &C;
            llvm::Type *i64, *dbl, *voidTy;
            llvm::PointerType *ptr;
            const std::unordered_map<std::string, llvm::Function *> &funcs;
            std::unordered_map<std::string, llvm::AllocaInst *>
                    locals;// name -> alloca<MXLeftValue*>
            llvm::Function *curFn = nullptr;
            bool inMain = false;
            std::vector<llvm::BasicBlock *> breakT, continueT;
            bool ok = true;

            void err(const std::string &m) {
                std::cerr << "core-codegen: " << m << "\n";
                ok = false;
            }
            llvm::Function *rt(const char *name, llvm::Type *ret,
                               llvm::ArrayRef<llvm::Type *> args) {
                if (auto *f = M->getFunction(name)) return f;
                return llvm::Function::Create(llvm::FunctionType::get(ret, args, false),
                                              llvm::Function::ExternalLinkage, name, M);
            }
            bool terminated() {
                auto *bb = B.GetInsertBlock();
                return bb && bb->getTerminator();
            }
            llvm::BasicBlock *bb(const char *n) {
                return llvm::BasicBlock::Create(C, n, curFn);
            }
            llvm::AllocaInst *allocaTy(llvm::Type *t, const std::string &nm) {
                llvm::IRBuilder<> tmp(&curFn->getEntryBlock(),
                                      curFn->getEntryBlock().begin());
                return tmp.CreateAlloca(t, nullptr, nm);
            }
            llvm::Value *nil() { return B.CreateCall(rt("mxs_nil_new", ptr, {}), {}); }
            // i1 truthiness of a boxed object (mxs_object_truthy != 0).
            llvm::Value *truthy(llvm::Value *o) {
                auto *t = B.CreateCall(rt("mxs_object_truthy", i64, { ptr }), { o }, "t");
                return B.CreateICmpNE(t, llvm::ConstantInt::get(i64, 0), "tobool");
            }
            llvm::Value *boolFromI1(llvm::Value *c) {
                return B.CreateCall(rt("mxs_bool_new", ptr, { i64 }),
                                    { B.CreateZExt(c, i64, "b") });
            }
            // Create a binding cell owning `value`; record it under `name`.
            void bind(const std::string &name, llvm::Value *value, bool mutable_) {
                auto *cell =
                        B.CreateCall(rt("mxs_lvalue_new", ptr, { ptr, i64 }),
                                     { value, llvm::ConstantInt::get(i64, mutable_) });
                auto *slot = allocaTy(ptr, name);
                B.CreateStore(cell, slot);
                locals[name] = slot;
            }

            llvm::Value *expr(const ast::MXASTNode *n);
            llvm::Value *shortCircuit(const ast::BinaryOp *bo);
            void stmt(const ast::MXASTNode *n);
            void block(const ast::Block *blk) {
                auto saved = locals;
                for (const auto &s : blk->statements) {
                    if (terminated()) break;
                    stmt(s.get());
                }
                locals = saved;
            }
            // A block evaluated as an expression (a match arm): its value is the last
            // expression-statement's value (other statements run for their effects).
            llvm::Value *blockValue(const ast::Block *blk) {
                auto saved = locals;
                llvm::Value *val = nil();
                for (const auto &s : blk->statements) {
                    if (terminated()) break;
                    if (const auto *es =
                                dynamic_cast<const ast::ExprStatement *>(s.get()))
                        val = es->expr ? expr(es->expr.get()) : nil();
                    else {
                        stmt(s.get());
                        val = nil();
                    }
                }
                locals = saved;
                return val;
            }
            void emitFunction(const ast::FunctionDef *fn);
        };

        llvm::Value *CoreGen::shortCircuit(const ast::BinaryOp *bo) {
            const bool isAnd = bo->op == "&&";
            llvm::Value *l = expr(bo->left.get());
            if (!l) return nullptr;
            llvm::Value *lc = truthy(l);
            auto *entryBB = B.GetInsertBlock();
            auto *rhsBB = bb(isAnd ? "and.rhs" : "or.rhs");
            auto *contBB = bb(isAnd ? "and.cont" : "or.cont");
            if (isAnd) B.CreateCondBr(lc, rhsBB, contBB);
            else
                B.CreateCondBr(lc, contBB, rhsBB);
            B.SetInsertPoint(rhsBB);
            llvm::Value *r = expr(bo->right.get());
            if (!r) return nullptr;
            llvm::Value *rc = truthy(r);
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
                        B.CreateCall(rt("mxs_arraylist_new", ptr, {}), {}, "list");
                auto *appendFn = rt("mxs_arraylist_append", voidTy, { ptr, ptr });
                for (const auto &el : ll->elements) {
                    llvm::Value *v = expr(el.get());
                    if (!v) return nullptr;
                    B.CreateCall(appendFn, { list, v });
                }
                return list;
            }
            if (const auto *ix = dynamic_cast<const ast::IndexExpr *>(n)) {
                llvm::Value *t = expr(ix->target.get());
                llvm::Value *i = expr(ix->index.get());
                if (!t || !i) return nullptr;
                return B.CreateCall(rt("mxs_arraylist_get", ptr, { ptr, ptr }), { t, i });
            }
            if (const auto *id = dynamic_cast<const ast::Identifier *>(n)) {
                auto it = locals.find(id->name);
                if (it == locals.end()) {
                    err("unknown identifier '" + id->name + "'");
                    return nullptr;
                }
                auto *cell = B.CreateLoad(ptr, it->second, id->name);
                return B.CreateCall(rt("mxs_lvalue_rvalue", ptr, { ptr }), { cell },
                                    "rv");
            }
            if (const auto *bo = dynamic_cast<const ast::BinaryOp *>(n)) {
                const std::string &op = bo->op;
                if (op == "&&" || op == "||") return shortCircuit(bo);
                if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=") {
                    // Subscript assignment: `xs[i] = v` (and compound forms).
                    if (const auto *ix =
                                dynamic_cast<const ast::IndexExpr *>(bo->left.get())) {
                        llvm::Value *tgt = expr(ix->target.get());
                        llvm::Value *idx = expr(ix->index.get());
                        llvm::Value *rhs = expr(bo->right.get());
                        if (!tgt || !idx || !rhs) return nullptr;
                        if (op != "=") {
                            auto *cur = B.CreateCall(
                                    rt("mxs_arraylist_get", ptr, { ptr, ptr }),
                                    { tgt, idx });
                            rhs = B.CreateCall(
                                    rt(core_op(op.substr(0, 1)), ptr, { ptr, ptr }),
                                    { cur, rhs });
                        }
                        B.CreateCall(rt("mxs_arraylist_set", ptr, { ptr, ptr, ptr }),
                                     { tgt, idx, rhs });
                        return rhs;
                    }
                    const auto *lid =
                            dynamic_cast<const ast::Identifier *>(bo->left.get());
                    if (!lid) {
                        err("assignment target must be a variable");
                        return nullptr;
                    }
                    auto it = locals.find(lid->name);
                    if (it == locals.end()) {
                        err("assignment to unknown variable '" + lid->name + "'");
                        return nullptr;
                    }
                    llvm::Value *rhs = expr(bo->right.get());
                    if (!rhs) return nullptr;
                    auto *cell = B.CreateLoad(ptr, it->second, lid->name);
                    if (op != "=") {
                        auto *cur = B.CreateCall(rt("mxs_lvalue_rvalue", ptr, { ptr }),
                                                 { cell }, "rv");
                        rhs = B.CreateCall(
                                rt(core_op(op.substr(0, 1)), ptr, { ptr, ptr }),
                                { cur, rhs });
                    }
                    // mxs_lvalue_update enforces immutability at runtime (returns an MXError on
                    // a `let` binding); the result is the assignment's value.
                    B.CreateCall(rt("mxs_lvalue_update", ptr, { ptr, ptr }),
                                 { cell, rhs });
                    return rhs;
                }
                if (const char *sym = core_op(op)) {
                    llvm::Value *l = expr(bo->left.get());
                    llvm::Value *r = expr(bo->right.get());
                    if (!l || !r) return nullptr;
                    return B.CreateCall(rt(sym, ptr, { ptr, ptr }), { l, r });
                }
                err("unsupported binary operator '" + op + "'");
                return nullptr;
            }
            if (const auto *uo = dynamic_cast<const ast::UnaryOp *>(n)) {
                llvm::Value *v = expr(uo->operand.get());
                if (!v) return nullptr;
                if (uo->op == "-")
                    return B.CreateCall(rt("mxs_op_neg", ptr, { ptr }), { v });
                if (uo->op == "!")
                    return B.CreateCall(rt("mxs_op_not", ptr, { ptr }), { v });
                return v;// unary '+'
            }
            if (const auto *call = dynamic_cast<const ast::FunctionCall *>(n)) {
                auto it = funcs.find(call->name);
                if (it == funcs.end()) {
                    err("call to unknown function '" + call->name + "'");
                    return nullptr;
                }
                llvm::Function *f = it->second;
                if (call->args.size() != f->arg_size()) {
                    err("call to '" + call->name + "' expects " +
                        std::to_string(f->arg_size()) + " argument(s), got " +
                        std::to_string(call->args.size()));
                    return nullptr;
                }
                std::vector<llvm::Value *> argv;
                for (const auto &a : call->args) {
                    llvm::Value *v = expr(a.get());
                    if (!v) return nullptr;
                    argv.push_back(v);
                }
                if (f->getReturnType()->isVoidTy()) {
                    B.CreateCall(f, argv);
                    return nil();
                }
                return B.CreateCall(f, argv, "call");
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
                        matches = B.CreateICmpNE(t, llvm::ConstantInt::get(i64, 0),
                                                 "tymatch");
                    } else if (cs.literal) {
                        llvm::Value *lit = expr(cs.literal.get());
                        if (!lit) return nullptr;
                        matches = truthy(B.CreateCall(rt("mxs_op_eq", ptr, { ptr, ptr }),
                                                      { subj, lit }));
                    } else {
                        matches =
                                llvm::ConstantInt::getTrue(C);// wildcard or plain binding
                    }
                    B.CreateCondBr(matches, bodyBB, nextBB);
                    B.SetInsertPoint(bodyBB);
                    auto saved = locals;
                    if (!cs.binding.empty()) bind(cs.binding, subj, /*mutable=*/false);
                    const auto *blk = dynamic_cast<const ast::Block *>(cs.body.get());
                    llvm::Value *val = blk ? blockValue(blk) : expr(cs.body.get());
                    if (!terminated()) {
                        if (val) B.CreateStore(val, result);
                        B.CreateBr(mergeBB);
                    }
                    locals = saved;
                    curFn->insert(curFn->end(), nextBB);
                    B.SetInsertPoint(nextBB);
                }
                B.CreateBr(mergeBB);// no case matched -> result stays nil
                curFn->insert(curFn->end(), mergeBB);
                B.SetInsertPoint(mergeBB);
                return B.CreateLoad(ptr, result, "match.val");
            }
            err("unsupported expression");
            return nullptr;
        }

        void CoreGen::stmt(const ast::MXASTNode *n) {
            if (const auto *blk = dynamic_cast<const ast::Block *>(n)) {
                block(blk);
                return;
            }
            if (const auto *ls = dynamic_cast<const ast::LetStatement *>(n)) {
                llvm::Value *v = ls->value ? expr(ls->value.get()) : nil();
                if (!v) return;
                for (const auto &nm : ls->names) bind(nm, v, ls->isMut);
                return;
            }
            if (const auto *es = dynamic_cast<const ast::ExprStatement *>(n)) {
                if (es->expr) expr(es->expr.get());
                return;
            }
            if (const auto *rs = dynamic_cast<const ast::ReturnStatement *>(n)) {
                if (inMain) {
                    if (rs->value) {
                        llvm::Value *v = expr(rs->value.get());
                        if (!v) return;
                        B.CreateRet(
                                B.CreateCall(rt("mxs_int_to_i64", i64, { ptr }), { v }));
                    } else {
                        B.CreateRet(llvm::ConstantInt::get(i64, 0));
                    }
                    return;
                }
                if (curFn->getReturnType()->isVoidTy()) {
                    if (rs->value) expr(rs->value.get());
                    B.CreateRetVoid();
                } else {
                    llvm::Value *v = rs->value ? expr(rs->value.get()) : nil();
                    if (v) B.CreateRet(v);
                }
                return;
            }
            if (const auto *is = dynamic_cast<const ast::IfStatement *>(n)) {
                llvm::Value *cv = is->condition ? expr(is->condition.get()) : nullptr;
                llvm::Value *cond = cv ? truthy(cv) : llvm::ConstantInt::getFalse(C);
                auto *thenBB = bb("then");
                auto *mergeBB = llvm::BasicBlock::Create(C, "ifcont");
                auto *elseBB =
                        is->elseBranch ? llvm::BasicBlock::Create(C, "else") : mergeBB;
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
                B.CreateCondBr(cv ? truthy(cv) : llvm::ConstantInt::getTrue(C), afterBB,
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
                B.CreateCondBr(cv ? truthy(cv) : llvm::ConstantInt::getTrue(C), afterBB,
                               bodyBB);
                curFn->insert(curFn->end(), afterBB);
                B.SetInsertPoint(afterBB);
                return;
            }
            if (const auto *fs = dynamic_cast<const ast::ForInStatement *>(n)) {
                // `for v in lo..hi` (integer range) or `for v in xs` (iterate a container by
                // index). Both run a hidden i64 counter; the loop var is rebound each iteration.
                const auto *range =
                        dynamic_cast<const ast::BinaryOp *>(fs->iterable.get());
                const bool isRange = range && range->op == "..";
                auto *toI64 = rt("mxs_int_to_i64", i64, { ptr });
                llvm::Value *lo = nullptr, *hi = nullptr, *listObj = nullptr;
                if (isRange) {
                    llvm::Value *loObj = expr(range->left.get());
                    llvm::Value *hiObj = expr(range->right.get());
                    if (!loObj || !hiObj) return;
                    lo = B.CreateCall(toI64, { loObj });
                    hi = B.CreateCall(toI64, { hiObj });
                } else {
                    listObj = expr(fs->iterable.get());
                    if (!listObj) return;
                    lo = llvm::ConstantInt::get(i64, 0);
                    hi = B.CreateCall(
                            toI64, { B.CreateCall(rt("mxs_arraylist_len", ptr, { ptr }),
                                                  { listObj }) });
                }
                auto *ctr = allocaTy(i64, fs->var + ".i");
                B.CreateStore(lo, ctr);
                auto saved = locals;
                auto *box = allocaTy(ptr, fs->var);// the loop var's binding cell
                locals[fs->var] = box;
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
                auto *ctrObj =
                        B.CreateCall(rt("mxs_int_from_i64", ptr, { i64 }), { cur });
                llvm::Value *iv =
                        isRange ? ctrObj
                                : B.CreateCall(rt("mxs_arraylist_get", ptr, { ptr, ptr }),
                                               { listObj, ctrObj });
                auto *cell = B.CreateCall(rt("mxs_lvalue_new", ptr, { ptr, i64 }),
                                          { iv, llvm::ConstantInt::get(i64, 0) });
                B.CreateStore(cell, box);
                continueT.push_back(incrBB);
                breakT.push_back(afterBB);
                if (fs->body) block(fs->body.get());
                if (!terminated()) B.CreateBr(incrBB);
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
                locals = saved;
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
                B.CreateCondBr(truthy(v), okBB, failBB);
                B.SetInsertPoint(failBB);
                B.CreateCall(rt("mxs_panic", voidTy, { ptr }),
                             { B.CreateGlobalStringPtr("assertion failed") });
                B.CreateUnreachable();
                B.SetInsertPoint(okBB);
                return;
            }
            err("unsupported statement");
        }

        void CoreGen::emitFunction(const ast::FunctionDef *fn) {
            curFn = funcs.at(fn->name);
            inMain = fn->name == "main";
            locals.clear();
            B.SetInsertPoint(llvm::BasicBlock::Create(C, "entry", curFn));
            unsigned i = 0;
            for (auto &arg : curFn->args()) {
                if (i >= fn->params.size()) break;
                bind(fn->params[i]->name, &arg, /*mutable=*/false);// params are immutable
                ++i;
            }
            if (fn->body) block(fn->body.get());
            if (!terminated()) {
                if (inMain) B.CreateRet(llvm::ConstantInt::get(i64, 0));
                else if (curFn->getReturnType()->isVoidTy())
                    B.CreateRetVoid();
                else
                    B.CreateRet(nil());
            }
        }
    }// namespace

    std::unique_ptr<llvm::Module> compile_core(const ast::TranslationUnit &tu,
                                               llvm::LLVMContext &llvmContext,
                                               const std::string &moduleName) {
        auto module = std::make_unique<llvm::Module>(moduleName, llvmContext);
        llvm::IRBuilder<> B(llvmContext);
        auto *i64 = llvm::Type::getInt64Ty(llvmContext);
        auto *voidTy = llvm::Type::getVoidTy(llvmContext);
        auto *ptr = llvm::PointerType::get(llvmContext, 0);

        std::unordered_map<std::string, llvm::Function *> funcs;
        bool hasMain = false;
        for (const auto &s : tu.statements) {
            const auto *fn = dynamic_cast<const ast::FunctionDef *>(s.get());
            if (!fn) continue;
            std::vector<llvm::Type *> argTys(fn->params.size(), ptr);
            llvm::Type *ret =
                    fn->name == "main"
                            ? i64
                            : ((fn->returnTypeName && *fn->returnTypeName == "nil")
                                       ? voidTy
                                       : ptr);
            const std::string sym =
                    fn->isForeign
                            ? (fn->foreignSymbol.empty() ? fn->name : fn->foreignSymbol)
                            : fn->name;
            auto *f = llvm::Function::Create(llvm::FunctionType::get(ret, argTys, false),
                                             llvm::Function::ExternalLinkage, sym,
                                             module.get());
            funcs[fn->name] = f;
            if (fn->name == "main") hasMain = true;
        }
        if (!hasMain) {
            std::cerr << "core-codegen: program has no main()\n";
            return nullptr;
        }
        CoreGen g{
            module.get(), B,   llvmContext, i64, llvm::Type::getDoubleTy(llvmContext),
            voidTy,       ptr, funcs
        };
        for (const auto &s : tu.statements)
            if (const auto *fn = dynamic_cast<const ast::FunctionDef *>(s.get()))
                if (!fn->isForeign) g.emitFunction(fn);
        if (!g.ok) return nullptr;

        std::string err;
        llvm::raw_string_ostream os(err);
        if (llvm::verifyModule(*module, &os)) {
            std::cerr << "core-codegen: verification failed:\n" << os.str();
            return nullptr;
        }
        return module;
    }

}// namespace mxs::backend::codegen
