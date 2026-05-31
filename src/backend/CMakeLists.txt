add_library(backend STATIC codegen.cpp)
target_include_directories(backend PUBLIC ../../include)

# 【新增】链接 frontend 和 LLVM
target_link_libraries(backend PUBLIC frontend ${MXS_LLVM_LIBRARIES})