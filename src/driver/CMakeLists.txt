add_executable(mxs main.cpp)
# Driver just needs to link to shell. All other dependencies are transitive.
#target_link_libraries(mxspp PRIVATE shell)
target_link_libraries(mxs PRIVATE shell)

# Your install rules can stay here
install(TARGETS core frontend backend jit shell LIBRARY DESTINATION lib)
install(TARGETS mxs RUNTIME DESTINATION bin)
install(FILES ${BIN_DIR}/runtime.bc DESTINATION lib)