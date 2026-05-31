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

// =============================================================================
// Object model (start) — a tagged boxed value + dynamic dispatch (docs §8).
// This is the runtime substrate for "everything is an object": boxed values flow
// through the program as MXObject*, and dynamic-dispatch ops (mxs_op_*) inspect the
// tag at runtime. (Fast-path typed ops, RTTI via MXRuntimeTypeInfo, and reconciling
// with core's MXObject class hierarchy come next; the tag is the first cut.)
// =============================================================================
namespace {
    enum MXTag : std::int64_t { MX_INT = 0, MX_FLOAT = 1, MX_BOOL = 2 };
    struct MXObject {
        MXTag tag;
        union {
            std::int64_t i;
            double f;
        };
    };
    double as_double(const MXObject *o) {
        return o->tag == MX_FLOAT ? o->f : static_cast<double>(o->i);
    }
}// namespace

extern "C" {

    MXObject *mxs_box_int(std::int64_t v) { return new MXObject{ MX_INT, { .i = v } }; }
    MXObject *mxs_box_bool(std::int64_t v) { return new MXObject{ MX_BOOL, { .i = !!v } }; }
    MXObject *mxs_box_float(double v) {
        auto *o = new MXObject{ MX_FLOAT };
        o->f = v;
        return o;
    }

    std::int64_t mxs_obj_tag(const MXObject *o) { return o->tag; }
    std::int64_t mxs_obj_as_int(const MXObject *o) {
        return o->tag == MX_FLOAT ? static_cast<std::int64_t>(o->f) : o->i;
    }
    double mxs_obj_as_float(const MXObject *o) { return as_double(o); }
    std::int64_t mxs_obj_truthy(const MXObject *o) {
        return o->tag == MX_FLOAT ? (o->f != 0.0) : (o->i != 0);
    }
    void mxs_obj_free(MXObject *o) { delete o; }

    // Dynamic-dispatch arithmetic: int op int stays int; any float promotes to float.
    MXObject *mxs_op_add(MXObject *a, MXObject *b) {
        if (a->tag == MX_INT && b->tag == MX_INT) return mxs_box_int(a->i + b->i);
        return mxs_box_float(as_double(a) + as_double(b));
    }
    MXObject *mxs_op_sub(MXObject *a, MXObject *b) {
        if (a->tag == MX_INT && b->tag == MX_INT) return mxs_box_int(a->i - b->i);
        return mxs_box_float(as_double(a) - as_double(b));
    }
    MXObject *mxs_op_mul(MXObject *a, MXObject *b) {
        if (a->tag == MX_INT && b->tag == MX_INT) return mxs_box_int(a->i * b->i);
        return mxs_box_float(as_double(a) * as_double(b));
    }

    // Polymorphic println — dispatches on the runtime tag (one entry point, any type).
    void mxs_obj_println(std::int64_t stream, const MXObject *o) {
        std::FILE *st = mxs_stream(stream);
        switch (o->tag) {
            case MX_INT: std::fprintf(st, "%lld\n", static_cast<long long>(o->i)); break;
            case MX_FLOAT: std::fprintf(st, "%g\n", o->f); break;
            case MX_BOOL: std::fprintf(st, "%s\n", o->i ? "true" : "false"); break;
        }
    }

}// extern "C"
