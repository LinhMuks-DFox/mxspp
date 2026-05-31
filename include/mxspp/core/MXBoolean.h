#pragma once
#include "mxspp/core/MXMacro.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/_type_def.h"

namespace mxs::builtin {

    // MXBoolean — MXScript's boolean type (docs §3.1): a real core::MXObject wrapping a bool.
    // Two-layer API (C++ + extern "C"), per progress09.
    class MXS_API MXBoolean : public core::MXObject {
    public:
        explicit MXBoolean(bool value = false, bool is_static = false);

        [[nodiscard]] auto logical_not() const -> MXObjectOwned;
        [[nodiscard]] auto value() const -> bool { return value_; }

        // --- MXObject overrides ---
        [[nodiscard]] auto repr() const -> core::repr_t override;// "true" / "false"
        [[nodiscard]] auto is_truthy() const -> bool override { return value_; }
        auto equals(MXObjectConstBorrow other) -> bool override;
        [[nodiscard]] auto get_hash_code() const -> MXHashCode_t override;

        // --- RTTI ---
        static auto get_rtti() -> const core::MXRuntimeTypeInfo &;

    private:
        bool value_ = false;
    };

}// namespace mxs::builtin
