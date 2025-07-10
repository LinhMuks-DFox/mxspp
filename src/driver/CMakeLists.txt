add_executable(mxspp main.cpp)
# Driver just needs to link to shell. All other dependencies are transitive.
#target_link_libraries(mxspp PRIVATE shell)

# Your install rules can stay here
install(TARGETS core frontend backend jit shell LIBRARY DESTINATION lib)
install(TARGETS mxspp RUNTIME DESTINATION bin)
install(FILES ${BIN_DIR}/runtime.bc DESTINATION lib)