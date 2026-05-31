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

    int run(std::unique_ptr<llvm::Module> module,
            std::unique_ptr<llvm::LLVMContext> context,
            const std::string &runtimeBcPath) {
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

        // Link the runtime bitcode (provides mxs_* fast-dispatch). This is the runtime.bc
        // + user-IR LTO model from the README.
        if (!runtimeBcPath.empty()) {
            auto rtCtx = std::make_unique<LLVMContext>();
            SMDiagnostic err;
            if (auto rtMod = parseIRFile(runtimeBcPath, err, *rtCtx)) {
                if (auto e = jit->addIRModule(
                            orc::ThreadSafeModule(std::move(rtMod), std::move(rtCtx))))
                    logAllUnhandledErrors(std::move(e), errs(), "mxs jit (runtime): ");
            } else {
                std::cerr << "warning: could not load runtime '" << runtimeBcPath
                          << "'; I/O builtins will be unresolved\n";
            }
        }

        if (auto e = jit->addIRModule(
                    orc::ThreadSafeModule(std::move(module), std::move(context)))) {
            logAllUnhandledErrors(std::move(e), errs(), "mxs jit (module): ");
            return 1;
        }

        auto sym = jit->lookup("main");
        if (!sym) {
            logAllUnhandledErrors(sym.takeError(), errs(), "mxs jit (no main): ");
            return 1;
        }
        auto *entry = sym->toPtr<int64_t (*)()>();
        return static_cast<int>(entry());
    }

}// namespace mxs::jit
