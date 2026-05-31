# 这个模块的目标是生成 runtime.bc 文件，供 JIT 使用

# 1. 编译 runtime.cpp，它需要 core 的 C++ 类定义
#    我们先把它编译成一个普通对象，以确保编译通过并处理依赖
add_library(runtime_obj OBJECT runtime.cpp)
target_include_directories(runtime_obj PRIVATE ../../include)
#target_link_libraries(runtime_obj PRIVATE core)

# macOS: this raw clang invocation bypasses CMake's automatic -isysroot injection, so the
# vendored libc++ headers can't find the SDK's <wchar.h>/mbstate_t. Pass the SDK sysroot
# explicitly on Apple (empty elsewhere, e.g. Linux).
set(MXS_BC_SYSROOT)
if(APPLE)
    set(_mxs_sdk "${CMAKE_OSX_SYSROOT}")   # empty under Command Line Tools (no full Xcode)
    if(NOT _mxs_sdk)
        execute_process(COMMAND xcrun --show-sdk-path
                        OUTPUT_VARIABLE _mxs_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    if(_mxs_sdk)
        set(MXS_BC_SYSROOT -isysroot ${_mxs_sdk})
    endif()
endif()

# 2. 定义一个自定义命令，用 clang 将 runtime.cpp 编译成 LLVM bitcode
add_custom_command(
        OUTPUT ${BIN_DIR}/runtime.bc
        COMMAND ${CMAKE_CXX_COMPILER}
        -emit-llvm
        -c ${CMAKE_CURRENT_SOURCE_DIR}/runtime.cpp
        -o ${BIN_DIR}/runtime.bc
        -std=c++23
        -stdlib=libc++
        ${MXS_BC_SYSROOT}
        # 【修正】将 DEPENDS 移到 COMMAND 之外，作为独立的参数
        DEPENDS runtime.cpp $<TARGET_OBJECTS:runtime_obj>
        COMMENT "Compiling runtime.cpp to LLVM bitcode (runtime.bc)..."
)

# 3. 创建一个自定义目标来触发上述命令
add_custom_target(runtime-bc ALL DEPENDS ${BIN_DIR}/runtime.bc)