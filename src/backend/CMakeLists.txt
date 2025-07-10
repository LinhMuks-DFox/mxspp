add_library(backend SHARED codegen.cpp)
target_include_directories(backend PUBLIC ../../include)