#pragma once
#include <memory>
#include <string>

namespace llvm {
    class Module;
    class LLVMContext;
}

namespace mxs::jit {

    // JIT-compile `module` and run its `main()`. The runtime bitcode at `runtimeBcPath` (the
    // mxs_* fast-dispatch / std.bc symbols) and, if non-empty, the core object-type bitcode at
    // `coreBcPath` (the new object model's type/lvalue/rc ABI, progress09 ④) are parsed into the
    // SAME context and llvm::Linker-folded INTO `module` — one Module — then an LLVM mid-end
    // optimization pipeline runs over it at `optLevel` (0/1/2/3 -> O0/O1/O2/O3, default O2) so the
    // inliner flattens across the user -> std -> core boundaries (task40 / progress19 D0). libc
    // symbols (printf, ...) resolve from the host process. Returns main()'s value, or a nonzero
    // error code.
    int run(std::unique_ptr<llvm::Module> module,
            std::unique_ptr<llvm::LLVMContext> context, const std::string &runtimeBcPath,
            const std::string &entry = "main", const std::string &coreBcPath = "",
            int optLevel = 2);

}// namespace mxs::jit
