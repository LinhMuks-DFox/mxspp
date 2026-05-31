# macOS (Apple Silicon) native toolchain for mxspp — uses Homebrew's llvm@20 (20.1.8).
#
# Why Homebrew llvm@20 instead of the LLVM.org macOS prebuilt vendored in lib/llvm:
# the official 20.1.8 macOS release static libs were built with type-aware allocation
# (typed operator new) enabled. Their startup static initializers call typed operator new
# before libc++'s guard initializer has run, so the process aborts at startup
# ("typed operator new being invoked before its static initializer"), and clang 20.1.8 has
# no flag (-fno-typed-cxx-new-delete is clang 21+) to disable it. Homebrew's llvm@20 is a
# sane build without that feature, and is the SAME version (20.1.8) the codebase targets,
# so the LLVM C++ API, the bitcode version (core.bc/runtime.bc), and the JIT all match.
#
# Prerequisite:  brew install llvm@20
# Native build — do NOT set CMAKE_SYSTEM_NAME.
# Used via:  cmake -G Ninja -S . -B build-macos -DCMAKE_TOOLCHAIN_FILE=$PWD/toolchain.macos.cmake

set(_brew_llvm "/opt/homebrew/opt/llvm@20")

set(CMAKE_C_COMPILER   "${_brew_llvm}/bin/clang")
set(CMAKE_CXX_COMPILER "${_brew_llvm}/bin/clang++")

# Resolve LLVM (and llvm-link for the .bc step) from Homebrew directly, overriding the
# lib/llvm path the top-level CMakeLists prepends to CMAKE_PREFIX_PATH.
set(LLVM_DIR "${_brew_llvm}/lib/cmake/llvm" CACHE PATH "Homebrew llvm@20 CMake package" FORCE)
set(MXS_LLVM_LINK "${_brew_llvm}/bin/llvm-link" CACHE FILEPATH "llvm-link from Homebrew llvm@20" FORCE)

# libc++ everywhere (matches the Linux toolchain and the .bc compile flags). Homebrew keeps
# libc++/libc++abi under lib/c++; -L finds them at link time and rpath at runtime.
add_compile_options(-stdlib=libc++)
add_link_options(-stdlib=libc++ "-L${_brew_llvm}/lib/c++" "-Wl,-rpath,${_brew_llvm}/lib/c++")
