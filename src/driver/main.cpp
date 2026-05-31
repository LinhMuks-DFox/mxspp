#include "mxspp/backend/codegen.h"
#include "mxspp/frontend/parser.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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
}// namespace

int main(int argc, char **argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);

    if (args.size() == 2 && (args[0] == "--dump-ast" || args[0] == "--emit-ir")) {
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

        // --emit-ir: AST -> LLVM IR (numeric/control-flow subset).
        llvm::LLVMContext context;
        auto module = mxs::backend::codegen::compile(*tu, context, args[1]);
        if (!module) {
            std::cerr << "error: codegen failed for " << args[1] << "\n";
            return 1;
        }
        module->print(llvm::outs(), nullptr);
        return 0;
    }

    std::cout << "mxs (MXScript)\n"
              << "  usage: mxs --dump-ast <file.mxs>   # parse and print the AST\n"
              << "         mxs --emit-ir  <file.mxs>   # lower the AST to LLVM IR\n";
    return 0;
}
