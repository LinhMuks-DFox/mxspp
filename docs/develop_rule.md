# MxScript C++ Runtime Development Rules

## Version

| Item        | Value      |
| ----------- | ---------- |
| **Version** | 2.0.0      |
| **Status**  | **Final**  |
| **Updated** | 2025-07-08 |

-----

## 1\. Core Philosophy

The MxScript C++ runtime is built on modern C++ principles to achieve **zero-cost memory safety**. The following rules are mandatory for all C++ code in the `runtime/` and `libcwrapper/` directories to ensure consistency, safety, and maintainability.

-----

## 2\. Memory Management & Object Lifecycle

The runtime's memory model is based on **Ownership**, **Move Semantics**, and **Explicit Copying**.

### 2.1. Ownership with `std::unique_ptr`

  * **The Golden Rule**: All dynamically allocated `mxs_runtime::MXObject` instances that are owned by a C++ variable **must** be held in a `std::unique_ptr<mxs_runtime::MXObject>`. Direct use of owning raw pointers (`MXObject*`) is strictly forbidden.
  * **Rationale**: `std::unique_ptr` automates object destruction (RAII), preventing memory leaks and providing compile-time checks on ownership rules. It is a zero-cost abstraction.

### 2.2. Move Semantics

  * **Default Behavior**: Assignment or passing an object from one owner to another **must** be a move operation, transferring ownership.
  * **C++ Implementation**: This is achieved by using `std::move()` on the `std::unique_ptr`.
    ```cpp
    // Transfers ownership from 'a' to 'b'. 'a' becomes nullptr.
    std::unique_ptr<MXObject> b = std::move(a);
    ```

### 2.3. Borrowing

  * **Rule**: When a function needs to access an object without taking ownership, it **must** accept a raw pointer (`MXObject*`) or a reference (`MXObject&`).
  * **Example**:
    ```cpp
    // This function borrows 'obj' but does not own it.
    void inspect_object(mxs_runtime::MXObject* obj);
    ```

### 2.4. Explicit Copying via `clone()`

  * **Rule**: To create a deep copy of an object, the `copy()` builtin function in MxScript must be used.
  * **C++ Implementation**: Any `MXObject` subtype that supports copying **must** implement a virtual `clone()` method.
    ```cpp
    // In MXObject base class
    virtual std::unique_ptr<MXObject> clone() const = 0;
    
    // In a derived class, e.g., MXString
    std::unique_ptr<MXObject> MXString::clone() const override {
        return std::make_unique<MXString>(this->value);
    }
    ```

-----

## 3\. Object Population Manager

The runtime uses a global manager to track all live `MXObject` instances for debugging and statistics, not for memory allocation.

  * **Name**: The manager is accessed via the global reference `mxs_runtime::MX_OBJECT_MANAGER`.
  * **Responsibility**: It only **tracks** live objects. It **must not** allocate or deallocate memory itself. Object lifetime is managed exclusively by `std::unique_ptr`.
  * **Implementation Rule**: Every `MXObject` constructor **must** call `MX_OBJECT_MANAGER.registerObject(this)`. Every `MXObject` destructor **must** call `MX_OBJECT_MANAGER.unregisterObject(this)`. This is handled in the base `MXObject` class.

-----

## 4\. Runtime Type Information (RTTI) Convention

