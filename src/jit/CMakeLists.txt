add_library(jit STATIC jit.cpp)
target_include_directories(jit PUBLIC ../../include)

target_link_libraries(jit PUBLIC core ${MXS_LLVM_LIBRARIES})

install(TARGETS jit ARCHIVE DESTINATION lib)