#include "mxspp/frontend/ast.h"
#include "mxspp/core/MXObject.h"

namespace mxs::frontend::ast {
    IntegerLiteral::IntegerLiteral(int64_t value, bool is_static)
        : core::MXObject(is_static), MXASTNode(is_static), value(value) { }
    llvm::Value *
    IntegerLiteral::codegen(mxs::backend::codegen::CodegenContext &ctx) const {
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx.llvmContext),// i64 类型
                                      value,// 你的 int64_t 值
                                      true// 有符号
        );
    }
}