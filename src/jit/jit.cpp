#include "mxspp/jit/jit.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Triple.h>

#include <iostream>

namespace mxs::jit {

    namespace {
        // A memory manager that skips .eh_frame registration. ORC/RTDyld otherwise hands the
        // whole .eh_frame section to libc++'s libunwind `__register_frame`, which expects a
        // single FDE per call and warns "bad fde: FDE is really a CIE" for every CIE it walks
        // past. mxs JIT'd code uses the error-value model (no C++ exceptions unwind through JIT
        // frames), so skipping registration is safe and silences the noise.
        class NoEHFrameMemoryManager : public llvm::SectionMemoryManager {
        public:
            void registerEHFrames(uint8_t *, uint64_t, size_t) override { }
            void deregisterEHFrames() override { }
        };

        // Parse a bitcode/IR file into `ctx` (the SAME context as the user module) and return it
        // for in-process linking. Returns nullptr if the path is empty (skip) or on parse failure
        // (a warning is printed; builtins from that .bc will be unresolved). Parsing into the user
        // module's context is what lets llvm::Linker fold the bodies into ONE module so the inliner
        // can cross the user -> std -> core boundaries (task40 / progress19 D0).
        std::unique_ptr<llvm::Module>
        parse_bitcode(llvm::LLVMContext &ctx, const std::string &path, const char *what) {
            using namespace llvm;
            if (path.empty()) return nullptr;
            SMDiagnostic err;
            if (auto mod = parseIRFile(path, err, ctx)) return mod;
            std::cerr << "warning: could not load " << what << " '" << path
                      << "'; some builtins will be unresolved\n";
            std::cerr << "  parse error: " << err.getMessage().str() << " (line "
                      << err.getLineNo() << ")\n";
            return nullptr;
        }

        // Map an integer opt level (0/1/2/3) to the LLVM PassBuilder OptimizationLevel; anything
        // out of range falls back to O2 (the JIT default).
        llvm::OptimizationLevel opt_level_of(int level) {
            switch (level) {
                case 0:
                    return llvm::OptimizationLevel::O0;
                case 1:
                    return llvm::OptimizationLevel::O1;
                case 3:
                    return llvm::OptimizationLevel::O3;
                case 2:
                default:
                    return llvm::OptimizationLevel::O2;
            }
        }

        // Run the PassBuilder default per-module pipeline at `level` over `module` (in place).
        // It runs AFTER linking core.bc/std.bc in, so it sees the whole call graph and runs
        // mem2reg/SROA/instcombine/GVN/DCE + the inliner across what were three modules — the
        // mid-end optimization the JIT never did before (progress19 D0). O0 leaves the module
        // essentially untouched (no inlining), which is the debug baseline / knob-off case.
        // NOTE: the core.bc/std.bc bodies (mxs_int_*, mxs_op_*, mxs_retain/release) are currently
        // emitted at -O0 -> marked `optnone noinline`, so the inliner cannot fold them into hot
        // loops yet; the win here is loop/lvalue cleanup. Making the primitives inlinable needs the
        // bitcode emitted at >= -O1 (a separate runtime-build decision — see the report / D0 notes).
        void optimize_module(llvm::Module &module, int level) {
            using namespace llvm;
            PassBuilder PB;
            LoopAnalysisManager LAM;
            FunctionAnalysisManager FAM;
            CGSCCAnalysisManager CGAM;
            ModuleAnalysisManager MAM;
            PB.registerModuleAnalyses(MAM);
            PB.registerCGSCCAnalyses(CGAM);
            PB.registerFunctionAnalyses(FAM);
            PB.registerLoopAnalyses(LAM);
            PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
            ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(opt_level_of(level));
            MPM.run(module, MAM);
        }
    }// namespace

    int run(std::unique_ptr<llvm::Module> module,
            std::unique_ptr<llvm::LLVMContext> context, const std::string &runtimeBcPath,
            const std::string &entry, const std::string &coreBcPath, int optLevel) {
        using namespace llvm;

        InitializeNativeTarget();
        InitializeNativeTargetAsmPrinter();

        orc::LLJITBuilder builder;
        // Use an RTDyld linking layer whose memory manager skips EH-frame registration
        // (silences the libunwind "FDE is really a CIE" warnings; see NoEHFrameMemoryManager).
        builder.setObjectLinkingLayerCreator(
                [](orc::ExecutionSession &ES,
                   const Triple &) -> Expected<std::unique_ptr<orc::ObjectLayer>> {
                    return std::make_unique<orc::RTDyldObjectLinkingLayer>(ES, []() {
                        return std::make_unique<NoEHFrameMemoryManager>();
                    });
                });
        auto jitOrErr = builder.create();
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

        // Fold the runtime bitcode (mxs_* fast-dispatch / std.bc) and the core object-type bitcode
        // (core.bc) INTO the user module — one Module in one context (task40 / progress19 D0). The
        // old code added them as three separate addIRModule calls in three separate contexts, so
        // the inliner could never see across the user -> std -> core boundaries; nothing inlined.
        // Parsing each into `*context` and llvm::Linker::linkInModule-ing them in gives the
        // optimizer the whole call graph to flatten (mxs_int_*, mxs_retain/release, ...).
        //
        // The Linker is scoped to this block so its IRMover (which holds a metadata-tracking map
        // keyed off `*module`) is destroyed HERE — before the module is optimized and moved into
        // the JIT. Letting it outlive the std::move into the ThreadSafeModule makes ~IRMover untrack
        // metadata of an already-handed-off module, a crash/hang at return (verified via stack).
        {
            Linker linker(*module);
            if (auto coreMod = parse_bitcode(*context, coreBcPath, "core"))
                if (linker.linkInModule(std::move(coreMod)))
                    std::cerr << "warning: failed to link core.bc; some builtins will be "
                                 "unresolved\n";
            if (auto stdMod = parse_bitcode(*context, runtimeBcPath, "runtime"))
                if (linker.linkInModule(std::move(stdMod)))
                    std::cerr << "warning: failed to link std.bc; some builtins will be "
                                 "unresolved\n";
        }

        // codegen builds the user IR module target-agnostically (no data layout / triple), so
        // it inherits a default that need not match the host (e.g. an AArch64-ELF layout vs the
        // macOS Mach-O JIT). Stamp it with the JIT host's data layout + triple before adding, or
        // ORC rejects it ("Added modules have incompatible data layouts"). Do this BEFORE
        // optimizing so the pipeline (incl. the inliner / target-aware passes) sees the host
        // layout.
        module->setDataLayout(jit->getDataLayout());
        // LLVM 21 changed Module::setTargetTriple to take a `Triple` (was a string). Guard so the
        // source builds against both the canonical LLVM 20 (Docker) and a newer host LLVM (e.g. 22).
#if LLVM_VERSION_MAJOR >= 21
        module->setTargetTriple(jit->getTargetTriple());
#else
        module->setTargetTriple(jit->getTargetTriple().str());
#endif

        // Run the mid-end IR optimization pipeline on the merged module (progress19 D0). Default
        // O2; @@optimize(level=N) dials O0/O1/O2/O3. This is the optimization the JIT never did.
        optimize_module(*module, optLevel);

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
