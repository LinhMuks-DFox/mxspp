# src/CMakeLists.txt (修改后)

# 首先，包含所有子目录来定义所有目标
add_subdirectory(core)
add_subdirectory(frontend)
add_subdirectory(runtime)
add_subdirectory(backend)
add_subdirectory(jit)
add_subdirectory(shell)
add_subdirectory(driver)

# ======================== 【链接逻辑集中于此】 ========================

# frontend 依赖 core 和 pegtl
target_link_libraries(frontend PUBLIC core pegtl)

# backend 依赖 frontend 和 LLVM 库
target_link_libraries(backend PUBLIC frontend ${MXS_LLVM_LIBRARIES})

# jit 依赖 core 和 LLVM 库
target_link_libraries(jit PUBLIC core ${MXS_LLVM_LIBRARIES})

# shell 依赖 jit 和 backend (它会自动获得所有传递过来的依赖)
target_link_libraries(shell PUBLIC jit backend)

# driver 依赖 shell
target_link_libraries(mxs PRIVATE shell)

# =====================================================================