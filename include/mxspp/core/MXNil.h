#pragma once
#include "mxspp/core/MXMacro.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/_type_def.h"

namespace mxs::builtin {

    // MXNil — MXScript's nil/unit type (docs §3.1): a real core::MXObject with no payload.
    // All nils are equal. Two-layer API (C++ + extern "C"), per progress09.
    class MXS_API MXNil : public core::MXObject {
    public:
        explicit MXNil(bool is_static = false);

        // --- MXObject overrides ---
        [[nodiscard]] auto repr() const -> core::repr_t override;// "nil"
        [[nodiscard]] auto is_truthy() const -> bool override { return false; }
        auto equals(MXObjectConstBorrow other) -> bool override;
        [[nodiscard]] auto get_hash_code() const -> MXHashCode_t override;

        // --- RTTI ---
        static auto get_rtti() -> const core::MXRuntimeTypeInfo &;
    };

}// namespace mxs::builtin
