#pragma once
#include "mxspp/core/MXObject.h"

// std.system / REPL diagnostics C-ABI (progress17: relocated out of src/core into src/_std). The
// REPL binds these via @@foreign for the `./objects_population` meta-command.
extern "C" {

// REPL introspection (the `./objects_population` meta-command). These MUST be reached through the
// JIT path (so they query the same MXPopulationManager singleton the user's JIT'd objects register
// with), not a direct C++ call from the shell. Both print and return void (mxs `nil`).
void mxs_population_dump(void);
void mxs_population_dump_all(void);
}
