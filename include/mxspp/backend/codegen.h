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
    // merged function keyed by the module's mangled name — distinguishing a module-qualified call
    // from a method call on a value (progress13 D2). The set is empty for programs with no
    // qualified imports.
    // `exposed` (progress18) maps a SURFACE call name (qualified `ns.fn`/`ns.Class`, or a selective
    // bare name) to the MANGLED `funcs`/`foreigns` key the resolver emitted. Codegen consults it at
    // a call site to translate the surface name to the mangled key before the flat `funcs` lookup,
    // so an imported module's internal siblings (private, only under their mangled name) resolve and
    // the program reaches only what its import form exposed. Empty without imports.
    std::unique_ptr<llvm::Module>
    compile_core(const frontend::ast::TranslationUnit &tu, llvm::LLVMContext &llvmContext,
                 const std::string &moduleName = "mxs_core",
                 const std::set<std::string> &moduleNamespaces = { },
                 const std::unordered_map<std::string, std::string> &exposed = { });

}// namespace mxs::backend::codegen
