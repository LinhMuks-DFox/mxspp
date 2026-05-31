#include "mxspp/backend/codegen.h"
#include "mxspp/frontend/parser.h"
#include "mxspp/jit/jit.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {
    std::string read_file(const std::string &path, bool &ok) {
        std::ifstream f(path);
        if (!f) {
            ok = false;
            return {};
        }
        std::stringstream ss;
        ss << f.rdbuf();
        ok = true;
        return ss.str();
    }

    // Locate runtime.bc next to the executable (build/bin/), with a couple of fallbacks.
    std::string runtime_bc_path() {
#if defined(__linux__)
        char buf[4096];
        const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string p(buf);
            const auto slash = p.find_last_of('/');
            if (slash != std::string::npos) {
                std::string c = p.substr(0, slash) + "/runtime.bc";
                if (std::ifstream(c)) return c;
            }
        }
#endif
        for (const char *c : { "runtime.bc", "build/bin/runtime.bc", "bin/runtime.bc" })
            if (std::ifstream(c)) return c;
        return "";
    }
}// namespace

int main(int argc, char **argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);

    if (args.size() == 2 &&
        (args[0] == "--dump-ast" || args[0] == "--emit-ir" || args[0] == "run")) {
        bool ok = false;
        const std::string source = read_file(args[1], ok);
        if (!ok) {
            std::cerr << "error: cannot open " << args[1] << "\n";
            return 1;
        }
        auto tu = mxs::frontend::parser::parse_to_ast(source, args[1]);
        if (!tu) {
            std::cerr << "error: failed to parse " << args[1] << "\n";
            return 1;
        }

        if (args[0] == "--dump-ast") {
            mxs::frontend::parser::dump_ast(*tu, std::cout);
            return 0;
        }

        auto context = std::make_unique<llvm::LLVMContext>();
        auto module = mxs::backend::codegen::compile(*tu, *context, args[1]);
        if (!module) {
            std::cerr << "error: codegen failed for " << args[1] << "\n";
            return 1;
        }

        if (args[0] == "--emit-ir") {
            module->print(llvm::outs(), nullptr);
            return 0;
        }

        // run: JIT-compile and execute main().
        return mxs::jit::run(std::move(module), std::move(context), runtime_bc_path());
    }

    std::cout << "mxs (MXScript)\n"
              << "  usage: mxs run        <file.mxs>   # JIT-compile and run main()\n"
              << "         mxs --emit-ir  <file.mxs>   # lower the AST to LLVM IR\n"
              << "         mxs --dump-ast <file.mxs>   # parse and print the AST\n";
    return 0;
}
