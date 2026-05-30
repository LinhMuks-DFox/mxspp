#include "mxspp/frontend/ast.h"
#include "mxspp/core/MXObject.h"

// NOTE: codegen() bodies are intentionally stubbed here. progress01 (2026-05-30) scopes
// the frontend to the source -> AST path only; lowering AST -> LLVM IR is a later progress.
// The stubs exist so the AST nodes are concrete and instantiable (their vtables need a
// definition for the declared virtual codegen()).

namespace mxs::frontend::ast {

    // --- The one real codegen: an integer literal lowers to an i64 constant. ---
    IntegerLiteral::IntegerLiteral(int64_t value, bool is_static)
        : core::MXObject(is_static), MXASTNode(is_static), value(value) { }

    llvm::Value *
    IntegerLiteral::codegen(mxs::backend::codegen::CodegenContext &ctx) const {
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx.llvmContext), value,
                                      /*isSigned=*/true);
    }

    // --- Statement stubs (return void) ---
    void TranslationUnit::codegen(mxs::backend::codegen::CodegenContext &) const { }
    void Block::codegen(mxs::backend::codegen::CodegenContext &) const { }
    void LetStatement::codegen(mxs::backend::codegen::CodegenContext &) const { }
    void ExprStatement::codegen(mxs::backend::codegen::CodegenContext &) const { }
    void IfStatement::codegen(mxs::backend::codegen::CodegenContext &) const { }
    void ReturnStatement::codegen(mxs::backend::codegen::CodegenContext &) const { }
    void ForInStatement::codegen(mxs::backend::codegen::CodegenContext &) const { }
    void LoopStatement::codegen(mxs::backend::codegen::CodegenContext &) const { }
    void BreakStatement::codegen(mxs::backend::codegen::CodegenContext &) const { }
    void ContinueStatement::codegen(mxs::backend::codegen::CodegenContext &) const { }
    void FunctionDef::codegen(mxs::backend::codegen::CodegenContext &) const { }
    void MatchStatement::codegen(mxs::backend::codegen::CodegenContext &) const { }

    // --- Expression stubs (return llvm::Value*) ---
    llvm::Value *Identifier::codegen(mxs::backend::codegen::CodegenContext &) const {
        return nullptr;
    }
    llvm::Value *FloatLiteral::codegen(mxs::backend::codegen::CodegenContext &) const {
        return nullptr;
    }
    llvm::Value *BooleanLiteral::codegen(mxs::backend::codegen::CodegenContext &) const {
        return nullptr;
    }
    llvm::Value *StringLiteral::codegen(mxs::backend::codegen::CodegenContext &) const {
        return nullptr;
    }
    llvm::Value *NilLiteral::codegen(mxs::backend::codegen::CodegenContext &) const {
        return nullptr;
    }
    llvm::Value *BinaryOp::codegen(mxs::backend::codegen::CodegenContext &) const {
        return nullptr;
    }
    llvm::Value *UnaryOp::codegen(mxs::backend::codegen::CodegenContext &) const {
        return nullptr;
    }
    llvm::Value *FunctionCall::codegen(mxs::backend::codegen::CodegenContext &) const {
        return nullptr;
    }
}// namespace mxs::frontend::ast
