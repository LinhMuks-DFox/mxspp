#include "mxspp/backend/codegen.h"
#include "mxspp/core/MXClassInfo.h"
#include "mxspp/frontend/ast.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <cassert>
#include <iostream>
#include <set>
#include <unordered_set>

namespace mxs::backend::codegen {
    namespace ast = mxs::frontend::ast;


    // ===================== new object-model lowering (core types) =====================
    // The rewired codegen (progress09 ④). Values are real core::MXObject* and operators emit the
    // typed core ABI (mxs_int_*, …) defined in core.bc, which the JIT links in so LLVM can inline
    // across the boundary (D6). Variables are MXLeftValue binding cells (`let` immutable,
    // `let mut` mutable; assignment goes through mxs_lvalue_update). The stdlib (println, …)
    // resolves via @@foreign — no per-function hardcoding (D3). Arithmetic/compare currently lower
    // to the integer ABI (full cross-type dynamic dispatch is a later step); strings/floats/bools/
    // nil literals and containers are constructed via their own ABIs.
    namespace {
        // Binary operator -> dynamic-dispatch core ABI symbol (docs §8): these inspect operand
        // types at runtime (int/float/string), so mixed-type arithmetic and comparisons work.
        const char *core_op(const std::string &op) {
            if (op == "+") return "mxs_op_add";
            if (op == "-") return "mxs_op_sub";
            if (op == "*") return "mxs_op_mul";
            if (op == "**") return "mxs_op_pow";
            if (op == "/") return "mxs_op_div";
            if (op == "%") return "mxs_op_mod";
            if (op == "<") return "mxs_op_lt";
            if (op == "<=") return "mxs_op_le";
            if (op == ">") return "mxs_op_gt";
            if (op == ">=") return "mxs_op_ge";
            if (op == "==") return "mxs_op_eq";
            if (op == "!=") return "mxs_op_ne";
            return nullptr;
        }

        // Operator symbol -> reserved vtable slot (shared with core.bc via MXClassInfo.h). `arity`
        // is the operator's parameter count, used to disambiguate `-`/`+` (0 params = unary).
        std::int64_t op_slot_for(const std::string &op, std::size_t arity) {
            using namespace mxs::core;
            if (op == "+") return arity == 0 ? -1 : MX_SLOT_OP_ADD;// no unary-plus slot
            if (op == "-") return arity == 0 ? MX_SLOT_OP_NEG : MX_SLOT_OP_SUB;
            if (op == "*") return MX_SLOT_OP_MUL;
            if (op == "/") return MX_SLOT_OP_DIV;
            if (op == "%") return MX_SLOT_OP_MOD;
            if (op == "**") return MX_SLOT_OP_POW;
            if (op == "<") return MX_SLOT_OP_LT;
            if (op == "<=") return MX_SLOT_OP_LE;
            if (op == ">") return MX_SLOT_OP_GT;
            if (op == ">=") return MX_SLOT_OP_GE;
            if (op == "==") return MX_SLOT_OP_EQ;
            if (op == "!=") return MX_SLOT_OP_NE;
            if (op == "!") return MX_SLOT_OP_NOT;
            if (op == "[]") return MX_SLOT_OP_INDEX_GET;
            if (op == "[]=") return MX_SLOT_OP_INDEX_SET;
            return -1;
        }

        struct CoreGen {
            llvm::Module *M;
            llvm::IRBuilder<> &B;
            llvm::LLVMContext &C;
            llvm::Type *i64, *dbl, *voidTy;
            llvm::PointerType *ptr;
            const std::unordered_map<std::string, llvm::Function *> &funcs;
            // Whole-program method selector -> vtable slot (>= MX_SLOT_RESERVED_COUNT). Used to
            // dispatch `obj.m(args)` through the receiver's classinfo->vtable.
            const std::unordered_map<std::string, std::int64_t> *selectors = nullptr;
            // Qualified-import namespaces (progress13 D2): an `Identifier(ns)` receiver whose name
            // is here marks `ns.fn(args)` as a module-qualified call resolving to `funcs[ns.fn]`,
            // told apart from a method call on a value. Empty without qualified imports.
            const std::set<std::string> *moduleNamespaces = nullptr;
            // Names of @@foreign functions: their C callees BORROW args (the caller releases),
            // whereas user mxs functions/ctors are callee-owned (params adopt; no caller release).
            const std::unordered_set<std::string> *foreigns = nullptr;
            // Names of variadic functions (last param is a rest `...args`). At a call, surplus args
            // beyond the fixed params are packed into a fresh MXArrayList (progress12 D-VARARG).
            const std::unordered_set<std::string> *variadics = nullptr;
            std::unordered_map<std::string, llvm::AllocaInst *>
                    locals;// name -> alloca<MXLeftValue*>
            // Per-binding mutability (progress13): `let` is immutable, `let mut` is mutable.
            // Assignment to an immutable binding is rejected at compile time. Saved/restored with
            // `locals` on block entry/exit so shadowing in inner scopes is handled.
            std::unordered_map<std::string, bool> localMut;
            // Per-scope declared names (progress13 D5/I2): tracks which names were declared in each
            // active lexical scope, moving in lockstep with `scopes`. Redeclaring a name already
            // present in the current scope is a compile error; shadowing across a nested scope is
            // legal (a fresh scope set is pushed/popped with the block).
            std::vector<std::unordered_set<std::string>> scopeNames;
            llvm::Function *curFn = nullptr;
            bool inMain = false;
            std::vector<llvm::BasicBlock *> breakT, continueT;
            // ARC (progress11): a stack of lexical scopes, each holding the binding cells
            // (MXLeftValue*) created in it. On normal scope exit the cells are deleted (releasing
            // their owned r-values, firing destructors); a `return` deletes every active scope's
            // cells before transferring the (separately-owned) return value out.
            std::vector<std::vector<llvm::Value *>> scopes;
            bool ok = true;

            void pushScope() {
                scopes.emplace_back();
                scopeNames.emplace_back();
                // Invariant: the two stacks move in lockstep (progress14 review). A future edit
                // that pushes/pops one without the other would corrupt the same-scope
                // redeclaration check; assert loudly instead of masking a silent desync.
                assert(scopes.size() == scopeNames.size());
            }
            void emitDeleteCell(llvm::Value *cell) {
                B.CreateCall(rt("mxs_lvalue_delete", voidTy, { ptr }), { cell });
            }
            // Pop the top scope, releasing its cells iff control falls through normally (a
            // terminator — return/break/continue — already handled releases on its path).
            void popScopeRelease() {
                if (scopes.empty()) return;
                assert(scopes.size() == scopeNames.size());// lockstep (see pushScope)
                if (!terminated())
                    for (auto *cell : scopes.back()) emitDeleteCell(cell);
                scopes.pop_back();
                scopeNames.pop_back();
            }
            // Release every active scope's cells (for `return`): the function is exiting.
            void releaseAllScopes() {
                for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
                    for (auto *cell : *it) emitDeleteCell(cell);
            }
            // Release an evaluated temporary (a +1 value codegen owns and is done with).
            void releaseTmp(llvm::Value *v) {
                if (v) B.CreateCall(rt("mxs_release", voidTy, { ptr }), { v });
            }

            // The MXClassInfo struct layout { name, parent, destructor, vtable_len, vtable } as an
            // LLVM literal struct; MUST byte-match core::MXClassInfo (MXClassInfo.h).
            llvm::StructType *classInfoTy() {
                return llvm::StructType::get(C, { ptr, ptr, ptr, i64, ptr });
            }

