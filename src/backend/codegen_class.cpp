#include "mxspp/backend/coregen.h"

namespace mxs::backend::codegen::detail {
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
        llvm::Value *self =
                B.CreateCall(rt("mxs_instance_new", ptr, { ptr }), { classinfo }, "self");
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

    void
    CoreGen::emitMethodLike(llvm::Function *f,
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
}// namespace mxs::backend::codegen::detail
