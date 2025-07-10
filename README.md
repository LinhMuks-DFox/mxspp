# mxspp

**mxspp** 是一个现代化的、动态类型的编程语言，其设计核心是通过即时编译（Just-In-Time, JIT）技术提供卓越的性能，同时保持语言本身的高度灵活性和表达能力。

本项目的目标是构建一个完整、模块化、可扩展的编译器和运行时环境，其技术栈主要包括 **C++23**、**LLVM 框架**和 **PEGTL 解析库**。

## ✨ 核心特性 (Core Features)

  * **高性能 JIT 编译**: 通过 LLVM 将脚本直接编译为针对目标平台优化的本地机器码。此外，通过将运行时库以 LLVM IR (Bitcode) 的形式与用户代码一同优化，实现了强大的跨模块链接时优化（LTO），特别是函数内联，以最大化性能。
  * **现代 C++23 实现**: 整个编译器和核心库采用现代 C++23 编写，充分利用其强类型系统和面向对象特性来保证代码的健壮性和可维护性。
  * **精巧的模块化架构**: 项目被清晰地分解为一组高内聚、低耦合的独立组件（`core`, `frontend`, `backend`, `jit`, `shell`），每个组件职责单一，确保了项目的可扩展性和可维护性。
  * **强大的类型系统**:
      * **万物皆对象**: 从整数到用户自定义类型，一切皆为一等公民对象。
      * **自动内存管理**: 基于 RAII 和智能指针的内存管理模型，确保了代码的内存安全。
      * **泛型与 POD**: 支持泛型编程以实现代码复用，并引入了 `@@POD` 注解，允许编译器对数据布局进行极致优化，实现与 C/C++ 的零成本互操作。
  * **优雅的外部函数接口 (FFI)**:
      * 通过独特的 `@@foreign` 注解，可以清晰、直接地将 MxScript 函数链接到外部 C++ 共享库中的符号。
      * 支持固定参数和可变参数两种分发模式，设计精巧且高效。
  * **健壮的错误处理**:
      * 采用基于值的统一错误处理模型，所有可恢复和不可恢复的错误都通过 `Error` 对象表示。
      * 函数签名必须显式声明可能返回错误，结合 `match` 语句，强制开发者处理所有可能失败的路径，杜绝未处理的异常。

## 🚀 开始使用 (Getting Started)

### 1\. 环境依赖 (Prerequisites)

在开始之前，请确保您的系统环境满足以下要求。项目提供的 `download_dep.py` 脚本会自动检查这些依赖：

  * **Clang/Clang++**: 版本 **\>= 20**。
  * **Python**: 版本 **3.10+**。
  * **libc++**: 在 Linux 系统上需要安装 `libc++`。
  * **zlib & zstd**: LLVM 的传递性依赖，需要安装它们的开发包（例如，在 Debian/Ubuntu 上使用 `sudo apt-get install zlib1g-dev libzstd-dev`）。

### 2\. 构建项目 (Building the Project)

构建过程被设计为简单的两步，由 Python 脚本驱动。

**第一步：下载依赖项**

在项目根目录下运行 `download_dep.py` 脚本。它会自动下载项目所需的 LLVM 预编译版本和 PEGTL 解析库，并将它们放置在 `lib/` 目录下。

```bash
python3 download_dep.py
```

**第二步：编译项目**

使用 `rebuild.py` 脚本进行编译。为了确保一个干净的、没有缓存问题的构建，强烈建议首次构建时使用 `--clean` 选项。

```bash
# 推荐首次构建或在修改 CMakeLists.txt 后使用
python3 rebuild.py --clean

# 后续进行增量构建
python3 rebuild.py
```

编译成功后，所有的库 (`.so` 文件)、`runtime.bc` 和最终的可执行文件 `mxspp` 都会被生成在 `build/bin/` 目录下。

## 🐳 Docker 开发环境

为了简化环境配置，项目提供了一个基于 Docker 的一站式开发环境。只需安装 Docker 和 Docker Compose，然后在项目根目录下运行：

```bash
docker-compose up -d
docker-compose exec mxspp-dev /bin/bash
```

此命令将启动一个包含所有依赖项的容器，并将您带入其中。您可以在这个隔离的环境中进行开发、构建和测试，无需在主机上安装任何工具链。

## 🤝 贡献代码 (Contributing)

我们欢迎任何形式的贡献！为了保证项目代码的高质量和一致性，请在提交代码前仔细阅读我们的开发规范。

  * **架构设计**: 请参考 [Architecture.md (Source 25)] 来了解项目的高层设计。
  * **运行时开发**: 所有对 `src/core` 和 `src/runtime` 的贡献，必须严格遵守 [Runtime Development Rules (Source 29)] 中定义的内存管理、RTTI 和函数设计模式。
  * **代码风格**: 本项目使用 `Clang-Format` 强制执行统一的代码风格。根目录下的 `.clang-format` 文件定义了所有规则。请在提交前运行 `clang-format -i <file>`。

