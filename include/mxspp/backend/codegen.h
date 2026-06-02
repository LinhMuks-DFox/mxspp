#pragma once
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace mxs::frontend::ast {
    class TranslationUnit;
}

namespace mxs::backend::codegen {

    // New object-model lowering (progress09 ④): values are real core::MXObject* and operators
    // emit the typed core ABI (mxs_int_add, …) defined in core.bc, which the JIT links in.
    // Seed slice: functions, integer literals, int arithmetic, and generic calls (the stdlib —
    // println, … — resolves via @@foreign, no per-function hardcoding). This is the single,
    // canonical codegen path.
    // `moduleNamespaces` are the qualified-import namespaces (alias or module last segment, e.g.
    // `io` from `import std.io;`). A call `ns.fn(args)` whose `ns` is in this set resolves to the
    // merged function keyed `ns.fn` — distinguishing a module-qualified call from a method call on
    // a value (progress13 D2). The set is empty for programs with no qualified imports.
    std::unique_ptr<llvm::Module>
    compile_core(const frontend::ast::TranslationUnit &tu, llvm::LLVMContext &llvmContext,
                 const std::string &moduleName = "mxs_core",
                 const std::set<std::string> &moduleNamespaces = { });

}// namespace mxs::backend::codegen
