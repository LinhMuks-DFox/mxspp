// MXScript runtime — the C-ABI "fast-dispatch" primitives the compiler emits direct calls
// to (see docs/type_system.md §8, docs/ffi.md). Compiled to runtime.bc and LTO-linked /
// JIT-loaded with user IR, and also linkable directly (AOT).
//
// I/O is built on the standard streams via C stdio (FILE*), NOT C++ iostreams: it is lighter
// for a JIT/LTO runtime (no iostream static-init / locale machinery) and maps cleanly to
// mxs's io.stdout / io.stderr / io.stdlog. Stream ids: 0=stdout, 1=stderr, 2=stdlog.
//
// First slice: native-typed numeric I/O. The MXObject object model + the full standard
// library (strings, containers, dynamic dispatch, the mxs-side wrappers) build on top later.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {
    std::FILE *mxs_stream(std::int64_t id) {
        switch (id) {
            case 1: return stderr;// stderr (unbuffered)
            case 2: return stderr;// stdlog: error/log channel (shares stderr for now)
            default: return stdout;
        }
    }
}// namespace

extern "C" {

    void mxs_print_int(std::int64_t s, std::int64_t v) {
        std::fprintf(mxs_stream(s), "%lld", static_cast<long long>(v));
    }
    void mxs_println_int(std::int64_t s, std::int64_t v) {
        std::fprintf(mxs_stream(s), "%lld\n", static_cast<long long>(v));
    }
    void mxs_print_float(std::int64_t s, double v) { std::fprintf(mxs_stream(s), "%g", v); }
    void mxs_println_float(std::int64_t s, double v) {
        std::fprintf(mxs_stream(s), "%g\n", v);
    }
    void mxs_print_bool(std::int64_t s, std::int64_t v) {
        std::fprintf(mxs_stream(s), "%s", v ? "true" : "false");
    }
    void mxs_println_bool(std::int64_t s, std::int64_t v) {
        std::fprintf(mxs_stream(s), "%s\n", v ? "true" : "false");
    }
    void mxs_print_str(std::int64_t s, const char *p) {
        std::fprintf(mxs_stream(s), "%s", p ? p : "");
    }
    void mxs_println_str(std::int64_t s, const char *p) {
        std::fprintf(mxs_stream(s), "%s\n", p ? p : "");
    }
    void mxs_println(std::int64_t s) { std::fputc('\n', mxs_stream(s)); }

    // Unrecoverable error: print to stderr and terminate (docs §6 "panic").
    [[noreturn]] void mxs_panic(const char *msg) {
        std::fprintf(stderr, "mxs: panic: %s\n", msg ? msg : "");
        std::fflush(stderr);
        std::exit(1);
    }

}// extern "C"
