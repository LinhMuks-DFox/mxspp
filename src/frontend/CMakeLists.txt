add_library(frontend SHARED ast.cpp)
#target_include_directories(frontend PUBLIC ../../include)
# Frontend's direct dependencies are core and pegtl. It does NOT need LLVM.
target_link_libraries(frontend PUBLIC core pegtl)