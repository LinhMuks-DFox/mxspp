#pragma once
#include "mxspp/core/MXObject.h"

// std.string text-formatting C-ABI (progress17: relocated out of src/core into src/_std). The
// language binds this via @@foreign in std/string.mxs.
extern "C" {

// format(fmt, args): a `{}`-placeholder template (`{}` / `{N}` fields, `{{`/`}}` literals, an
// optional `:spec` = `[[fill]align][width][.precision][?]`, `{:?}` selecting the developer form).
// `args` is an MXArrayList of the variadic arguments. Returns a fresh, owned (+1) MXString, or an
// MXError on a bad field index / malformed spec.
mxs::core::MXObject *mxs_format(mxs::core::MXObject *fmt, mxs::core::MXObject *args);
}