            void err(const std::string &m) {
                std::cerr << "core-codegen: " << m << "\n";
                ok = false;
            }
            llvm::Function *rt(const char *name, llvm::Type *ret,
                               llvm::ArrayRef<llvm::Type *> args) {
                if (auto *f = M->getFunction(name)) return f;
                return llvm::Function::Create(llvm::FunctionType::get(ret, args, false),
                                              llvm::Function::ExternalLinkage, name, M);
            }
            bool terminated() {
                auto *bb = B.GetInsertBlock();
                return bb && bb->getTerminator();
            }
            llvm::BasicBlock *bb(const char *n) {
                return llvm::BasicBlock::Create(C, n, curFn);
            }
            llvm::AllocaInst *allocaTy(llvm::Type *t, const std::string &nm) {
                llvm::IRBuilder<> tmp(&curFn->getEntryBlock(),
                                      curFn->getEntryBlock().begin());
                return tmp.CreateAlloca(t, nullptr, nm);
            }
            llvm::Value *nil() { return B.CreateCall(rt("mxs_nil_new", ptr, { }), { }); }
            // i1 truthiness of a boxed object (mxs_object_truthy != 0).
            llvm::Value *truthy(llvm::Value *o) {
                auto *t = B.CreateCall(rt("mxs_object_truthy", i64, { ptr }), { o }, "t");
                return B.CreateICmpNE(t, llvm::ConstantInt::get(i64, 0), "tobool");
            }
            // Truthiness of a +1 temporary, releasing it (conditions consume their value).
            llvm::Value *truthyTmp(llvm::Value *o) {
                auto *t = truthy(o);
                releaseTmp(o);
                return t;
            }
            llvm::Value *boolFromI1(llvm::Value *c) {
                return B.CreateCall(rt("mxs_bool_new", ptr, { i64 }),
                                    { B.CreateZExt(c, i64, "b") });
            }
            // Create a binding cell owning `value`; record it under `name`.
            void bind(const std::string &name, llvm::Value *value, bool mutable_) {
                // Same-scope redeclaration is a compile error (progress13 D5/I2): declaring a name
                // already present in the current scope mirrors C++'s illegal `int a; int a;`.
                // Shadowing across a nested scope stays legal (that scope has its own name set).
                if (!scopeNames.empty() && scopeNames.back().count(name)) {
                    err("redeclaration of '" + name +
                        "' in the same scope; it is already declared here");
                    return;
                }
                if (!scopeNames.empty()) scopeNames.back().insert(name);
                auto *cell =
                        B.CreateCall(rt("mxs_lvalue_new", ptr, { ptr, i64 }),
                                     { value, llvm::ConstantInt::get(i64, mutable_) });
                auto *slot = allocaTy(ptr, name);
                B.CreateStore(cell, slot);
                locals[name] = slot;
                localMut[name] = mutable_;
                // Track the cell so its scope deletes it (releasing the adopted r-value) on exit.
                if (!scopes.empty()) scopes.back().push_back(cell);
            }

            llvm::Value *expr(const ast::MXASTNode *n);
            llvm::Value *shortCircuit(const ast::BinaryOp *bo);
            void stmt(const ast::MXASTNode *n);
            void block(const ast::Block *blk) {
                auto saved = locals;
                auto savedMut = localMut;
                pushScope();
                for (const auto &s : blk->statements) {
                    if (terminated()) break;
                    stmt(s.get());
                }
                popScopeRelease();
                locals = saved;
                localMut = savedMut;
            }
            // A block evaluated as an expression (a match arm): its value is the last
            // expression-statement's value (other statements run for their effects).
            llvm::Value *blockValue(const ast::Block *blk) {
                auto saved = locals;
                auto savedMut = localMut;
                pushScope();
                llvm::Value *val = nil();
                for (const auto &s : blk->statements) {
                    if (terminated()) break;
                    if (const auto *es =
                                dynamic_cast<const ast::ExprStatement *>(s.get())) {
                        releaseTmp(val);// drop the previous (non-final) arm value
                        val = es->expr ? expr(es->expr.get()) : nil();
                    } else {
                        stmt(s.get());
                        // A `return`/`break`/`continue` inside the arm terminates this block; do
                        // NOT emit the release/nil after the terminator (that would put
                        // instructions after a `ret` -> invalid IR / verifier failure).
                        if (terminated()) break;
                        releaseTmp(val);
                        val = nil();
                    }
                }
                popScopeRelease();// the arm value `val` is separately owned and survives this
                locals = saved;
                localMut = savedMut;
                return val;
            }
            void emitFunction(const ast::FunctionDef *fn);
            // OOP (progress11): emit a constructor body (creates `self` from `classinfo`, binds the
            // ctor params, runs the body, returns self) and a method/operator/destructor body
            // (binds `self` = arg0, then the params). `isVoid` = destructor (returns void).
            void emitClassCtor(llvm::Function *f, llvm::Constant *classinfo,
                               const ast::ConstructorDef *ctor);
            void
            emitMethodLike(llvm::Function *f,
                           const std::vector<std::unique_ptr<ast::Parameter>> &params,
                           const ast::Block *body, bool isVoid);
        };

        llvm::Value *CoreGen::shortCircuit(const ast::BinaryOp *bo) {
            const bool isAnd = bo->op == "&&";
            llvm::Value *l = expr(bo->left.get());
            if (!l) return nullptr;
            llvm::Value *lc = truthyTmp(l);// releases l
            auto *entryBB = B.GetInsertBlock();
            auto *rhsBB = bb(isAnd ? "and.rhs" : "or.rhs");
            auto *contBB = bb(isAnd ? "and.cont" : "or.cont");
            if (isAnd) B.CreateCondBr(lc, rhsBB, contBB);
            else
                B.CreateCondBr(lc, contBB, rhsBB);
            B.SetInsertPoint(rhsBB);
            llvm::Value *r = expr(bo->right.get());
            if (!r) return nullptr;
            llvm::Value *rc = truthyTmp(r);// releases r
            auto *rhsEnd = B.GetInsertBlock();
            B.CreateBr(contBB);
            B.SetInsertPoint(contBB);
            auto *phi = B.CreatePHI(llvm::Type::getInt1Ty(C), 2, "logic");
            phi->addIncoming(isAnd ? llvm::ConstantInt::getFalse(C)
                                   : llvm::ConstantInt::getTrue(C),
                             entryBB);
            phi->addIncoming(rc, rhsEnd);
            return boolFromI1(phi);
        }

