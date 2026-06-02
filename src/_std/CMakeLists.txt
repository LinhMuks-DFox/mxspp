# src/_std/CMakeLists.txt — the C/C++ backends for the importable std.* modules (progress17).
# These are the @@foreign leaves bound by std/*.mxs. They build into a standalone std.bc the JIT
# links alongside core.bc (mirroring src/core/CMakeLists.txt's bitcode machinery). For the JIT
# path the static lib is NOT required (symbols resolve from std.bc) — but the unit tests link
# these symbols natively (core_test exercises mxs_str/mxs_repr/mxs_format), so a `std` static lib
# is built for that native-link path.

# 定义库 std — the native static lib (used by the unit tests; not on the JIT path).
add_library(std STATIC
        io.cpp
        string.cpp
        builtins.cpp
        time.cpp
        types.cpp
        system.cpp
)
# PUBLIC include dir so any target linking `std` inherits it; link core for the MXObject types.
target_include_directories(std PUBLIC
        ../../include
)
target_link_libraries(std PUBLIC core)

# --- std.bc : the std-library backends compiled to LLVM bitcode ---
# Re-run find_program + the APPLE sysroot block locally: these are non-cache vars and are NOT
# visible across directories. MXS_BC_CXX (a cache var) and BIN_DIR (a root-scope var) ARE visible.
find_program(MXS_LLVM_LINK NAMES llvm-link llvm-link-20 llvm-link-19 llvm-link-18 REQUIRED)
# macOS: these bitcode commands are raw clang invocations that bypass CMake's automatic
# -isysroot injection, so the vendored libc++ headers can't find the SDK's <wchar.h>/mbstate_t.
# Pass the SDK sysroot explicitly on Apple (the list stays empty elsewhere, e.g. Linux).
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
set(STD_BC_SOURCES
        io.cpp string.cpp builtins.cpp time.cpp types.cpp system.cpp)
# Bitcode must rebuild when any core OR _std header changes too (vtable layouts / inline virtuals
# live in the headers); depending only on the .cpp leaves stale, ABI-incompatible bitcode.
file(GLOB STD_BC_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include/mxspp/core/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include/mxspp/_std/*.h)
set(STD_BC_PARTS "")
foreach(src IN LISTS STD_BC_SOURCES)
    get_filename_component(nm ${src} NAME_WE)
    set(out ${BIN_DIR}/std_bc_${nm}.bc)
    add_custom_command(OUTPUT ${out}
            COMMAND ${MXS_BC_CXX} -emit-llvm -c
                    ${CMAKE_CURRENT_SOURCE_DIR}/${src} -o ${out}
                    -std=c++23 -stdlib=libc++ -fPIC -mno-outline-atomics
                    ${MXS_BC_SYSROOT}
                    -I ${CMAKE_CURRENT_SOURCE_DIR}/../../include
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${STD_BC_HEADERS}
            COMMENT "std -> bitcode: ${src}")
    list(APPEND STD_BC_PARTS ${out})
endforeach()
add_custom_command(OUTPUT ${BIN_DIR}/std.bc
        COMMAND ${MXS_LLVM_LINK} ${STD_BC_PARTS} -o ${BIN_DIR}/std.bc
        DEPENDS ${STD_BC_PARTS}
        COMMENT "Linking std bitcode -> std.bc")
add_custom_target(std-bc ALL DEPENDS ${BIN_DIR}/std.bc)
