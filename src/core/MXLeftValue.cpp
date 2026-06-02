#include "mxspp/core/MXLeftValue.h"
#include "mxspp/core/MXError.h"
#include "mxspp/core/MXNil.h"
#include "mxspp/core/MXObject.h"

#include <cstdint>
#include <utility>

namespace mxs::core {

    MXLeftValue::MXLeftValue(MXObjectOwned value, bool is_mutable)
        : value_(value.release()), mutable_(is_mutable) { }// adopt the +1

    MXLeftValue::~MXLeftValue() {
        if (value_) value_->release();
    }

    auto MXLeftValue::rvalue() const -> MXObject * { return value_; }// a borrow

    auto MXLeftValue::rvalue_update(MXObjectOwned newval) -> MXObjectOwned {
        MXObject *nv = newval.release();// adopt the new value's +1
        if (!mutable_) {
            if (nv) nv->release();// reject: drop the reference we just took
            return std::make_unique<MXError>(
                    "ImmutableError", "cannot assign to an immutable (let) binding");
        }
        if (value_) value_->release();// drop the old value
        value_ = nv;
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
// Returns the held r-value RETAINED (+1) — the ARC accessor rule (progress11): a value read out
// of a binding is owned by the reader, who releases it when done.
MXObject *mxs_lvalue_rvalue(MXLeftValue *lv) {
    if (!lv) return nullptr;
    MXObject *r = lv->rvalue();
    if (r) r->retain();
    return r;
}
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
