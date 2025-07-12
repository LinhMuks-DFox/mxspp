# CMakeLists.txt (根目录)

cmake_minimum_required(VERSION 3.20)
project(mxs LANGUAGES C CXX)

# === 全局配置 ====================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# --- 输出目录 (保持不变) ---
set(BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BIN_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BIN_DIR})

# 确保找到 ZLIB 和 ZSTD
find_package(ZLIB REQUIRED)
# find_package(ZSTD REQUIRED)
find_package(ZLIB REQUIRED)

# --- 寻找 ZSTD (核心改动) ---
# 尝试通过 PkgConfig 查找 ZSTD
find_package(PkgConfig REQUIRED)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(ZSTD_PC QUIET libzstd) # 使用 libzstd 模块名
    if (ZSTD_PC_FOUND)
        # 如果通过 PkgConfig 找到了，设置 CMake 变量
        set(ZSTD_FOUND TRUE)
        set(ZSTD_INCLUDE_DIRS "${ZSTD_PC_INCLUDE_DIRS}")
        set(ZSTD_LIBRARIES "${ZSTD_PC_LIBRARIES}")
        message(STATUS "Found ZSTD (PkgConfig): ${ZSTD_LIBRARIES}")
    endif()
endif()

if (NOT ZSTD_FOUND)
    # 如果 PkgConfig 没找到，或者 PkgConfig 没安装，尝试传统的 find_library/find_path
    # 这通常是 /usr/lib/x86_64-linux-gnu 和 /usr/include
    find_path(ZSTD_INCLUDE_DIRS NAMES zstd.h HINTS /usr/include /usr/local/include)
    find_library(ZSTD_LIBRARIES NAMES zstd libzstd HINTS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib)

    if (ZSTD_INCLUDE_DIRS AND ZSTD_LIBRARIES)
        set(ZSTD_FOUND TRUE)
        message(STATUS "Found ZSTD (manual): ${ZSTD_LIBRARIES}")
    else()
        message(FATAL_ERROR "ZSTD development files not found. Please install libzstd-dev.")
    endif()
endif()

# 如果 ZSTD_FOUND 仍然是假，强制失败
if (NOT ZSTD_FOUND)
    message(FATAL_ERROR "ZSTD development files are REQUIRED but not found.")
endif()

# --- 寻找 LLVM ---
# 1. 设置本地 LLVM 安装目录的路径
set(LLVM_LOCAL_INSTALL_DIR "${PROJECT_SOURCE_DIR}/lib/llvm")

if(IS_DIRECTORY "${LLVM_LOCAL_INSTALL_DIR}")
    list(PREPEND CMAKE_PREFIX_PATH "${LLVM_LOCAL_INSTALL_DIR}")
    message(STATUS "Found local LLVM. Prioritizing path: ${LLVM_LOCAL_INSTALL_DIR}")
endif()

find_package(LLVM REQUIRED CONFIG)

if(LLVM_FOUND)
    message(STATUS "Found LLVM version ${LLVM_VERSION_STRING}")
    message(STATUS "Using LLVMConfig.cmake from: ${LLVM_CMAKE_DIR}")
else()
    message(FATAL_ERROR "LLVM not found. Please ensure it is installed locally in './lib/llvm' or available in the system path.")
endif()

message(STATUS "Using LLVM from: ${LLVM_DIR}")  # LLVM_DIR 由 find_package 设置
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# 把需要的库名收集到变量
llvm_map_components_to_libnames(MXS_LLVM_LIBRARIES
    Core Support ExecutionEngine OrcJIT OrcTargetProcess)

message(STATUS "LLVM version: ${LLVM_PACKAGE_VERSION}")
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# 3. 使用 LLVM 提供的辅助函数，将所有需要的组件库名存入一个变量
llvm_map_components_to_libnames(MXS_LLVM_LIBRARIES
    Core Support ExecutionEngine OrcJIT OrcTargetProcess
)

# --- 寻找 PEGTL (保持不变, 但路径指向新的统一目录名) ---
set(PEGTL_INSTALL_DIR "${PROJECT_SOURCE_DIR}/lib/pegtl") # 修改为 lib/pegtl
if(NOT EXISTS "${PEGTL_INSTALL_DIR}/include/tao/pegtl.hpp")
    message(FATAL_ERROR "PEGTL not found at path: ${PEGTL_INSTALL_DIR}")
endif()
add_library(pegtl INTERFACE)
target_include_directories(pegtl INTERFACE "${PEGTL_INSTALL_DIR}/include")
message(STATUS "Found PEGTL headers at ${PEGTL_INSTALL_DIR}/include")

# --- LTO/IPO 支持 (保持不变) ---
include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT ipo_err)
if(lto_supported)
    message(STATUS "LTO/IPO is supported, enabling by default.")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(WARNING "LTO/IPO not supported: ${ipo_err}")
endif()

# --- 安装规则 (保持不变) ---
install(DIRECTORY include/ DESTINATION include)

# ======================================================================
add_subdirectory(src)
# ======================================================================