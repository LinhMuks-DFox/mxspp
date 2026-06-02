#include "mxspp/frontend/imports.h"

#include "mxspp/frontend/parser.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace mxs::frontend::imports {
    namespace {
        // Read a file fully; `ok` reports whether it opened.
        std::string read_file(const std::string &path, bool &ok) {
            std::ifstream f(path);
            if (!f) {
                ok = false;
                return { };
            }
            std::stringstream ss;
            ss << f.rdbuf();
            ok = true;
            return ss.str();
        }

        // Map a dotted module path (`std.io` -> {"std","io"}) to a relative file path
        // `std/io.mxs`. The caller prefixes a search-base directory.
        std::string module_rel_path(const std::vector<std::string> &path) {
            std::string p;
            for (std::size_t i = 0; i < path.size(); ++i) {
                if (i) p += '/';
                p += path[i];
            }
            return p + ".mxs";
        }

        std::string join_path(const std::vector<std::string> &path) {
            std::string p;
            for (std::size_t i = 0; i < path.size(); ++i) {
                if (i) p += '.';
                p += path[i];
            }
            return p;
        }

        // Locate a module file on the search path (first hit wins). Empty string = not found.
        std::string resolve_file(const std::vector<std::string> &path,
                                 const std::vector<std::string> &searchDirs) {
            const std::string rel = module_rel_path(path);
            for (const auto &dir : searchDirs) {
                std::string cand = dir.empty() ? rel : (dir + "/" + rel);
                if (std::ifstream(cand)) return cand;
            }
            return { };
        }
    }// namespace

    Resolution resolve_imports(ast::TranslationUnit &tu, const std::string &sourceName,
                               const std::vector<std::string> &searchDirs) {
        Resolution res;
        res.ok = true;

        // Names of functions already present in the importing TU (its own defs + already-merged
        // imports). Used to catch a selective import colliding with an existing unqualified name.
        std::unordered_set<std::string> existingFns;
        for (const auto &s : tu.statements)
            if (const auto *fn = dynamic_cast<const ast::FunctionDef *>(s.get()))
                existingFns.insert(fn->name);

        // Collected, fully-resolved declarations to splice in where the imports were. We build a
        // fresh statement list: each Import node is replaced by its merged FunctionDefs; every
        // other node is moved across unchanged. (Imports are removed before codegen.)
        std::vector<std::unique_ptr<ast::MXASTNode>> merged;
        merged.reserve(tu.statements.size());

        for (auto &stmtPtr : tu.statements) {
            auto *imp = dynamic_cast<ast::Import *>(stmtPtr.get());
            if (!imp) {
                merged.push_back(std::move(stmtPtr));
                continue;
            }

            const std::string fqdn = join_path(imp->path);
            const std::string file = resolve_file(imp->path, searchDirs);
            if (file.empty()) {
                std::cerr << sourceName << ": error: cannot resolve module '" << fqdn
                          << "' (looked for " << module_rel_path(imp->path)
                          << " on the module search path)\n";
                res.ok = false;
                continue;
            }
            bool ok = false;
            const std::string src = read_file(file, ok);
            if (!ok) {
                std::cerr << sourceName << ": error: cannot open module file '" << file
                          << "' for import '" << fqdn << "'\n";
                res.ok = false;
                continue;
            }
            auto mod = parser::parse_to_ast(src, file);
            if (!mod) {
                // parse_to_ast already printed a `file:line:col` diagnostic.
                std::cerr << sourceName << ": error: failed to parse module '" << fqdn
                          << "' (" << file << ")\n";
                res.ok = false;
                continue;
            }

            // Transitive imports are out of scope for v1: resolution is a single non-recursive
            // pass over the top-level TU, so a module's own `import` would otherwise be dropped
            // silently and its symbols would surface as a misleading "unknown function" at codegen.
            // Fail fast with a clear diagnostic instead.
            bool hasNestedImport = false;
            for (auto &ms : mod->statements)
                if (dynamic_cast<const ast::Import *>(ms.get())) {
                    hasNestedImport = true;
                    break;
                }
            if (hasNestedImport) {
                std::cerr << sourceName << ": error: module '" << fqdn << "' (" << file
                          << ") contains a nested `import`, which is not supported "
                             "(transitive imports are out of scope)\n";
                res.ok = false;
                continue;
            }

            // Index the module's top-level functions by name, preserving order for the
            // qualified-merge-all path.
            std::vector<ast::FunctionDef *> modFns;
            for (auto &ms : mod->statements)
                if (auto *fn = dynamic_cast<ast::FunctionDef *>(ms.get()))
                    modFns.push_back(fn);

            // Move a FunctionDef out of the module TU into `merged`, optionally renamed.
            auto take_fn = [&](ast::FunctionDef *fn, const std::string &newName) {
                for (auto &ms : mod->statements) {
                    if (ms.get() != fn) continue;
                    if (!newName.empty()) fn->name = newName;
                    merged.push_back(std::move(ms));
                    return;
                }
            };

            if (imp->selected) {
                // Selective `import a.b.{x, y};` — bring exactly those names into scope
                // UNQUALIFIED. Error if a requested name is not a function in the module.
                for (const auto &want : *imp->selected) {
                    ast::FunctionDef *found = nullptr;
                    for (auto *fn : modFns)
                        if (fn->name == want) {
                            found = fn;
                            break;
                        }
                    if (!found) {
                        std::cerr << sourceName << ": error: module '" << fqdn
                                  << "' has no exported function '" << want << "'\n";
                        res.ok = false;
                        continue;
                    }
                    if (existingFns.count(want)) {
                        std::cerr << sourceName << ": error: import of '" << want
                                  << "' from '" << fqdn
                                  << "' collides with a name already in scope\n";
                        res.ok = false;
                        continue;
                    }
                    existingFns.insert(want);
                    take_fn(found, /*newName=*/"");
                }
            } else {
                // Qualified `import a.b;` / aliased `import a.b as ns;` — namespace = alias or the
                // last path segment; every module function is merged under the key `ns.fn`, and
                // `ns` is registered so codegen resolves `ns.fn(args)` as a direct call.
                const std::string ns =
                        imp->alias
                                ? *imp->alias
                                : (imp->path.empty() ? std::string{ } : imp->path.back());
                // A namespace name must be bound by exactly one import. This rejects importing the
                // same module twice (`import std.io; import std.io;`) and two different modules
                // under one alias (`import std.a as x; import std.b as x;`) — both of which would
                // otherwise silently merge/overwrite `ns.fn` keys. Use `as` for a distinct name.
                if (res.namespaces.count(ns)) {
                    std::cerr << sourceName << ": error: import namespace '" << ns
                              << "' is already in use by a previous import (rename one "
                                 "with `as`)\n";
                    res.ok = false;
                    continue;
                }
                res.namespaces.insert(ns);
                for (auto *fn : modFns) {
                    const std::string qualified = ns + "." + fn->name;
                    take_fn(fn, qualified);
                }
            }
        }

        tu.statements = std::move(merged);
        return res;
    }

}// namespace mxs::frontend::imports
