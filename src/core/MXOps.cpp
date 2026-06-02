#include "mxspp/core/MXArrayList.h"
#include "mxspp/core/MXBoolean.h"
#include "mxspp/core/MXClassInfo.h"
#include "mxspp/core/MXError.h"
#include "mxspp/core/MXFloat.h"
#include "mxspp/core/MXInstance.h"
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
    using mxs::builtin::MXArrayList;
    using mxs::builtin::MXBoolean;
    using mxs::builtin::MXFloat;
    using mxs::builtin::MXInstance;
    using mxs::builtin::MXInteger;
    using mxs::builtin::MXString;
    using mxs::core::MXClassInfo;
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
    // Operator overloading dispatch (progress11). If `a` is a user instance whose class overrides
    // the operator at `slot` (a non-null vtable entry), call the user operator. The user operator
    // function has the shape `MXObject*(self, other)` (binary) / `MXObject*(self)` (unary). Returns
    // nullptr when there is no override, so the builtin numeric/string logic runs instead.
    // The user operator (emitted as a method) is callee-owned: its parameter bindings adopt and
    // then release `self`/`other`. The mxs_op_* path, by contrast, only BORROWS its operands (the
    // codegen caller releases them). So retain the operands across the user-operator call to keep
    // those two conventions balanced (no double-release).
    MXObject *user_binop(MXObject *a, MXObject *b, std::int64_t slot) {
        const auto *inst = as<MXInstance>(a);
        if (!inst) return nullptr;
        const MXClassInfo *ci = inst->classinfo();
        if (!ci || !ci->vtable || slot >= ci->vtable_len) return nullptr;
        void *fn = ci->vtable[slot];
        if (!fn) return nullptr;
        a->retain();
        if (b) b->retain();
        return reinterpret_cast<MXObject *(*) (MXObject *, MXObject *)>(fn)(a, b);
    }
    MXObject *user_unop(MXObject *a, std::int64_t slot) {
        const auto *inst = as<MXInstance>(a);
        if (!inst) return nullptr;
        const MXClassInfo *ci = inst->classinfo();
        if (!ci || !ci->vtable || slot >= ci->vtable_len) return nullptr;
        void *fn = ci->vtable[slot];
        if (!fn) return nullptr;
        a->retain();
        return reinterpret_cast<MXObject *(*) (MXObject *)>(fn)(a);
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
    if (MXObject *r = user_binop(a, b, mxs::core::MX_SLOT_OP_ADD)) return r;
    if (const auto *ia = as<MXInteger>(a))
        if (const auto *ib = as<MXInteger>(b)) return ia->add(*ib).release();
    if (const auto *sa = as<MXString>(a))
        if (const auto *sb = as<MXString>(b)) return sa->concat(*sb).release();
    if (is_num(a) && is_num(b)) return new MXFloat(to_d(a) + to_d(b));
    return type_err("+");
}
MXObject *mxs_op_sub(MXObject *a, MXObject *b) {
    if (MXObject *r = user_binop(a, b, mxs::core::MX_SLOT_OP_SUB)) return r;
    if (const auto *ia = as<MXInteger>(a))
        if (const auto *ib = as<MXInteger>(b)) return ia->sub(*ib).release();
    if (is_num(a) && is_num(b)) return new MXFloat(to_d(a) - to_d(b));
    return type_err("-");
}
MXObject *mxs_op_mul(MXObject *a, MXObject *b) {
    if (MXObject *r = user_binop(a, b, mxs::core::MX_SLOT_OP_MUL)) return r;
    if (const auto *ia = as<MXInteger>(a))
        if (const auto *ib = as<MXInteger>(b)) return ia->mul(*ib).release();
    if (is_num(a) && is_num(b)) return new MXFloat(to_d(a) * to_d(b));
    return type_err("*");
}
MXObject *mxs_op_div(MXObject *a, MXObject *b) {
    if (MXObject *r = user_binop(a, b, mxs::core::MX_SLOT_OP_DIV)) return r;
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
    if (MXObject *r = user_binop(a, b, mxs::core::MX_SLOT_OP_MOD)) return r;
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
    if (MXObject *r = user_binop(a, b, mxs::core::MX_SLOT_OP_POW)) return r;
    if (const auto *ia = as<MXInteger>(a))
        if (const auto *ib = as<MXInteger>(b)) return ia->pow(*ib).release();
    if (is_num(a) && is_num(b)) return new MXFloat(std::pow(to_d(a), to_d(b)));
    return type_err("**");
}
MXObject *mxs_op_neg(MXObject *a) {
    if (MXObject *r = user_unop(a, mxs::core::MX_SLOT_OP_NEG)) return r;
    if (const auto *ia = as<MXInteger>(a)) return ia->neg().release();
    if (const auto *fa = as<MXFloat>(a)) return new MXFloat(-fa->value());
    return type_err("unary -");
}
MXObject *mxs_op_not(MXObject *a) {
    if (MXObject *r = user_unop(a, mxs::core::MX_SLOT_OP_NOT)) return r;
    return new MXBoolean(!(a && a->is_truthy()));
}

