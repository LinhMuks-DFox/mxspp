#pragma once
#include <memory>
#include <string>

namespace llvm {
    class Module;
    class LLVMContext;
}

namespace mxs::jit {

    // JIT-compile `module` and run its `main()`, linking the runtime bitcode at
    // `runtimeBcPath` (for the mxs_* fast-dispatch symbols) and resolving libc symbols
    // (printf, ...) from the host process. Returns main()'s value, or a nonzero error code.
    // `coreBcPath`, if non-empty, links the core object-type bitcode (core.bc) too — the new
    // object model's type/lvalue/rc ABI (progress09 ④).
    int run(std::unique_ptr<llvm::Module> module,
            std::unique_ptr<llvm::LLVMContext> context, const std::string &runtimeBcPath,
            const std::string &entry = "main", const std::string &coreBcPath = "");

}// namespace mxs::jit
