add_library(jit STATIC jit.cpp)
target_include_directories(jit PUBLIC ../../include)

# jit.cpp subclasses llvm::SectionMemoryManager (NoEHFrameMemoryManager, to skip EH-frame
# registration). LLVM is built -fno-rtti and emits no typeinfo for it, so this TU must also be
# -fno-rtti or the link fails on "typeinfo for llvm::SectionMemoryManager". jit.cpp uses no
# dynamic_cast/typeid, so dropping RTTI here is safe.
set_source_files_properties(jit.cpp PROPERTIES COMPILE_OPTIONS -fno-rtti)

target_link_libraries(jit PUBLIC core ${MXS_LLVM_LIBRARIES})

install(TARGETS jit ARCHIVE DESTINATION lib)