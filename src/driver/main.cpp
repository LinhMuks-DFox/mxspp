#include "mxspp/backend/codegen.h"
#include "mxspp/frontend/imports.h"
#include "mxspp/frontend/parser.h"
#include "mxspp/jit/jit.h"
#include "mxspp/shell/shell.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <cstdio>
#include <cstdlib>

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
    // The legacy hardcoded stdlib prelude string is GONE (progress13 D2): the stdlib is now
    // import-gated — nothing is in scope without an `import`. Its bindings live in std/io.mxs
    // (println/print/str/repr/format/arraylist/raise/exit) + std/time.mxs, loaded by the import
    // resolver. There are no implicit globals.

    std::string read_file(const std::string &path, bool &ok) {
        std::ifstream f(path);
        if (!f) {
            ok = false;
            return { };
        }
        std::stringstream ss;
        ss << f.rdbuf();
        ok = true;
        return ss.str();
    }

    // Locate a bitcode file (core.bc): next to the executable first, then a few
    // common build-relative locations.
    std::string find_bc(const std::string &name) {
#if defined(__linux__)
        char buf[4096];
        const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string p(buf);
            const auto slash = p.find_last_of('/');
            if (slash != std::string::npos) {
                std::string c = p.substr(0, slash) + "/" + name;
                if (std::ifstream(c)) return c;
            }
        }
#elif defined(__APPLE__)
        char buf[4096];
        std::uint32_t bufsize = sizeof(buf);
        if (_NSGetExecutablePath(buf, &bufsize) == 0) {
            std::string p(buf);
            const auto slash = p.find_last_of('/');
            if (slash != std::string::npos) {
                std::string c = p.substr(0, slash) + "/" + name;
                if (std::ifstream(c)) return c;
            }
        }
#endif
        for (const std::string base :
             { std::string{ }, std::string{ "build/bin/" }, std::string{ "bin/" } })
            if (std::ifstream(base + name)) return base + name;
        return "";
    }
    std::string core_bc_path() { return find_bc("core.bc"); }
    // The std-library bitcode (std.bc): the C/C++ backends for the importable std.* modules
    // (progress17), JIT-linked alongside core.bc so the @@foreign symbols in std/*.mxs resolve.
    std::string std_bc_path() { return find_bc("std.bc"); }

    // Directory containing the running executable (empty if undiscoverable). The std modules are
    // copied next to the binary at build time (src/CMakeLists), so this is the robust install path.
    std::string exe_dir() {
#if defined(__linux__)
        char buf[4096];
        const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string p(buf);
            const auto slash = p.find_last_of('/');
            if (slash != std::string::npos) return p.substr(0, slash);
        }
#elif defined(__APPLE__)
        char buf[4096];
        std::uint32_t bufsize = sizeof(buf);
        if (_NSGetExecutablePath(buf, &bufsize) == 0) {
            std::string p(buf);
            const auto slash = p.find_last_of('/');
            if (slash != std::string::npos) return p.substr(0, slash);
        }
#endif
        return "";
    }

    // The module search path for `import` (progress13 D2). A module `std.io` is sought at
    // `<dir>/std/io.mxs` for each dir in order: the CWD first (dev runs from the repo root, where
    // `std/` lives), then the executable's directory (std/ is copied beside the binary at build
    // time), then build-relative fallbacks. First hit wins.
    std::vector<std::string> std_search_dirs() {
        std::vector<std::string> dirs;
        dirs.emplace_back("");// CWD-relative: ./std/<path>.mxs
        if (const std::string ed = exe_dir(); !ed.empty()) dirs.push_back(ed);
        dirs.emplace_back("build/bin");
        dirs.emplace_back("bin");
        return dirs;
    }
}// namespace

int main(int argc, char **argv) {
    namespace parser = mxs::frontend::parser;
    const std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty() || (args.size() == 1 && args[0] == "shell"))
        return mxs::shell::repl(std_search_dirs(), core_bc_path(), std_bc_path());

    // New object model (progress09 ④): values are real core::MXObject*; arithmetic emits the
    // typed core ABI (mxs_int_*), linked from core.bc. Seed slice (int arithmetic + print).
    if (args.size() == 2 && args[0] == "run-core") {
        bool ok = false;
        const std::string source = read_file(args[1], ok);
        if (!ok) {
            std::cerr << "error: cannot open " << args[1] << "\n";
            return 1;
        }
        auto tu = parser::parse_to_ast(source, args[1]);
        if (!tu) {
            std::cerr << "error: failed to parse " << args[1] << "\n";
            return 1;
        }
        // Resolve + load + merge `import` modules (progress13 D2). After this the TU holds no
        // Import nodes; qualified-import namespaces flow to codegen for `ns.fn(...)` resolution.
        // There is no implicit prelude — a program reaches stdlib names only through its imports.
        auto imp =
                mxs::frontend::imports::resolve_imports(*tu, args[1], std_search_dirs());
        if (!imp.ok) return 1;
        auto context = std::make_unique<llvm::LLVMContext>();
        auto module = mxs::backend::codegen::compile_core(*tu, *context, args[1],
                                                          imp.namespaces);
        if (!module) {
            std::cerr << "error: core codegen failed for " << args[1] << "\n";
            return 1;
        }
        const int rc =
                mxs::jit::run(std::move(module), std::move(context),
                              /*runtimeBc=*/std_bc_path(), /*entry=*/"main",
                              core_bc_path());
        // One-shot run: the program's output is done. Exit immediately, skipping global/atexit
        // teardown — JIT'd code may have registered __cxa_atexit handlers whose code lives in
        // now-freed JIT memory (the standard ORC one-shot-runner shutdown hazard). (REPL paths
        // return through jit::run normally and are unaffected.)
        std::fflush(nullptr);
        std::_Exit(rc);
    }

    // Parse-only lint: report syntax errors and exit non-zero on failure. The parser already
    // prints `file:line:col: syntax error: …` diagnostics to stderr, so we add nothing here.
    if (args.size() == 2 && args[0] == "check") {
        bool ok = false;
        const std::string source = read_file(args[1], ok);
        if (!ok) {
            std::cerr << "error: cannot open " << args[1] << "\n";
            return 1;
        }
        auto tu = parser::parse_to_ast(source, args[1]);
        if (!tu) return 1;
        // Validate that every `import` resolves + parses (loads the std modules) as part of the
        // lint, mirroring run-core. Import-gating means a missing/typo'd module is an error here.
        auto imp =
                mxs::frontend::imports::resolve_imports(*tu, args[1], std_search_dirs());
        if (!imp.ok) return 1;
        std::cout << "ok\n";
        return 0;
    }

    // Parse-only: print the AST. Does not run codegen.
    if (args.size() == 2 && args[0] == "--dump-ast") {
        bool ok = false;
        const std::string source = read_file(args[1], ok);
        if (!ok) {
            std::cerr << "error: cannot open " << args[1] << "\n";
            return 1;
        }
        auto tu = parser::parse_to_ast(source, args[1]);
        if (!tu) {
            std::cerr << "error: failed to parse " << args[1] << "\n";
            return 1;
        }
        parser::dump_ast(*tu, std::cout);
        return 0;
    }

    std::cout << "mxs (MXScript)\n"
              << "  usage: mxs            (or: mxs shell)  # interactive REPL\n"
              << "         mxs run-core   <file.mxs>       # JIT-compile and run main()\n"
              << "         mxs check      <file.mxs>       # parse-only lint (syntax "
                 "errors)\n"
              << "         mxs --dump-ast <file.mxs>       # parse and print the AST\n";
    return 0;
}
