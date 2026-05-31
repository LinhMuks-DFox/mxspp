#pragma once
#include <string>

namespace mxs::shell {

    // Interactive read-eval-print loop (JIT-backed). `prelude` is the std prelude source,
    // `runtimeBcPath` the runtime bitcode for the fast-dispatch symbols. Returns an exit code.
    int repl(const std::string &prelude, const std::string &runtimeBcPath);

}// namespace mxs::shell
