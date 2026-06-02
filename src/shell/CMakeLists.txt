# linenoise: vendored single-file BSD line editor (lib/linenoise) for REPL line editing +
# history. Compiled as C99. The global -stdlib=libc++ (a C++ toolchain flag) is "unused" on a
# .c TU and warns harmlessly (there is no -Werror); silence it on this target only.
add_library(linenoise STATIC ${CMAKE_SOURCE_DIR}/lib/linenoise/linenoise.c)
target_include_directories(linenoise PUBLIC ${CMAKE_SOURCE_DIR}/lib/linenoise)
set_target_properties(linenoise PROPERTIES C_STANDARD 99)
target_compile_options(linenoise PRIVATE -Wno-unused-command-line-argument)

add_library(shell STATIC shell.cpp)
target_include_directories(shell PUBLIC ../../include)

target_link_libraries(shell PUBLIC jit backend linenoise)
