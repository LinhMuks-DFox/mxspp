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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    std::FILE *mxs_stream(std::int64_t id) {
        switch (id) {
            case 1:
                return stderr;// stderr (unbuffered)
            case 2:
                return stderr;// stdlog: error/log channel (shares stderr for now)
            default:
                return stdout;
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
    enum MXTag : std::int64_t {
        MX_INT = 0,
        MX_FLOAT = 1,
        MX_BOOL = 2,
        MX_STR = 3,
        MX_NIL = 4,
    };
    struct MXObject {
        MXTag tag;
        union {
            std::int64_t i;
            double f;
            char *s;// owned heap string (MX_STR)
        };
    };
    double as_double(const MXObject *o) {
        return o->tag == MX_FLOAT ? o->f : static_cast<double>(o->i);
    }
    bool both_int(const MXObject *a, const MXObject *b) {
        return a->tag == MX_INT && b->tag == MX_INT;
    }
    bool both_str(const MXObject *a, const MXObject *b) {
        return a->tag == MX_STR && b->tag == MX_STR;
    }
    // Ordered comparison: -1 / 0 / 1. Strings compare lexicographically; everything else
    // numerically (bool reads as 0/1). Mixing a string with a number is a panic.
    int mx_cmp(const MXObject *a, const MXObject *b) {
        if (both_str(a, b)) {
            const int r = std::strcmp(a->s, b->s);
            return r < 0 ? -1 : r > 0 ? 1 : 0;
        }
        if (a->tag == MX_STR || b->tag == MX_STR)
            mxs_panic("cannot compare string and number");
        const double x = as_double(a), y = as_double(b);
        return x < y ? -1 : x > y ? 1 : 0;
    }
    void print_obj(std::FILE *st, const MXObject *o, bool newline) {
        switch (o->tag) {
            case MX_INT:
                std::fprintf(st, "%lld", static_cast<long long>(o->i));
                break;
            case MX_FLOAT:
                std::fprintf(st, "%g", o->f);
                break;
            case MX_BOOL:
                std::fprintf(st, "%s", o->i ? "true" : "false");
                break;
            case MX_STR:
                std::fprintf(st, "%s", o->s ? o->s : "");
                break;
            case MX_NIL:
                std::fprintf(st, "nil");
                break;
        }
        if (newline) std::fputc('\n', st);
    }
}// namespace

extern "C" {

MXObject *mxs_box_int(std::int64_t v) { return new MXObject{ MX_INT, { .i = v } }; }
MXObject *mxs_box_bool(std::int64_t v) { return new MXObject{ MX_BOOL, { .i = !!v } }; }
MXObject *mxs_box_nil() { return new MXObject{ MX_NIL, { .i = 0 } }; }
MXObject *mxs_box_float(double v) {
    auto *o = new MXObject{ MX_FLOAT };
    o->f = v;
    return o;
}
// Box a string: takes ownership of a freshly-allocated copy of `p`.
MXObject *mxs_box_str(const char *p) {
    auto *o = new MXObject{ MX_STR };
    const std::size_t n = p ? std::strlen(p) : 0;
    o->s = static_cast<char *>(std::malloc(n + 1));
    if (p) std::memcpy(o->s, p, n);
    o->s[n] = '\0';
    return o;
}

std::int64_t mxs_obj_tag(const MXObject *o) { return o->tag; }
std::int64_t mxs_obj_as_int(const MXObject *o) {
    return o->tag == MX_FLOAT ? static_cast<std::int64_t>(o->f) : o->i;
}
double mxs_obj_as_float(const MXObject *o) { return as_double(o); }
std::int64_t mxs_obj_truthy(const MXObject *o) {
    switch (o->tag) {
        case MX_FLOAT:
            return o->f != 0.0;
        case MX_STR:
            return o->s && o->s[0] != '\0';
        case MX_NIL:
            return 0;
        default:
            return o->i != 0;
    }
}
void mxs_obj_free(MXObject *o) {
    if (o && o->tag == MX_STR) std::free(o->s);
    delete o;
}

// Dynamic-dispatch arithmetic: int op int stays int; any float promotes to float;
// string + string concatenates.
MXObject *mxs_op_add(MXObject *a, MXObject *b) {
    if (both_int(a, b)) return mxs_box_int(a->i + b->i);
    if (both_str(a, b)) {
        const std::size_t la = std::strlen(a->s), lb = std::strlen(b->s);
        char *buf = static_cast<char *>(std::malloc(la + lb + 1));
        std::memcpy(buf, a->s, la);
        std::memcpy(buf + la, b->s, lb + 1);
        MXObject *r = mxs_box_str(buf);
        std::free(buf);
        return r;
    }
    if (a->tag == MX_STR || b->tag == MX_STR) mxs_panic("cannot add string and number");
    return mxs_box_float(as_double(a) + as_double(b));
}
MXObject *mxs_op_sub(MXObject *a, MXObject *b) {
    if (both_int(a, b)) return mxs_box_int(a->i - b->i);
    return mxs_box_float(as_double(a) - as_double(b));
}
MXObject *mxs_op_mul(MXObject *a, MXObject *b) {
    if (both_int(a, b)) return mxs_box_int(a->i * b->i);
    return mxs_box_float(as_double(a) * as_double(b));
}
MXObject *mxs_op_div(MXObject *a, MXObject *b) {
    if (both_int(a, b)) {
        if (b->i == 0) mxs_panic("division by zero");
        return mxs_box_int(a->i / b->i);
    }
    const double y = as_double(b);
    if (y == 0.0) mxs_panic("division by zero");
    return mxs_box_float(as_double(a) / y);
}
MXObject *mxs_op_mod(MXObject *a, MXObject *b) {
    if (both_int(a, b)) {
        if (b->i == 0) mxs_panic("modulo by zero");
        return mxs_box_int(a->i % b->i);
    }
    const double y = as_double(b);
    if (y == 0.0) mxs_panic("modulo by zero");
    return mxs_box_float(std::fmod(as_double(a), y));
}
MXObject *mxs_op_neg(MXObject *a) {
    if (a->tag == MX_FLOAT) return mxs_box_float(-a->f);
    if (a->tag == MX_INT || a->tag == MX_BOOL) return mxs_box_int(-a->i);
    mxs_panic("cannot negate this value");
}
MXObject *mxs_op_not(MXObject *a) { return mxs_box_bool(!mxs_obj_truthy(a)); }

// Comparisons — each returns a boxed bool.
MXObject *mxs_op_lt(MXObject *a, MXObject *b) { return mxs_box_bool(mx_cmp(a, b) < 0); }
MXObject *mxs_op_le(MXObject *a, MXObject *b) { return mxs_box_bool(mx_cmp(a, b) <= 0); }
MXObject *mxs_op_gt(MXObject *a, MXObject *b) { return mxs_box_bool(mx_cmp(a, b) > 0); }
MXObject *mxs_op_ge(MXObject *a, MXObject *b) { return mxs_box_bool(mx_cmp(a, b) >= 0); }
MXObject *mxs_op_eq(MXObject *a, MXObject *b) {
    if (a->tag == MX_STR || b->tag == MX_STR)
        return mxs_box_bool(both_str(a, b) && std::strcmp(a->s, b->s) == 0);
    return mxs_box_bool(as_double(a) == as_double(b));
}
MXObject *mxs_op_ne(MXObject *a, MXObject *b) {
    return mxs_box_bool(!mxs_obj_truthy(mxs_op_eq(a, b)));
}

// Polymorphic print — dispatches on the runtime tag (one entry point, any type).
// The stream-aware form plus stdout/stderr convenience symbols the stdlib binds to.
void mxs_obj_println(std::int64_t stream, const MXObject *o) {
    print_obj(mxs_stream(stream), o, true);
}
void mxs_print_obj(const MXObject *o) { print_obj(stdout, o, false); }
void mxs_println_obj(const MXObject *o) { print_obj(stdout, o, true); }
void mxs_eprint_obj(const MXObject *o) { print_obj(stderr, o, false); }
void mxs_eprintln_obj(const MXObject *o) { print_obj(stderr, o, true); }

}// extern "C"
