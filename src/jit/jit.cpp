#include "mxspp/jit/jit.h"

#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>

#include <iostream>

namespace mxs::jit {

    namespace {
        // Parse a bitcode/IR file (in its own context) and add it to the JIT. No-op if empty.
        void link_bitcode(llvm::orc::LLJIT &jit, const std::string &path,
                          const char *what) {
            using namespace llvm;
            if (path.empty()) return;
            auto ctx = std::make_unique<LLVMContext>();
            SMDiagnostic err;
            if (auto mod = parseIRFile(path, err, *ctx)) {
                if (auto e = jit.addIRModule(
                            orc::ThreadSafeModule(std::move(mod), std::move(ctx))))
                    logAllUnhandledErrors(std::move(e), errs(), "mxs jit (bitcode): ");
            } else {
                std::cerr << "warning: could not load " << what << " '" << path
                          << "'; some builtins will be unresolved\n";
            }
        }
    }// namespace

    int run(std::unique_ptr<llvm::Module> module,
            std::unique_ptr<llvm::LLVMContext> context, const std::string &runtimeBcPath,
            const std::string &entry, const std::string &coreBcPath) {
        using namespace llvm;

        InitializeNativeTarget();
        InitializeNativeTargetAsmPrinter();

        auto jitOrErr = orc::LLJITBuilder().create();
        if (!jitOrErr) {
            logAllUnhandledErrors(jitOrErr.takeError(), errs(), "mxs jit: ");
            return 1;
        }
        auto jit = std::move(*jitOrErr);

        // Resolve libc symbols (printf, ...) from the running process.
        if (auto gen = orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
                    jit->getDataLayout().getGlobalPrefix()))
            jit->getMainJITDylib().addGenerator(std::move(*gen));
        else
            logAllUnhandledErrors(gen.takeError(), errs(), "mxs jit (process syms): ");

        // Link the runtime bitcode (mxs_* fast-dispatch) and, for the new object model, the
        // core object-type bitcode (core.bc). user IR + lib IR optimized together (README / D6).
        link_bitcode(*jit, runtimeBcPath, "runtime");
        link_bitcode(*jit, coreBcPath, "core");

        if (auto e = jit->addIRModule(
                    orc::ThreadSafeModule(std::move(module), std::move(context)))) {
            logAllUnhandledErrors(std::move(e), errs(), "mxs jit (module): ");
            return 1;
        }

        auto sym = jit->lookup(entry);
        if (!sym) {
            logAllUnhandledErrors(sym.takeError(), errs(), "mxs jit (lookup): ");
            return 1;
        }
        auto *fn = sym->toPtr<int64_t (*)()>();
        return static_cast<int>(fn());
    }

}// namespace mxs::jit
