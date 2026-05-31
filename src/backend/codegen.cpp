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
    namespace {
        llvm::Function *rt_decl(llvm::Module *M, const char *name, llvm::Type *ret,
                                llvm::ArrayRef<llvm::Type *> args) {
            if (auto *f = M->getFunction(name)) return f;
            return llvm::Function::Create(llvm::FunctionType::get(ret, args, false),
                                          llvm::Function::ExternalLinkage, name, M);
        }

        // Lower an expression to a boxed MXObject* (ptr). Recursive for binary ops.
        llvm::Value *obj_expr(llvm::IRBuilder<> &B, llvm::Module *M,
                              const ast::MXASTNode *n) {
            auto &C = M->getContext();
            auto *i64 = llvm::Type::getInt64Ty(C);
            auto *dbl = llvm::Type::getDoubleTy(C);
            auto *ptr = llvm::PointerType::get(C, 0);
            if (const auto *il = dynamic_cast<const ast::IntegerLiteral *>(n))
                return B.CreateCall(rt_decl(M, "mxs_box_int", ptr, { i64 }),
                                    { llvm::ConstantInt::get(i64, il->value, true) });
            if (const auto *fl = dynamic_cast<const ast::FloatLiteral *>(n))
                return B.CreateCall(rt_decl(M, "mxs_box_float", ptr, { dbl }),
                                    { llvm::ConstantFP::get(dbl, fl->value) });
            if (const auto *bl = dynamic_cast<const ast::BooleanLiteral *>(n))
                return B.CreateCall(rt_decl(M, "mxs_box_bool", ptr, { i64 }),
                                    { llvm::ConstantInt::get(i64, bl->value) });
            if (const auto *bo = dynamic_cast<const ast::BinaryOp *>(n)) {
                const char *fn = bo->op == "+"   ? "mxs_op_add"
                                 : bo->op == "-" ? "mxs_op_sub"
                                 : bo->op == "*" ? "mxs_op_mul"
                                                 : nullptr;
                if (fn)
                    return B.CreateCall(rt_decl(M, fn, ptr, { ptr, ptr }),
                                        { obj_expr(B, M, bo->left.get()),
                                          obj_expr(B, M, bo->right.get()) });
            }
            std::cerr << "object-mode: unsupported expression (slice covers literals, "
                         "+, -, *)\n";
            return nullptr;
        }

        void obj_stmt(llvm::IRBuilder<> &B, llvm::Module *M, const ast::MXASTNode *n) {
            auto &C = M->getContext();
            auto *i64 = llvm::Type::getInt64Ty(C);
            auto *ptr = llvm::PointerType::get(C, 0);
            if (const auto *es = dynamic_cast<const ast::ExprStatement *>(n)) {
                if (const auto *call = dynamic_cast<const ast::FunctionCall *>(es->expr.get()))
                    if ((call->name == "println" || call->name == "print") &&
                        !call->args.empty())
                        if (auto *o = obj_expr(B, M, call->args[0].get()))
                            B.CreateCall(rt_decl(M, "mxs_obj_println",
                                                 llvm::Type::getVoidTy(C), { i64, ptr }),
                                         { llvm::ConstantInt::get(i64, 0), o });
            } else if (const auto *rs = dynamic_cast<const ast::ReturnStatement *>(n)) {
                if (rs->value)
                    if (auto *o = obj_expr(B, M, rs->value.get())) {
                        B.CreateRet(B.CreateCall(rt_decl(M, "mxs_obj_as_int", i64, { ptr }),
                                                 { o }));
                        return;
                    }
                B.CreateRet(llvm::ConstantInt::get(i64, 0));
            } else if (const auto *blk = dynamic_cast<const ast::Block *>(n)) {
                for (const auto &s : blk->statements) obj_stmt(B, M, s.get());
            }
        }
    }// namespace

    std::unique_ptr<llvm::Module> compile_obj(const ast::TranslationUnit &tu,
                                              llvm::LLVMContext &llvmContext,
                                              const std::string &moduleName) {
        auto module = std::make_unique<llvm::Module>(moduleName, llvmContext);
        llvm::IRBuilder<> B(llvmContext);
        auto *i64 = llvm::Type::getInt64Ty(llvmContext);

        const ast::FunctionDef *mainFn = nullptr;
        for (const auto &s : tu.statements)
            if (const auto *f = dynamic_cast<const ast::FunctionDef *>(s.get()))
                if (f->name == "main") mainFn = f;
        if (!mainFn) {
            std::cerr << "object-mode: program has no main()\n";
            return nullptr;
        }
        auto *fn = llvm::Function::Create(llvm::FunctionType::get(i64, {}, false),
                                          llvm::Function::ExternalLinkage, "main",
                                          module.get());
        B.SetInsertPoint(llvm::BasicBlock::Create(llvmContext, "entry", fn));
        if (mainFn->body)
            for (const auto &s : mainFn->body->statements) obj_stmt(B, module.get(), s.get());
        if (!B.GetInsertBlock()->getTerminator())
            B.CreateRet(llvm::ConstantInt::get(i64, 0));

        std::string err2;
        llvm::raw_string_ostream os2(err2);
        if (llvm::verifyModule(*module, &os2)) {
            std::cerr << "object-mode codegen: verification failed:\n" << os2.str();
            return nullptr;
        }
        return module;
    }

}// namespace mxs::backend::codegen
