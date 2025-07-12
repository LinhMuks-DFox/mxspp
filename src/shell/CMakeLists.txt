add_library(shell SHARED shell.cpp)
target_include_directories(shell PUBLIC ../../include)

# 【修正】取消此行的注释来链接 shell 的直接依赖
target_link_libraries(shell PUBLIC jit backend)