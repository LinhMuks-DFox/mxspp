# src/CMakeLists.txt (修正后)

# 只包含所有子目录来定义所有目标
# 链接逻辑将移至各模块自己的 CMakeLists.txt 文件中
add_subdirectory(core)
add_subdirectory(frontend)
add_subdirectory(runtime)
add_subdirectory(backend)
add_subdirectory(jit)
add_subdirectory(shell)
add_subdirectory(driver)

# ======================== 【删除所有 target_link_libraries】 ========================
# 此处的所有 target_link_libraries(...) 命令都应该被删除