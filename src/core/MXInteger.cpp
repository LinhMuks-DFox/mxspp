#include "mxspp/core/MXInteger.h"
#include "mxspp/core/MXBoolean.h"
#include "mxspp/core/MXError.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// MXInteger — arbitrary-precision signed integer. The magnitude is a little-endian vector of
// 64-bit limbs (base 2^64); the bignum kernels below are schoolbook add/sub/mul plus a
// bit-at-a-time long division (correct and simple; tuned later if needed). All kernels keep
// magnitudes normalized (no trailing zero limbs). 128-bit intermediates use the compiler's
// __int128 (clang/libc++).

namespace mxs::builtin {

    namespace {
        using limb = std::uint64_t;
        using Mag = std::vector<limb>;
        using u128 = unsigned __int128;

        void trim(Mag &m) {
            while (!m.empty() && m.back() == 0) m.pop_back();
        }

        int mag_cmp(const Mag &a, const Mag &b) {
            if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
            for (std::size_t i = a.size(); i-- > 0;)
                if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
            return 0;
        }

        Mag mag_add(const Mag &a, const Mag &b) {
            Mag r;
            const std::size_t n = a.size() > b.size() ? a.size() : b.size();
            r.reserve(n + 1);
            u128 carry = 0;
            for (std::size_t i = 0; i < n; ++i) {
                u128 s = carry;
                if (i < a.size()) s += a[i];
                if (i < b.size()) s += b[i];
                r.push_back(static_cast<limb>(s));
                carry = s >> 64;
            }
            if (carry) r.push_back(static_cast<limb>(carry));
            trim(r);
            return r;
        }

        // Requires a >= b (by magnitude).
        Mag mag_sub(const Mag &a, const Mag &b) {
            Mag r;
            r.reserve(a.size());
            u128 borrow = 0;
            for (std::size_t i = 0; i < a.size(); ++i) {
                u128 ai = a[i];
                u128 bi = (i < b.size() ? static_cast<u128>(b[i]) : 0) + borrow;
                if (ai >= bi) {
                    r.push_back(static_cast<limb>(ai - bi));
                    borrow = 0;
                } else {
                    r.push_back(
                            static_cast<limb>((static_cast<u128>(1) << 64) + ai - bi));
                    borrow = 1;
                }
            }
            trim(r);
            return r;
        }

        Mag mag_mul(const Mag &a, const Mag &b) {
            if (a.empty() || b.empty()) return {};
            Mag r(a.size() + b.size(), 0);
            for (std::size_t i = 0; i < a.size(); ++i) {
                u128 carry = 0;
                for (std::size_t j = 0; j < b.size(); ++j) {
                    u128 cur = static_cast<u128>(a[i]) * b[j] + r[i + j] + carry;
                    r[i + j] = static_cast<limb>(cur);
                    carry = cur >> 64;
                }
                r[i + b.size()] = static_cast<limb>(r[i + b.size()] + carry);
            }
            trim(r);
            return r;
        }

        Mag mag_shl1(const Mag &m) {
            Mag r(m.size(), 0);
            limb carry = 0;
            for (std::size_t i = 0; i < m.size(); ++i) {
                r[i] = (m[i] << 1) | carry;
                carry = m[i] >> 63;
            }
            if (carry) r.push_back(carry);
            return r;
        }

        bool mag_bit(const Mag &m, std::size_t idx) {
            const std::size_t limbIdx = idx >> 6, bit = idx & 63;
            return limbIdx < m.size() && ((m[limbIdx] >> bit) & 1ULL);
        }
        std::size_t mag_bitlen(const Mag &m) {
            if (m.empty()) return 0;
            const std::size_t hi = m.size() - 1;
            limb top = m[hi];
            std::size_t bits = 0;
            while (top) {
                ++bits;
                top >>= 1;
            }
            return hi * 64 + bits;
        }
        void mag_setbit(Mag &m, std::size_t idx) {
            const std::size_t limbIdx = idx >> 6, bit = idx & 63;
            if (limbIdx >= m.size()) m.resize(limbIdx + 1, 0);
            m[limbIdx] |= (1ULL << bit);
        }

        // Bit-at-a-time long division. Requires b non-empty (non-zero). Returns {quotient, rem}.
        std::pair<Mag, Mag> mag_divmod(const Mag &a, const Mag &b) {
            if (mag_cmp(a, b) < 0) return { Mag{}, a };
            Mag q, r;
            for (std::size_t i = mag_bitlen(a); i-- > 0;) {
                r = mag_shl1(r);
                if (mag_bit(a, i)) {
                    if (r.empty()) r.push_back(1);
                    else
                        r[0] |= 1ULL;
                }
                if (mag_cmp(r, b) >= 0) {
                    r = mag_sub(r, b);
                    mag_setbit(q, i);
                }
            }
            trim(q);
            trim(r);
            return { q, r };
        }

