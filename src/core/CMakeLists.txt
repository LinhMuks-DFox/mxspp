# 定义库 mxs-core
add_library(core STATIC
        MXArrayList.cpp
        MXBoolean.cpp
        MXError.cpp
        MXFloat.cpp
        MXInteger.cpp
        MXLeftValue.cpp
        MXMacro.cpp
        MXNil.cpp
        MXNumeric.cpp
        MXObject.cpp
        MXPopulationManager.cpp
        MXString.cpp
        MXType.cpp
        builtin_func.cpp
        MXDynamicTypeInfoManager.cpp
)

# 为此目标设置 include 目录
# PUBLIC: 意味着任何链接到 core 的目标，都会自动继承这个 include 目录
target_include_directories(core PUBLIC
        ../../include
)
target_link_libraries(core PUBLIC ${MXS_LLVM_LIBRARIES})

# --- core.bc : the core object types compiled to LLVM bitcode (progress09 D3/D6) ---
# Linked into the JIT module alongside runtime.bc + user IR so the mxs_* type/lvalue/rc ABI
# symbols resolve and LLVM can inline across the mxs/lib boundary. These sources are LLVM-free
# (no project-LLVM headers), so they lower to plain bitcode; we llvm-link the parts together.
find_program(MXS_LLVM_LINK NAMES llvm-link llvm-link-20 llvm-link-19 llvm-link-18 REQUIRED)
set(CORE_BC_SOURCES
        MXObject.cpp MXPopulationManager.cpp MXError.cpp
        MXInteger.cpp MXString.cpp MXFloat.cpp MXBoolean.cpp MXNil.cpp
        MXArrayList.cpp MXLeftValue.cpp)
set(CORE_BC_PARTS "")
foreach(src IN LISTS CORE_BC_SOURCES)
    get_filename_component(nm ${src} NAME_WE)
    set(out ${BIN_DIR}/core_bc_${nm}.bc)
    add_custom_command(OUTPUT ${out}
            COMMAND ${CMAKE_CXX_COMPILER} -emit-llvm -c
                    ${CMAKE_CURRENT_SOURCE_DIR}/${src} -o ${out}
                    -std=c++23 -stdlib=libc++ -fPIC
                    -I ${CMAKE_CURRENT_SOURCE_DIR}/../../include
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src}
            COMMENT "core -> bitcode: ${src}")
    list(APPEND CORE_BC_PARTS ${out})
endforeach()
add_custom_command(OUTPUT ${BIN_DIR}/core.bc
        COMMAND ${MXS_LLVM_LINK} ${CORE_BC_PARTS} -o ${BIN_DIR}/core.bc
        DEPENDS ${CORE_BC_PARTS}
        COMMENT "Linking core bitcode -> core.bc")
add_custom_target(core-bc ALL DEPENDS ${BIN_DIR}/core.bc)
