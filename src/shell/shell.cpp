#include "mxspp/shell/shell.h"

#include "mxspp/backend/codegen.h"
#include "mxspp/frontend/imports.h"
#include "mxspp/frontend/parser.h"
#include "mxspp/jit/jit.h"

#include <llvm/IR/LLVMContext.h>

extern "C" {
#include "linenoise.h"
}

#include <algorithm>
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
            for (const char *kw : { "if", "for", "until", "loop", "return", "break",
                                    "continue", "assert", "defer", "do" })
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

        // Number of accumulated entries (each let/def is appended with a trailing newline).
        std::size_t count_entries(const std::string &accum) {
            return static_cast<std::size_t>(std::count(accum.begin(), accum.end(), '\n'));
        }

        // Build + parse + resolve-imports + codegen + JIT-run one REPL thunk
        // (`fullPrelude + defs + func main() { body; return 0; }`). Returns true iff it built and
        // ran. Shared by the normal expression/statement path and the `./objects_population`
        // command (which must go through this JIT path so it queries core.bc's population
        // singleton — the one the user's objects register with).
        bool eval_thunk(const std::string &fullPrelude, const std::string &defs,
                        const std::string &body,
                        const std::vector<std::string> &searchDirs,
                        const std::string &coreBcPath, const std::string &stdBcPath) {
            const std::string src = fullPrelude + "\n" + defs + "func main() -> int {\n" +
                                    body + "    return 0;\n}\n";
            auto tu = mxs::frontend::parser::parse_to_ast(src, "<repl>");
            if (!tu) return false;
            auto imp = mxs::frontend::imports::resolve_imports(*tu, "<repl>", searchDirs);
            if (!imp.ok) return false;
            auto ctx = std::make_unique<llvm::LLVMContext>();
            auto mod = mxs::backend::codegen::compile_core(*tu, *ctx, "<repl>",
                                                           imp.namespaces, imp.exposed);
            if (!mod) return false;
            // The REPL defaults to O2 (the JIT sweet spot); an `@@optimize(level=N)` in the
            // accumulated defs dials it (task40 / progress19 D0). tu->optLevel is -1 when unset.
            const int optLevel = tu->optLevel < 0 ? 2 : tu->optLevel;
            mxs::jit::run(std::move(mod), std::move(ctx), /*runtimeBc=*/stdBcPath, "main",
                          coreBcPath, optLevel);
            return true;
        }
    }// namespace

    int repl(const std::vector<std::string> &searchDirs, const std::string &coreBcPath,
             const std::string &stdBcPath) {
        std::cout << "mxs REPL — enter an expression (1 + 2 * 3), a let (let x = 5), a "
                     "statement\n"
                     "(for i in 0..3 { ... }), or a definition (func/class/...).\n"
                     "Meta-commands: ./quit  ./reset  ./status  ./objects_population "
                     "[all]\n";

        // Line editing + in-session history via linenoise (lib/linenoise). ↑/↓ walk history,
        // ←/→ move the cursor, etc. History is in-memory only (no save/load) — it dies with the
        // process. On non-TTY stdin (a pipe) linenoise falls back to a plain line read.
        linenoiseHistorySetMaxLen(1000);

        // Import-gated stdlib (progress13 D2): no implicit globals. As a REPL-only convenience the
        // loop auto-injects a selective `import std.io.{...}` so interactive `println(...)`/format
        // work without the user importing every session. `__repl_echo` prints repr() but skips nil
        // (so bare statements don't echo a spurious nil); `__objects_population[_all]` back the
        // `./objects_population` command — they bind the core.bc symbols so the count reflects the
        // JIT-side MXPopulationManager singleton (the one user objects live in).
        const std::string fullPrelude =
                "import std.io.{println, print, str, repr, format};\n"
                "@@foreign(symbol_name=\"mxs_repl_echo\") func __repl_echo(x: any) -> "
                "nil;\n"
                "@@foreign(symbol_name=\"mxs_population_dump\") func "
                "__objects_population() "
                "-> nil;\n"
                "@@foreign(symbol_name=\"mxs_population_dump_all\") func "
                "__objects_population_all() -> nil;\n";

        std::string defs;// accumulated definitions (func/class/...)
        std::string
                lets;// accumulated `let` bindings — replayed each eval so they persist
        std::size_t historyCount = 0;

        char *raw = nullptr;
        // linenoise prints the prompt, reads a line with editing, returns a malloc'd string (free
        // with linenoiseFree), or nullptr on EOF / Ctrl-D / Ctrl-C — which ends the session.
        while ((raw = linenoise("mxs> ")) != nullptr) {
            const std::string t = trim(raw);
            if (!t.empty()) {
                linenoiseHistoryAdd(raw);// in-session history; skip blank lines
                ++historyCount;
            }
            linenoiseFree(raw);
            if (t.empty()) continue;

            // `./`-prefixed meta-commands (progress15). These replace the old colon commands.
            if (t.rfind("./", 0) == 0) {
                if (t == "./quit") break;
                if (t == "./reset") {
                    defs.clear();
                    lets.clear();
                    std::cout << "(reset)\n";
                } else if (t == "./status") {
                    std::cout << "accumulated lets: " << count_entries(lets) << "\n"
                              << "accumulated defs: " << count_entries(defs) << "\n"
                              << "history entries:  " << historyCount << "\n"
                              << "core.bc:          "
                              << (coreBcPath.empty() ? "<not found>" : coreBcPath) << "\n"
                              << "std.bc:           "
                              << (stdBcPath.empty() ? "<not found>" : stdBcPath) << "\n"
                              << "module search dirs:";
                    for (const auto &d : searchDirs)
                        std::cout << "\n  - " << (d.empty() ? "<cwd>" : d);
                    std::cout << "\n";
                } else if (t == "./objects_population") {
                    // Route through the JIT eval path (replaying the accumulated lets) so the
                    // count reflects core.bc's singleton + the current session's live objects.
                    eval_thunk(fullPrelude, defs, lets + "    __objects_population();\n",
                               searchDirs, coreBcPath, stdBcPath);
                } else if (t == "./objects_population all") {
                    eval_thunk(fullPrelude, defs,
                               lets + "    __objects_population_all();\n", searchDirs,
                               coreBcPath, stdBcPath);
                } else {
                    std::cout << "unknown command: " << t
                              << "  (try ./quit  ./reset  ./status  ./objects_population "
                                 "[all])\n";
                }
                continue;
            }

            if (is_definition(t)) {
                defs += t + "\n";
                continue;
            }

            // Build a main() thunk: replay the accumulated lets, then this line. A `let` persists
            // (appended to `lets`); a statement runs once for effect; an expression is echoed via
            // __repl_echo (repr form, nil suppressed).
            const bool isLet = is_let(t);
            const bool isStmt = !isLet && looks_like_statement(t);
            std::string stmt = t;
            if (!stmt.empty() && stmt.back() == ';') stmt.pop_back();

            // Block-form statements (for/if/loop/...) end in `}` and take no trailing `;`;
            // let/assignment/expression-statements do. Strict redeclaration (progress13 D5):
            // re-`let`ing a name is NOT special-cased — the replayed accumulated `let`s + this one
            // collide in main()'s scope and hit the same-scope redeclaration error. Use `./reset`.
            const std::string semi = (!stmt.empty() && stmt.back() == '}') ? "" : ";";
            std::string body = lets;
            if (isLet || isStmt) body += "    " + stmt + semi + "\n";
            else
                body += "    __repl_echo(" + stmt + ");\n";

            // Persist a `let` only if it built (a failed redefinition keeps the prior binding).
            if (eval_thunk(fullPrelude, defs, body, searchDirs, coreBcPath, stdBcPath)) {
                if (isLet) lets += "    " + stmt + ";\n";
            }
        }
        std::cout << "\n";
        // Skip global/atexit teardown: JIT'd code (and linked core.bc) may have registered
        // __cxa_atexit handlers whose code lives in now-freed JIT memory — the standard ORC
        // one-shot-runner shutdown hazard (same reason run-core uses _Exit). Flush first.
        std::fflush(nullptr);
        std::_Exit(0);
    }

}// namespace mxs::shell