        // Divide by a single limb; returns {quotient, remainder}. Used for decimal output.
        std::pair<Mag, limb> divmod_small(const Mag &a, limb d) {
            Mag q(a.size(), 0);
            u128 rem = 0;
            for (std::size_t i = a.size(); i-- > 0;) {
                u128 cur = (rem << 64) | a[i];
                q[i] = static_cast<limb>(cur / d);
                rem = cur % d;
            }
            trim(q);
            return { q, static_cast<limb>(rem) };
        }

        Mag mag_pow2(std::size_t k) {
            Mag m(k / 64 + 1, 0);
            m[k / 64] = 1ULL << (k % 64);
            return m;
        }

        Mag mag_from_u64(std::uint64_t v) {
            Mag m;
            if (v) m.push_back(v);
            return m;
        }

        std::string mag_to_decimal(Mag m) {
            if (m.empty()) return "0";
            constexpr limb CHUNK = 1000000000000000000ULL;// 10^18 (18 digits)
            std::vector<limb> chunks;
            while (!m.empty()) {
                auto [q, rem] = divmod_small(m, CHUNK);
                chunks.push_back(rem);
                m = std::move(q);
            }
            std::string s = std::to_string(chunks.back());
            for (std::size_t i = chunks.size() - 1; i-- > 0;) {
                std::string part = std::to_string(chunks[i]);
                s += std::string(18 - part.size(), '0') + part;
            }
            return s;
        }

        MXObjectOwned make_int(bool negative, Mag mag) {
            return std::make_unique<MXInteger>(negative, std::move(mag));
        }
        MXObjectOwned zero_div_error() {
            return std::make_unique<core::MXError>("ZeroDivisionError",
                                                   "integer division or modulo by zero");
        }
    }// namespace

    // ---- construction ----------------------------------------------------------------------

    MXInteger::MXInteger(std::int64_t value, bool is_static)
        : core::MXObject(is_static), negative_(value < 0) {
        // -INT64_MIN overflows int64; widen through unsigned to take the magnitude safely.
        std::uint64_t m = value < 0 ? (~static_cast<std::uint64_t>(value) + 1ULL)
                                    : static_cast<std::uint64_t>(value);
        if (m) mag_.push_back(m);
    }

    MXInteger::MXInteger(bool negative, std::vector<std::uint64_t> magnitude,
                         bool is_static)
        : core::MXObject(is_static), negative_(negative), mag_(std::move(magnitude)) {
        normalize();
    }

    void MXInteger::normalize() {
        while (!mag_.empty() && mag_.back() == 0) mag_.pop_back();
        if (mag_.empty()) negative_ = false;// canonical zero is non-negative
    }

