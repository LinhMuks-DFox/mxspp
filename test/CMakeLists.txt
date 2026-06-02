# test/CMakeLists.txt — frontend unit tests
# Links `frontend` (parser + ast) and `core`; PEGTL / include dirs / LLVM headers come
# transitively from those targets and the root-level include_directories.
add_executable(frontend_test frontend_test.cpp)
target_include_directories(frontend_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(frontend_test PRIVATE frontend core)

add_test(NAME frontend_test COMMAND frontend_test)

# Core type tests — the real C++ MXObject subclasses (MXInteger, …). Links `core` for the object
# model and `std` for the std-backing builtins it exercises natively (mxs_str/mxs_repr/mxs_format,
# relocated to src/_std in progress17).
add_executable(core_test core_test.cpp)
target_include_directories(core_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core_test PRIVATE core std)

add_test(NAME core_test COMMAND core_test)

# Red/green language corpus (progress14 / task22): JIT-runs every test/corpus/green/*.mxs and
# checks it produces its .out (rc 0); JIT-runs every test/corpus/red/*.mxs and checks it is
# rejected (rc != 0) with the expected diagnostic substring from its .err. The runner is given the
# freshly-built mxs binary; std/ + core.bc are copied/built beside it. See test/corpus/run_corpus.sh.
add_test(NAME corpus
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/corpus/run_corpus.sh $<TARGET_FILE:mxs>)
