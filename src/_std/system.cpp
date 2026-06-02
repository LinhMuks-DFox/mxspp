#include "mxspp/_std/system.h"

#include "mxspp/core/MXObject.h"
#include "mxspp/core/MXPopulationManager.h"

#include <cstdio>
#include <string>

// ============================================================================================
// std.system / REPL diagnostics (progress17: relocated from src/core/MXOps.cpp). Future home for
// progress20's mxs_sys_*.
// ============================================================================================
extern "C" {

// REPL introspection (the `./objects_population` meta-command). These MUST be reached through
// the JIT path (not a direct C++ call from the shell): MXPopulationManager is a function-local
// singleton duplicated in the statically-linked `core` lib AND in the JIT-linked bitcode, and
// JIT'd user objects register with the bitcode's instance. The shell invokes these via a JIT'd
// expression so the count reflects the SAME singleton the user's objects live in. Both print and
// return void (mxs `nil`); they allocate no MXObject, so they don't perturb the snapshot.
void mxs_population_dump(void) {
    std::printf("live MXObjects: %zu\n",
                mxs::core::MXPopulationManager::get_manager().population_count());
}
void mxs_population_dump_all(void) {
    auto &mgr = mxs::core::MXPopulationManager::get_manager();
    const std::size_t n = mgr.population_count();
    const std::string dump = mgr.repr();// snapshot taken before any printing/allocation
    std::printf("live MXObjects: %zu\n", n);
    std::fputs(dump.c_str(), stdout);
    std::fputc('\n', stdout);
}

}// extern "C"
