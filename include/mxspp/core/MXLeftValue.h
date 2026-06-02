#pragma once
#include "mxspp/core/MXMacro.h"
#include "mxspp/core/_type_def.h"

#include <memory>

namespace mxs::core {
    class MXObject;

    // MXLeftValue — a binding cell (progress09 D2). A left-value owns the current r-value (an
    // MXObject) and records mutability: `let x = 3` is an *immutable* binding, `let mut x = 3`
    // a mutable one. The r-value is what operators act on; the left-value is what an identifier
    // resolves to. Updating an immutable binding is an error (returned as an MXError, so it
    // composes with the match-based error model, progress06).
    //
    // Ownership (develop_rule.md default, per progress09 open Q): the binding *owns* its current
    // r-value via unique_ptr; rvalue_update frees the old value and takes ownership of the new.
    class MXS_API MXLeftValue {
    public:
        MXLeftValue(MXObjectOwned value, bool is_mutable);
        ~MXLeftValue();

        [[nodiscard]] auto rvalue() const -> MXObject *;// borrow the held value
        [[nodiscard]] auto is_mutable() const -> bool { return mutable_; }

        // Replace the held r-value. On an immutable binding: returns an MXError and leaves the
        // value unchanged. On success: returns an MXNil. (result-or-error, match-compatible.)
        auto rvalue_update(MXObjectOwned newval) -> MXObjectOwned;

    private:
        // The held r-value as a strong reference (ARC, progress11): the cell adopts the +1 of the
        // value handed to it and releases it on update / destruction.
        MXObject *value_;
        bool mutable_;
    };
}// namespace mxs::core

namespace mxs {
    // Factories mirroring progress09 D2's worked example (`mxs::make_immutable_left_value(...)`).
    auto make_immutable_left_value(MXObjectOwned value)
            -> std::unique_ptr<core::MXLeftValue>;
    auto make_mutable_left_value(MXObjectOwned value)
            -> std::unique_ptr<core::MXLeftValue>;
}// namespace mxs
