#pragma once

// Internal shared declaration for the core-type code generator (progress14: codegen.cpp was split
// into per-concern translation units). The only PUBLIC entry point is compile_core() in codegen.h;
// everything here lives in the `detail` namespace and is shared by codegen{,_expr,_stmt,_class}.cpp.

#include "mxspp/core/MXClassInfo.h"
#include "mxspp/frontend/ast.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mxs::backend::codegen::detail {
    namespace ast = mxs::frontend::ast;

    // Binary operator -> dynamic-dispatch core ABI symbol (docs §8): these inspect operand
    // types at runtime (int/float/string), so mixed-type arithmetic and comparisons work.
    inline const char *core_op(const std::string &op) {
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
    inline std::int64_t op_slot_for(const std::string &op, std::size_t arity) {
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
            auto *cell = B.CreateCall(rt("mxs_lvalue_new", ptr, { ptr, i64 }),
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
        void block(const ast::Block *blk);
        // A block evaluated as an expression (a match arm): its value is the last
        // expression-statement's value (other statements run for their effects).
        llvm::Value *blockValue(const ast::Block *blk);
        void emitFunction(const ast::FunctionDef *fn);
        // OOP (progress11): emit a constructor body (creates `self` from `classinfo`, binds the
        // ctor params, runs the body, returns self) and a method/operator/destructor body
        // (binds `self` = arg0, then the params). `isVoid` = destructor (returns void).
        void emitClassCtor(llvm::Function *f, llvm::Constant *classinfo,
                           const ast::ConstructorDef *ctor);
        void emitMethodLike(llvm::Function *f,
                            const std::vector<std::unique_ptr<ast::Parameter>> &params,
                            const ast::Block *body, bool isVoid);
    };
}// namespace mxs::backend::codegen::detail
