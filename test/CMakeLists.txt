# test/CMakeLists.txt — frontend unit tests
# Links `frontend` (parser + ast) and `core`; PEGTL / include dirs / LLVM headers come
# transitively from those targets and the root-level include_directories.
add_executable(frontend_test frontend_test.cpp)
target_include_directories(frontend_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(frontend_test PRIVATE frontend core)

add_test(NAME frontend_test COMMAND frontend_test)

# Runtime object-model tests — pure C++ (no LLVM); links the compiled runtime objects.
add_executable(runtime_test runtime_test.cpp $<TARGET_OBJECTS:runtime_obj>)
target_include_directories(runtime_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME runtime_test COMMAND runtime_test)

# Core type tests — the real C++ MXObject subclasses (MXInteger, …). Links `core`.
add_executable(core_test core_test.cpp)
target_include_directories(core_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core_test PRIVATE core)

add_test(NAME core_test COMMAND core_test)