MXObject *mxs_op_lt(MXObject *a, MXObject *b) {
    if (MXObject *r = user_binop(a, b, mxs::core::MX_SLOT_OP_LT)) return r;
    const int c = order(a, b);
    return c == 2 ? type_err("<") : new MXBoolean(c < 0);
}
MXObject *mxs_op_le(MXObject *a, MXObject *b) {
    if (MXObject *r = user_binop(a, b, mxs::core::MX_SLOT_OP_LE)) return r;
    const int c = order(a, b);
    return c == 2 ? type_err("<=") : new MXBoolean(c <= 0);
}
MXObject *mxs_op_gt(MXObject *a, MXObject *b) {
    if (MXObject *r = user_binop(a, b, mxs::core::MX_SLOT_OP_GT)) return r;
    const int c = order(a, b);
    return c == 2 ? type_err(">") : new MXBoolean(c > 0);
}
MXObject *mxs_op_ge(MXObject *a, MXObject *b) {
    if (MXObject *r = user_binop(a, b, mxs::core::MX_SLOT_OP_GE)) return r;
    const int c = order(a, b);
    return c == 2 ? type_err(">=") : new MXBoolean(c >= 0);
}
MXObject *mxs_op_eq(MXObject *a, MXObject *b) {
    if (MXObject *r = user_binop(a, b, mxs::core::MX_SLOT_OP_EQ)) return r;
    return new MXBoolean(structurally_equal(a, b));
}
MXObject *mxs_op_ne(MXObject *a, MXObject *b) {
    if (MXObject *r = user_binop(a, b, mxs::core::MX_SLOT_OP_NE)) return r;
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
    // User class instances: match by the instance's class name (its MXClassInfo->name), not the
    // C++ RTTI name (which is the shared "MXInstance"). This is how `case x: Point =>` works.
    if (const auto *inst = as<MXInstance>(o)) return inst->class_name() == t ? 1 : 0;
    return o->get_rtti().name == t ? 1 : 0;// other user/class types: match by RTTI name
}

// Generic length: ArrayList element count or String byte length, as an MXInteger. Used by
// `len(...)` and `for x in xs`. Non-sized objects -> MXError.
MXObject *mxs_len(const MXObject *o) {
    if (const auto *l = as<MXArrayList>(o)) return l->length().release();
    if (const auto *s = as<MXString>(o)) return s->length().release();
    return new MXError("TypeError", "object has no length");
}

// Generic indexing `o[idx]`: ArrayList element (a borrow) or String character (a fresh 1-char
// MXString). Used by subscript and `for x in xs`. Bad index / non-indexable -> MXError.
MXObject *mxs_index_get(MXObject *o, MXObject *idx) {
    const auto *ii = as<MXInteger>(idx);
    if (!ii) return new MXError("TypeError", "index must be an integer");
    bool ok = false;
    const std::int64_t i = ii->to_i64(ok);
    if (const auto *l = as<MXArrayList>(o)) {
        MXObject *e = l->get(i);
        if (!e) return new MXError("IndexError", "list index out of range");
        e->retain();// accessor returns +1 (ARC)
        return e;
    }
    if (const auto *s = as<MXString>(o)) {
        const std::string &v = s->value();
        if (i < 0 || static_cast<std::size_t>(i) >= v.size())
            return new MXError("IndexError", "string index out of range");
        return new MXString(std::string(1, v[static_cast<std::size_t>(i)]));
    }
    return new MXError("TypeError", "object is not indexable");
}

// Member access `obj.name` (read). For an MXError: `msg`/`message` and `type`/`kind`. Other
// objects have no built-in attributes yet (full member/method dispatch waits on OOP) -> nil.
MXObject *mxs_get_attr(const MXObject *o, const char *name) {
    const std::string n = name ? name : "";
    // User class instance: return the named field, retained (+1, the ARC accessor rule). An unset
    // field reads as nil.
    if (const auto *inst = as<MXInstance>(o)) {
        if (MXObject *f = inst->get_field(n)) {
            f->retain();
            return f;
        }
        return new mxs::builtin::MXNil();
    }
    if (const auto *e = as<MXError>(o)) {
        if (n == "msg" || n == "message") return new MXString(e->message());
        if (n == "type" || n == "kind") return new MXString(e->error_type());
    }
    return new mxs::builtin::MXNil();
}

// --- User class instances (progress11 OOP v1) ---

// Construct a fresh instance of the class described by `ci` (rc 1, caller-owned). The constructor
// (emitted by codegen) then binds `self` to this and runs the ctor body (mxs_set_attr per field).
MXObject *mxs_instance_new(const MXClassInfo *ci) { return new MXInstance(ci); }

// Member assignment `obj.name = v` / `self.name = v`. The instance ADOPTS `v` (takes its +1); the
// previous value of the field is released. Codegen does NOT release `v` at the call site (the field
// adopts it). If `o` is not an instance the field never adopts it, so we release `v` here to keep
// the +1 balanced (otherwise it would leak).
void mxs_set_attr(MXObject *o, const char *name, MXObject *v) {
    if (auto *inst = dynamic_cast<MXInstance *>(o)) inst->set_field(name ? name : "", v);
    else if (v)
        v->release();
}

// The instance's class descriptor (for operator routing / dispatch); nullptr for non-instances.
const MXClassInfo *mxs_object_classinfo(const MXObject *o) {
    if (const auto *inst = as<MXInstance>(o)) return inst->classinfo();
    return nullptr;
}

// A method call `recv.name(...)` whose name is a user-class selector but whose receiver is not an
// instance (its classinfo is null). Returns a fresh TypeError value (the match-based error model)
// rather than dereferencing a null vtable. Built-in method names (len/append/get) are routed to
// their runtime symbols by codegen before reaching here; this covers a user-only method name
// invoked on a non-instance receiver. The receiver is borrowed (codegen releases it).
MXObject *mxs_method_missing(const MXObject *recv, const char *name) {
    (void) recv;
    return new MXError("TypeError",
                       std::string("object has no method '") + (name ? name : "") + "'");
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
