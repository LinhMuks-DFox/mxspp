#pragma once
#include "mxspp/frontend/ast.h"

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace mxs::frontend::imports {

    // The result of resolving + loading the `import` statements of a translation unit
    // (progress13 D2, "1+3" / progress18 modules-as-namespaces). On success the TU's Import nodes
    // have been replaced by the merged module declarations.
    //
    // progress18: a module is resolved as a UNIT with its own internal scope (name-mangle,
    // Direction (a)). Every top-level decl of an imported module (functions AND classes) is renamed
    // to a unique per-module prefix `__mod$<fqdn>$<name>`, and intra-module bare references (sibling
    // calls / constructor calls) are rewritten to the mangled name so a module's internal helpers
    // bind under EVERY import form. The importing program sees the module only through `exposed`:
    //   - `namespaces` is the set of qualified-import namespaces (alias or last path segment). A
    //     call `ns.fn(args)` whose `ns` is here is a module-qualified call (not a method on a value).
    //   - `exposed` maps a SURFACE name (qualified `ns.fn`/`ns.Class`, or a selective bare name) to
    //     the MANGLED `funcs`/`foreigns` key codegen actually emits. Unlisted siblings stay only
    //     under their mangled name => module-private, non-colliding with program names.
    struct Resolution {
        bool ok = false;
        std::set<std::string> namespaces;
        std::unordered_map<std::string, std::string> exposed;// surfaceName -> mangledName
    };

    // Resolve, load, and merge every `import` in `tu` in place. `sourceName` is the importing
    // file (for diagnostics). `searchDirs` are the base directories searched, in order, for a
    // module `a.b.c` at `<dir>/a/b/c.mxs` (the first hit wins). Diagnostics go to std::cerr.
    // After a successful return, `tu` contains NO Import nodes (codegen never sees them); an
    // import-free program therefore resolves no stdlib name (import-gating).
    Resolution resolve_imports(ast::TranslationUnit &tu, const std::string &sourceName,
                               const std::vector<std::string> &searchDirs);

}// namespace mxs::frontend::imports
