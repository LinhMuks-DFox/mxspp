#include "mxspp/backend/codegen.h"
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
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {
    // The std prelude (std.io), auto-included on codegen paths so programs can call
    // println(...) etc. without a manual @@foreign line. Mirrors std/io.mxs; once import
    // resolution lands this is read from the file instead. Parsed as its own unit so user
    // source line numbers stay accurate for diagnostics.
    constexpr const char *kPrelude = R"MXS(
@@foreign(symbol_name="mxs_println_int") func __println_int(stream: int, x: int) -> nil;
@@foreign(symbol_name="mxs_print_int")   func __print_int(stream: int, x: int) -> nil;
func println(x: int) -> nil { __println_int(0, x); }
func print(x: int) -> nil { __print_int(0, x); }
func eprintln(x: int) -> nil { __println_int(1, x); }
)MXS";

    // The object-mode prelude. In object mode every value is a boxed MXObject*, so the print
    // family binds DIRECTLY (via generic @@foreign — no per-function codegen) to the runtime's
    // polymorphic print symbols, each taking one MXObject*. `any` is the dynamic object type
    // (every object-mode parameter lowers to a boxed ptr regardless of annotation).
    constexpr const char *kObjPrelude = R"MXS(
@@foreign(symbol_name="mxs_println_obj")  func println(x: any) -> nil;
@@foreign(symbol_name="mxs_print_obj")    func print(x: any) -> nil;
@@foreign(symbol_name="mxs_eprintln_obj") func eprintln(x: any) -> nil;
@@foreign(symbol_name="mxs_eprint_obj")   func eprint(x: any) -> nil;
)MXS";

    // The new object-model prelude (progress09 ④). Values are real core::MXObject*; the print
    // family binds via generic @@foreign to the polymorphic print over MXObject::repr() (defined
    // in core.bc). No per-function hardcoding (D3).
    constexpr const char *kCorePrelude = R"MXS(
@@foreign(symbol_name="mxs_println_object")   func println(x: any) -> nil;
@@foreign(symbol_name="mxs_print_object")     func print(x: any) -> nil;
@@foreign(symbol_name="mxs_len")              func len(xs: any) -> any;
@@foreign(symbol_name="mxs_arraylist_append") func append(xs: any, v: any) -> nil;
@@foreign(symbol_name="mxs_arraylist_new")    func arraylist() -> any;
@@foreign(symbol_name="mxs_raise")            func raise(e: any) -> nil;
@@foreign(symbol_name="mxs_exit")             func exit(code: any) -> nil;
)MXS";

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

    // Locate a bitcode file (runtime.bc / core.bc): next to the executable first, then a few
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
             { std::string{}, std::string{ "build/bin/" }, std::string{ "bin/" } })
            if (std::ifstream(base + name)) return base + name;
        return "";
    }
    std::string runtime_bc_path() { return find_bc("runtime.bc"); }
    std::string core_bc_path() { return find_bc("core.bc"); }
}// namespace

int main(int argc, char **argv) {
    namespace parser = mxs::frontend::parser;
    const std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty() || (args.size() == 1 && args[0] == "shell"))
        return mxs::shell::repl(kPrelude, runtime_bc_path());

    // Object-mode: values are boxed MXObject*; arithmetic, comparisons and the print family
    // all go through dynamic dispatch / generic @@foreign bindings (kObjPrelude).
    if (args.size() == 2 && args[0] == "run-obj") {
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
        // Prepend the object-mode std prelude (parsed separately so user line numbers stay
        // accurate). println/print/... resolve through it like any other call — no hardcoding.
        if (auto prelude = parser::parse_to_ast(kObjPrelude, "<obj-prelude>")) {
            tu->statements.insert(tu->statements.begin(),
                                  std::make_move_iterator(prelude->statements.begin()),
                                  std::make_move_iterator(prelude->statements.end()));
        }
        auto context = std::make_unique<llvm::LLVMContext>();
        auto module = mxs::backend::codegen::compile_obj(*tu, *context, args[1]);
        if (!module) {
            std::cerr << "error: object-mode codegen failed for " << args[1] << "\n";
            return 1;
        }
        return mxs::jit::run(std::move(module), std::move(context), runtime_bc_path());
    }

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
        if (auto prelude = parser::parse_to_ast(kCorePrelude, "<core-prelude>")) {
            tu->statements.insert(tu->statements.begin(),
                                  std::make_move_iterator(prelude->statements.begin()),
                                  std::make_move_iterator(prelude->statements.end()));
        }
        auto context = std::make_unique<llvm::LLVMContext>();
        auto module = mxs::backend::codegen::compile_core(*tu, *context, args[1]);
        if (!module) {
            std::cerr << "error: core codegen failed for " << args[1] << "\n";
            return 1;
        }
        const int rc =
                mxs::jit::run(std::move(module), std::move(context), /*runtimeBc=*/"",
                              /*entry=*/"main", core_bc_path());
        // One-shot run: the program's output is done. Exit immediately, skipping global/atexit
        // teardown — JIT'd code may have registered __cxa_atexit handlers whose code lives in
        // now-freed JIT memory (the standard ORC one-shot-runner shutdown hazard). (REPL paths
        // return through jit::run normally and are unaffected.)
        std::fflush(nullptr);
        std::_Exit(rc);
    }

    if (args.size() == 2 &&
        (args[0] == "--dump-ast" || args[0] == "--emit-ir" || args[0] == "run")) {
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

        if (args[0] == "--dump-ast") {
            parser::dump_ast(*tu, std::cout);
            return 0;
        }

        // Prepend the std prelude (parsed separately so user line numbers are unaffected).
        if (auto prelude = parser::parse_to_ast(kPrelude, "<prelude>")) {
            tu->statements.insert(tu->statements.begin(),
                                  std::make_move_iterator(prelude->statements.begin()),
                                  std::make_move_iterator(prelude->statements.end()));
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
        return mxs::jit::run(std::move(module), std::move(context), runtime_bc_path());
    }

    std::cout << "mxs (MXScript)\n"
              << "  usage: mxs            (or: mxs shell)  # interactive REPL\n"
              << "         mxs run        <file.mxs>       # JIT-compile and run main()\n"
              << "         mxs --emit-ir  <file.mxs>       # lower the AST to LLVM IR\n"
              << "         mxs --dump-ast <file.mxs>       # parse and print the AST\n";
    return 0;
}
