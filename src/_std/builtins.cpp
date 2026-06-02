#include "mxspp/_std/builtins.h"

#include "mxspp/core/MXInteger.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/MXString.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

// ============================================================================================
// Auto-imported builtins (progress17: relocated from src/core/MXString.cpp + MXOps.cpp).
// str/repr are thin wrappers over MXObject::str()/repr(); raise/exit are process control.
// ============================================================================================
namespace {
    using mxs::builtin::MXInteger;
    using mxs::builtin::MXString;
    using mxs::core::MXObject;

    template<class T>
    const T *as(const MXObject *o) {
        return dynamic_cast<const T *>(o);
    }
}// namespace

extern "C" {

// str(x) / repr(x) builtins (progress12 D-STR-REPR): a fresh, owned (+1) MXString of the value's
// human / developer form. Null → "nil". Polymorphic over any MXObject via its virtual str()/repr().
MXObject *mxs_str(MXObject *o) { return new MXString(o ? o->str() : "nil"); }
MXObject *mxs_repr(MXObject *o) { return new MXString(o ? o->repr() : "nil"); }

// `raise` / `exit` as functions (progress06: `raise` is a special form of `exit` — exit with
// error). Both terminate the process immediately (flush + _Exit, skipping the ORC exit-teardown
// hazard). raise prints the error object; exit uses the given integer code.
[[noreturn]] void mxs_raise(const MXObject *err) {
    std::fprintf(stderr, "mxs: unhandled error: %s\n", err ? err->repr().c_str() : "nil");
    std::fflush(nullptr);
    std::_Exit(1);
}
[[noreturn]] void mxs_exit(const MXObject *code) {
    std::int64_t n = 0;
    if (const auto *i = as<MXInteger>(code)) {
        bool ok = false;
        n = i->to_i64(ok);
    }
    std::fflush(nullptr);
    std::_Exit(static_cast<int>(n));
}

}// extern "C"
