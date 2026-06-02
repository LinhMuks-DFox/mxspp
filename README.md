# MXScript (mxs)

**mxs** 是一门动态类型脚本语言，通过 **LLVM JIT** 把脚本直接编译为本地机器码运行。**mxspp** 是它的
**C++23 实现**——就像 CPython 之于 Python，mxspp 之于 mxs。技术栈：**C++23 + LLVM + PEGTL**。

> **当前状态（早期可运行子集）**：语言已经能跑真实程序——算术、字符串、列表、控制流、函数、`match`、
> 单类 OOP（含 ARC 自动内存管理）、以及一个 *import 限定* 的标准库。但仍处于早期阶段，许多设计中的
> 特性尚未实现。**实际能跑什么、不能跑什么，以下文档是权威：**
> - **[docs/tutorial.zh.md](docs/tutorial.zh.md)**（中文） / **[docs/tutorial.md](docs/tutorial.md)**（English）
>   — 面向使用者的上手教程（每个示例都验证过可运行）。
> - **[docs/syntax.md](docs/syntax.md)** — 权威语法参考；每个构造都标注了 `[parses + runs]`（可运行）
>   或 `[parses only]`（仅能解析、还不能运行）。

## ✨ 特性

**已实现并可运行：**
- **JIT 编译**：经 LLVM 将脚本编译为本地代码；运行时库以 LLVM Bitcode（`core.bc`）形式与用户代码
  一起优化，支持跨模块内联等链接时优化。
- **万物皆对象**：整数、浮点、字符串、布尔、`nil`、列表、用户自定义类都是一等对象。
- **ARC 自动内存管理**：基于引用计数，对象的析构函数（`~ClassName`）在最后一个引用消失时确定性触发。
- **单类 OOP**：字段、构造函数、方法（vtable 派发）、运算符重载、析构函数。
- **import 限定的标准库**：没有 `import` 就没有任何标准库名字在作用域里（连 `println` 也要 import）。
  三种形式：`import std.io;`（限定）/ `import std.io as o;`（别名）/ `import std.io.{println};`（选择性）。
- **基于值的错误模型**：错误是 `Error` 对象（不是异常），用 `match` 的类型绑定分支处理。
- **`@@foreign` FFI**：以注解把 mxs 函数直接绑定到 C-ABI 运行时符号，支持定长与可变参数两种分发。
- **绑定语义**：`let` 默认不可变、`let mut` 可变；不可变重赋值与同作用域重声明都是**编译期错误**。

**设计中 / 尚未实现**（详见教程的"未实现"一节）：
- **继承**、**泛型**（`List<T>` 目前仅解析）、`interface` / `type` / `enum`（仅解析）。
- **lambda**、关键字参数 / 默认参数、错误传播 `?`、`assert` / `defer`（仅解析）。
- 强制"函数签名声明可能的错误返回"、f-string、文件 IO、控制台输入、显式类型转换。
- 设计上**有意不提供 `while`**：循环用 `until (c)`（直到 c 成立）/ `do … until (c)` / `loop` / `for … in`。

## 🚀 快速上手

```mxs
# hello.mxs — 标准库是 import 限定的，println 也需要先 import
import std.io.{println};

func main() -> int {
    println("Hello, World!");
    return 0;
}
```

```bash
$ mxs run-core hello.mxs
Hello, World!
```

完整的语言用法见 **[docs/tutorial.zh.md](docs/tutorial.zh.md)**。

## 🛠️ 构建

需要 **Clang/Clang++ >= 20**、**libc++**（Linux）、**Python 3.10+**、**Ninja**，以及 LLVM 的传递依赖
`zlib` / `zstd`（例如 Debian/Ubuntu：`sudo apt-get install zlib1g-dev libzstd-dev`）。

```bash
# 1) 一次性：把 LLVM + PEGTL 拉取到 lib/
python3 project_init.py

# 2) 首次构建（或改了 CMakeLists 之后）
python3 rebuild.py --clean

# 后续增量构建
python3 rebuild.py
```

产物在 `build/bin/`：可执行文件 **`mxs`**、运行时 bitcode **`core.bc`**，以及标准库模块（`std/` 会被
拷贝到二进制旁边，供 `import` 解析）。

## ⌨️ 命令行用法

```bash
mxs run-core   <file.mxs>   # JIT 编译并运行 main()
mxs check      <file.mxs>   # 仅解析的 lint（语法 + import 错误），通过则打印 "ok"
mxs --dump-ast <file.mxs>   # 解析并打印 AST
mxs            # 或 mxs shell —— 交互式 REPL
```

每个程序的入口是 `func main() -> int`，其返回值即进程退出码。可用 `example/examples/*.mxs` 做冒烟测试，
例如 `mxs run-core example/examples/hello_world.mxs`。

## 📚 文档

- **[docs/tutorial.zh.md](docs/tutorial.zh.md)**（中文）/ **[docs/tutorial.md](docs/tutorial.md)**（English）
  — 使用教程（按主题、含可运行示例、附"未实现"清单）。
- **[docs/syntax.md](docs/syntax.md)** — 权威语法参考 + 与原始 `syntax.ebnf` 的差异分析。
- **[docs/object_model.md](docs/object_model.md)** — 对象模型与 ARC 协议。
- **[docs/ffi.md](docs/ffi.md)** — `@@foreign` FFI 契约。
- **[docs/type_system.md](docs/type_system.md)** — 类型系统与静/动态混合派发设计。
- **[docs/Architecture.md](docs/Architecture.md)** — 编译流水线（core → frontend → backend → jit）。

## 🤝 贡献

- **架构**：先读 [docs/Architecture.md](docs/Architecture.md)。
- **运行时开发**：对 `src/core` 的改动必须遵守 [docs/develop_rule.md](docs/develop_rule.md) 里的内存管理、
  RTTI 与函数设计规范。
- **测试**：单元测试在 `test/`（`ctest --test-dir build`），其中包含 red/green 语料库
  （`test/corpus/`，见其 README）；`example/examples/*.mxs` 作为集成冒烟测试。
- **代码风格**：根目录 `.clang-format`（WebKit 基准、4 空格、90 列）。提交前运行 `clang-format -i <file>`
  或 `python3 before_commit.py --staged`。