    auto MXInteger::from_literal(const std::string &text) -> MXObjectOwned {
        std::size_t i = 0;
        bool neg = false;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            neg = text[i] == '-';
            ++i;
        }
        if (i >= text.size())
            return std::make_unique<core::MXError>(
                    "ValueError", "invalid integer literal: '" + text + "'");
        Mag mag;// build via Horner: mag = mag*10 + digit
        const Mag ten = mag_from_u64(10);
        for (; i < text.size(); ++i) {
            const char c = text[i];
            if (c < '0' || c > '9')
                return std::make_unique<core::MXError>(
                        "ValueError", "invalid integer literal: '" + text + "'");
            mag = mag_add(mag_mul(mag, ten), mag_from_u64(static_cast<limb>(c - '0')));
        }
        return make_int(neg, std::move(mag));
    }

    // ---- arithmetic ------------------------------------------------------------------------

    auto MXInteger::add(const MXInteger &o) const -> MXObjectOwned {
        if (negative_ == o.negative_) return make_int(negative_, mag_add(mag_, o.mag_));
        const int c = mag_cmp(mag_, o.mag_);
        if (c == 0) return make_int(false, Mag{});
        if (c > 0) return make_int(negative_, mag_sub(mag_, o.mag_));
        return make_int(o.negative_, mag_sub(o.mag_, mag_));
    }

    auto MXInteger::sub(const MXInteger &o) const -> MXObjectOwned {
        MXInteger negated(!o.negative_, o.mag_);
        return add(negated);
    }

    auto MXInteger::mul(const MXInteger &o) const -> MXObjectOwned {
        return make_int(negative_ != o.negative_, mag_mul(mag_, o.mag_));
    }

    auto MXInteger::div(const MXInteger &o) const -> MXObjectOwned {
        if (o.is_zero()) return zero_div_error();
        auto [q, r] = mag_divmod(mag_, o.mag_);
        return make_int(negative_ != o.negative_, std::move(q));// truncates toward zero
    }

    auto MXInteger::mod(const MXInteger &o) const -> MXObjectOwned {
        if (o.is_zero()) return zero_div_error();
        auto [q, r] = mag_divmod(mag_, o.mag_);
        return make_int(negative_, std::move(r));// remainder takes the dividend's sign
    }

    auto MXInteger::pow(const MXInteger &exponent) const -> MXObjectOwned {
        if (exponent.negative_)
            return std::make_unique<core::MXError>(
                    "ValueError", "negative exponent is not valid for integer pow");
        // Square-and-multiply over the exponent's bits.
        Mag result = mag_from_u64(1);
        Mag base = mag_;
        const std::size_t bits = mag_bitlen(exponent.mag_);
        for (std::size_t i = 0; i < bits; ++i) {
            if (mag_bit(exponent.mag_, i)) result = mag_mul(result, base);
            if (i + 1 < bits) base = mag_mul(base, base);
        }
        // Result is negative iff the base is negative and the exponent is odd.
        const bool resultNeg = negative_ && mag_bit(exponent.mag_, 0);
        return make_int(resultNeg, std::move(result));
    }

    auto MXInteger::neg() const -> MXObjectOwned { return make_int(!negative_, mag_); }

    auto MXInteger::cmp(const MXInteger &o) const -> int {
        if (negative_ != o.negative_) return negative_ ? -1 : 1;
        const int c = mag_cmp(mag_, o.mag_);
        return negative_ ? -c : c;// when both negative, larger magnitude is smaller
    }

    // ---- classification / conversion -------------------------------------------------------

    auto MXInteger::int_type() const -> std::string {
        // Smallest fitting fixed-width type, else UltraInteger. Signed intN range is
        // [-2^(N-1), 2^(N-1)-1]; unsigned uintN is [0, 2^N - 1].
        auto fits_signed = [&](std::size_t bits) {
            const int c = mag_cmp(mag_, mag_pow2(bits - 1));
            return negative_ ? c <= 0 : c < 0;
        };
        auto fits_unsigned = [&](std::size_t bits) {
            return !negative_ && mag_cmp(mag_, mag_pow2(bits)) < 0;
        };
        if (fits_signed(8)) return "int8";
        if (fits_unsigned(8)) return "uint8";
        if (fits_signed(16)) return "int16";
        if (fits_unsigned(16)) return "uint16";
        if (fits_signed(32)) return "int32";
        if (fits_unsigned(32)) return "uint32";
        if (fits_signed(64)) return "int64";
        if (fits_unsigned(64)) return "uint64";
        return "UltraInteger";
    }

    auto MXInteger::int_size() const -> std::size_t {
        return sizeof(MXInteger) + mag_.capacity() * sizeof(std::uint64_t);
    }

    auto MXInteger::to_decimal() const -> std::string {
        const std::string digits = mag_to_decimal(mag_);
        return negative_ ? "-" + digits : digits;
    }

    auto MXInteger::to_i64(bool &ok) const -> std::int64_t {
        if (mag_.empty()) {
            ok = true;
            return 0;
        }
        if (mag_.size() > 1) {
            ok = false;
            return 0;
        }
        const limb m = mag_[0];
        if (negative_) {
            if (m > (static_cast<std::uint64_t>(1) << 63)) {
                ok = false;
                return 0;
            }
            ok = true;
            return static_cast<std::int64_t>(~m + 1ULL);
        }
        if (m > static_cast<std::uint64_t>(INT64_MAX)) {
            ok = false;
            return 0;
        }
        ok = true;
        return static_cast<std::int64_t>(m);
    }

    // ---- MXObject overrides ----------------------------------------------------------------

    auto MXInteger::repr() const -> core::repr_t { return to_decimal(); }

    auto MXInteger::equals(MXObjectConstBorrow other) -> bool {
        const auto *o = dynamic_cast<const MXInteger *>(other.get());
        return o && negative_ == o->negative_ && mag_ == o->mag_;
    }

    auto MXInteger::get_hash_code() const -> MXHashCode_t {
        MXHashCode_t h = negative_ ? 1469598103934665603ULL : 1099511628211ULL;
        for (const limb l : mag_) h = (h ^ l) * 1099511628211ULL;
        return h;
    }

    auto MXInteger::get_rtti() -> const core::MXRuntimeTypeInfo & {
        static core::MXRuntimeTypeInfo instance{ "MXInteger",
                                                 &core::MXObject::get_rtti() };
        return instance;
    }

}// namespace mxs::builtin

