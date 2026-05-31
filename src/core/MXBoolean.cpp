#include "mxspp/core/MXBoolean.h"

#include <cstdint>

namespace mxs::builtin {

    MXBoolean::MXBoolean(bool value, bool is_static)
        : core::MXObject(is_static), value_(value) { }

    auto MXBoolean::logical_not() const -> MXObjectOwned {
        return std::make_unique<MXBoolean>(!value_);
    }

    auto MXBoolean::repr() const -> core::repr_t { return value_ ? "true" : "false"; }

    auto MXBoolean::equals(MXObjectConstBorrow other) -> bool {
        const auto *o = dynamic_cast<const MXBoolean *>(other.get());
        return o && value_ == o->value_;
    }

    auto MXBoolean::get_hash_code() const -> MXHashCode_t { return value_ ? 1 : 0; }

    auto MXBoolean::get_rtti() -> const core::MXRuntimeTypeInfo & {
        static core::MXRuntimeTypeInfo instance{ "MXBoolean",
                                                 &core::MXObject::get_rtti() };
        return instance;
    }

}// namespace mxs::builtin

// ============================================================================================
// extern "C" ABI — JIT-facing, ABI-stable surface (progress09 D3).
// ============================================================================================
namespace {
    using mxs::builtin::MXBoolean;
    using mxs::core::MXObject;
}// namespace

extern "C" {

MXObject *mxs_bool_new(std::int64_t v) { return new MXBoolean(v != 0); }

// Truthy value as 0/1 (the boolean's own value).
std::int64_t mxs_bool_value(MXObject *o) {
    const auto *b = dynamic_cast<const MXBoolean *>(o);
    return b && b->value() ? 1 : 0;
}

MXObject *mxs_bool_not(MXObject *o) {
    const auto *b = dynamic_cast<const MXBoolean *>(o);
    return new MXBoolean(!(b && b->value()));
}

}// extern "C"
