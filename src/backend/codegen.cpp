#include "mxspp/backend/codegen.h"
#include "mxspp/frontend/ast.h"

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
            auto *f = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
                                             fn->name, module.get());
            unsigned i = 0;
            for (auto &arg : f->args())
                if (i < fn->params.size()) arg.setName(fn->params[i++]->name);
            ctx.functions[fn->name] = f;
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

}// namespace mxs::backend::codegen
