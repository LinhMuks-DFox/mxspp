#include "mxspp/frontend/imports.h"

#include "mxspp/frontend/parser.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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

        // A module's unique internal name prefix, derived from its fqdn (progress18 name-mangle):
        // `std.types` -> `__mod$std$types$`. Every top-level decl of an imported module is renamed
        // to `prefix + originalName`, so two modules' identically-named helpers never collide and
        // a module's siblings stay private to it (the program reaches only what it exposes).
        std::string mangle_prefix(const std::string &fqdn) {
            std::string p = "__mod$";
            for (char c : fqdn) p += (c == '.') ? '$' : c;
            return p + "$";
        }

        // The mangled name of a top-level decl `name` in a module with the given prefix.
        std::string mangled(const std::string &prefix, const std::string &name) {
            return prefix + name;
        }

        // ---- Intra-module reference rewrite (progress18, the core new step) ----
        // Walk a module function/method/ctor/op body and, for every bare (no-receiver) reference
        // whose surface name maps to a mangled symbol in `rename`, rewrite the name to the mangled
        // one. `rename` carries BOTH the module's own siblings (bare `name` -> `prefix+name`) and
        // the names the module imported transitively (surface -> nested-module mangled). This is
        // what makes a module's internal helper/sibling/imported call bind under EVERY import form;
        // without it, bodies keep bare names that no longer exist after mangling (the whole bug).
        //
        // Covered reference forms (all object-mode):
        //   - FunctionCall with NO receiver: `f(args)` (sibling fn) and `C(args)` (sibling ctor) and
        //     a transitively-imported bare `g(args)`.
        //   - Identifier: a bare reference to a sibling/imported name used as a value (rare today,
        //     but cheap and correct to map).
        // A FunctionCall/MemberExpr WITH a receiver is a method/qualified access — its `name` is a
        // member selector, NOT a module-scope decl, so it is left untouched (but its sub-exprs are
        // still visited).
        struct RefRewriter {
            const std::unordered_map<std::string, std::string> &rename;
            // The owning module's own nested-import namespaces (`import std._b as bee;` -> `bee`).
            // A call/member whose receiver is a bare Identifier in this set is a module-qualified
            // reference (`bee.b_value(...)`), NOT a method on a value: it is collapsed to a
            // receiver-less reference to the mangled symbol via `rename["bee.b_value"]`.
            const std::set<std::string> &namespaces;

            void rewriteName(std::string &name) {
                if (auto it = rename.find(name); it != rename.end()) name = it->second;
            }
            // If `n` is a bare Identifier naming a nested namespace, return its name; else "".
            std::string nsReceiver(const ast::MXASTNode *n) const {
                if (const auto *id = dynamic_cast<const ast::Identifier *>(n))
                    if (namespaces.count(id->name)) return id->name;
                return { };
            }

            void exprNode(ast::MXASTNode *n) {
                if (!n) return;
                if (auto *call = dynamic_cast<ast::FunctionCall *>(n)) {
                    // A receiver-less call's name is a module-scope decl (fn or ctor); rewrite it.
                    if (!call->receiver) {
                        rewriteName(call->name);
                    } else if (const std::string ns = nsReceiver(call->receiver.get());
                               !ns.empty()) {
                        // `ns.fn(...)` over the module's own qualified import: collapse to a
                        // receiver-less call to the mangled symbol (`rename["ns.fn"]`).
                        if (auto it = rename.find(ns + "." + call->name);
                            it != rename.end()) {
                            call->name = it->second;
                            call->receiver.reset();
                        }
                    } else {
                        // A genuine method call (`recv.m(...)`): keep `m` (a selector), visit recv.
                        exprNode(call->receiver.get());
                    }
                    for (auto &a : call->args) exprNode(a.get());
                    return;
                }
                if (auto *id = dynamic_cast<ast::Identifier *>(n)) {
                    rewriteName(id->name);
                    return;
                }
                if (auto *bo = dynamic_cast<ast::BinaryOp *>(n)) {
                    exprNode(bo->left.get());
                    exprNode(bo->right.get());
                    return;
                }
                if (auto *uo = dynamic_cast<ast::UnaryOp *>(n)) {
                    exprNode(uo->operand.get());
                    return;
                }
                if (auto *me = dynamic_cast<ast::MemberExpr *>(n)) {
                    // `target.name`: `name` is a member selector (left alone); visit `target`.
                    exprNode(me->target.get());
                    return;
                }
                if (auto *ix = dynamic_cast<ast::IndexExpr *>(n)) {
                    exprNode(ix->target.get());
                    exprNode(ix->index.get());
                    return;
                }
                if (auto *ll = dynamic_cast<ast::ListLiteral *>(n)) {
                    for (auto &e : ll->elements) exprNode(e.get());
                    return;
                }
                if (auto *mx = dynamic_cast<ast::MatchExpr *>(n)) {
                    exprNode(mx->subject.get());
                    for (auto &cs : mx->cases) {
                        // A type-binding pattern's `typeName` may name a sibling/imported class.
                        if (cs.typeName)
                            if (auto it = rename.find(*cs.typeName); it != rename.end())
                                cs.typeName = it->second;
                        exprNode(cs.literal.get());
                        node(cs.body.get());
                    }
                    return;
                }
            }

            // Walk a statement (and recurse into nested blocks/expressions).
            void node(ast::MXASTNode *n) {
                if (!n) return;
                if (auto *b = dynamic_cast<ast::Block *>(n)) {
                    for (auto &s : b->statements) node(s.get());
                    return;
                }
                if (auto *es = dynamic_cast<ast::ExprStatement *>(n)) {
                    exprNode(es->expr.get());
                    return;
                }
                if (auto *ls = dynamic_cast<ast::LetStatement *>(n)) {
                    exprNode(ls->value.get());
                    return;
                }
                if (auto *rs = dynamic_cast<ast::ReturnStatement *>(n)) {
                    exprNode(rs->value.get());
                    return;
                }
                if (auto *is = dynamic_cast<ast::IfStatement *>(n)) {
                    exprNode(is->condition.get());
                    node(is->thenBlock.get());
                    node(is->elseBranch.get());
                    return;
                }
                if (auto *fs = dynamic_cast<ast::ForInStatement *>(n)) {
                    exprNode(fs->iterable.get());
                    node(fs->body.get());
                    return;
                }
                if (auto *lp = dynamic_cast<ast::LoopStatement *>(n)) {
                    node(lp->body.get());
                    return;
                }
                if (auto *us = dynamic_cast<ast::UntilStatement *>(n)) {
                    exprNode(us->condition.get());
                    node(us->body.get());
                    return;
                }
                if (auto *du = dynamic_cast<ast::DoUntilStatement *>(n)) {
                    node(du->body.get());
                    exprNode(du->condition.get());
                    return;
                }
                if (auto *as = dynamic_cast<ast::AssertStatement *>(n)) {
                    exprNode(as->expr.get());
                    return;
                }
                if (auto *df = dynamic_cast<ast::DeferStatement *>(n)) {
                    node(df->body.get());
                    return;
                }
                // Anything else is either a leaf statement with no sub-references we mangle, or an
                // expression node reached directly (a match-arm body that is an expression).
                exprNode(n);
            }

            void function(ast::FunctionDef *fn) {
                if (fn->body) node(fn->body.get());
            }
            // Rewrite all member bodies of a class (methods / operators / ctor / dtor).
            void classMembers(ast::ClassDef *cd) {
                for (auto &m : cd->members) {
                    if (auto *md = dynamic_cast<ast::MethodDef *>(m.get())) {
                        if (md->body) node(md->body.get());
                    } else if (auto *od = dynamic_cast<ast::OperatorDef *>(m.get())) {
                        if (od->body) node(od->body.get());
                    } else if (auto *ct = dynamic_cast<ast::ConstructorDef *>(m.get())) {
                        if (ct->body) node(ct->body.get());
                    } else if (auto *dt = dynamic_cast<ast::DestructorDef *>(m.get())) {
                        if (dt->body) node(dt->body.get());
                    } else if (auto *fd = dynamic_cast<ast::FieldDecl *>(m.get())) {
                        if (fd->value) exprNode(fd->value.get());
                    }
                }
            }
        };

        // ---- Recursive module resolution (progress18 transitive imports + cycle detection) ----
        // A small shared context for resolving imports of BOTH the program TU and any imported
        // module TU (the operation is the same, applied recursively, depth-first). `resolve_one_import`
        // mangles+merges one imported module and records how its importer sees it.
        struct Engine {
            const std::string &sourceName;
            const std::vector<std::string> &searchDirs;
            bool &ok;
            std::unordered_set<std::string>
                    &inProgress;// fqdns being resolved (cycle detection)

            // Resolve one `import` node belonging to an owner TU (the program, or a module). Appends
            // the imported module's fully-mangled, body-rewritten top-level decls to `merged`, and
            // records what the OWNER gains:
            //   - `ownerRewrite`: surface-name -> mangled-symbol. For the program TU this IS the
            //     exposure table codegen consults; for a module TU it is that module's body-rewrite
            //     map (so the module's own bodies reach the symbols it imported). Same entry shape.
            //   - `ownerNamespaces`: namespaces the owner has bound (one import per namespace).
            //   - `ownerExistingExposed`: names already exposed unqualified in the owner (collision
            //     check for selective imports).
            void
            resolve_one_import(ast::Import *imp,
                               std::vector<std::unique_ptr<ast::MXASTNode>> &merged,
                               std::unordered_map<std::string, std::string> &ownerRewrite,
                               std::set<std::string> &ownerNamespaces,
                               std::unordered_set<std::string> &ownerExistingExposed) {
                const std::string fqdn = join_path(imp->path);
                const std::string file = resolve_file(imp->path, searchDirs);
                if (file.empty()) {
                    std::cerr << sourceName << ": error: cannot resolve module '" << fqdn
                              << "' (looked for " << module_rel_path(imp->path)
                              << " on the module search path)\n";
                    ok = false;
                    return;
                }
                if (inProgress.count(fqdn)) {
                    std::cerr << sourceName
                              << ": error: cyclic import detected involving '" << fqdn
                              << "' (" << file << ")\n";
                    ok = false;
                    return;
                }
                bool fok = false;
                const std::string src = read_file(file, fok);
                if (!fok) {
                    std::cerr << sourceName << ": error: cannot open module file '"
                              << file << "' for import '" << fqdn << "'\n";
                    ok = false;
                    return;
                }
                auto mod = parser::parse_to_ast(src, file);
                if (!mod) {
                    std::cerr << sourceName << ": error: failed to parse module '" << fqdn
                              << "' (" << file << ")\n";
                    ok = false;
                    return;
                }

                const std::string prefix = mangle_prefix(fqdn);

                // (1) Resolve THIS module's own nested imports first (depth-first), each with its
                //     own prefix. `modRewrite` accumulates what the module's bodies must be rewritten
                //     with: its siblings (added below) + everything it imports transitively.
                inProgress.insert(fqdn);
                std::unordered_map<std::string, std::string> modRewrite;
                std::set<std::string> modNamespaces;
                std::unordered_set<std::string> modExposedNames;
                for (auto &ms : mod->statements) {
                    if (auto *nestedImp = dynamic_cast<ast::Import *>(ms.get()))
                        resolve_one_import(nestedImp, merged, modRewrite, modNamespaces,
                                           modExposedNames);
                }
                inProgress.erase(fqdn);
                if (!ok) return;

                // (2) Index the module's own top-level functions + classes; add each as a sibling
                //     (bare name -> mangled) to the module's body-rewrite map.
                std::vector<ast::FunctionDef *> modFns;
                std::vector<ast::ClassDef *> modClasses;
                for (auto &ms : mod->statements) {
                    if (auto *fn = dynamic_cast<ast::FunctionDef *>(ms.get())) {
                        modFns.push_back(fn);
                        modRewrite[fn->name] = mangled(prefix, fn->name);
                    } else if (auto *cd = dynamic_cast<ast::ClassDef *>(ms.get())) {
                        modClasses.push_back(cd);
                        modRewrite[cd->name] = mangled(prefix, cd->name);
                    }
                }

                // (3) Rewrite the module's bodies (fn + class members) with the combined map BEFORE
                //     renaming the decls, then rename the decls and move them into `merged`.
                //     `modNamespaces` lets the rewriter recognize the module's own qualified imports
                //     (`bee.b_value(...)`) and collapse them to the mangled symbol.
                RefRewriter rw{ modRewrite, modNamespaces };
                for (auto *fn : modFns) rw.function(fn);
                for (auto *cd : modClasses) rw.classMembers(cd);

                // Move every top-level fn/class out of the module TU into `merged`, renamed to its
                // mangled name. Step (2)/(3) only built the rewrite map and rewrote BODIES — the
                // decl names are still original here, so `fn->name`/`cd->name` is the surface name.
                // @@foreign fns: rename the funcs/foreigns KEY (fn->name) but DO NOT touch
                // foreignSymbol — codegen emits the LLVM external under foreignSymbol; fn->name is
                // only the map key (codegen.cpp:50/62/74).
                auto moveDecl = [&](ast::MXASTNode *target) {
                    for (auto &ms : mod->statements) {
                        if (ms.get() != target) continue;
                        merged.push_back(std::move(ms));
                        return;
                    }
                };
                for (auto *fn : modFns) {
                    modExposedNames.insert(fn->name);
                    fn->name = mangled(prefix, fn->name);
                    moveDecl(fn);
                }
                for (auto *cd : modClasses) {
                    modExposedNames.insert(cd->name);
                    cd->name = mangled(prefix, cd->name);
                    moveDecl(cd);
                }

                // (4) Expose the module to its importer per the import FORM, on top of the fully
                //     mangled internal set. Unlisted siblings stay only under their mangled name =>
                //     module-private, non-colliding.
                if (imp->selected) {
                    // Selective `import a.b.{x, y};`: bind exactly those names UNQUALIFIED in the
                    // importer. Each listed name must be a top-level decl of the module.
                    for (const auto &want : *imp->selected) {
                        if (!modExposedNames.count(want)) {
                            std::cerr << sourceName << ": error: module '" << fqdn
                                      << "' has no exported function '" << want << "'\n";
                            ok = false;
                            continue;
                        }
                        if (ownerExistingExposed.count(want)) {
                            std::cerr << sourceName << ": error: import of '" << want
                                      << "' from '" << fqdn
                                      << "' collides with a name already in scope\n";
                            ok = false;
                            continue;
                        }
                        ownerExistingExposed.insert(want);
                        // Importer sees `want` (unqualified) -> mangled symbol. When the owner is
                        // the program TU this map IS codegen's exposure table; when it is a module
                        // it is that module's body-rewrite map. Same `surface -> mangled` entry.
                        ownerRewrite[want] = mangled(prefix, want);
                    }
                } else {
                    // Qualified `import a.b;` / aliased `import a.b as ns;`: namespace = alias or
                    // last segment; `ns.fn` / `ns.Class` resolve to the mangled symbols. A namespace
                    // is bound by exactly one import (rejects `import std.io; import std.io;`).
                    const std::string ns =
                            imp->alias ? *imp->alias
                                       : (imp->path.empty() ? std::string{ }
                                                            : imp->path.back());
                    if (ownerNamespaces.count(ns)) {
                        std::cerr
                                << sourceName << ": error: import namespace '" << ns
                                << "' is already in use by a previous import (rename one "
                                   "with `as`)\n";
                        ok = false;
                        return;
                    }
                    ownerNamespaces.insert(ns);
                    // Importer sees `ns.fn` / `ns.Class` -> mangled symbol. For the program TU this
                    // is codegen's exposure table; for a module it is that module's body-rewrite map
                    // (a module that wrote `ns.fn(...)` over its own qualified import resolves here).
                    for (const auto &name : modExposedNames)
                        ownerRewrite[ns + "." + name] = mangled(prefix, name);
                }
            }
        };
    }// namespace

    Resolution resolve_imports(ast::TranslationUnit &tu, const std::string &sourceName,
                               const std::vector<std::string> &searchDirs) {
        Resolution res;
        res.ok = true;

        // Names already exposed unqualified in the importing program (its own fn defs + already
        // selectively-imported names). Used to reject a selective import colliding with an existing
        // unqualified name (preserves the historical collision diagnostic).
        std::unordered_set<std::string> existingExposed;
        for (const auto &s : tu.statements)
            if (const auto *fn = dynamic_cast<const ast::FunctionDef *>(s.get()))
                existingExposed.insert(fn->name);

        // Fresh statement list: each Import is replaced by its (transitively) merged module decls;
        // every other node moves across unchanged. Imports are removed before codegen.
        std::vector<std::unique_ptr<ast::MXASTNode>> merged;
        merged.reserve(tu.statements.size());

        std::unordered_set<std::string> inProgress;
        Engine eng{ sourceName, searchDirs, res.ok, inProgress };

        // The program's body-rewrite map IS the exposure table codegen consults: a program call to
        // a selective bare name or a qualified `ns.fn` is translated to the mangled key there.
        std::unordered_map<std::string, std::string> &programRewrite = res.exposed;

        for (auto &stmtPtr : tu.statements) {
            auto *imp = dynamic_cast<ast::Import *>(stmtPtr.get());
            if (!imp) {
                merged.push_back(std::move(stmtPtr));
                continue;
            }
            eng.resolve_one_import(imp, merged, programRewrite, res.namespaces,
                                   existingExposed);
            if (!res.ok) {
                // Keep scanning to surface multiple diagnostics, but skip merging on failure.
            }
        }

        tu.statements = std::move(merged);
        return res;
    }

}// namespace mxs::frontend::imports