        llvm::Value *CoreGen::expr(const ast::MXASTNode *n) {
            if (const auto *il = dynamic_cast<const ast::IntegerLiteral *>(n))
                return B.CreateCall(rt("mxs_int_from_i64", ptr, { i64 }),
                                    { llvm::ConstantInt::get(i64, il->value, true) });
            if (const auto *fl = dynamic_cast<const ast::FloatLiteral *>(n))
                return B.CreateCall(rt("mxs_float_new", ptr, { dbl }),
                                    { llvm::ConstantFP::get(dbl, fl->value) });
            if (const auto *bl = dynamic_cast<const ast::BooleanLiteral *>(n))
                return B.CreateCall(rt("mxs_bool_new", ptr, { i64 }),
                                    { llvm::ConstantInt::get(i64, bl->value) });
            if (dynamic_cast<const ast::NilLiteral *>(n)) return nil();
            if (const auto *sl = dynamic_cast<const ast::StringLiteral *>(n))
                return B.CreateCall(rt("mxs_str_new", ptr, { ptr }),
                                    { B.CreateGlobalStringPtr(sl->value, "str") });
            if (const auto *ll = dynamic_cast<const ast::ListLiteral *>(n)) {
                llvm::Value *list =
                        B.CreateCall(rt("mxs_arraylist_new", ptr, { }), { }, "list");
                auto *appendFn = rt("mxs_arraylist_append", voidTy, { ptr, ptr });
                for (const auto &el : ll->elements) {
                    llvm::Value *v = expr(el.get());
                    if (!v) return nullptr;
                    B.CreateCall(appendFn, { list, v });
                    releaseTmp(v);// append retained its own ref; drop ours
                }
                return list;
            }
            if (const auto *ix = dynamic_cast<const ast::IndexExpr *>(n)) {
                llvm::Value *t = expr(ix->target.get());
                llvm::Value *i = expr(ix->index.get());
                if (!t || !i) return nullptr;
                // Generic: works for ArrayList elements and String characters.
                auto *r = B.CreateCall(rt("mxs_index_get", ptr, { ptr, ptr }), { t, i });
                releaseTmp(t);
                releaseTmp(i);
                return r;
            }
            if (const auto *me = dynamic_cast<const ast::MemberExpr *>(n)) {
                llvm::Value *t = expr(me->target.get());
                if (!t) return nullptr;
                auto *r = B.CreateCall(rt("mxs_get_attr", ptr, { ptr, ptr }),
                                       { t, B.CreateGlobalStringPtr(me->name, "attr") });
                releaseTmp(t);
                return r;
            }
            if (const auto *id = dynamic_cast<const ast::Identifier *>(n)) {
                auto it = locals.find(id->name);
                if (it == locals.end()) {
                    err("unknown identifier '" + id->name + "'");
                    return nullptr;
                }
                auto *cell = B.CreateLoad(ptr, it->second, id->name);
                return B.CreateCall(rt("mxs_lvalue_rvalue", ptr, { ptr }), { cell },
                                    "rv");
            }
            if (const auto *bo = dynamic_cast<const ast::BinaryOp *>(n)) {
                const std::string &op = bo->op;
                if (op == "&&" || op == "||") return shortCircuit(bo);
                if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=") {
                    // Assignment yields nil (a fresh +1) as its value — uniform and ARC-safe; the
                    // stored r-value is consumed by the target (adopted or retained), not returned.
                    // Subscript assignment: `xs[i] = v` (and compound forms).
                    if (const auto *ix =
                                dynamic_cast<const ast::IndexExpr *>(bo->left.get())) {
                        llvm::Value *tgt = expr(ix->target.get());
                        llvm::Value *idx = expr(ix->index.get());
                        llvm::Value *rhs = expr(bo->right.get());
                        if (!tgt || !idx || !rhs) return nullptr;
                        if (op != "=") {
                            auto *cur = B.CreateCall(
                                    rt("mxs_arraylist_get", ptr, { ptr, ptr }),
                                    { tgt, idx });
                            auto *nr = B.CreateCall(
                                    rt(core_op(op.substr(0, 1)), ptr, { ptr, ptr }),
                                    { cur, rhs });
                            releaseTmp(cur);
                            releaseTmp(rhs);
                            rhs = nr;
                        }
                        // set retains its own reference (borrow convention) -> release ours.
                        B.CreateCall(rt("mxs_arraylist_set", ptr, { ptr, ptr, ptr }),
                                     { tgt, idx, rhs });
                        releaseTmp(rhs);
                        releaseTmp(tgt);
                        releaseTmp(idx);
                        return nil();
                    }
                    // Member assignment: `obj.field = v` / `self.field = v` (and compound forms).
                    if (const auto *me =
                                dynamic_cast<const ast::MemberExpr *>(bo->left.get())) {
                        llvm::Value *obj = expr(me->target.get());
                        llvm::Value *rhs = expr(bo->right.get());
                        if (!obj || !rhs) return nullptr;
                        auto *nameP = B.CreateGlobalStringPtr(me->name, "attr");
                        if (op != "=") {
                            auto *cur =
                                    B.CreateCall(rt("mxs_get_attr", ptr, { ptr, ptr }),
                                                 { obj, nameP });
                            auto *nr = B.CreateCall(
                                    rt(core_op(op.substr(0, 1)), ptr, { ptr, ptr }),
                                    { cur, rhs });
                            releaseTmp(cur);
                            releaseTmp(rhs);
                            rhs = nr;
                        }
                        // mxs_set_attr ADOPTS rhs (the field takes the +1) -> do not release rhs.
                        B.CreateCall(rt("mxs_set_attr", voidTy, { ptr, ptr, ptr }),
                                     { obj, nameP, rhs });
                        releaseTmp(obj);
                        return nil();
                    }
                    const auto *lid =
                            dynamic_cast<const ast::Identifier *>(bo->left.get());
                    if (!lid) {
                        err("assignment target must be a variable");
                        return nullptr;
                    }
                    auto it = locals.find(lid->name);
                    if (it == locals.end()) {
                        err("assignment to unknown variable '" + lid->name + "'");
                        return nullptr;
                    }
                    // Reject reassignment of an immutable `let` binding at compile time (progress13).
                    // `let mut` is required to reassign. (The MXLeftValue runtime is the backstop.)
                    auto mut = localMut.find(lid->name);
                    if (mut != localMut.end() && !mut->second) {
                        err("cannot assign to immutable binding '" + lid->name +
                            "'; declare it with `let mut` to allow reassignment");
                        return nullptr;
                    }
                    llvm::Value *rhs = expr(bo->right.get());
                    if (!rhs) return nullptr;
                    auto *cell = B.CreateLoad(ptr, it->second, lid->name);
                    if (op != "=") {
                        auto *cur = B.CreateCall(rt("mxs_lvalue_rvalue", ptr, { ptr }),
                                                 { cell }, "rv");
                        auto *nr = B.CreateCall(
                                rt(core_op(op.substr(0, 1)), ptr, { ptr, ptr }),
                                { cur, rhs });
                        releaseTmp(cur);
                        releaseTmp(rhs);
                        rhs = nr;
                    }
                    // mxs_lvalue_update ADOPTS rhs (releases the old value; enforces immutability,
                    // returning an MXError on a `let` binding). Discard that result.
                    releaseTmp(B.CreateCall(rt("mxs_lvalue_update", ptr, { ptr, ptr }),
                                            { cell, rhs }));
                    return nil();
                }
                if (const char *sym = core_op(op)) {
                    llvm::Value *l = expr(bo->left.get());
                    llvm::Value *r = expr(bo->right.get());
                    if (!l || !r) return nullptr;
                    auto *res = B.CreateCall(rt(sym, ptr, { ptr, ptr }), { l, r });
                    releaseTmp(l);// operands are borrowed by the op; drop the temporaries
                    releaseTmp(r);
                    return res;
                }
                err("unsupported binary operator '" + op + "'");
                return nullptr;
            }
            if (const auto *uo = dynamic_cast<const ast::UnaryOp *>(n)) {
                llvm::Value *v = expr(uo->operand.get());
                if (!v) return nullptr;
                if (uo->op == "-") {
                    auto *res = B.CreateCall(rt("mxs_op_neg", ptr, { ptr }), { v });
                    releaseTmp(v);
                    return res;
                }
                if (uo->op == "!") {
                    auto *res = B.CreateCall(rt("mxs_op_not", ptr, { ptr }), { v });
                    releaseTmp(v);
                    return res;
                }
                return v;// unary '+' yields the operand as-is
            }
            if (const auto *call = dynamic_cast<const ast::FunctionCall *>(n)) {
                // Module-qualified call `ns.fn(args)` (progress13 D2): a receiver that is a bare
                // Identifier naming a qualified-import namespace resolves to the merged function
                // `funcs[ns.fn]` as a direct call — NOT a method on a value. Checked before method
                // dispatch (a namespace identifier is not an evaluable value). The merged function
                // is itself either @@foreign or a user wrapper, so it flows through the normal
                // call path below by rewriting the lookup name.
                std::string callName = call->name;
                bool moduleQualified = false;
                if (call->receiver && moduleNamespaces) {
                    if (const auto *nsId = dynamic_cast<const ast::Identifier *>(
                                call->receiver.get())) {
                        // A bound local variable shadows an imported namespace of the same name
                        // (lexical scoping): `import std.io; let io = SomeValue; io.m()` must be a
                        // method call on the value, not a module-qualified call. Only treat the
                        // receiver as a namespace when it is NOT a live local.
                        if (moduleNamespaces->count(nsId->name) &&
                            !locals.count(nsId->name)) {
                            callName = nsId->name + "." + call->name;
                            moduleQualified = true;
                        }
                    }
                }
                // Method call `recv.m(args)`. Two dispatch paths (progress13 D4):
                //  (1) user-class method  -> the receiver's classinfo vtable (progress11);
                //  (2) built-in container/string method (`xs.append(v)`, `xs.len()`,
                //      `s.len()`, `xs.get(i)`) -> a direct call to the polymorphic runtime
                //      C-ABI symbol with the receiver as arg0. Built-ins have no MXClassInfo
                //      (their classinfo is null), so they MUST NOT take the vtable path.
                if (call->receiver && !moduleQualified) {
                    // Built-in container/string method table (progress13 D4): name -> {runtime
                    // symbol, arg count excl. receiver, returns void?}. Routed by method name to a
                    // polymorphic runtime C-ABI symbol. (`set`/`concat` are not here for v1 —
                    // `mxs_arraylist_set` returns a *borrow*, not a clean +1, and `concat` has two
                    // non-polymorphic symbols; both need bespoke handling. See progress13/task20.)
                    struct BuiltinMethod {
                        const char *symbol;
                        std::size_t arity;// args excluding the receiver
                        bool returnsVoid;
                    };
                    static const std::unordered_map<std::string, BuiltinMethod> kBuiltins{
                        { "append", { "mxs_arraylist_append", 1, true } },
                        { "len", { "mxs_len", 0, false } },
                        { "get", { "mxs_index_get", 1, false } },
                    };
                    auto bit = kBuiltins.find(call->name);
                    const bool isBuiltinName = bit != kBuiltins.end();
                    // A user-class method *name* (whole-program selector set). NOTE: this is
                    // name-only — it does NOT mean the receiver is actually an instance, so a name
                    // that is a selector still needs the runtime classinfo check below (a built-in
                    // list/string has a null classinfo and MUST NOT take the vtable path).
                    const bool isUserSel = selectors && selectors->count(call->name);
                    if (!isUserSel && !isBuiltinName) {
                        err("call to unknown method '" + call->name + "'");
                        return nullptr;
                    }
                    // For a pure built-in call (no same-named user method) the arity is fixed, so
                    // check it at compile time for a clear diagnostic.
                    if (isBuiltinName && !isUserSel &&
                        call->args.size() != bit->second.arity) {
                        err("method '" + call->name + "' expects " +
                            std::to_string(bit->second.arity) + " argument(s), got " +
                            std::to_string(call->args.size()));
                        return nullptr;
                    }
                    // Evaluate the receiver + args once (shared by every dispatch path below).
                    llvm::Value *recv = expr(call->receiver.get());
                    if (!recv) return nullptr;
                    std::vector<llvm::Value *> argv{ recv };
                    for (const auto &a : call->args) {
                        llvm::Value *v = expr(a.get());
                        if (!v) return nullptr;
                        argv.push_back(v);
                    }
                    std::vector<llvm::Type *> argTys(argv.size(), ptr);
                    // Emit a built-in runtime call with the already-evaluated argv. Foreign/borrow
                    // convention: release every operand (receiver + args); a void result yields
                    // nil(), a non-void result is the fresh +1 expression value.
                    auto emitBuiltin = [&](const BuiltinMethod &bm) -> llvm::Value * {
                        if (bm.returnsVoid) {
                            B.CreateCall(rt(bm.symbol, voidTy, argTys), argv);
                            for (auto *a : argv) releaseTmp(a);
                            return nil();
                        }
                        auto *res =
                                B.CreateCall(rt(bm.symbol, ptr, argTys), argv, "method");
                        for (auto *a : argv) releaseTmp(a);
                        return res;
                    };
                    if (!isUserSel) {
                        // Built-in name only: dispatch straight to the runtime symbol (the symbol
                        // checks the receiver kind internally; a wrong receiver yields nil / an
                        // error value, never a crash — see progress14 §6).
                        return emitBuiltin(bit->second);
                    }
                    // The name is a user-class selector, so the receiver MIGHT be a user instance
                    // (vtable dispatch) or a non-instance value (a built-in list/string, or a type
                    // error). Built-ins have a null classinfo, so we branch at runtime — a null
                    // classinfo means the vtable path is invalid (it would deref null = SIGSEGV):
                    //   classinfo != null -> user-class vtable[slot]; the method is callee-owned
                    //                        (adopts self + args), so the caller does NOT release;
                    //   classinfo == null -> fall back to the built-in symbol when the name is a
                    //                        built-in with matching arity (so `xs.len()` still works
                    //                        even if some class also defines `len`), otherwise a
                    //                        TypeError value (mxs_method_missing) — never a deref.
                    const std::int64_t slot = selectors->at(call->name);
                    auto *ci = B.CreateCall(rt("mxs_object_classinfo", ptr, { ptr }),
                                            { recv }, "ci");
                    auto *isInst = B.CreateICmpNE(ci, llvm::ConstantPointerNull::get(ptr),
                                                  "isinst");
                    auto *vtBB = bb("dispatch.vtable");
                    auto *fallBB = bb("dispatch.fallback");
                    auto *contBB = bb("dispatch.cont");
                    B.CreateCondBr(isInst, vtBB, fallBB);
                    // Vtable path (user instance): callee-owned args (do NOT release here).
                    B.SetInsertPoint(vtBB);
                    auto *ciTy = classInfoTy();
                    auto *vtSlotPtr = B.CreateStructGEP(ciTy, ci, 4, "vtable.addr");
                    auto *vtable = B.CreateLoad(ptr, vtSlotPtr, "vtable");
                    auto *fnPtr =
                            B.CreateGEP(ptr, vtable,
                                        { llvm::ConstantInt::get(i64, slot) }, "fn.addr");
                    auto *fn = B.CreateLoad(ptr, fnPtr, "fn");
                    auto *fnTy = llvm::FunctionType::get(ptr, argTys, false);
                    auto *vtRes = B.CreateCall(fnTy, fn, argv, "method");
                    auto *vtEnd = B.GetInsertBlock();
                    B.CreateBr(contBB);
                    // Fallback path (non-instance receiver): built-in symbol (if name+arity fit) or
                    // a TypeError value. Both borrow operands -> release every evaluated operand.
                    B.SetInsertPoint(fallBB);
                    llvm::Value *fallRes = nullptr;
                    if (isBuiltinName && call->args.size() == bit->second.arity) {
                        fallRes = emitBuiltin(bit->second);
                    } else {
                        auto *nameP = B.CreateGlobalStringPtr(call->name, "method.name");
                        fallRes =
                                B.CreateCall(rt("mxs_method_missing", ptr, { ptr, ptr }),
                                             { recv, nameP }, "nomethod");
                        for (auto *a : argv) releaseTmp(a);
                    }
                    auto *fallEnd = B.GetInsertBlock();
                    B.CreateBr(contBB);
                    B.SetInsertPoint(contBB);
                    auto *phi = B.CreatePHI(ptr, 2, "dispatch.val");
                    phi->addIncoming(vtRes, vtEnd);
                    phi->addIncoming(fallRes, fallEnd);
                    return phi;
                }
                auto it = funcs.find(callName);
                if (it == funcs.end()) {
                    err("call to unknown function '" + callName + "'");
                    return nullptr;
                }
                llvm::Function *f = it->second;
                const bool isVariadic = variadics && variadics->count(callName);
                std::vector<llvm::Value *> argv;
                if (isVariadic) {
                    // Signature is (fixed…, MXArrayList* rest): the rest param occupies the last
                    // LLVM slot, so #fixed = arg_size - 1. Surplus args are packed into a fresh
                    // list (progress12 D-VARARG). append retains, so each appended +1 temporary is
                    // released right after (the list now owns it); the list is one owned value
                    // handled like any other arg below (foreign borrows → released; user adopts).
                    const std::size_t fixed = f->arg_size() ? f->arg_size() - 1 : 0;
                    if (call->args.size() < fixed) {
                        err("call to '" + callName + "' expects at least " +
                            std::to_string(fixed) + " argument(s), got " +
                            std::to_string(call->args.size()));
                        return nullptr;
                    }
                    for (std::size_t i = 0; i < fixed; ++i) {
                        llvm::Value *v = expr(call->args[i].get());
                        if (!v) return nullptr;
                        argv.push_back(v);
                    }
                    auto *list = B.CreateCall(rt("mxs_arraylist_new", ptr, { }), { },
                                              "varargs");
                    auto *appendFn = rt("mxs_arraylist_append", voidTy, { ptr, ptr });
                    for (std::size_t i = fixed; i < call->args.size(); ++i) {
                        llvm::Value *v = expr(call->args[i].get());
                        if (!v) return nullptr;
                        B.CreateCall(appendFn, { list, v });
                        releaseTmp(v);
                    }
                    argv.push_back(list);
                } else {
                    if (call->args.size() != f->arg_size()) {
                        err("call to '" + callName + "' expects " +
                            std::to_string(f->arg_size()) + " argument(s), got " +
                            std::to_string(call->args.size()));
                        return nullptr;
                    }
                    for (const auto &a : call->args) {
                        llvm::Value *v = expr(a.get());
                        if (!v) return nullptr;
                        argv.push_back(v);
                    }
                }
                // Foreign C callees borrow their args (caller releases); user functions/ctors are
                // callee-owned (their params adopt the args), so the caller must NOT release them.
                const bool isForeign = foreigns && foreigns->count(callName);
                if (f->getReturnType()->isVoidTy()) {
                    B.CreateCall(f, argv);
                    if (isForeign)
                        for (auto *a : argv) releaseTmp(a);
                    return nil();
                }
                auto *res = B.CreateCall(f, argv, "call");
                if (isForeign)
                    for (auto *a : argv) releaseTmp(a);
                return res;
            }
            if (const auto *m = dynamic_cast<const ast::MatchExpr *>(n)) {
                // Evaluate the subject once; try each case in order; the first matching arm's
                // body value is the match's value (nil if none matches). Type-binding patterns
                // (`x: Type`) test the runtime type — this is the match-based error model.
                llvm::Value *subj = m->subject ? expr(m->subject.get()) : nil();
                if (!subj) return nullptr;
                auto *result = allocaTy(ptr, "match.result");
                B.CreateStore(nil(), result);
                auto *mergeBB = llvm::BasicBlock::Create(C, "match.end");
                for (const auto &cs : m->cases) {
                    auto *bodyBB = bb("case.body");
                    auto *nextBB = llvm::BasicBlock::Create(C, "case.next");
                    llvm::Value *matches = nullptr;
                    if (cs.typeName) {
                        auto *t = B.CreateCall(
                                rt("mxs_is_type", i64, { ptr, ptr }),
                                { subj, B.CreateGlobalStringPtr(*cs.typeName, "ty") });
                        matches = B.CreateICmpNE(t, llvm::ConstantInt::get(i64, 0),
                                                 "tymatch");
                    } else if (cs.literal) {
                        llvm::Value *lit = expr(cs.literal.get());
                        if (!lit) return nullptr;
                        auto *eq = B.CreateCall(rt("mxs_op_eq", ptr, { ptr, ptr }),
                                                { subj, lit });
                        matches = truthy(eq);
                        releaseTmp(eq);
                        releaseTmp(lit);
                    } else {
                        matches =
                                llvm::ConstantInt::getTrue(C);// wildcard or plain binding
                    }
                    B.CreateCondBr(matches, bodyBB, nextBB);
                    B.SetInsertPoint(bodyBB);
                    auto saved = locals;
                    // The arm gets its own scope so a `case x: T =>` binding's cell is released
                    // within the block that creates it (SSA dominance) at the arm's end.
                    pushScope();
                    if (!cs.binding.empty()) {
                        // Retain subj for the binding (released at arm-scope exit); subj's own +1
                        // is released once after the match.
                        B.CreateCall(rt("mxs_retain", voidTy, { ptr }), { subj });
                        bind(cs.binding, subj, /*mutable=*/false);
                    }
                    const auto *blk = dynamic_cast<const ast::Block *>(cs.body.get());
                    llvm::Value *val = blk ? blockValue(blk) : expr(cs.body.get());
                    if (!terminated()) {
                        popScopeRelease();// release the arm's case binding (if any)
                        if (val) {
                            // Release the slot's current contents (the initial nil placeholder, a
                            // +1) before overwriting it with this arm's value, so the placeholder
                            // is not leaked. Arms are mutually exclusive at runtime, so exactly one
                            // store runs and releases exactly the initial nil.
                            releaseTmp(B.CreateLoad(ptr, result, "match.old"));
                            B.CreateStore(val, result);
                        }
                        B.CreateBr(mergeBB);
                    } else {
                        // terminated (return): releaseAllScopes already ran. Pop both stacks in
                        // lockstep (see pushScope).
                        assert(scopes.size() == scopeNames.size());
                        scopes.pop_back();
                        scopeNames.pop_back();
                    }
                    locals = saved;
                    curFn->insert(curFn->end(), nextBB);
                    B.SetInsertPoint(nextBB);
                }
                B.CreateBr(mergeBB);// no case matched -> result stays nil
                curFn->insert(curFn->end(), mergeBB);
                B.SetInsertPoint(mergeBB);
                releaseTmp(subj);// done with the subject (its own +1)
                return B.CreateLoad(ptr, result, "match.val");
            }
            err("unsupported expression");
            return nullptr;
        }