The RTTI system is designed to be fast and safe across dynamic library boundaries.

  * **Core Mechanism**: A type's identity is defined by the unique memory address of its global static `MXTypeInfo` instance.
  * **Type Checking Rule**: Type comparison **must** be performed via direct pointer address comparison.
    ```cpp
    // Correct way to check type
    if (obj->get_type_info() == wrapper_libc::WrapperLibcMXMallocPointerHolder) {
        // ...
    }
    ```
  * **Declaration & Definition Pattern**:
    1.  **Header (`.h`/`.hpp`)**: Publicly accessible type info pointers **must** be declared using the `DELCARE_HEADER_API_CONST_TYPEINFO_POINTER` macro (which expands to `extern MXS_API const MXTypeInfo*`).
        ```cpp
        // In, e.g., wrapper_stdlib.h
        DELCARE_HEADER_API_CONST_TYPEINFO_POINTER(WrapperLibcMXMallocPointerHolder);
        ```
    2.  **Source (`.cpp`)**: The static `MXTypeInfo` struct is defined locally, and the exported pointer is set to its address.
        ```cpp
        // In, e.g., wrapper_stdlib.cpp
        MXS_API static const mxs_runtime::MXTypeInfo WrapperLibcMXMallocPointerHolder_TYPE_INFO { ... };
        const MXTypeInfo* wrapper_libc::WrapperLibcMXMallocPointerHolder = &WrapperLibcMXMallocPointerHolder_TYPE_INFO;
        ```
  * **Axiomatic Rules**:
    1.  The `parent` pointer of an `MXTypeInfo` struct **must not** be modified after initialization.
    2.  All `MXTypeInfo` instances **must** be static globals, ensuring their lifetime matches the program's lifetime and their addresses are constant.
    3.  For any type other than the root `MXObject`, its `parent` pointer **must not** be `nullptr`, ensuring a valid inheritance chain.

-----

## 5\. FFI Wrapper Best Practices (Reference Implementation)

The implementation in `runtime/libcwrapper/impl/wrapper_stdlib.cpp` serves as the gold standard for writing new FFI wrappers. Key elements to follow are:

  * **Resource Management**: For external resources acquired from C libraries (like from `malloc`), their lifetime **must** be managed by a `std::unique_ptr` with a custom deleter inside a dedicated `MXObject` holder class (e.g., `MXMallocPointerHolder`).

  * **Robust Argument Checking**: All wrappers **must** perform rigorous type and value checking on their `MXObject*` arguments before proceeding.

  * **Error Reporting**: On failure, wrappers **must** return a `std::unique_ptr` to a new `MXError` object.

    * ```c++
      // On failure, wrappers must return a std::unique_ptr to a new MXError object.
      if (some_error_condition) {
          return std::make_unique<MXError>("FFIError", "Invalid argument provided.");
      }
      ```

  * **Allocation**: Direct use of `new` is discouraged. If a unified allocation function (e.g., `Manager.allocate<T>()`) is implemented in the future, it should be used. For now, the key is that the returned raw pointer from `new` is immediately wrapped in a `std::unique_ptr` to ensure safety.


-----

## 6\. Function Design Principles

To ensure clarity, type safety, and predictable resource management, all functions within the C++ runtime must adhere to one of the following two design patterns.

### 6.1. Pattern 1: Operations with Potentially Ambiguous Return Types

This pattern applies to any function that performs a computation that can result in either a success value or a runtime error.

  * **Examples**: `op_add`, `op_sub`, `op_getitem`, `op_setitem`.
  * **Return Type**: `std::unique_ptr<MXObject>`
  * **Semantic Contract**: The function returns an owning smart pointer to an `MXObject`. The caller receives full ownership and **is responsible for determining the actual type** of the object contained within.
      * On success, the `unique_ptr` contains an object of the expected result type (e.g., `MXInteger`).
      * On failure, the `unique_ptr` contains an `MXError` object.
  * **Caller Responsibility**: The caller **must** check the `TypeInfo` of the returned object before use to differentiate between a success value and an error.
    ```cpp
    auto result_ptr = obj->op_add(other);
    // Use the new RTTI pattern to check the type by address
    if (&(result_ptr->get_rtti()) == &MXError::get_rtti()) {
        // Handle error case...
    } else {
        // Handle success case...
    }
    ```
  * **Implementation Example (`op_add`)**:
    ```cpp
    MXObjectOwnerPtr MXInteger::op_add(const MXObject *other) {
        if (&(other->get_rtti()) != &MXInteger::get_rtti()) { // Updated type check
            return std::make_unique<MXError>("TypeError", "Operand must be an integer");
        }
        // ...
    }
    ```

### 6.2. Pattern 2: Infallible Predicates & Accessors

