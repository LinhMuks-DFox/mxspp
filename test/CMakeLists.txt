# test/CMakeLists.txt — frontend unit tests
# Links `frontend` (parser + ast) and `core`; PEGTL / include dirs / LLVM headers come
# transitively from those targets and the root-level include_directories.
add_executable(frontend_test frontend_test.cpp)
target_include_directories(frontend_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(frontend_test PRIVATE frontend core)

add_test(NAME frontend_test COMMAND frontend_test)
