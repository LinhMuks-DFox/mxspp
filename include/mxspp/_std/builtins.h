#pragma once
#include "mxspp/core/MXObject.h"

// Auto-imported builtins C-ABI (progress17: relocated out of src/core into src/_std). The language
// binds these via @@foreign in std/builtins.mxs.
extern "C" {

// str(x) / repr(x) builtins (progress12 D-STR-REPR): a fresh, owned (+1) MXString of the value's
// human / developer form. Null → "nil". Polymorphic over any MXObject via its virtual str()/repr().
mxs::core::MXObject *mxs_str(mxs::core::MXObject *o);
mxs::core::MXObject *mxs_repr(mxs::core::MXObject *o);

// `raise` / `exit` as functions (progress06: `raise` is a special form of `exit` — exit with
// error). Both terminate the process immediately (flush + _Exit). raise prints the error object;
// exit uses the given integer code.
[[noreturn]] void mxs_raise(const mxs::core::MXObject *err);
[[noreturn]] void mxs_exit(const mxs::core::MXObject *code);
}
