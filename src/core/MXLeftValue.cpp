#include "mxspp/core/MXLeftValue.h"
#include "mxspp/core/MXError.h"
#include "mxspp/core/MXNil.h"
#include "mxspp/core/MXObject.h"

#include <cstdint>
#include <utility>

namespace mxs::core {

    MXLeftValue::MXLeftValue(MXObjectOwned value, bool is_mutable)
        : value_(std::move(value)), mutable_(is_mutable) { }

    MXLeftValue::~MXLeftValue() = default;

    auto MXLeftValue::rvalue() const -> MXObject * { return value_.get(); }

    auto MXLeftValue::rvalue_update(MXObjectOwned newval) -> MXObjectOwned {
        if (!mutable_)
            return std::make_unique<MXError>(
                    "ImmutableError", "cannot assign to an immutable (let) binding");
        value_ = std::move(newval);
        return std::make_unique<builtin::MXNil>();
    }

}// namespace mxs::core

namespace mxs {
    auto make_immutable_left_value(MXObjectOwned value)
            -> std::unique_ptr<core::MXLeftValue> {
        return std::make_unique<core::MXLeftValue>(std::move(value),
                                                   /*is_mutable=*/false);
    }
    auto make_mutable_left_value(MXObjectOwned value)
            -> std::unique_ptr<core::MXLeftValue> {
        return std::make_unique<core::MXLeftValue>(std::move(value), /*is_mutable=*/true);
    }
}// namespace mxs

// ============================================================================================
// extern "C" ABI — JIT-facing, ABI-stable surface (progress09 D3). A binding cell takes
// ownership of the r-value pointer handed to it; mxs_lvalue_update frees the old value.
// ============================================================================================
namespace {
    using mxs::MXObjectOwned;
    using mxs::core::MXLeftValue;
    using mxs::core::MXObject;
}// namespace

extern "C" {

MXLeftValue *mxs_lvalue_new(MXObject *value, std::int64_t is_mutable) {
    return new MXLeftValue(MXObjectOwned(value), is_mutable != 0);
}
MXObject *mxs_lvalue_rvalue(MXLeftValue *lv) { return lv ? lv->rvalue() : nullptr; }
std::int64_t mxs_lvalue_is_mutable(MXLeftValue *lv) {
    return lv && lv->is_mutable() ? 1 : 0;
}
// Returns MXNil on success, MXError if the binding is immutable.
MXObject *mxs_lvalue_update(MXLeftValue *lv, MXObject *newval) {
    if (!lv) return nullptr;
    return lv->rvalue_update(MXObjectOwned(newval)).release();
}
void mxs_lvalue_delete(MXLeftValue *lv) { delete lv; }

}// extern "C"
