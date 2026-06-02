add_executable(mxs main.cpp)
# Driver just needs to link to shell. All other dependencies are transitive.
#target_link_libraries(mxspp PRIVATE shell)
target_link_libraries(mxs PRIVATE shell frontend)

# Copy the std/ modules next to the binary (progress13 D2). The import resolver searches the
# executable's directory (exe-dir/std/<path>.mxs), so this makes `import std.io;` work even when
# the CWD is not the repo root. Dev runs from the repo root resolve ./std/ first either way.
add_custom_command(TARGET mxs POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/std ${BIN_DIR}/std
        COMMENT "Copying std/ modules next to the mxs binary")

# Your install rules can stay here
install(TARGETS core frontend backend jit shell ARCHIVE DESTINATION lib)
install(TARGETS mxs RUNTIME DESTINATION bin)