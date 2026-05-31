#pragma once
#include "mxspp/core/MXMacro.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/_type_def.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mxs::builtin {

    // MXInteger — MXScript's integer type (docs §3.2, progress09 D4): a real C++ object
    // inheriting core::MXObject, with native arbitrary-precision ("big number") arithmetic.
    //
    // Representation: sign-magnitude. `negative_` is the sign; `mag_` holds the magnitude as
    // little-endian base-2^64 limbs with no trailing zero limbs (so zero == empty `mag_`,
    // `negative_ == false`). Fixed-width integers (int8..int64, uint8..uint64) are not stored
    // distinctly — they are simply small magnitudes; `int_type()` classifies the current value
    // into the smallest representation that fits, or "UltraInteger" when it exceeds 64 bits.
    //
    // Arithmetic methods do not mutate the operands; each returns a freshly-owned result
    // (an MXInteger, or an MXError for e.g. division by zero — forward-compatible with the
    // match-based error model, progress06).
    class MXS_API MXInteger : public core::MXObject {
    public:
        explicit MXInteger(std::int64_t value = 0, bool is_static = false);
        MXInteger(bool negative, std::vector<std::uint64_t> magnitude,
                  bool is_static = false);

        // Parse a base-10 literal, optionally signed (`+`/`-`). Returns an MXError on bad input.
        static auto from_literal(const std::string &text) -> MXObjectOwned;

        // Arithmetic (operands unchanged). div is truncating; mod takes the dividend's sign.
        // div / mod by zero return an MXError. pow requires a non-negative exponent.
        [[nodiscard]] auto add(const MXInteger &other) const -> MXObjectOwned;
        [[nodiscard]] auto sub(const MXInteger &other) const -> MXObjectOwned;
        [[nodiscard]] auto mul(const MXInteger &other) const -> MXObjectOwned;
        [[nodiscard]] auto div(const MXInteger &other) const -> MXObjectOwned;
        [[nodiscard]] auto mod(const MXInteger &other) const -> MXObjectOwned;
        [[nodiscard]] auto pow(const MXInteger &exponent) const -> MXObjectOwned;
        [[nodiscard]] auto neg() const -> MXObjectOwned;
        [[nodiscard]] auto cmp(const MXInteger &other) const -> int;// -1 / 0 / 1

        [[nodiscard]] auto is_zero() const -> bool { return mag_.empty(); }
        [[nodiscard]] auto is_negative() const -> bool { return negative_; }

        // The smallest fixed-width type the current value fits in: "int8"/"uint8"/.../"int64"/
        // "uint64", or "UltraInteger" if it exceeds 64 bits.
        [[nodiscard]] auto int_type() const -> std::string;
        // sizeof(MXInteger) + the heap bytes backing the magnitude limbs.
        [[nodiscard]] auto int_size() const -> std::size_t;

        // Decimal text of the value (no sign for zero). Also used by repr().
        [[nodiscard]] auto to_decimal() const -> std::string;
        // Value as int64 when it fits (ok=true); otherwise ok=false and the result is unspecified.
        [[nodiscard]] auto to_i64(bool &ok) const -> std::int64_t;

        // --- MXObject overrides ---
        [[nodiscard]] auto repr() const -> core::repr_t override;
        auto equals(MXObjectConstBorrow other) -> bool override;
        [[nodiscard]] auto get_hash_code() const -> MXHashCode_t override;

        // --- RTTI ---
        static auto get_rtti() -> const core::MXRuntimeTypeInfo &;

    private:
        bool negative_ = false;
        std::vector<std::uint64_t> mag_;// little-endian limbs, no trailing zeros
        void normalize();
    };

}// namespace mxs::builtin
