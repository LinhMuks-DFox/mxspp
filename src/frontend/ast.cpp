#include "mxspp/frontend/ast.h"
#include "mxspp/core/MXObject.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>

#include <iostream>

// First-slice codegen: a statically-typed numeric subset (int=i64, float=f64, bool=i1,
// nil=void) — functions, let/assign, return, if/else, loops, arithmetic/compare/logic and
// direct calls. The object model (MXObject, strings as objects, dynamic dispatch, OOP
// member codegen) is intentionally NOT here yet — those need the runtime. OOP/match nodes
// keep no-op codegen stubs so they remain instantiable.

namespace cg = mxs::backend::codegen;

namespace mxs::backend::codegen {
    llvm::Type *map_type(llvm::LLVMContext &c, const std::string &name) {
        if (name == "int") return llvm::Type::getInt64Ty(c);
        if (name == "float") return llvm::Type::getDoubleTy(c);
        if (name == "bool") return llvm::Type::getInt1Ty(c);
        if (name == "nil") return llvm::Type::getVoidTy(c);
        return llvm::Type::getInt64Ty(c);// unknown/user type -> i64 placeholder for now
    }
}

namespace {
    using cg::CodegenContext;

    bool is_float(llvm::Value *v) { return v && v->getType()->isFloatingPointTy(); }

    // Coerce a value into an i1 branch condition.
    llvm::Value *to_cond(CodegenContext &ctx, llvm::Value *v) {
        auto &b = *ctx.builder;
        if (!v) return llvm::ConstantInt::getFalse(ctx.llvmContext);
        llvm::Type *t = v->getType();
        if (t->isIntegerTy(1)) return v;
        if (t->isIntegerTy())
            return b.CreateICmpNE(v, llvm::ConstantInt::get(t, 0), "tobool");
        if (t->isFloatingPointTy())
            return b.CreateFCmpONE(v, llvm::ConstantFP::get(t, 0.0), "tobool");
        return llvm::ConstantInt::getFalse(ctx.llvmContext);
    }

    // An alloca in the function entry block (so mem2reg can promote it later).
    llvm::AllocaInst *entry_alloca(CodegenContext &ctx, llvm::Function *fn,
                                   const std::string &name, llvm::Type *ty) {
        llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
        return tmp.CreateAlloca(ty, nullptr, name);
    }

    // Emit a binary numeric/comparison/logic op. Promotes int->float on mixed operands.
    llvm::Value *emit_binop(CodegenContext &ctx, const std::string &op, llvm::Value *l,
                            llvm::Value *r) {
        auto &b = *ctx.builder;
        if (!l || !r) return nullptr;
        if (op == "&&") return b.CreateAnd(to_cond(ctx, l), to_cond(ctx, r), "and");
        if (op == "||") return b.CreateOr(to_cond(ctx, l), to_cond(ctx, r), "or");
        if (is_float(l) || is_float(r)) {
            if (!is_float(l)) l = b.CreateSIToFP(l, r->getType(), "promote");
            if (!is_float(r)) r = b.CreateSIToFP(r, l->getType(), "promote");
            if (op == "+") return b.CreateFAdd(l, r, "fadd");
            if (op == "-") return b.CreateFSub(l, r, "fsub");
            if (op == "*") return b.CreateFMul(l, r, "fmul");
            if (op == "/") return b.CreateFDiv(l, r, "fdiv");
            if (op == "%") return b.CreateFRem(l, r, "frem");
            if (op == "<") return b.CreateFCmpOLT(l, r, "lt");
            if (op == ">") return b.CreateFCmpOGT(l, r, "gt");
            if (op == "<=") return b.CreateFCmpOLE(l, r, "le");
            if (op == ">=") return b.CreateFCmpOGE(l, r, "ge");
            if (op == "==") return b.CreateFCmpOEQ(l, r, "eq");
            if (op == "!=") return b.CreateFCmpONE(l, r, "ne");
        } else {
            if (op == "+") return b.CreateAdd(l, r, "add");
            if (op == "-") return b.CreateSub(l, r, "sub");
            if (op == "*") return b.CreateMul(l, r, "mul");
            if (op == "/") return b.CreateSDiv(l, r, "div");
            if (op == "%") return b.CreateSRem(l, r, "rem");
            if (op == "<") return b.CreateICmpSLT(l, r, "lt");
            if (op == ">") return b.CreateICmpSGT(l, r, "gt");
            if (op == "<=") return b.CreateICmpSLE(l, r, "le");
            if (op == ">=") return b.CreateICmpSGE(l, r, "ge");
            if (op == "==") return b.CreateICmpEQ(l, r, "eq");
            if (op == "!=") return b.CreateICmpNE(l, r, "ne");
        }
        return nullptr;// ".." (range) or unsupported op
    }

