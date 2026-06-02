#include "mxspp/core/MXString.h"
#include "mxspp/core/MXInteger.h"

#include <functional>
#include <string>
#include <utility>

namespace mxs::builtin {

    MXString::MXString(std::string value, bool is_static)
        : core::MXObject(is_static), value_(std::move(value)) { }

    auto MXString::from_literal(const std::string &contents) -> MXObjectOwned {
        return std::make_unique<MXString>(contents);
    }

    auto MXString::concat(const MXString &other) const -> MXObjectOwned {
        return std::make_unique<MXString>(value_ + other.value_);
    }

    auto MXString::length() const -> MXObjectOwned {
        return std::make_unique<MXInteger>(static_cast<std::int64_t>(value_.size()));
    }

    auto MXString::cmp(const MXString &other) const -> int {
        const int c = value_.compare(other.value_);
        return c < 0 ? -1 : c > 0 ? 1 : 0;
    }

    auto MXString::str() const -> core::repr_t { return value_; }

    // Quoted + escaped form: round-trips the common escapes so a string is unambiguous inside a
    // container / at the REPL / under format `{:?}` (progress12 D-STR-REPR).
    auto MXString::repr() const -> core::repr_t {
        std::string out;
        out.reserve(value_.size() + 2);
        out.push_back('"');
        for (const char c : value_) {
            switch (c) {
                case '\\':
                    out += "\\\\";
                    break;
                case '"':
                    out += "\\\"";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\0':
                    out += "\\0";
                    break;
                default:
                    out.push_back(c);
            }
        }
        out.push_back('"');
        return out;
    }

    auto MXString::equals(MXObjectConstBorrow other) -> bool {
        const auto *o = dynamic_cast<const MXString *>(other.get());
        return o && value_ == o->value_;
    }

    auto MXString::get_hash_code() const -> MXHashCode_t {
        return static_cast<MXHashCode_t>(std::hash<std::string>{}(value_));
    }

    auto MXString::get_rtti() -> const core::MXRuntimeTypeInfo & {
        static core::MXRuntimeTypeInfo instance{ "MXString",
                                                 &core::MXObject::get_rtti() };
        return instance;
    }

}// namespace mxs::builtin

// ============================================================================================
// extern "C" ABI — JIT-facing, ABI-stable surface (progress09 D3).
// ============================================================================================
namespace {
    using mxs::builtin::MXString;
    using mxs::core::MXObject;

    const MXString *as_str(const MXObject *o) {
        return dynamic_cast<const MXString *>(o);
    }
}// namespace

extern "C" {

MXObject *mxs_str_new(const char *s) { return new MXString(s ? s : ""); }

// str(x) / repr(x) builtins (progress12 D-STR-REPR): a fresh, owned (+1) MXString of the value's
// human / developer form. Null → "nil". Polymorphic over any MXObject via its virtual str()/repr().
MXObject *mxs_str(MXObject *o) { return new MXString(o ? o->str() : "nil"); }
MXObject *mxs_repr(MXObject *o) { return new MXString(o ? o->repr() : "nil"); }

MXObject *mxs_str_concat(MXObject *a, MXObject *b) {
    const MXString *sa = as_str(a), *sb = as_str(b);
    if (!sa || !sb) return new MXString();
    return sa->concat(*sb).release();
}

// Byte length as an MXInteger.
MXObject *mxs_str_len(MXObject *s) {
    const MXString *str = as_str(s);
    return str ? str->length().release() : nullptr;
}

std::int64_t mxs_str_cmp(MXObject *a, MXObject *b) {
    const MXString *sa = as_str(a), *sb = as_str(b);
    return (sa && sb) ? sa->cmp(*sb) : 0;
}

// Borrowed view of the bytes (NUL-terminated); valid while the MXString lives. For print/interop.
const char *mxs_str_cstr(MXObject *s) {
    const MXString *str = as_str(s);
    return str ? str->value().c_str() : "";
}

}// extern "C"
