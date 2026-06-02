#include "mxspp/shell/shell.h"

#include "mxspp/backend/codegen.h"
#include "mxspp/frontend/imports.h"
#include "mxspp/frontend/parser.h"
#include "mxspp/jit/jit.h"

#include <llvm/IR/LLVMContext.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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
            for (const char *kw : { "func ", "func(", "@@", "class ", "interface ",
                                    "enum ", "type ", "import ", "static ", "dynamic " })
                if (s.rfind(kw, 0) == 0) return true;
            return false;
        }

        // Does `s` begin with the whole word `kw` (followed by a non-identifier char or end)?
        bool starts_with_word(const std::string &s, const char *kw) {
            const std::size_t n = std::strlen(kw);
            if (s.size() < n || s.compare(0, n, kw) != 0) return false;
            if (s.size() == n) return true;
            const char c = s[n];
            return !(std::isalnum(static_cast<unsigned char>(c)) || c == '_');
        }

        bool is_let(const std::string &s) { return starts_with_word(s, "let"); }

        // A leading statement keyword. `match` is an expression in mxs, so it is excluded (it can
        // be echoed). These run for their effect, not their value.
        bool starts_statement_kw(const std::string &s) {
            for (const char *kw : { "if", "for", "while", "until", "loop", "return",
                                    "break", "continue", "assert", "defer", "do" })
                if (starts_with_word(s, kw)) return true;
            return false;
        }

        // A top-level assignment `lhs = rhs` (incl. compound `+=` etc.), told apart from the
        // comparison operators `== != <= >=`. Good enough for one-line REPL inputs.
        bool has_toplevel_assign(const std::string &s) {
            bool inStr = false;
            for (std::size_t i = 0; i < s.size(); ++i) {
                const char c = s[i];
                if (c == '"') {
                    inStr = !inStr;
                } else if (!inStr && c == '=') {
                    const char prev = i > 0 ? s[i - 1] : '\0';
                    const char next = i + 1 < s.size() ? s[i + 1] : '\0';
                    if (next != '=' && prev != '=' && prev != '!' && prev != '<' &&
                        prev != '>')
                        return true;
                }
            }
            return false;
        }

        // A statement (run for effect, no echo) vs. an expression (echoed). `let` is handled
        // separately (it persists).
        bool looks_like_statement(const std::string &s) {
            return starts_statement_kw(s) || has_toplevel_assign(s);
        }
    }// namespace

    int repl(const std::vector<std::string> &searchDirs, const std::string &coreBcPath) {
        std::cout << "mxs REPL — enter an expression (1 + 2 * 3), a let (let x = 5), a "
                     "statement\n"
                     "(for i in 0..3 { ... }), or a definition (func/class/...). :reset "
                     "clears "
                     "state, :q quits.\n";

        // Import-gated stdlib (progress13 D2): no implicit globals. As a REPL-only convenience the
        // loop auto-injects a selective `import std.io.{...}` so interactive `println(...)`/format
        // work without the user importing every session (flagged to Mux — he wants no implicit
        // globals in PROGRAMS; this is interactive ergonomics, resolved through the real import
        // path, not a hidden prelude). The REPL's own __repl_echo stays a direct @@foreign binding
        // (it prints repr() but skips nil, so print(x)/bare statements don't echo a spurious nil).
        const std::string fullPrelude =
                "import std.io.{println, print, str, repr, format};\n"
                "@@foreign(symbol_name=\"mxs_repl_echo\") func __repl_echo(x: any) -> "
                "nil;\n";

        std::string defs;// accumulated definitions (func/class/...)
        std::string
                lets;// accumulated `let` bindings — replayed each eval so they persist
        std::string line;
        std::cout << "mxs> " << std::flush;
        while (std::getline(std::cin, line)) {
            const std::string t = trim(line);
            if (t.empty()) {
                std::cout << "mxs> " << std::flush;
                continue;
            }
            if (t == ":q" || t == ":quit" || t == ":exit") break;
            if (t ==
                ":reset") {// clear accumulated state so names can be redefined (D5 strict)
                defs.clear();
                lets.clear();
                std::cout << "(reset)\nmxs> " << std::flush;
                continue;
            }

            if (is_definition(t)) {
                defs += t + "\n";
                std::cout << "mxs> " << std::flush;
                continue;
            }

            // Build a main() thunk: replay the accumulated lets, then this line. A `let` persists
            // (appended to `lets`); a statement runs once for effect; an expression is echoed via
            // __repl_echo (repr form, nil suppressed). compile_core requires the entry be main().
            const bool isLet = is_let(t);
            const bool isStmt = !isLet && looks_like_statement(t);
            std::string stmt = t;
            if (!stmt.empty() && stmt.back() == ';') stmt.pop_back();

            // Block-form statements (for/if/while/loop/...) end in `}` and take no trailing `;`;
            // let/assignment/expression-statements do.
            const std::string semi = (!stmt.empty() && stmt.back() == '}') ? "" : ";";
            // Strict redeclaration (progress13 D5): re-`let`ing a name is NOT special-cased — the
            // replayed accumulated `let`s + this one collide in main()'s scope and hit the same-scope
            // redeclaration error (task15). Use `:reset` to clear state and redefine.
            std::string body = lets;
            if (isLet || isStmt) body += "    " + stmt + semi + "\n";
            else
                body += "    __repl_echo(" + stmt + ");\n";

            const std::string src = fullPrelude + "\n" + defs + "func main() -> int {\n" +
                                    body + "    return 0;\n}\n";
            if (auto tu = mxs::frontend::parser::parse_to_ast(src, "<repl>")) {
                // Resolve the auto-injected `import std.io.{...}` (and any the user typed) through
                // the real import path before codegen — same machinery as the driver.
                auto imp = mxs::frontend::imports::resolve_imports(*tu, "<repl>",
                                                                   searchDirs);
                if (!imp.ok) {
                    std::cout << "mxs> " << std::flush;
                    continue;
                }
                auto ctx = std::make_unique<llvm::LLVMContext>();
                if (auto mod = mxs::backend::codegen::compile_core(*tu, *ctx, "<repl>",
                                                                   imp.namespaces)) {
                    mxs::jit::run(std::move(mod), std::move(ctx), /*runtimeBc=*/"",
                                  "main", coreBcPath);
                    // Persist only if it built (a failed redefinition keeps the prior binding).
                    if (isLet) lets += "    " + stmt + ";\n";
                }
            }
            std::cout << "mxs> " << std::flush;
        }
        std::cout << "\n";
        // Skip global/atexit teardown: JIT'd code (and linked core.bc) may have registered
        // __cxa_atexit handlers whose code lives in now-freed JIT memory — the standard ORC
        // one-shot-runner shutdown hazard (same reason run-core uses _Exit). Flush first.
        std::fflush(nullptr);
        std::_Exit(0);
    }

}// namespace mxs::shell
