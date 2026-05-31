#include "mxspp/shell/shell.h"

#include "mxspp/backend/codegen.h"
#include "mxspp/frontend/parser.h"
#include "mxspp/jit/jit.h"

#include <llvm/IR/LLVMContext.h>

#include <iostream>
#include <memory>
#include <string>

namespace mxs::shell {
    namespace {
        std::string trim(const std::string &s) {
            std::size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return "";
            std::size_t b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        }

        // A line is a top-level definition (accumulated) rather than an expression to eval.
        bool is_definition(const std::string &s) {
            for (const char *kw : { "func ", "func(", "@@", "class ", "interface ", "enum ",
                                    "type ", "import ", "static ", "dynamic " })
                if (s.rfind(kw, 0) == 0) return true;
            return false;
        }
    }// namespace

    int repl(const std::string &prelude, const std::string &runtimeBcPath) {
        std::cout << "mxs REPL — type an expression (e.g. 1 + 2 * 3) or a one-line\n"
                     "definition (e.g. func sq(x: int) -> int { return x * x; }); :q to quit.\n";

        std::string accumulated;// definitions entered so far
        std::string line;
        std::cout << "mxs> " << std::flush;
        while (std::getline(std::cin, line)) {
            const std::string t = trim(line);
            if (t.empty()) {
                std::cout << "mxs> " << std::flush;
                continue;
            }
            if (t == ":q" || t == ":quit" || t == ":exit") break;

            if (is_definition(t)) {
                accumulated += t + "\n";
            } else {
                std::string expr = t;
                if (!expr.empty() && expr.back() == ';') expr.pop_back();
                // Recompile prelude + accumulated defs + a thunk that prints the expression.
                const std::string src = prelude + "\n" + accumulated +
                                        "func __mxs_repl() -> int {\n    println(" + expr +
                                        ");\n    return 0;\n}\n";
                if (auto tu = mxs::frontend::parser::parse_to_ast(src, "<repl>")) {
                    auto ctx = std::make_unique<llvm::LLVMContext>();
                    if (auto mod = mxs::backend::codegen::compile(*tu, *ctx, "<repl>"))
                        mxs::jit::run(std::move(mod), std::move(ctx), runtimeBcPath,
                                      "__mxs_repl");
                }
            }
            std::cout << "mxs> " << std::flush;
        }
        std::cout << "\n";
        return 0;
    }

}// namespace mxs::shell
