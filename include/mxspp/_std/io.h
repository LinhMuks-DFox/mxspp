#pragma once
#include "mxspp/core/MXObject.h"

// std.io text-output C-ABI (progress17: relocated out of src/core into src/_std). JIT-facing; all
// values are core::MXObject*. The language binds these via @@foreign in std/io.mxs.
extern "C" {

// Variadic print / println: `args` is an MXArrayList; elements are written via their human form
// str(), single-space separated. println appends a newline.
void mxs_print(mxs::core::MXObject *args);
void mxs_println(mxs::core::MXObject *args);

// REPL value echo: print repr(o) + newline, but skip nil (so `print(x)` — returning nil — and bare
// statements don't echo a spurious "nil"), matching a Python-style prompt.
void mxs_repl_echo(mxs::core::MXObject *o);
}
