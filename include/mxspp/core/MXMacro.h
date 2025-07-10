#pragma once

#ifdef _WIN32
#ifdef MXS_BUILD_DLL// 这个宏应该在编译动态库时由编译器定义
#define MXS_API __declspec(dllexport)
#else
#define MXS_API __declspec(dllimport)
#endif
#else// For GCC/Clang on Linux/macOS
#define MXS_API __attribute__((visibility("default")))
#endif
