#pragma once
#include "mxspp/frontend/ast.h"

#include <set>
#include <string>
#include <vector>

namespace mxs::frontend::imports {

    // The result of resolving + loading the `import` statements of a translation unit
    // (progress13 D2, "1+3"). On success the TU's Import nodes have been replaced by the merged
    // module declarations (selective names enter unqualified; qualified/aliased modules are merged
    // under `ns.fn` keys). `namespaces` is the set of module namespaces (alias or last path
    // segment) used by qualified imports — codegen needs it to tell a module-qualified call
    // `ns.fn(args)` apart from a method call `value.m(args)`.
    struct Resolution {
        bool ok = false;
        std::set<std::string> namespaces;
    };

    // Resolve, load, and merge every `import` in `tu` in place. `sourceName` is the importing
    // file (for diagnostics). `searchDirs` are the base directories searched, in order, for a
    // module `a.b.c` at `<dir>/a/b/c.mxs` (the first hit wins). Diagnostics go to std::cerr.
    // After a successful return, `tu` contains NO Import nodes (codegen never sees them); an
    // import-free program therefore resolves no stdlib name (import-gating).
    Resolution resolve_imports(ast::TranslationUnit &tu, const std::string &sourceName,
                               const std::vector<std::string> &searchDirs);

}// namespace mxs::frontend::imports
