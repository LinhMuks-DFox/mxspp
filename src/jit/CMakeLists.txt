add_library(jit SHARED jit.cpp)
target_include_directories(jit PUBLIC ../../include)

target_link_libraries(jit PUBLIC core ${MXS_LLVM_LIBRARIES})

install(TARGETS jit LIBRARY DESTINATION lib)