#pragma once
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

namespace mxs::backend::codegen {
    struct CodegenContext {
        llvm::LLVMContext &llvmContext;
        llvm::Module *module;
        llvm::IRBuilder<> *builder;
        std::unordered_map<std::string, llvm::Value *> namedValues;
    };
}