    bool terminated(CodegenContext &ctx) {
        auto *bb = ctx.builder->GetInsertBlock();
        return bb && bb->getTerminator();
    }
}// namespace

namespace mxs::frontend::ast {

    // ===================== Expressions =====================
    IntegerLiteral::IntegerLiteral(int64_t value, bool is_static)
        : core::MXObject(is_static), MXASTNode(is_static), value(value) { }

    llvm::Value *IntegerLiteral::codegen(CodegenContext &ctx) const {
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx.llvmContext), value, true);
    }
    llvm::Value *FloatLiteral::codegen(CodegenContext &ctx) const {
        return llvm::ConstantFP::get(llvm::Type::getDoubleTy(ctx.llvmContext), value);
    }
    llvm::Value *BooleanLiteral::codegen(CodegenContext &ctx) const {
        return llvm::ConstantInt::get(llvm::Type::getInt1Ty(ctx.llvmContext), value);
    }
    llvm::Value *NilLiteral::codegen(CodegenContext &ctx) const {
        // No object model yet; nil is a 0 placeholder in the numeric slice.
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx.llvmContext), 0);
    }
    llvm::Value *StringLiteral::codegen(CodegenContext &ctx) const {
        return ctx.builder->CreateGlobalStringPtr(value, "str");
    }
    llvm::Value *Identifier::codegen(CodegenContext &ctx) const {
        auto it = ctx.namedValues.find(name);
        if (it == ctx.namedValues.end()) {
            std::cerr << "codegen: unknown identifier '" << name << "'\n";
            return nullptr;
        }
        return ctx.builder->CreateLoad(it->second->getAllocatedType(), it->second, name);
    }
    llvm::Value *BinaryOp::codegen(CodegenContext &ctx) const {
        if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=") {
            const auto *lid = dynamic_cast<const Identifier *>(left.get());
            if (!lid) {
                std::cerr << "codegen: assignment target must be a variable\n";
                return nullptr;
            }
            auto it = ctx.namedValues.find(lid->name);
            if (it == ctx.namedValues.end()) {
                std::cerr << "codegen: assignment to unknown variable '" << lid->name
                          << "'\n";
                return nullptr;
            }
            llvm::Value *rhs = right->codegen(ctx);
            if (op != "=") {
                llvm::Value *cur =
                        ctx.builder->CreateLoad(it->second->getAllocatedType(), it->second);
                rhs = emit_binop(ctx, op.substr(0, 1), cur, rhs);
            }
            if (rhs) ctx.builder->CreateStore(rhs, it->second);
            return rhs;
        }
        return emit_binop(ctx, op, left->codegen(ctx), right->codegen(ctx));
    }
    llvm::Value *UnaryOp::codegen(CodegenContext &ctx) const {
        llvm::Value *v = operand->codegen(ctx);
        if (!v) return nullptr;
        if (op == "-") return is_float(v) ? ctx.builder->CreateFNeg(v, "neg")
                                          : ctx.builder->CreateNeg(v, "neg");
        if (op == "!") return ctx.builder->CreateNot(to_cond(ctx, v), "not");
        return v;// unary '+'
    }
    llvm::Value *FunctionCall::codegen(CodegenContext &ctx) const {
        auto it = ctx.functions.find(name);
        if (it == ctx.functions.end()) {
            std::cerr << "codegen: call to unknown/unsupported function '" << name
                      << "' (runtime/FFI not wired yet)\n";
            return nullptr;
        }
        llvm::Function *fn = it->second;
        std::vector<llvm::Value *> argv;
        for (const auto &a : args) argv.push_back(a->codegen(ctx));
        return ctx.builder->CreateCall(fn, argv, fn->getReturnType()->isVoidTy() ? "" : "call");
    }

    // ===================== Statements =====================
    void Block::codegen(CodegenContext &ctx) const {
        auto saved = ctx.namedValues;
        for (const auto &n : statements) {
            if (terminated(ctx)) break;
            if (auto *s = dynamic_cast<const Statement *>(n.get())) s->codegen(ctx);
        }
        ctx.namedValues = saved;// block scope ends
    }
    void LetStatement::codegen(CodegenContext &ctx) const {
        llvm::Value *init = value ? value->codegen(ctx) : nullptr;
        llvm::Type *ty = typeName ? cg::map_type(ctx.llvmContext, *typeName)
                         : init    ? init->getType()
                                   : llvm::Type::getInt64Ty(ctx.llvmContext);
        for (const auto &nm : names) {
            auto *a = entry_alloca(ctx, ctx.currentFunction, nm, ty);
            if (init) ctx.builder->CreateStore(init, a);
            ctx.namedValues[nm] = a;
        }
    }
    void ExprStatement::codegen(CodegenContext &ctx) const {
        if (expr) expr->codegen(ctx);
    }
    void ReturnStatement::codegen(CodegenContext &ctx) const {
        if (value) {
            if (llvm::Value *v = value->codegen(ctx)) ctx.builder->CreateRet(v);
            else ctx.builder->CreateRetVoid();
        } else {
            ctx.builder->CreateRetVoid();
        }
    }
    void IfStatement::codegen(CodegenContext &ctx) const {
        llvm::Function *fn = ctx.currentFunction;
        llvm::Value *cond = to_cond(ctx, condition ? condition->codegen(ctx) : nullptr);
        auto *thenBB = llvm::BasicBlock::Create(ctx.llvmContext, "then", fn);
        auto *mergeBB = llvm::BasicBlock::Create(ctx.llvmContext, "ifcont");
        auto *elseBB =
                elseBranch ? llvm::BasicBlock::Create(ctx.llvmContext, "else") : mergeBB;
        ctx.builder->CreateCondBr(cond, thenBB, elseBB);

        ctx.builder->SetInsertPoint(thenBB);
        if (thenBlock) thenBlock->codegen(ctx);
        if (!terminated(ctx)) ctx.builder->CreateBr(mergeBB);

        if (elseBranch) {
            fn->insert(fn->end(), elseBB);
            ctx.builder->SetInsertPoint(elseBB);
            elseBranch->codegen(ctx);
            if (!terminated(ctx)) ctx.builder->CreateBr(mergeBB);
        }
        fn->insert(fn->end(), mergeBB);
        ctx.builder->SetInsertPoint(mergeBB);
    }
    void LoopStatement::codegen(CodegenContext &ctx) const {
        llvm::Function *fn = ctx.currentFunction;
        auto *bodyBB = llvm::BasicBlock::Create(ctx.llvmContext, "loop", fn);
        auto *afterBB = llvm::BasicBlock::Create(ctx.llvmContext, "loopend");
        ctx.builder->CreateBr(bodyBB);
        ctx.builder->SetInsertPoint(bodyBB);
        ctx.continueTargets.push_back(bodyBB);
        ctx.breakTargets.push_back(afterBB);
        if (body) body->codegen(ctx);
        if (!terminated(ctx)) ctx.builder->CreateBr(bodyBB);
        ctx.continueTargets.pop_back();
        ctx.breakTargets.pop_back();
        fn->insert(fn->end(), afterBB);
        ctx.builder->SetInsertPoint(afterBB);
    }
    void UntilStatement::codegen(CodegenContext &ctx) const {// runs while cond is false
        llvm::Function *fn = ctx.currentFunction;
        auto *condBB = llvm::BasicBlock::Create(ctx.llvmContext, "until.cond", fn);
        auto *bodyBB = llvm::BasicBlock::Create(ctx.llvmContext, "until.body");
        auto *afterBB = llvm::BasicBlock::Create(ctx.llvmContext, "until.end");
        ctx.builder->CreateBr(condBB);
        ctx.builder->SetInsertPoint(condBB);
        llvm::Value *c = to_cond(ctx, condition ? condition->codegen(ctx) : nullptr);
        ctx.builder->CreateCondBr(c, afterBB, bodyBB);// true -> exit, false -> body
        fn->insert(fn->end(), bodyBB);
        ctx.builder->SetInsertPoint(bodyBB);
        ctx.continueTargets.push_back(condBB);
        ctx.breakTargets.push_back(afterBB);
        if (body) body->codegen(ctx);
        if (!terminated(ctx)) ctx.builder->CreateBr(condBB);
        ctx.continueTargets.pop_back();
        ctx.breakTargets.pop_back();
        fn->insert(fn->end(), afterBB);
        ctx.builder->SetInsertPoint(afterBB);
    }
    void DoUntilStatement::codegen(CodegenContext &ctx) const {// body, then test
        llvm::Function *fn = ctx.currentFunction;
        auto *bodyBB = llvm::BasicBlock::Create(ctx.llvmContext, "do.body", fn);
        auto *condBB = llvm::BasicBlock::Create(ctx.llvmContext, "do.cond");
        auto *afterBB = llvm::BasicBlock::Create(ctx.llvmContext, "do.end");
        ctx.builder->CreateBr(bodyBB);
        ctx.builder->SetInsertPoint(bodyBB);
        ctx.continueTargets.push_back(condBB);
        ctx.breakTargets.push_back(afterBB);
        if (body) body->codegen(ctx);
        if (!terminated(ctx)) ctx.builder->CreateBr(condBB);
        ctx.continueTargets.pop_back();
        ctx.breakTargets.pop_back();
        fn->insert(fn->end(), condBB);
        ctx.builder->SetInsertPoint(condBB);
        llvm::Value *c = to_cond(ctx, condition ? condition->codegen(ctx) : nullptr);
        ctx.builder->CreateCondBr(c, afterBB, bodyBB);
        fn->insert(fn->end(), afterBB);
        ctx.builder->SetInsertPoint(afterBB);
    }
    void ForInStatement::codegen(CodegenContext &ctx) const {
        // Only `for v in lo..hi { }` is supported in the numeric slice.
        const auto *range = dynamic_cast<const BinaryOp *>(iterable.get());
        if (!range || range->op != "..") {
            std::cerr << "codegen: for-in only supports integer ranges (lo..hi) for now\n";
            return;
        }
        llvm::Function *fn = ctx.currentFunction;
        auto *i64 = llvm::Type::getInt64Ty(ctx.llvmContext);
        llvm::Value *lo = range->left->codegen(ctx);
        llvm::Value *hi = range->right->codegen(ctx);
        auto *iv = entry_alloca(ctx, fn, var, i64);
        ctx.builder->CreateStore(lo, iv);
        ctx.namedValues[var] = iv;

        auto *condBB = llvm::BasicBlock::Create(ctx.llvmContext, "for.cond", fn);
        auto *bodyBB = llvm::BasicBlock::Create(ctx.llvmContext, "for.body");
        auto *incrBB = llvm::BasicBlock::Create(ctx.llvmContext, "for.incr");
        auto *afterBB = llvm::BasicBlock::Create(ctx.llvmContext, "for.end");
        ctx.builder->CreateBr(condBB);
        ctx.builder->SetInsertPoint(condBB);
        llvm::Value *cur = ctx.builder->CreateLoad(i64, iv, var);
        ctx.builder->CreateCondBr(ctx.builder->CreateICmpSLT(cur, hi, "forcmp"), bodyBB,
                                  afterBB);
        fn->insert(fn->end(), bodyBB);
        ctx.builder->SetInsertPoint(bodyBB);
        ctx.continueTargets.push_back(incrBB);
        ctx.breakTargets.push_back(afterBB);
        if (body) body->codegen(ctx);
        if (!terminated(ctx)) ctx.builder->CreateBr(incrBB);
        ctx.continueTargets.pop_back();
        ctx.breakTargets.pop_back();
        fn->insert(fn->end(), incrBB);
        ctx.builder->SetInsertPoint(incrBB);
        llvm::Value *next = ctx.builder->CreateAdd(
                ctx.builder->CreateLoad(i64, iv), llvm::ConstantInt::get(i64, 1), "inc");
        ctx.builder->CreateStore(next, iv);
        ctx.builder->CreateBr(condBB);
        fn->insert(fn->end(), afterBB);
        ctx.builder->SetInsertPoint(afterBB);
    }
    void BreakStatement::codegen(CodegenContext &ctx) const {
        if (!ctx.breakTargets.empty()) ctx.builder->CreateBr(ctx.breakTargets.back());
        else std::cerr << "codegen: 'break' outside a loop\n";
    }
    void ContinueStatement::codegen(CodegenContext &ctx) const {
        if (!ctx.continueTargets.empty())
            ctx.builder->CreateBr(ctx.continueTargets.back());
        else std::cerr << "codegen: 'continue' outside a loop\n";
    }
    void AssertStatement::codegen(CodegenContext &ctx) const {
        if (expr) expr->codegen(ctx);// evaluated for side effects; no runtime trap yet
    }
    void DeferStatement::codegen(CodegenContext &) const {
        // defer needs scope-exit scheduling — not in the numeric slice (no-op for now).
    }
    void FunctionDef::codegen(CodegenContext &ctx) const {
        auto it = ctx.functions.find(name);
        if (it == ctx.functions.end()) {
            std::cerr << "codegen: missing prototype for function '" << name << "'\n";
            return;
        }
        llvm::Function *fn = it->second;
        ctx.currentFunction = fn;
        ctx.namedValues.clear();
        auto *entry = llvm::BasicBlock::Create(ctx.llvmContext, "entry", fn);
        ctx.builder->SetInsertPoint(entry);
        unsigned i = 0;
        for (auto &arg : fn->args()) {
            if (i >= params.size()) break;
            auto *a = entry_alloca(ctx, fn, params[i]->name, arg.getType());
            ctx.builder->CreateStore(&arg, a);
            ctx.namedValues[params[i]->name] = a;
            ++i;
        }
        if (body) body->codegen(ctx);
        if (!terminated(ctx)) {
            if (fn->getReturnType()->isVoidTy()) ctx.builder->CreateRetVoid();
            else ctx.builder->CreateRet(llvm::Constant::getNullValue(fn->getReturnType()));
        }
        ctx.currentFunction = nullptr;
    }

    // ===================== Not in the numeric slice (no-op stubs) =====================
    // These parse + build AST, but codegen needs the object model / runtime (later).
    void TranslationUnit::codegen(CodegenContext &) const { }
    void MatchStatement::codegen(CodegenContext &) const { }
    void FieldDecl::codegen(CodegenContext &) const { }
    void MethodDef::codegen(CodegenContext &) const { }
    void ConstructorDef::codegen(CodegenContext &) const { }
    void DestructorDef::codegen(CodegenContext &) const { }
    void OperatorDef::codegen(CodegenContext &) const { }
    void ClassDef::codegen(CodegenContext &) const { }
    void InterfaceDef::codegen(CodegenContext &) const { }
    void EnumDef::codegen(CodegenContext &) const { }
    void TypeDef::codegen(CodegenContext &) const { }

}// namespace mxs::frontend::ast
