# 设定目标系统为Linux
set(CMAKE_SYSTEM_NAME Linux)

# 强制指定C和C++编译器
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# 使用全局命令为所有后续目标添加编译和链接选项。
# 这是最直接、最权威的方式，确保所有模块都使用libc++。
add_compile_options("-stdlib=libc++")
add_link_options("-stdlib=libc++")