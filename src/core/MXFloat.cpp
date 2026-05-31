#include "mxspp/core/MXFloat.h"
#include "mxspp/core/MXError.h"

#include <cstdint>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>

namespace mxs::builtin {

    MXFloat::MXFloat(double value, bool is_static)
        : core::MXObject(is_static), value_(value) { }

    auto MXFloat::from_literal(const std::string &text) -> MXObjectOwned {
        try {
            std::size_t consumed = 0;
            const double v = std::stod(text, &consumed);
            if (consumed != text.size())
                return std::make_unique<core::MXError>(
                        "ValueError", "invalid float literal: '" + text + "'");
            return std::make_unique<MXFloat>(v);
        } catch (const std::exception &) {
            return std::make_unique<core::MXError>(
                    "ValueError", "invalid float literal: '" + text + "'");
        }
    }

    auto MXFloat::add(const MXFloat &o) const -> MXObjectOwned {
        return std::make_unique<MXFloat>(value_ + o.value_);
    }
    auto MXFloat::sub(const MXFloat &o) const -> MXObjectOwned {
        return std::make_unique<MXFloat>(value_ - o.value_);
    }
    auto MXFloat::mul(const MXFloat &o) const -> MXObjectOwned {
        return std::make_unique<MXFloat>(value_ * o.value_);
    }
    auto MXFloat::div(const MXFloat &o) const -> MXObjectOwned {
        if (o.value_ == 0.0)
            return std::make_unique<core::MXError>("ZeroDivisionError",
                                                   "float division by zero");
        return std::make_unique<MXFloat>(value_ / o.value_);
    }
    auto MXFloat::neg() const -> MXObjectOwned {
        return std::make_unique<MXFloat>(-value_);
    }
    auto MXFloat::cmp(const MXFloat &o) const -> int {
        if (value_ < o.value_) return -1;
        if (value_ > o.value_) return 1;
        if (value_ == o.value_) return 0;
        return 2;// unordered (NaN involved)
    }

    auto MXFloat::repr() const -> core::repr_t { return std::format("{}", value_); }

    auto MXFloat::equals(MXObjectConstBorrow other) -> bool {
        const auto *o = dynamic_cast<const MXFloat *>(other.get());
        return o && value_ == o->value_;
    }

    auto MXFloat::get_hash_code() const -> MXHashCode_t {
        MXHashCode_t h = 0;
        static_assert(sizeof(h) == sizeof(value_));
        std::memcpy(&h, &value_, sizeof(h));
        return h;
    }

    auto MXFloat::get_rtti() -> const core::MXRuntimeTypeInfo & {
        static core::MXRuntimeTypeInfo instance{ "MXFloat", &core::MXObject::get_rtti() };
        return instance;
    }

}// namespace mxs::builtin

// ============================================================================================
// extern "C" ABI — JIT-facing, ABI-stable surface (progress09 D3).
// ============================================================================================
namespace {
    using mxs::builtin::MXFloat;
    using mxs::core::MXObject;

    const MXFloat *as_float(const MXObject *o) {
        return dynamic_cast<const MXFloat *>(o);
    }
}// namespace

extern "C" {

MXObject *mxs_float_new(double v) { return new MXFloat(v); }
MXObject *mxs_float_from_literal(const char *s) {
    return MXFloat::from_literal(s ? s : "").release();
}

#define MX_FLOAT_BINOP(cname, method)                                                    \
    MXObject *cname(MXObject *a, MXObject *b) {                                          \
        const MXFloat *fa = as_float(a), *fb = as_float(b);                              \
        if (!fa || !fb)                                                                  \
            return new mxs::core::MXError("TypeError", "expected MXFloat operands");     \
        return fa->method(*fb).release();                                                \
    }

MX_FLOAT_BINOP(mxs_float_add, add)
MX_FLOAT_BINOP(mxs_float_sub, sub)
MX_FLOAT_BINOP(mxs_float_mul, mul)
MX_FLOAT_BINOP(mxs_float_div, div)
#undef MX_FLOAT_BINOP

MXObject *mxs_float_neg(MXObject *a) {
    const MXFloat *fa = as_float(a);
    if (!fa) return new mxs::core::MXError("TypeError", "expected an MXFloat operand");
    return fa->neg().release();
}
std::int64_t mxs_float_cmp(MXObject *a, MXObject *b) {
    const MXFloat *fa = as_float(a), *fb = as_float(b);
    return (fa && fb) ? fa->cmp(*fb) : 2;
}
double mxs_float_value(MXObject *o) {
    const MXFloat *f = as_float(o);
    return f ? f->value() : 0.0;
}

}// extern "C"
