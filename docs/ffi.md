# MxScript Foreign Function Interface (FFI) Specification

## 1\. Core Philosophy & Convention

The FFI is the sole and unified mechanism for MxScript to interface with compiled C++ code. It enables both the language's standard library implementation and user-defined bindings to external shared libraries.

The system is based on **Direct Call with a Type Convention**. Its core principle is that the MxScript compiler generates a direct LLVM `call` to a C++ function. There is **no** central runtime dispatch helper.

All C++ functions exposed to MxScript via FFI **must** adhere to the following convention:

* **Signature:** All function arguments and return values must be of the type `mxs_runtime::MXObject*`.
* **Responsibility:** The C++ function itself is responsible for runtime type checking (using the RTTI system like `isa`/`cast`) and handling potential type errors.

## 2\. The `@@foreign` Annotation

The `@@foreign` annotation links an MxScript function declaration to a C++ symbol in a shared library.

### 2.1. Syntax

```mxscript
@@foreign(lib: string, symbol_name: string, argv: list)
func mxscript_function_name(arg1: Type1, ...) -> ReturnType;
```

### 2.2. Annotation Parameter Parsing and Handling

The content within the `@@foreign(...)` parentheses is parsed by the compiler's frontend as a set of key-value arguments. The parser's role is to extract this metadata and attach it to the function's AST node for later use by the code generator.

* **`lib: string` (Mandatory)**

    * **Parsing:** The parser expects the key `lib` followed by a **string literal**. It extracts the content of the string (e.g., `"runtime.so"`).
    * **Handling:** This string is stored as the library name that the final executable will need to link against or dynamically load.

* **`symbol_name: string` (Optional)**

    * **Parsing:** The parser expects the key `symbol_name` followed by a **string literal**.
    * **Handling:** This string is stored as the exact external symbol name to be used in the LLVM `call` instruction. If this parameter is omitted, the code generator defaults to using the MxScript function's own name (e.g., `c_printf`) as the symbol name.

* **`argv: list` (Optional, Special Directive)**

    * **Parsing:** The parser expects the key `argv` followed by a **list literal**. The contents of this list literal dictate the argument packing strategy.
    * **Handling:** The presence of the `argv` key signals the code generator to activate the "Variadic Dispatch Mode". The generator analyzes the list's content (e.g., `[1,...]`) to determine which arguments are fixed and which are to be packed into the `MXFFICallArgv` object.

## 3\. Dispatch Modes & Compiler Behavior

### 3.1. Fixed-Arity Function Dispatch (Default)

This mode is used when the `argv` parameter is **not** present in the annotation.

* **MxScript Declaration:**
  ```mxscript
  @@foreign(lib="runtime.so", symbol_name="mxs_string_from_integer")
  static func from(value: Integer) -> String;
  ```
* **Compiler Behavior:** The code generator produces a direct LLVM `call` to the symbol `mxs_string_from_integer`, passing the single `MXObject*` argument directly.

### 3.2. Variadic Function Dispatch (Triggered by `argv`)

This mode is activated by the presence of the `argv` parameter.

* **MxScript Declaration:**
  ```mxscript
  @@foreign(lib="my_wrappers.so", symbol_name="printf_wrapper", argv=[1,...])
  func c_printf(format: String, ...);
  ```
* **Compiler Behavior:**
    1.  The code generator sees the `argv=[1,...]` directive.
    2.  It designates argument 0 (`format`) as a fixed argument to be passed directly.
    3.  It gathers all arguments from index 1 onwards into a `std::vector<MXObject*>`.
    4.  It generates code to call the `MXFFICallArgv` constructor with this vector.
    5.  It generates a direct LLVM `call` to `printf_wrapper`, passing the fixed argument first, followed by the pointer to the new `MXFFICallArgv` object.
    6.  Immediately after the wrapper returns, it emits a call to `MXFFICallArgv_destructor` to release the packed arguments object.

## 4\. C++ API & Implementation Contract

### 4.1. The FFI Argument Vector: `MXFFICallArgv`

This is a specialized, internal-only `MXObject` subtype for variadic argument passing. Its definition resides in `runtime/include/object.h`.

### 4.2. Contract for C++ Wrapper Developers

* **Fixed-Arity Functions:** Declare the C++ function with an exact matching number of `MXObject*` arguments.
* **Variadic Functions:** Declare the C++ function to accept its fixed arguments, followed by a **single final `MXObject*`** for the packed arguments. Inside the function, use `cast<MXFFICallArgv>(...)` to safely access the packed arguments.
* **Lifecycle:** The compiler creates the `MXFFICallArgv` before dispatch and destroys it immediately after the call, so wrapper code should not retain this object beyond the call scope.
