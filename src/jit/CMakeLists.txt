add_library(jit SHARED jit.cpp)
target_include_directories(jit PUBLIC ../../include)
install(TARGETS jit LIBRARY DESTINATION lib)