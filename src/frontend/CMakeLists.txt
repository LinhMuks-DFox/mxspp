add_library(frontend SHARED ast.cpp)
target_include_directories(frontend PUBLIC ../../include)

# 【修正】只链接直接依赖。c++ 和 LLVM 将从 core 传递过来
target_link_libraries(frontend PUBLIC core pegtl)