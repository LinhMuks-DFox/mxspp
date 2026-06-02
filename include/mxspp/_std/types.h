#pragma once
#include "mxspp/core/MXObject.h"

// std.types reflection C-ABI (progress17: relocated out of src/core into src/_std). The language
// binds this via @@foreign in std/types.mxs.
extern "C" {

// The mxs type name of a value, as a fresh MXString: a user instance -> its class name; built-ins
// -> the canonical name (int/float/str/bool/nil/List/Error). Backs `std.types.typeof`. Returns a
// +1 the caller owns.
mxs::core::MXObject *mxs_typeof(const mxs::core::MXObject *o);
}
