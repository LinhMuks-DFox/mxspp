# Core-type code generator, split per concern (progress14): the public driver (compile_core) in
# codegen.cpp; the shared CoreGen declaration in coregen.h; expression / statement / class-body
# lowering in their own translation units.
add_library(backend STATIC
    codegen.cpp
    codegen_expr.cpp
    codegen_stmt.cpp
    codegen_class.cpp)
target_include_directories(backend PUBLIC ../../include)

# 【新增】链接 frontend 和 LLVM
target_link_libraries(backend PUBLIC frontend ${MXS_LLVM_LIBRARIES})