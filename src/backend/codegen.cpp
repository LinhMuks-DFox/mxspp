#include "mxspp/backend/codegen.h"

#include "mxspp/backend/coregen.h"
#include "mxspp/core/MXClassInfo.h"
#include "mxspp/frontend/ast.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace mxs::backend::codegen {
    namespace ast = mxs::frontend::ast;
    using namespace detail;// CoreGen, op_slot_for, core_op

    std::unique_ptr<llvm::Module>
    compile_core(const ast::TranslationUnit &tu, llvm::LLVMContext &llvmContext,
                 const std::string &moduleName,
                 const std::set<std::string> &moduleNamespaces,
                 const std::unordered_map<std::string, std::string> &exposed) {
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
        g.exposed = &exposed;
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
