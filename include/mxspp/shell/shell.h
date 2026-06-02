#pragma once
#include <string>
#include <vector>

namespace mxs::shell {

    // Interactive read-eval-print loop (JIT-backed). `coreBcPath` is the core object-model bitcode
    // (core.bc) and `stdBcPath` the std-library bitcode (std.bc, progress17) — both linked into
    // each JIT'd thunk so the runtime + std @@foreign symbols resolve. `searchDirs` is the module
    // search path used to resolve the REPL's `import`s (same as the driver's). Returns an exit code.
    //
    // The stdlib is import-gated (progress13 D2): there are no implicit globals. As a REPL-only
    // convenience the loop auto-injects `import std.io.{...}` (the common print/format names) so an
    // interactive `1 + 2` / `println("hi")` works without the user typing an import every session.
    int repl(const std::vector<std::string> &searchDirs, const std::string &coreBcPath,
             const std::string &stdBcPath);

}// namespace mxs::shell
