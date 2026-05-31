#include "mxspp/core/MXArrayList.h"
#include "mxspp/core/MXBoolean.h"
#include "mxspp/core/MXError.h"
#include "mxspp/core/MXFloat.h"
#include "mxspp/core/MXInteger.h"
#include "mxspp/core/MXNil.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/MXString.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

// Dynamic-dispatch operators over the real core objects (docs §8): codegen emits calls to these
// and they inspect the runtime types to pick int / float / string behavior. int op int stays exact
// (MXInteger bignum); any float operand promotes to float; string + string concatenates; an
// unsupported combination yields an MXError (forward-compatible with the match-based error model).
// These are the extern "C" symbols the new object-model codegen lowers operators to.

namespace {
    using mxs::builtin::MXBoolean;
    using mxs::builtin::MXFloat;
    using mxs::builtin::MXInteger;
    using mxs::builtin::MXString;
    using mxs::core::MXError;
    using mxs::core::MXObject;

    template<class T>
    const T *as(const MXObject *o) {
        return dynamic_cast<const T *>(o);
    }
    bool is_num(const MXObject *o) { return as<MXInteger>(o) || as<MXFloat>(o); }
    // Numeric value as a double (lossy for integers beyond 2^53 — refined later).
    double to_d(const MXObject *o) {
        if (const auto *f = as<MXFloat>(o)) return f->value();
        if (const auto *i = as<MXInteger>(o)) {
            bool ok = false;
            return static_cast<double>(i->to_i64(ok));
        }
        return 0.0;
    }
    MXObject *type_err(const char *op) {
        return new MXError("TypeError",
                           std::string("unsupported operand type(s) for ") + op);
    }
    // -1 / 0 / 1 ordered, or 2 if incomparable (mixed string/number).
    int order(const MXObject *a, const MXObject *b) {
        if (const auto *ia = as<MXInteger>(a))
            if (const auto *ib = as<MXInteger>(b)) return ia->cmp(*ib);
        if (const auto *sa = as<MXString>(a))
            if (const auto *sb = as<MXString>(b)) return sa->cmp(*sb);
        if (is_num(a) && is_num(b)) {
            const double x = to_d(a), y = to_d(b);
            return x < y ? -1 : x > y ? 1 : 0;
        }
        return 2;// incomparable
    }
    bool structurally_equal(const MXObject *a, const MXObject *b) {
        if (const auto *ia = as<MXInteger>(a))
            if (const auto *ib = as<MXInteger>(b)) return ia->cmp(*ib) == 0;
        if (const auto *sa = as<MXString>(a))
            if (const auto *sb = as<MXString>(b)) return sa->cmp(*sb) == 0;
        if (is_num(a) && is_num(b)) return to_d(a) == to_d(b);
        if (const auto *ba = as<MXBoolean>(a))
            if (const auto *bb = as<MXBoolean>(b)) return ba->value() == bb->value();
        if (as<mxs::builtin::MXNil>(a) && as<mxs::builtin::MXNil>(b)) return true;
        return false;
    }
}// namespace