This pattern applies to any function that is guaranteed to successfully return a result without generating a runtime error that the script needs to handle.

  * **Core Rule**: These functions **must** return their natural C++ primitive type (e.g., `bool`, `size_t`) or a non-owning raw pointer/reference (`const MXObject*`/`&`).
  * **Semantic Contract**: The function promises to always return a valid result. The caller does not need to handle an error case. If types are incompatible for a comparison, the result is simply `false`.

#### Sub-Pattern 2A: Predicates (returning `bool`)

  * **Examples**: `op_equals`, `op_less_than`.
  * **Return Type**: `bool` (or `inner_boolean`).
  * **Implementation Example (`op_equals`)**:
    ```cpp
    bool MXInteger::op_equals(const MXObject *other) const override {
        // Updated type check
        if (&(other->get_rtti()) != &(this->get_rtti())) {
            return false;
        }
        // ...
    }
    ```

#### Sub-Pattern 2B: Accessors (returning pointers, references, or other primitives)

  * **Examples**: Accessing singletons, peeking at container elements, getting a hash code.
  * **Return Type**: `const MXObject&`, `const MXObject*`, `size_t`, etc.
  * **Implementation Example**:
    ```cpp
    // Accessing a global singleton
    auto get_nil_singleton() -> const MXNil& {
        return MXNil::getInstance();
    }
    
    // Getting a hash code
    size_t MXString::hash_code() const {
        return std::hash<std::string>()(this->value);
    }

## 7\. C++ Runtime Coding Standards

This chapter codifies the mandatory standards for all C++ code within the runtime. Adherence to these rules is required to ensure consistency, safety, maintainability, and architectural integrity.

### 7.1. Naming Conventions

A consistent naming scheme is critical for code readability and predictability.

  * **Types (Classes, Structs, Enums, Type Aliases)**: Must use `UpperCamelCase`.

      * *Example*: `class MXInteger;`, `struct MXTypeInfo;`, `using MXObjectOwnerPtr = std::unique_ptr<MXObject>;`

  * **Functions and Methods**: Must use `snake_case`.

      * *Example*: `auto get_value() const;`, `MXObjectOwnerPtr validate_arguments(...);`

  * **Member Variables**: Must use `snake_case` with a trailing underscore (`_`). This convention clearly distinguishes member data from local variables and parameters.

      * *Example*: `int value_;`, `std::string message_;`

  * **Constants and Enumeration Members**: Must use `UPPER_SNAKE_CASE`.

      * *Example*: `const int MAX_SIZE = 100;`, `enum class Status { OK, ERROR };`

### 7.2. RTTI and Type Identification **(Completely Rewritten)**

To ensure pointer validity and eliminate the “static initialization order fiasco,” the definition and access of RTTI **MUST** follow the **Construct on First Use in Class Static Method** pattern. This is the only accepted pattern.

1.  **No RTTI Data Members**:

      * **Rule**: `MXObject` and any of its subclasses **SHOULD NOT** include any `static` `MXRuntimeTypeInfo` data members (whether named `rtti`, `S_MX...`, or anything else) in the class definition.

2.  **Public Static Accessor Function (`get_rtti`)**:

      * **Rule**: Every class requiring RTTI **MUST** provide a `public static` member function as the sole entry point to access its type information.
      * **Name**: `get_rtti`.
      * **Signature**: `static auto get_rtti() -> const MXRuntimeTypeInfo&;`
      * **Implementation**: The function body **MUST** contain a `static` local instance of `MXRuntimeTypeInfo`, and return a reference to it.
      * **Example (`mx_integer.cpp`)**:
        ```cpp
        auto MXInteger::get_rtti() -> const MXRuntimeTypeInfo& {
            // Return the address of a file-local static instance.
            static const MXRuntimeTypeInfo instance("Integer", &MXObject::get_rtti());
            return instance;
        }
        ```

3.  **Type Check Implementation (`is_instance_of`)**:

      * **Rule**: All type checks **MUST** be performed via a common utility function `mxs_runtime::is_instance_of()`, which works by comparing the addresses returned by `get_rtti()` along the parent chain.
      * **Example**:
        ```cpp
        // A helper function in a common utility file
        bool is_instance_of(const MXObject* obj, const MXRuntimeTypeInfo& target_rtti) {
            if (!obj) return false;
            const MXRuntimeTypeInfo* current_rtti = &(obj->get_rtti());
            while (current_rtti != nullptr) {
                if (current_rtti == &target_rtti) {
                    return true;
                }
                current_rtti = current_rtti->parent;
            }
            return false;
        }
        
        // Usage within a class's static helper
        bool MXInteger::is_a(const MXObject* obj) {
            return is_instance_of(obj, MXInteger::get_rtti());
        }
        ```


### 7.3. Argument Validation

To enforce robustness and provide consistent error feedback, argument validation is mandatory and must be standardized.

  * **Rule**: All functions, especially those at the FFI boundary, **must** validate their `MXObject*` arguments for both non-nullity and correct type before use.
  * **Practice**: Manual, repetitive `if` blocks for validation are strictly forbidden. The global helper function `mxs_runtime::validate_arguments()` **must** be used for this purpose. This centralizes validation logic, standardizes error messages, and improves code clarity.
  * **Example**:
    ```cpp
    // At the entry point of a function:
    if (auto err = validate_arguments({
        {fmt,  MXGetTypeInfoOfMXString(), "fmt"},
        {argv, MXGetTypeInfoOfMXFFICallArgv(), "argv"}
    })) {
        // If validation fails, immediately return the error object.
        return err;
    }
    
    // Proceed with function logic, now guaranteed to have valid arguments.
    // ...
    ```

### 7.4. General C++ Style and Safety

  * **`const` Correctness**:

      * **Rule**: Any member function that does not modify the object's state **must** be declared `const`. Any pointer or reference parameter that should not be modified by the function **must** be declared `const`. This is a fundamental contract that the compiler helps enforce.

  * **Header File Policy**:

      * **Include Guards**: All header files **must** use `#pragma once`.
      * **Forward Declarations**: To minimize compile times and reduce dependencies, prefer forward-declaring types (`class MyClass;`) in header files over including their full header, unless the full definition is required.

  * **C++ Casts**:

      * **Rule**: C-style casts (e.g., `(MyType*)ptr`) are strictly forbidden as they are not type-safe. The appropriate C++ cast **must** be used:
          * `static_cast`: For safe, compile-time-checked conversions (e.g., up/down casting in a known-correct hierarchy).
          * `dynamic_cast`: For safe, runtime-checked down-casting of polymorphic types.
          * `const_cast`: Must be avoided unless absolutely necessary to interface with a legacy C-style API that lacks `const` correctness.
          * `reinterpret_cast`: Strictly forbidden, except for highly specialized, low-level platform code.



