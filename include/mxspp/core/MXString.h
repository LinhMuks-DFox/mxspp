#pragma once
#include "mxspp/core/MXMacro.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/_type_def.h"

#include <string>

namespace mxs::builtin {

    // MXString — MXScript's string type (docs §3.1): a real core::MXObject holding a UTF-8
    // byte sequence (std::string). Strings are immutable values; every mutating-looking
    // operation returns a fresh MXString. Two-layer API (C++ + extern "C"), per progress09.
    class MXS_API MXString : public core::MXObject {
    public:
        explicit MXString(std::string value = {}, bool is_static = false);

        // Construct from a literal's already-unquoted contents (parser strips the quotes).
        static auto from_literal(const std::string &contents) -> MXObjectOwned;

        [[nodiscard]] auto concat(const MXString &other) const -> MXObjectOwned;
        // Byte length as an MXInteger (UTF-8 byte count; codepoint length is a later method).
        [[nodiscard]] auto length() const -> MXObjectOwned;
        [[nodiscard]] auto cmp(const MXString &other) const
                -> int;// -1 / 0 / 1, lexicographic

        [[nodiscard]] auto value() const -> const std::string & { return value_; }
        [[nodiscard]] auto empty() const -> bool { return value_.empty(); }

        // --- MXObject overrides ---
        [[nodiscard]] auto repr() const
                -> core::repr_t override;// the raw bytes (for print)
        [[nodiscard]] auto is_truthy() const -> bool override { return !value_.empty(); }
        auto equals(MXObjectConstBorrow other) -> bool override;
        [[nodiscard]] auto get_hash_code() const -> MXHashCode_t override;

        // --- RTTI ---
        static auto get_rtti() -> const core::MXRuntimeTypeInfo &;

    private:
        std::string value_;
    };

}// namespace mxs::builtin