        void CoreGen::stmt(const ast::MXASTNode *n) {
            if (const auto *blk = dynamic_cast<const ast::Block *>(n)) {
                block(blk);
                return;
            }
            if (const auto *ls = dynamic_cast<const ast::LetStatement *>(n)) {
                llvm::Value *v = ls->value ? expr(ls->value.get()) : nil();
                if (!v) return;
                // `v` is a +1 the first binding adopts; each additional name needs its own
                // reference (retain) so every binding cell owns exactly one (no double-free).
                bool first = true;
                for (const auto &nm : ls->names) {
                    if (!first) B.CreateCall(rt("mxs_retain", voidTy, { ptr }), { v });
                    bind(nm, v, ls->isMut);
                    first = false;
                }
                return;
            }
            if (const auto *es = dynamic_cast<const ast::ExprStatement *>(n)) {
                if (es->expr) releaseTmp(expr(es->expr.get()));// discard the value
                return;
            }
            if (const auto *rs = dynamic_cast<const ast::ReturnStatement *>(n)) {
                if (inMain) {
                    if (rs->value) {
                        llvm::Value *v = expr(rs->value.get());
                        if (!v) return;
                        auto *code =
                                B.CreateCall(rt("mxs_int_to_i64", i64, { ptr }), { v });
                        releaseTmp(v);
                        releaseAllScopes();
                        B.CreateRet(code);
                    } else {
                        releaseAllScopes();
                        B.CreateRet(llvm::ConstantInt::get(i64, 0));
                    }
                    return;
                }
                if (curFn->getReturnType()->isVoidTy()) {
                    if (rs->value) releaseTmp(expr(rs->value.get()));
                    releaseAllScopes();
                    B.CreateRetVoid();
                } else {
                    // Evaluate the value (a +1) BEFORE releasing scopes, so if it was read out of a
                    // binding it survives that binding's release; then transfer it to the caller.
                    llvm::Value *v = rs->value ? expr(rs->value.get()) : nil();
                    if (!v) return;
                    releaseAllScopes();
                    B.CreateRet(v);
                }
                return;
            }
            if (const auto *is = dynamic_cast<const ast::IfStatement *>(n)) {
                llvm::Value *cv = is->condition ? expr(is->condition.get()) : nullptr;
                llvm::Value *cond = cv ? truthyTmp(cv) : llvm::ConstantInt::getFalse(C);
                auto *thenBB = bb("then");
                auto *mergeBB = llvm::BasicBlock::Create(C, "ifcont");
                auto *elseBB =
                        is->elseBranch ? llvm::BasicBlock::Create(C, "else") : mergeBB;
                B.CreateCondBr(cond, thenBB, elseBB);
                B.SetInsertPoint(thenBB);
                if (is->thenBlock) block(is->thenBlock.get());
                if (!terminated()) B.CreateBr(mergeBB);
                if (is->elseBranch) {
                    curFn->insert(curFn->end(), elseBB);
                    B.SetInsertPoint(elseBB);
                    stmt(is->elseBranch.get());
                    if (!terminated()) B.CreateBr(mergeBB);
                }
                curFn->insert(curFn->end(), mergeBB);
                B.SetInsertPoint(mergeBB);
                return;
            }
            if (const auto *lp = dynamic_cast<const ast::LoopStatement *>(n)) {
                auto *bodyBB = bb("loop");
                auto *afterBB = llvm::BasicBlock::Create(C, "loopend");
                B.CreateBr(bodyBB);
                B.SetInsertPoint(bodyBB);
                continueT.push_back(bodyBB);
                breakT.push_back(afterBB);
                if (lp->body) block(lp->body.get());
                if (!terminated()) B.CreateBr(bodyBB);
                continueT.pop_back();
                breakT.pop_back();
                curFn->insert(curFn->end(), afterBB);
                B.SetInsertPoint(afterBB);
                return;
            }
            if (const auto *us = dynamic_cast<const ast::UntilStatement *>(n)) {
                auto *condBB = bb("until.cond");
                auto *bodyBB = llvm::BasicBlock::Create(C, "until.body");
                auto *afterBB = llvm::BasicBlock::Create(C, "until.end");
                B.CreateBr(condBB);
                B.SetInsertPoint(condBB);
                llvm::Value *cv = us->condition ? expr(us->condition.get()) : nullptr;
                B.CreateCondBr(cv ? truthyTmp(cv) : llvm::ConstantInt::getTrue(C),
                               afterBB, bodyBB);
                curFn->insert(curFn->end(), bodyBB);
                B.SetInsertPoint(bodyBB);
                continueT.push_back(condBB);
                breakT.push_back(afterBB);
                if (us->body) block(us->body.get());
                if (!terminated()) B.CreateBr(condBB);
                continueT.pop_back();
                breakT.pop_back();
                curFn->insert(curFn->end(), afterBB);
                B.SetInsertPoint(afterBB);
                return;
            }
            if (const auto *ds = dynamic_cast<const ast::DoUntilStatement *>(n)) {
                auto *bodyBB = bb("do.body");
                auto *condBB = llvm::BasicBlock::Create(C, "do.cond");
                auto *afterBB = llvm::BasicBlock::Create(C, "do.end");
                B.CreateBr(bodyBB);
                B.SetInsertPoint(bodyBB);
                continueT.push_back(condBB);
                breakT.push_back(afterBB);
                if (ds->body) block(ds->body.get());
                if (!terminated()) B.CreateBr(condBB);
                continueT.pop_back();
                breakT.pop_back();
                curFn->insert(curFn->end(), condBB);
                B.SetInsertPoint(condBB);
                llvm::Value *cv = ds->condition ? expr(ds->condition.get()) : nullptr;
                B.CreateCondBr(cv ? truthyTmp(cv) : llvm::ConstantInt::getTrue(C),
                               afterBB, bodyBB);
                curFn->insert(curFn->end(), afterBB);
                B.SetInsertPoint(afterBB);
                return;
            }
            if (const auto *fs = dynamic_cast<const ast::ForInStatement *>(n)) {
                // `for v in lo..hi` (integer range) or `for v in xs` (iterate a container by
                // index). Both run a hidden i64 counter; the loop var is rebound each iteration.
                const auto *range =
                        dynamic_cast<const ast::BinaryOp *>(fs->iterable.get());
                const bool isRange = range && range->op == "..";
                auto *toI64 = rt("mxs_int_to_i64", i64, { ptr });
                llvm::Value *lo = nullptr, *hi = nullptr, *listObj = nullptr;
                if (isRange) {
                    llvm::Value *loObj = expr(range->left.get());
                    llvm::Value *hiObj = expr(range->right.get());
                    if (!loObj || !hiObj) return;
                    lo = B.CreateCall(toI64, { loObj });
                    hi = B.CreateCall(toI64, { hiObj });
                    releaseTmp(loObj);// bounds consumed
                    releaseTmp(hiObj);
                } else {
                    listObj = expr(fs->iterable.get());
                    if (!listObj) return;
                    lo = llvm::ConstantInt::get(i64, 0);
                    auto *lenObj = B.CreateCall(rt("mxs_len", ptr, { ptr }), { listObj });
                    hi = B.CreateCall(toI64, { lenObj });
                    releaseTmp(lenObj);
                }
                auto *ctr = allocaTy(i64, fs->var + ".i");
                B.CreateStore(lo, ctr);
                auto saved = locals;
                auto savedMut = localMut;
                auto *box = allocaTy(ptr, fs->var);// the loop var's binding cell
                locals[fs->var] = box;
                // Track the loop var's mutability (progress13 D5): `for v` is immutable (assigning
                // to it is a compile error), `for mut v` is reassignable. Without this the var was
                // absent from localMut, so the compile-time check passed and the runtime immutable
                // backstop was silently discarded — `v = …` no-oped with no diagnostic.
                localMut[fs->var] = fs->isMut;
                auto *condBB = bb("for.cond");
                auto *bodyBB = llvm::BasicBlock::Create(C, "for.body");
                auto *incrBB = llvm::BasicBlock::Create(C, "for.incr");
                auto *afterBB = llvm::BasicBlock::Create(C, "for.end");
                B.CreateBr(condBB);
                B.SetInsertPoint(condBB);
                llvm::Value *cur = B.CreateLoad(i64, ctr, fs->var + ".i");
                B.CreateCondBr(B.CreateICmpSLT(cur, hi, "forcmp"), bodyBB, afterBB);
                curFn->insert(curFn->end(), bodyBB);
                B.SetInsertPoint(bodyBB);
                // The element: the counter itself (range) or xs[counter] (container).
                auto *ctrObj =
                        B.CreateCall(rt("mxs_int_from_i64", ptr, { i64 }), { cur });
                llvm::Value *iv = nullptr;
                if (isRange) {
                    iv = ctrObj;// the loop var's cell adopts the counter object
                } else {
                    iv = B.CreateCall(rt("mxs_index_get", ptr, { ptr, ptr }),
                                      { listObj, ctrObj });
                    releaseTmp(ctrObj);// the index object was just a temporary
                }
                auto *cell = B.CreateCall(rt("mxs_lvalue_new", ptr, { ptr, i64 }),
                                          { iv, llvm::ConstantInt::get(i64, fs->isMut) });
                B.CreateStore(cell, box);
                continueT.push_back(incrBB);
                breakT.push_back(afterBB);
                if (fs->body) block(fs->body.get());
                if (!terminated()) {
                    emitDeleteCell(
                            cell);// release this iteration's loop var (and its value)
                    B.CreateBr(incrBB);
                }
                continueT.pop_back();
                breakT.pop_back();
                curFn->insert(curFn->end(), incrBB);
                B.SetInsertPoint(incrBB);
                B.CreateStore(B.CreateAdd(B.CreateLoad(i64, ctr),
                                          llvm::ConstantInt::get(i64, 1), "inc"),
                              ctr);
                B.CreateBr(condBB);
                curFn->insert(curFn->end(), afterBB);
                B.SetInsertPoint(afterBB);
                if (listObj) releaseTmp(listObj);// the iterable, done after the loop
                locals = saved;
                localMut = savedMut;
                return;
            }
            if (dynamic_cast<const ast::BreakStatement *>(n)) {
                if (!breakT.empty()) B.CreateBr(breakT.back());
                else
                    err("'break' outside a loop");
                return;
            }
            if (dynamic_cast<const ast::ContinueStatement *>(n)) {
                if (!continueT.empty()) B.CreateBr(continueT.back());
                else
                    err("'continue' outside a loop");
                return;
            }
            if (const auto *as = dynamic_cast<const ast::AssertStatement *>(n)) {
                llvm::Value *v = as->expr ? expr(as->expr.get()) : nullptr;
                if (!v) return;
                auto *failBB = bb("assert.fail");
                auto *okBB = bb("assert.ok");
                B.CreateCondBr(truthyTmp(v), okBB, failBB);
                B.SetInsertPoint(failBB);
                B.CreateCall(rt("mxs_panic", voidTy, { ptr }),
                             { B.CreateGlobalStringPtr("assertion failed") });
                B.CreateUnreachable();
                B.SetInsertPoint(okBB);
                return;
            }
            err("unsupported statement");
        }