### **7.5. Function Parameter Passing Conventions**

To balance performance and code clarity, all function parameter passing must follow these conventions:

  * **In-Parameters**:

      * **Large or complex objects (e.g., `std::string`, `std::vector`)**: For read-only large objects, you **MUST** pass them using `const T&` (constant lvalue reference) to avoid unnecessary copy overhead.
        ```cpp
        // Correct ✅
        void set_message(const std::string& msg); 
        // Incorrect ❌
        void set_message(std::string msg); 
        ```
      * **Built-in or small types (e.g., `int`, `double`, `bool`, pointers)**: For these types, **pass-by-value** is typically the most efficient and clear.
        ```cpp
        // Correct ✅
        void set_value(int v, bool flag, const MXObject* ptr);
        ```

  * **Out-Parameters**:

      * Prefer returning the result via **return value**.
      * If you must output via parameter, you **MUST** use a non-`const` pointer (`T*`) or reference (`T&`), and the function name should clearly indicate its side effect.

  * **In-Out-Parameters**:

      * **MUST** use a non-`const` reference (`T&`).

  * **Ownership Transfer**:

      * For dynamically allocated objects that need to transfer ownership, you **MUST** pass them by value as `std::unique_ptr<T>` or use an rvalue reference `std::unique_ptr<T>&&`, to clearly indicate ownership transfer.
        ```cpp
        // Receiving ownership
        void set_property(const std::string& name, std::unique_ptr<MXObject> value);
        ```