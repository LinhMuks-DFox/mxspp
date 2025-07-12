# 定义库 mxs-core
add_library(core SHARED
        MXBoolean.cpp
        MXError.cpp
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
