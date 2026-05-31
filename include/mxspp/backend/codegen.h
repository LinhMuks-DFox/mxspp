#pragma once
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mxs::frontend::ast {
    class TranslationUnit;
}

namespace mxs::backend::codegen {

    // State threaded through every AST node's codegen(). First-slice codegen targets a
    // statically-typed numeric subset (int=i64, float=f64, bool=i1, nil=void): functions,
    // let/assign, return, if/else, loops, arithmetic/compare/logic, and direct calls.
    struct CodegenContext {
        llvm::LLVMContext &llvmContext;
        llvm::Module *module;
        llvm::IRBuilder<> *builder;
        std::unordered_map<std::string, llvm::AllocaInst *> namedValues;// locals + params
        std::unordered_map<std::string, llvm::Function *> functions;    // by name
        llvm::Function *currentFunction = nullptr;
        std::vector<llvm::BasicBlock *> breakTargets;   // innermost loop exit
        std::vector<llvm::BasicBlock *> continueTargets;// innermost loop continue
    };

    // Map an MXScript type name to an LLVM type ("int"->i64, "float"->double,
    // "bool"->i1, "nil"->void; unknown -> i64 for now).
    llvm::Type *map_type(llvm::LLVMContext &ctx, const std::string &name);

    // Compile a whole program (TranslationUnit) into an LLVM module. Declares all
    // function prototypes first (so recursion / mutual calls resolve), then emits bodies,
    // then runs the LLVM verifier. Returns nullptr on error (diagnostics to stderr).
    std::unique_ptr<llvm::Module> compile(const frontend::ast::TranslationUnit &tu,
                                          llvm::LLVMContext &llvmContext,
                                          const std::string &moduleName = "mxs");

    // Object-mode lowering ("everything is an object"): values are boxed MXObject*,
    // arithmetic goes through dynamic dispatch (mxs_op_*), and println is polymorphic
    // (mxs_obj_println). First slice: main + literals + +/-/* + println, to prove the
    // box -> dynamic-dispatch -> polymorphic-print path end-to-end.
    std::unique_ptr<llvm::Module> compile_obj(const frontend::ast::TranslationUnit &tu,
                                              llvm::LLVMContext &llvmContext,
                                              const std::string &moduleName = "mxs_obj");

}// namespace mxs::backend::codegen