        void CoreGen::emitFunction(const ast::FunctionDef *fn) {
            curFn = funcs.at(fn->name);
            inMain = fn->name == "main";
            locals.clear();
            scopes.clear();
            scopeNames.clear();
            B.SetInsertPoint(llvm::BasicBlock::Create(C, "entry", curFn));
            pushScope();// function/param scope
            unsigned i = 0;
            for (auto &arg : curFn->args()) {
                if (i >= fn->params.size()) break;
                bind(fn->params[i]->name, &arg, /*mutable=*/false);// params are immutable
                ++i;
            }
            if (fn->body) block(fn->body.get());
            if (!terminated()) {
                popScopeRelease();// release params on fall-through
                if (inMain) B.CreateRet(llvm::ConstantInt::get(i64, 0));
                else if (curFn->getReturnType()->isVoidTy())
                    B.CreateRetVoid();
                else
                    B.CreateRet(nil());
            }
            scopes.clear();// function done (releases already emitted on every path)
            scopeNames.clear();
        }

        void CoreGen::emitClassCtor(llvm::Function *f, llvm::Constant *classinfo,
                                    const ast::ConstructorDef *ctor) {
            curFn = f;
            inMain = false;
            locals.clear();
            scopes.clear();
            scopeNames.clear();
            B.SetInsertPoint(llvm::BasicBlock::Create(C, "entry", f));
            pushScope();
            // self = a fresh instance of this class; bind it, then bind the ctor params.
            llvm::Value *self = B.CreateCall(rt("mxs_instance_new", ptr, { ptr }),
                                             { classinfo }, "self");
            bind("self", self, /*mutable=*/false);
            unsigned i = 0;
            for (auto &arg : f->args()) {
                if (!ctor || i >= ctor->params.size()) break;
                bind(ctor->params[i]->name, &arg, /*mutable=*/false);
                ++i;
            }
            if (ctor && ctor->body) block(ctor->body.get());
            if (!terminated()) {
                // The constructor returns the new instance: retain it so it survives releasing the
                // ctor's scope (which holds `self`'s binding cell), transferring +1 to the caller.
                B.CreateCall(rt("mxs_retain", voidTy, { ptr }), { self });
                popScopeRelease();
                B.CreateRet(self);
            }
            scopes.clear();
            scopeNames.clear();
        }

