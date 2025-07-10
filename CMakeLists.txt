# CMakeLists.txt (根目录)

cmake_minimum_required(VERSION 3.20)
project(mxspp LANGUAGES CXX)

# === 全局配置 ====================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- 强制 Clang 使用 libc++ (保持不变) ---
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(STATUS "Clang compiler detected. Forcing use of libc++.")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
endif()

# --- 输出目录 (保持不变) ---
#set(BIN_DIR ${PROJECT_SOURCE_DIR}/bin) # 改用 CMAKE_BINARY_DIR 更标准
set(BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BIN_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BIN_DIR})
find_package(ZLIB REQUIRED)
find_package(zstd REQUIRED)
# --- 寻找 LLVM (保持不变, 写得很好) ---
set(LLVM_INSTALL_DIR "${PROJECT_SOURCE_DIR}/lib/llvm")
if(NOT EXISTS "${LLVM_INSTALL_DIR}/lib/cmake/llvm/LLVMConfig.cmake")
    message(FATAL_ERROR "LLVM not found at path: ${LLVM_INSTALL_DIR}")
endif()
set(LLVM_DIR "${LLVM_INSTALL_DIR}/lib/cmake/llvm")
find_package(LLVM REQUIRED CONFIG
        COMPONENTS Core ExecutionEngine OrcJIT Support OrcTargetProcess
)
message(STATUS "LLVM version: ${LLVM_PACKAGE_VERSION}")

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# 3. 使用 LLVM 提供的辅助函数，将所有需要的组件库名存入一个变量
llvm_map_components_to_libnames(MXS_LLVM_LIBRARIES
        Core Support ExecutionEngine OrcJIT OrcTargetProcess
)
# --- 寻找 PEGTL (保持不变, 写得很好) ---
set(PEGTL_INSTALL_DIR "${PROJECT_SOURCE_DIR}/lib/PEGTL-3.2.7")
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
# 【核心改动】
# 移除所有 add_library, add_executable, target_link_libraries 调用
# 用下面这一行代替，将构建任务分派到 src 目录
add_subdirectory(src)
# ======================================================================