extern "C" {

MXObject *mxs_op_add(MXObject *a, MXObject *b) {
    if (const auto *ia = as<MXInteger>(a))
        if (const auto *ib = as<MXInteger>(b)) return ia->add(*ib).release();
    if (const auto *sa = as<MXString>(a))
        if (const auto *sb = as<MXString>(b)) return sa->concat(*sb).release();
    if (is_num(a) && is_num(b)) return new MXFloat(to_d(a) + to_d(b));
    return type_err("+");
}
MXObject *mxs_op_sub(MXObject *a, MXObject *b) {
    if (const auto *ia = as<MXInteger>(a))
        if (const auto *ib = as<MXInteger>(b)) return ia->sub(*ib).release();
    if (is_num(a) && is_num(b)) return new MXFloat(to_d(a) - to_d(b));
    return type_err("-");
}
MXObject *mxs_op_mul(MXObject *a, MXObject *b) {
    if (const auto *ia = as<MXInteger>(a))
        if (const auto *ib = as<MXInteger>(b)) return ia->mul(*ib).release();
    if (is_num(a) && is_num(b)) return new MXFloat(to_d(a) * to_d(b));
    return type_err("*");
}
MXObject *mxs_op_div(MXObject *a, MXObject *b) {
    if (const auto *ia = as<MXInteger>(a))
        if (const auto *ib = as<MXInteger>(b)) return ia->div(*ib).release();
    if (is_num(a) && is_num(b)) {
        const double y = to_d(b);
        if (y == 0.0) return new MXError("ZeroDivisionError", "float division by zero");
        return new MXFloat(to_d(a) / y);
    }
    return type_err("/");
}
MXObject *mxs_op_mod(MXObject *a, MXObject *b) {
    if (const auto *ia = as<MXInteger>(a))
        if (const auto *ib = as<MXInteger>(b)) return ia->mod(*ib).release();
    if (is_num(a) && is_num(b)) {
        const double y = to_d(b);
        if (y == 0.0) return new MXError("ZeroDivisionError", "float modulo by zero");
        return new MXFloat(std::fmod(to_d(a), y));
    }
    return type_err("%");
}
MXObject *mxs_op_pow(MXObject *a, MXObject *b) {
    if (const auto *ia = as<MXInteger>(a))
        if (const auto *ib = as<MXInteger>(b)) return ia->pow(*ib).release();
    if (is_num(a) && is_num(b)) return new MXFloat(std::pow(to_d(a), to_d(b)));
    return type_err("**");
}
MXObject *mxs_op_neg(MXObject *a) {
    if (const auto *ia = as<MXInteger>(a)) return ia->neg().release();
    if (const auto *fa = as<MXFloat>(a)) return new MXFloat(-fa->value());
    return type_err("unary -");
}
MXObject *mxs_op_not(MXObject *a) { return new MXBoolean(!(a && a->is_truthy())); }

MXObject *mxs_op_lt(MXObject *a, MXObject *b) {
    const int c = order(a, b);
    return c == 2 ? type_err("<") : new MXBoolean(c < 0);
}
MXObject *mxs_op_le(MXObject *a, MXObject *b) {
    const int c = order(a, b);
    return c == 2 ? type_err("<=") : new MXBoolean(c <= 0);
}
MXObject *mxs_op_gt(MXObject *a, MXObject *b) {
    const int c = order(a, b);
    return c == 2 ? type_err(">") : new MXBoolean(c > 0);
}
MXObject *mxs_op_ge(MXObject *a, MXObject *b) {
    const int c = order(a, b);
    return c == 2 ? type_err(">=") : new MXBoolean(c >= 0);
}
MXObject *mxs_op_eq(MXObject *a, MXObject *b) {
    return new MXBoolean(structurally_equal(a, b));
}
MXObject *mxs_op_ne(MXObject *a, MXObject *b) {
    return new MXBoolean(!structurally_equal(a, b));
}

// Runtime type test for match's type-binding patterns (`case x: Type => …`): is `o` of the
// mxs type named `type`? Maps the mxs type name to the runtime class. `any`/`Object` match
// anything; unknown names fall back to comparing the object's RTTI name.
std::int64_t mxs_is_type(const MXObject *o, const char *type) {
    if (!o || !type) return 0;
    const std::string t = type;
    if (t == "any" || t == "Object" || t == "object") return 1;
    if (t == "Error") return as<MXError>(o) != nullptr;
    if (t == "int" || t == "Int" || t == "Integer") return as<MXInteger>(o) != nullptr;
    if (t == "float" || t == "Float") return as<MXFloat>(o) != nullptr;
    if (t == "String" || t == "string" || t == "Str") return as<MXString>(o) != nullptr;
    if (t == "bool" || t == "Bool" || t == "Boolean") return as<MXBoolean>(o) != nullptr;
    if (t == "nil" || t == "Nil") return as<mxs::builtin::MXNil>(o) != nullptr;
    if (t == "ArrayList" || t == "List")
        return as<mxs::builtin::MXArrayList>(o) != nullptr;
    return o->get_rtti().name == t ? 1 : 0;// user/class types: match by RTTI name
}

// `raise` / `exit` as functions (progress06: `raise` is a special form of `exit` — exit with
// error). Both terminate the process immediately (flush + _Exit, skipping the ORC exit-teardown
// hazard). raise prints the error object; exit uses the given integer code.
[[noreturn]] void mxs_raise(const MXObject *err) {
    std::fprintf(stderr, "mxs: unhandled error: %s\n", err ? err->repr().c_str() : "nil");
    std::fflush(nullptr);
    std::_Exit(1);
}
[[noreturn]] void mxs_exit(const MXObject *code) {
    std::int64_t n = 0;
    if (const auto *i = as<MXInteger>(code)) {
        bool ok = false;
        n = i->to_i64(ok);
    }
    std::fflush(nullptr);
    std::_Exit(static_cast<int>(n));
}

}// extern "C"
