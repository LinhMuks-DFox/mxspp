#pragma once
#include "mxspp/core/MXObject.h"

// stdio / text-formatting C-ABI (progress12 D-FORMAT / D-VARARG / D-STR-REPR). JIT-facing; all
// values are core::MXObject*. The language binds these via @@foreign in the core prelude.
extern "C" {

// format(fmt, args): a `{}`-placeholder template (`{}` / `{N}` fields, `{{`/`}}` literals, an
// optional `:spec` = `[[fill]align][width][.precision][?]`, `{:?}` selecting the developer form).
// `args` is an MXArrayList of the variadic arguments. Returns a fresh, owned (+1) MXString, or an
// MXError on a bad field index / malformed spec.
mxs::core::MXObject *mxs_format(mxs::core::MXObject *fmt, mxs::core::MXObject *args);

// Variadic print / println: `args` is an MXArrayList; elements are written via their human form
// str(), single-space separated. println appends a newline.
void mxs_print(mxs::core::MXObject *args);
void mxs_println(mxs::core::MXObject *args);

// REPL value echo: print repr(o) + newline, but skip nil (so `print(x)` — returning nil — and bare
// statements don't echo a spurious "nil"), matching a Python-style prompt.
void mxs_repl_echo(mxs::core::MXObject *o);
}
