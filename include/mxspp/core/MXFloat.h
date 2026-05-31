#pragma once
#include "mxspp/core/MXMacro.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/_type_def.h"

#include <string>

namespace mxs::builtin {

    // MXFloat — MXScript's floating-point type (docs §3.2): a real core::MXObject wrapping an
    // IEEE-754 double. Arithmetic returns a fresh MXFloat; division by zero returns an MXError
    // (consistent with MXInteger and the numerical-error direction, progress06). Two-layer API
    // (C++ + extern "C"), per progress09.
    class MXS_API MXFloat : public core::MXObject {
    public:
        explicit MXFloat(double value = 0.0, bool is_static = false);

        static auto from_literal(const std::string &text) -> MXObjectOwned;

        [[nodiscard]] auto add(const MXFloat &other) const -> MXObjectOwned;
        [[nodiscard]] auto sub(const MXFloat &other) const -> MXObjectOwned;
        [[nodiscard]] auto mul(const MXFloat &other) const -> MXObjectOwned;
        [[nodiscard]] auto div(const MXFloat &other) const
                -> MXObjectOwned;// /0 -> MXError
        [[nodiscard]] auto neg() const -> MXObjectOwned;
        // -1 / 0 / 1 when ordered; 2 when unordered (a NaN is involved).
        [[nodiscard]] auto cmp(const MXFloat &other) const -> int;

        [[nodiscard]] auto value() const -> double { return value_; }

        // --- MXObject overrides ---
        [[nodiscard]] auto repr() const -> core::repr_t override;
        [[nodiscard]] auto is_truthy() const -> bool override { return value_ != 0.0; }
        auto equals(MXObjectConstBorrow other) -> bool override;
        [[nodiscard]] auto get_hash_code() const -> MXHashCode_t override;

        // --- RTTI ---
        static auto get_rtti() -> const core::MXRuntimeTypeInfo &;

    private:
        double value_ = 0.0;
    };

}// namespace mxs::builtin
