# MXScript 使用教程（当前版本）

这是一份面向使用者的实操指南，只讲**当前 `mxs` 二进制能真正跑起来的那部分语言**。所有标注"✅ 可用"的
内容都已用当前的 `mxs` 实际执行验证过；只能解析、或者还没实现的特性，统一收集并明确标注在
[§14 尚未实现](#14-尚未实现)。

- **权威语法参考**是 [`docs/syntax.md`](./syntax.md)（每个构造都标了 `[parses + runs]` 可运行 /
  `[parses only]` 仅解析）。本教程是它的*任务导向*的姊妹篇。
- 对象模型 / ARC 见 [`docs/object_model.md`](./object_model.md)；FFI 契约见 [`docs/ffi.md`](./ffi.md)；
  类型与派发设计见 [`docs/type_system.md`](./type_system.md)。
- 英文版：[`docs/tutorial.md`](./tutorial.md)。

> 状态：语言已经能跑真实程序（算术、字符串、列表、控制流、函数、`match`、含 ARC 的单类 OOP、import
> 限定的标准库），但仍是早期版本——边界见 §14。

---

## 1. 构建与运行

```bash
python3 project_init.py        # 一次性：把 LLVM + PEGTL 拉取到 lib/
python3 rebuild.py --clean     # 首次构建（或改了 CMake 之后）
python3 rebuild.py             # 之后的增量构建
```

产物是 `build/bin/` 下的 `mxs` 可执行文件和运行时 bitcode `core.bc`。

命令行有四种模式：

```bash
mxs run-core <file.mxs>     # JIT 编译并运行 main()
mxs check    <file.mxs>     # 仅解析的 lint（语法 + import 错误），通过打印 "ok"
mxs --dump-ast <file.mxs>   # 解析并打印 AST
mxs                         # 或 mxs shell —— 交互式 REPL
```

每个程序的入口都是 `func main() -> int`，其返回的整数即进程退出码。

---

## 2. Hello, world —— 以及 import 规则

```mxs
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

**标准库是 import 限定的。** 没有 `import`，任何标准库名字（连 `println` 都算）都不在作用域里。一个不
import 就调用 `println` 的程序会被拒绝：

```
core-codegen: call to unknown function 'println'
```

三种 import 形式见 [§11 模块与导入](#11-模块与导入)。

---

## 3. 值与变量

mxs 是**动态类型**（值在运行期携带类型）但**静态作用域**的语言。变量用 `let` 引入，**默认不可变**：

```mxs
import std.io.{println};

func main() -> int {
    let a = 10;          # 不可变
    let mut b = 1;       # 可变 —— 可重新赋值
    b = b + a;
    b += 5;              # 复合赋值对 `let mut` 有效
    println(b);          # 16

    let name = "mxs";    # 字符串
    let flag = true;     # 布尔
    let nothing = nil;   # nil
    let pi = 3.14;       # 浮点
    println(name);
    return 0;
}
```

有三条绑定规则在**编译期**强制执行：

| 你写的 | 结果 |
|---|---|
| `let a = 1; a = 2;` | 错误：`cannot assign to immutable binding 'a'` |
| `let a = 1; let mut a = 2;`（同一作用域） | 错误：`redeclaration of 'a' in the same scope` |
| 嵌套块里重用同名 | **允许**——内层绑定遮蔽外层 |

嵌套块里的遮蔽是合法的，块退出后外层绑定恢复：

```mxs
import std.io.{println};

func main() -> int {
    let a = 1;
    if true {
        let a = 2;       # 在这个块内遮蔽外层的 a
        println(a);      # 2
    }
    println(a);          # 1
    return 0;
}
```

> 可以写**类型标注**（`let a: int = 1;`），但目前它只是说明性的——codegen 是动态类型的，不会检查或强制它。

---

## 4. 运算符

```mxs
import std.io.{println};

func main() -> int {
    println(2 + 3 * 4);    # 14   （* 比 + 优先）
    println(2 ** 10);      # 1024 （幂运算）
    println(2 ** 3 ** 2);  # 64   （** 在本版本是左结合：(2**3)**2）
    println(7 / 2);        # 3    （整数除法）
    println(7 % 3);        # 1
    println(-5 + 3);       # -2
    println(3 < 5 && 5 >= 5);  # true   （&&、||、! 短路）
    println(!false);       # true
    println("a" + "b");    # ab   （+ 拼接字符串）
    return 0;
}
```

比较（`< <= > >= == !=`）和逻辑（`&& || !`）产出布尔值。算术是动态的：`int op int` 保持精确，有浮点
操作数则提升为浮点，`字符串 + 字符串` 做拼接。

> 注意本版本 `**` 是**左**结合（数学惯例是右结合）。记录在 `docs/syntax.md` §2。

---

## 5. 字符串与列表

列表用 `[...]` 书写，用 `[i]` 取下标，用 `for x in xs` 遍历：

```mxs
import std.io.{println, print};

func main() -> int {
    let xs = [10, 20, 30];
    xs.append(40);          # 方法，修改列表内容
    println(xs.len());      # 4
    println(xs.get(1));     # 20
    println(xs[2]);         # 30  （下标）
    for v in xs { print(v); print(" "); }   # 10 20 30 40
    println("");

    println("hello".len()); # 5  （字符串也有 .len()）
    return 0;
}
```

**容器和字符串操作是接收者上的方法，不是自由函数**——`xs.append(v)`、`xs.len()`、`xs.get(i)`、
`"s".len()`。不存在全局的 `len(...)` 或 `append(...)`，调用它们会报错（`call to unknown function 'len'`）。

> `xs.append(v)` **不需要** `let mut`——它修改的是列表的*内容*，不是绑定本身。

---

## 6. 控制流

mxs 有 `if`、`for … in`、`loop`、`until`、`do … until`。**没有 `while`——这是有意的设计。** 循环构造是
`until`，读作*"一直执行**直到**条件成立"*（退出条件正向陈述）。`until (c)` 在 **`c` 为假时**反复执行循环
体，`c` 一旦变真就停止——这正符合"直到 X 满足"的直觉。

```mxs
import std.io.{println};

func main() -> int {
    # if / else if / else —— 条件是一个裸表达式（不需要括号）
    let x = 5;
    if x > 10 { println("big"); }
    else if x > 3 { println("mid"); }   # 输出：mid
    else { println("small"); }

    # for 遍历左闭右开的整数区间，以及遍历列表
    for i in 0..3 { println(i); }       # 0 1 2
    for v in [7, 8] { println(v); }     # 7 8

    # until：前测。一直执行直到条件成立。
    let mut n = 0;
    until (n >= 3) { println(n); n = n + 1; }   # 0 1 2

    # do … until：后测（循环体至少执行一次）
    let mut m = 0;
    do { println(m); m = m + 1; } until (m >= 2);   # 0 1

    # loop + break / continue
    let mut k = 0;
    loop {
        if k >= 2 { break; }
        println(k);                      # 0 1
        k = k + 1;
    }
    return 0;
}
```

循环一览：

| 形式 | 含义 |
|---|---|
| `for v in lo..hi { … }` | 遍历左闭右开整数区间 `[lo, hi)` |
| `for v in xs { … }` | 按元素遍历列表（或字符串） |
| `until (c) { … }` | 前测：执行循环体直到 `c` 成立 |
| `do { … } until (c);` | 后测：先执行一次，再重复直到 `c` 成立 |
| `loop { … }` | 无限循环；用 `break` 退出 |

`break` 和 `continue` 在任何循环里都可用。`for` 的循环变量默认不可变（`for v`）；要在循环体里改它就写
`for mut v`。

---

## 7. 函数

```mxs
import std.io.{println};

func add(a: int, b: int) -> int { return a + b; }

func fib(n: int) -> int {
    if n < 2 { return n; }
    return fib(n - 1) + fib(n - 2);     # 递归
}

func main() -> int {
    println(add(2, 3));   # 5
    println(fib(10));     # 55
    return 0;
}
```

**可变参数函数**用末尾的 `...rest` 参数把多余实参收集成一个列表：

```mxs
import std.io.{println};

func count(first: int, ...rest: any) -> int {
    return 1 + rest.len();   # `rest` 是装着多余实参的列表
}

func main() -> int {
    println(count(10));           # 1
    println(count(10, 20, 30));   # 3
    return 0;
}
```

（`println`、`print`、`format` 自身就是可变参数函数——`println(a, b, c)` 就是这么工作的。）

---

## 8. `match` 与错误模型

`match` 是**表达式**：它求值为命中分支的值。分支可以是字面量、**类型绑定**模式（`名字: 类型`）、或通配符
`_`。

```mxs
import std.io.{println};

func describe(n: int) -> any {
    return match (n) {
        case 0 => "zero"
        case 1 => "one"
        case _ => "many"
    };
}

func main() -> int {
    println(describe(0));   # zero
    println(describe(9));   # many
    return 0;
}
```

错误是**值**（不是异常）。失败的操作返回一个 `Error`，你用类型绑定的 `match` 分支来区分它：

```mxs
import std.io.{println};

func main() -> int {
    # 1/0 产出一个 Error 值，而不是抛异常
    let result = match (1 / 0) {
        case e: Error => "caught a division error"
        case v: int   => "got a value"
        case _        => "other"
    };
    println(result);        # caught a division error
    return 0;
}
```

`raise(...)` 和 `exit(code)` 是从 `std.io` 导入的普通**函数**（没有 `raise` 关键字）；`raise` 打印错误并
终止进程。

---

## 9. 面向对象

一个 `class` 含有：字段、与类同名的构造函数、方法（经每类一份的 vtable 派发）、运算符重载、以及析构函数
`~ClassName()`。内存由 **ARC**（自动引用计数）管理：最后一个引用消失时析构函数确定性触发。

```mxs
import std.io.{println};

class Point {
    Point(x: int, y: int) {        # 构造函数（与类同名）
        self.x = x;
        self.y = y;
    }
    func sum() -> int { return self.x + self.y; }      # 方法
    operator+(o: Point) -> Point {                     # 运算符重载
        return Point(self.x + o.x, self.y + o.y);
    }
    ~Point() { println("dropping a Point"); }          # 析构函数（ARC）
    let x: int;
    let y: int;
}

func main() -> int {
    let p = Point(3, 4);
    println(p.x);          # 3       （字段访问）
    println(p.sum());      # 7       （经 vtable 的方法调用）
    let q = p + Point(1, 1);   # operator+
    println(q.sum());      # 9
    return 0;              # 每个存活实例的 ~Point 在这里触发
}
```

要点：
- 构造函数就是名字等于类名的那个方法；调用写 `Point(3, 4)`。
- 成员前可加可选的 `public:` / `private:` 访问标签（访问控制目前未强制）。
- 可重载的运算符：`+ - * / % ** < <= > >= == != !`。（`operator**` 和 `operator[]` 目前还**写不出来**，
  尽管运行时已经预留了 vtable 槽位——见 §14。）
- **暂无继承**——类是独立的（见 §14）。

---

## 10. 格式化、`str`、`repr`

```mxs
import std.io.{println, str, repr, format};

func main() -> int {
    println(str(42));                 # 42      （给人看的形式）
    println(repr("hi"));              # "hi"    （调试形式——字符串带引号）
    println(format("{} + {} = {}", 2, 3, 5));     # 2 + 3 = 5   （位置占位）
    println(format("{0} {0} {1}", "a", "b"));     # a a b       （索引占位）
    println(format("[{:5}][{:<5}][{:>5}]", 1, 2, 3));  # [1    ][2    ][    3]  （{:N} 默认左对齐）
    println(format("{:?}", "q"));     # "q"     （调试规格）
    return 0;
}
```

`format` 支持位置占位 `{}`、索引占位 `{N}`、宽度/对齐规格 `{:5}` / `{:<5}` / `{:>5}`、以及调试规格
`{:?}`。

---

## 11. 模块与导入

标准库位于 `std/*.mxs`（如 `std/io.mxs`、`std/time.mxs`），只能通过 `import` 访问。三种形式：

```mxs
# (a) 限定 —— 模块的最后一段成为命名空间：
import std.io;
# ... io.println("hi");

# (b) 别名 —— 给命名空间改名：
import std.io as o;
# ... o.println("hi");

# (c) 选择性 —— 把列出的名字以非限定形式带入作用域：
import std.io.{println, format};
# ... println("hi");
```

一个三者都用到的完整程序：

```mxs
import std.io;
import std.time.{now, monotonic_ns};

func main() -> int {
    let t0 = monotonic_ns();
    let t1 = monotonic_ns();
    io.println(t1 - t0 >= 0);   # true  （单调时钟不会倒退）
    io.println(now() > 0);      # true  （Unix 纪元以来的墙钟秒数）
    return 0;
}
```

规则：
- 一个命名空间必须由**恰好一个** import 绑定。重复导入同一模块、或把两个模块导入到同一别名下，都是错误
  ——用 `as` 起一个不同的名字。
- **局部变量遮蔽**同名的导入命名空间（`let io = …;` 之后 `io.m()` 是对该局部变量的方法调用，不是模块调用）。
- **不支持传递性导入**：一个自身带 `import` 的模块会被拒绝并给出诊断。

`std.io` 导出 `println, print, str, repr, format, raise, exit`（以及 `arraylist`）。`std.time` 导出
`now, now_ms, monotonic_ns`。

---

## 12. REPL

```bash
$ mxs shell
mxs> let x = 21
mxs> x * 2
42
mxs> 1 + 2 * 3
7
mxs> :reset      # 清空累积的 let / 定义，以便重新定义同名
mxs> :q          # 退出
```

REPL 会跨行累积 `let` 绑定和 `func`/`class` 定义，每次求值都重放它们，所以 `let` 会持久化。为了交互方便，
它会自动 import `std.io.{println, print, str, repr, format}`——于是这些名字在提示符下不写 `import` 也能用
（这只是 REPL 的便利；**程序文件仍然必须 import**）。

> REPL 已知限制：对**已有绑定的赋值与原地修改不会跨行持久化**（只有 `let` 行会被重放）。`let mut a = 4`
> 之后 `a = 10`，再 `a` 仍然打印 `4`。持久化可变环境的 REPL 在计划中；目前请用 `:reset` 后重新 `let`。

---

## 13. 一个稍大的例子

```mxs
import std.io.{println};

class Counter {
    Counter(start: int) { self.n = start; }
    func bump() -> int { self.n = self.n + 1; return self.n; }
    let n: int;
}

func sum_to(n: int) -> int {
    let mut total = 0;
    for i in 1..n { total = total + i; }   # 1 + 2 + … + (n-1)
    return total;
}

func main() -> int {
    let c = Counter(10);
    println(c.bump());     # 11
    println(c.bump());     # 12
    println(sum_to(5));    # 10  （1+2+3+4）

    let nums = [3, 1, 2];
    nums.append(9);
    println(nums.len());   # 4

    let label = match (nums.len()) {
        case 4 => "four items"
        case _ => "some items"
    };
    println(label);        # four items
    return 0;
}
```

---

## 14. 尚未实现

这里记录下来，免得你误用。分两大类：

### 有意不提供（设计决定）
- **`while`** —— 故意没有 `while` 循环。用 `until (c)`（前测，"执行直到 `c` 成立"）或 `do … until (c)`
  （后测）。见 §6。
- **全局 `len` / `append`** —— 容器操作是方法（`xs.len()`），绝不是自由函数。
- **隐式标准库** —— 不 `import` 就没有任何名字可用（没有自动 prelude）。

### 能解析但**还不能运行** `[parses only]`
语法接受这些，但 `mxs run-core` 不会执行它们（会遇到 codegen/JIT 错误）：
- **`interface` / `type` / `enum`** 定义。
- **泛型** —— `func f<T>(...)` 和作为值标注的 `List<int>` 只能解析，没有泛型实例化。
- **lambda 表达式**和独立的**块表达式**。
- **关键字参数** `f(a=5)` 和**默认参数值** `func g(a = 7)`。
- **错误传播 `?`** 后缀运算符。
- **`static` / `dynamic let`** 顶层绑定，以及 **`export`**。
- **`assert`** —— codegen 会下降它，但运行时符号 `mxs_panic` 还没提供，所以它在 JIT 链接时失败。
- **`defer`** —— 仅解析。
- 类内的 **`static` 成员**。

### 还没做
- **继承** —— 类不能继承另一个类；类都是独立的。
- **`operator**` / `operator[]` / `operator[]=`** —— 运行时预留了 vtable 槽位，但运算符符号语法目前还表达
  不出它们。
- **`set` / `concat`** 列表方法（目前只接通了 `append` / `len` / `get`）。
- **f-string、文件 I/O、控制台输入、显式类型转换。**

### 当前版本的"锋利边角"
- 内置方法名用在类型不对的接收者上不会崩溃，但行为不一致：`42.append(1)` 是静默 no-op，而 `42.len()` 返回
  一个 `TypeError` *值*（不会被抛出）。这是 v1 的既定行为，等静态接收者类型推断落地后再统一。
- `let`/参数上的类型标注会被解析但不强制（codegen 是动态类型）。

---

*本教程对应 `progress14` 审查时的版本。当某个特性从 §14 毕业，请把它移进正文并补一个可运行示例。
`docs/syntax.md` 里的语法标记是"什么能跑"的最终依据。*