// ============================================================================================
// extern "C" ABI — the JIT-facing, ABI-stable surface (progress09 D3). Codegen emits direct
// calls to these; they construct/operate on real MXInteger objects. Results are owning raw
// pointers (the future left-value/ownership layer manages lifetime; see progress09 open Qs).
// ============================================================================================
namespace {
    using mxs::builtin::MXInteger;
    using mxs::core::MXObject;

    const MXInteger *as_int(const MXObject *o) {
        return dynamic_cast<const MXInteger *>(o);
    }
}// namespace

extern "C" {

MXObject *mxs_int_from_i64(std::int64_t v) { return new MXInteger(v); }
MXObject *mxs_int_from_literal(const char *s) {
    return MXInteger::from_literal(s ? s : "").release();
}

#define MX_INT_BINOP(cname, method)                                                      \
    MXObject *cname(MXObject *a, MXObject *b) {                                          \
        const MXInteger *ia = as_int(a), *ib = as_int(b);                                \
        if (!ia || !ib)                                                                  \
            return new mxs::core::MXError("TypeError", "expected MXInteger operands");   \
        return ia->method(*ib).release();                                                \
    }

MX_INT_BINOP(mxs_int_add, add)
MX_INT_BINOP(mxs_int_sub, sub)
MX_INT_BINOP(mxs_int_mul, mul)
MX_INT_BINOP(mxs_int_div, div)
MX_INT_BINOP(mxs_int_mod, mod)
MX_INT_BINOP(mxs_int_pow, pow)
#undef MX_INT_BINOP

MXObject *mxs_int_neg(MXObject *a) {
    const MXInteger *ia = as_int(a);
    if (!ia) return new mxs::core::MXError("TypeError", "expected an MXInteger operand");
    return ia->neg().release();
}

std::int64_t mxs_int_cmp(MXObject *a, MXObject *b) {
    const MXInteger *ia = as_int(a), *ib = as_int(b);
    return (ia && ib) ? ia->cmp(*ib) : 0;
}

// Value as a host i64 (for main's return / loop bounds); 0 if it doesn't fit.
std::int64_t mxs_int_to_i64(MXObject *o) {
    const MXInteger *i = as_int(o);
    bool ok = false;
    return i ? i->to_i64(ok) : 0;
}

// Comparisons — each returns a boxed MXBoolean (the result-object the rest of the model expects).
#define MX_INT_CMP(cname, expr)                                                          \
    MXObject *cname(MXObject *a, MXObject *b) {                                          \
        const MXInteger *ia = as_int(a), *ib = as_int(b);                                \
        if (!ia || !ib)                                                                  \
            return new mxs::core::MXError("TypeError", "expected MXInteger operands");   \
        const int c = ia->cmp(*ib);                                                      \
        return new mxs::builtin::MXBoolean(expr);                                        \
    }
MX_INT_CMP(mxs_int_lt, c < 0)
MX_INT_CMP(mxs_int_le, c <= 0)
MX_INT_CMP(mxs_int_gt, c > 0)
MX_INT_CMP(mxs_int_ge, c >= 0)
MX_INT_CMP(mxs_int_eq, c == 0)
MX_INT_CMP(mxs_int_ne, c != 0)
#undef MX_INT_CMP

// Underlying representation name ("int8".."uint64"/"UltraInteger"). Returns a stable literal.
const char *mxs_int_type(MXObject *o) {
    const MXInteger *i = as_int(o);
    if (!i) return "UltraInteger";
    static const char *const kNames[] = { "int8",   "uint8",  "int16",
                                          "uint16", "int32",  "uint32",
                                          "int64",  "uint64", "UltraInteger" };
    const std::string t = i->int_type();
    for (const char *n : kNames)
        if (t == n) return n;
    return "UltraInteger";
}
std::int64_t mxs_int_size(MXObject *o) {
    const MXInteger *i = as_int(o);
    return i ? static_cast<std::int64_t>(i->int_size()) : 0;
}

void mxs_obj_delete(MXObject *o) { delete o; }

}// extern "C"
