add_library(shell SHARED shell.cpp)
target_include_directories(shell PUBLIC ../../include)
# Shell is the orchestrator. It links to the modules it uses directly.
# It will inherit the LLVM dependency transitively from backend and jit.
#target_link_libraries(shell PUBLIC jit backend)