        void CoreGen::emitMethodLike(
                llvm::Function *f,
                const std::vector<std::unique_ptr<ast::Parameter>> &params,
                const ast::Block *body, bool isVoid) {
            curFn = f;
            inMain = false;
            locals.clear();
            scopes.clear();
            scopeNames.clear();
            B.SetInsertPoint(llvm::BasicBlock::Create(C, "entry", f));
            pushScope();
            auto ai = f->arg_begin();
            if (ai != f->arg_end()) {// arg0 is the receiver
                bind("self", &*ai, /*mutable=*/false);
                ++ai;
            }
            unsigned i = 0;
            for (; ai != f->arg_end() && i < params.size(); ++ai, ++i)
                bind(params[i]->name, &*ai, /*mutable=*/false);
            if (body) block(body);
            if (!terminated()) {
                popScopeRelease();
                if (isVoid) B.CreateRetVoid();
                else
                    B.CreateRet(nil());
            }
            scopes.clear();
            scopeNames.clear();
        }
    }// namespace

    std::unique_ptr<llvm::Module>
    compile_core(const ast::TranslationUnit &tu, llvm::LLVMContext &llvmContext,
                 const std::string &moduleName,
                 const std::set<std::string> &moduleNamespaces) {
        auto module = std::make_unique<llvm::Module>(moduleName, llvmContext);
        llvm::IRBuilder<> B(llvmContext);
        auto *i64 = llvm::Type::getInt64Ty(llvmContext);
        auto *voidTy = llvm::Type::getVoidTy(llvmContext);
        auto *ptr = llvm::PointerType::get(llvmContext, 0);

        std::unordered_map<std::string, llvm::Function *> funcs;
        std::unordered_set<std::string>
                foreigns;// @@foreign function names (borrow their args)
        std::unordered_set<std::string>
                variadics;// last param is a rest `...args` (D-VARARG)
        // One external LLVM declaration per @@foreign runtime symbol. Several FunctionDefs may bind
        // the SAME symbol — two import aliases of one module (`import std.io as a; ... as b;`), a
        // module imported twice, or two mxs names for one C-ABI function — and LLVM would otherwise
        // auto-rename the duplicate global to `<sym>.1`, an unresolvable JIT symbol. Reuse the first
        // declaration per symbol so they all share it.
        std::unordered_map<std::string, llvm::Function *> foreignBySym;
        bool hasMain = false;
        for (const auto &s : tu.statements) {
            const auto *fn = dynamic_cast<const ast::FunctionDef *>(s.get());
            if (!fn) continue;
            if (fn->isForeign) foreigns.insert(fn->name);
            if (!fn->params.empty() && fn->params.back()->isRest)
                variadics.insert(fn->name);
            std::vector<llvm::Type *> argTys(fn->params.size(), ptr);
            llvm::Type *ret =
                    fn->name == "main"
                            ? i64
                            : ((fn->returnTypeName && *fn->returnTypeName == "nil")
                                       ? voidTy
                                       : ptr);
            const std::string sym =
                    fn->isForeign
                            ? (fn->foreignSymbol.empty() ? fn->name : fn->foreignSymbol)
                            : fn->name;
            llvm::Function *f = nullptr;
            if (fn->isForeign)
                if (auto it = foreignBySym.find(sym); it != foreignBySym.end())
                    f = it->second;
            if (!f) {
                f = llvm::Function::Create(llvm::FunctionType::get(ret, argTys, false),
                                           llvm::Function::ExternalLinkage, sym,
                                           module.get());
                if (fn->isForeign) foreignBySym[sym] = f;
            }
            funcs[fn->name] = f;
            if (fn->name == "main") hasMain = true;
        }
        if (!hasMain) {
            std::cerr << "core-codegen: program has no main()\n";
            return nullptr;
        }

        // ---- OOP class pre-pass (progress11): assign whole-program method selector slots, declare
        // the ctor/method/operator/destructor functions, and emit each class's MXClassInfo + vtable
        // as constant globals (so dispatch can fold to a direct call once the type is known). ----
        std::vector<const ast::ClassDef *> classes;
        for (const auto &s : tu.statements)
            if (const auto *cd = dynamic_cast<const ast::ClassDef *>(s.get()))
                classes.push_back(cd);

        std::unordered_map<std::string, std::int64_t> selectors;
        std::int64_t nextSlot = mxs::core::MX_SLOT_RESERVED_COUNT;
        for (const auto *cd : classes)
            for (const auto &m : cd->members)
                if (const auto *md = dynamic_cast<const ast::MethodDef *>(m.get()))
                    if (!selectors.count(md->name)) selectors[md->name] = nextSlot++;
        const std::int64_t vtableLen = nextSlot;// MX_SLOT_RESERVED_COUNT + #selectors

        auto *ciTy = llvm::StructType::get(llvmContext, { ptr, ptr, ptr, i64, ptr });
        auto *vtArrTy = llvm::ArrayType::get(ptr, vtableLen);
        auto *nullPtr = llvm::ConstantPointerNull::get(ptr);

        struct MethodEmit {
            llvm::Function *f;
            const ast::MethodDef *def;
        };
        struct OpEmit {
            llvm::Function *f;
            const ast::OperatorDef *def;
        };
        struct ClassEmit {
            llvm::Constant *classinfo = nullptr;
            llvm::Function *ctorFn = nullptr;
            const ast::ConstructorDef *ctorDef = nullptr;
            std::vector<MethodEmit> methods;
            std::vector<OpEmit> ops;
            llvm::Function *dtorFn = nullptr;
            const ast::DestructorDef *dtorDef = nullptr;
        };
        std::vector<ClassEmit> classEmits;
        classEmits.reserve(classes.size());

        for (const auto *cd : classes) {
            ClassEmit ce;
            for (const auto &m : cd->members) {
                if (const auto *ctor =
                            dynamic_cast<const ast::ConstructorDef *>(m.get())) {
                    if (!ce.ctorDef) ce.ctorDef = ctor;
                } else if (const auto *dt =
                                   dynamic_cast<const ast::DestructorDef *>(m.get())) {
                    if (!ce.dtorDef) ce.dtorDef = dt;
                }
            }
            // Constructor: callable as the class name (`Point(3,4)`), N ptr args -> ptr.
            const std::size_t nctor = ce.ctorDef ? ce.ctorDef->params.size() : 0;
            std::vector<llvm::Type *> ctorArgs(nctor, ptr);
            ce.ctorFn = llvm::Function::Create(
                    llvm::FunctionType::get(ptr, ctorArgs, false),
                    llvm::Function::ExternalLinkage, cd->name, module.get());
            funcs[cd->name] = ce.ctorFn;

            std::vector<llvm::Constant *> vt(static_cast<std::size_t>(vtableLen),
                                             nullPtr);
            for (const auto &m : cd->members) {
                if (const auto *md = dynamic_cast<const ast::MethodDef *>(m.get())) {
                    std::vector<llvm::Type *> a(1 + md->params.size(), ptr);
                    auto *mf = llvm::Function::Create(
                            llvm::FunctionType::get(ptr, a, false),
                            llvm::Function::ExternalLinkage, cd->name + "$" + md->name,
                            module.get());
                    ce.methods.push_back({ mf, md });
                    vt[static_cast<std::size_t>(selectors[md->name])] = mf;
                } else if (const auto *od =
                                   dynamic_cast<const ast::OperatorDef *>(m.get())) {
                    const std::int64_t slot = op_slot_for(od->op, od->params.size());
                    std::vector<llvm::Type *> a(1 + od->params.size(), ptr);
                    auto *of = llvm::Function::Create(
                            llvm::FunctionType::get(ptr, a, false),
                            llvm::Function::ExternalLinkage,
                            cd->name + "$op" + std::to_string(slot), module.get());
                    ce.ops.push_back({ of, od });
                    if (slot >= 0 && slot < vtableLen)
                        vt[static_cast<std::size_t>(slot)] = of;
                }
            }

            llvm::Constant *dtorC = nullPtr;
            if (ce.dtorDef) {
                ce.dtorFn = llvm::Function::Create(
                        llvm::FunctionType::get(voidTy, { ptr }, false),
                        llvm::Function::ExternalLinkage, cd->name + "$dtor",
                        module.get());
                dtorC = ce.dtorFn;
            }

            auto *nameConst =
                    llvm::ConstantDataArray::getString(llvmContext, cd->name, true);
            auto *nameGV = new llvm::GlobalVariable(*module, nameConst->getType(), true,
                                                    llvm::GlobalValue::PrivateLinkage,
                                                    nameConst, cd->name + ".name");
            auto *vtInit = llvm::ConstantArray::get(vtArrTy, vt);
            auto *vtGV = new llvm::GlobalVariable(*module, vtArrTy, true,
                                                  llvm::GlobalValue::PrivateLinkage,
                                                  vtInit, cd->name + ".vtable");
            auto *ciInit = llvm::ConstantStruct::get(
                    ciTy, { nameGV, nullPtr, dtorC,
                            llvm::ConstantInt::get(i64, vtableLen), vtGV });
            auto *ciGV = new llvm::GlobalVariable(*module, ciTy, true,
                                                  llvm::GlobalValue::PrivateLinkage,
                                                  ciInit, cd->name + ".classinfo");
            ce.classinfo = ciGV;
            classEmits.push_back(std::move(ce));
        }

        CoreGen g{
            module.get(), B,   llvmContext, i64, llvm::Type::getDoubleTy(llvmContext),
            voidTy,       ptr, funcs
        };
        g.selectors = &selectors;
        g.foreigns = &foreigns;
        g.variadics = &variadics;
        g.moduleNamespaces = &moduleNamespaces;
        for (const auto &s : tu.statements)
            if (const auto *fn = dynamic_cast<const ast::FunctionDef *>(s.get()))
                if (!fn->isForeign) g.emitFunction(fn);
        // Emit class function bodies (ctor / methods / operators / destructor).
        const std::vector<std::unique_ptr<ast::Parameter>> noParams;
        for (auto &ce : classEmits) {
            g.emitClassCtor(ce.ctorFn, ce.classinfo, ce.ctorDef);
            for (auto &me : ce.methods)
                g.emitMethodLike(me.f, me.def->params, me.def->body.get(), false);
            for (auto &oe : ce.ops)
                g.emitMethodLike(oe.f, oe.def->params, oe.def->body.get(), false);
            if (ce.dtorFn)
                g.emitMethodLike(ce.dtorFn, noParams, ce.dtorDef->body.get(), true);
        }
        if (!g.ok) return nullptr;

        std::string err;
        llvm::raw_string_ostream os(err);
        if (llvm::verifyModule(*module, &os)) {
            std::cerr << "core-codegen: verification failed:\n" << os.str();
            return nullptr;
        }
        return module;
    }

}// namespace mxs::backend::codegen
