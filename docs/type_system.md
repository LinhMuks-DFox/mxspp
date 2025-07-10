# MXScript Type System Specification

This document outlines the architecture and design of the MXScript type system. It is a unified, object-oriented system designed for safety, expressiveness, and clarity.

### 1. Core Philosophy

The type system is built on three fundamental principles that create a consistent and predictable programming model:

1.  **Everything is an Object**: There is no distinction between primitive types and object types. Every value, from an integer to a user-defined class instance, is treated as a first-class object.
2.  **All Variables are References**: Variables in MXScript do not store objects directly. Instead, they hold references (pointers) to objects that reside on the heap.
3.  **Memory is Managed Automatically**: Object lifetimes are managed by an Automatic Reference Counting (ARC) system. The compiler is responsible for inserting the necessary `retain` and `release` calls to manage memory automatically.

### 2. The `Object` Root Type

`Object` is the implicit root class for all types in MXScript. Every other type, whether built-in or user-defined, inherits from `Object`. It provides a common interface and foundational capabilities.

The backend representation of every object includes a header containing:

* **Reference Count**: A 64-bit integer for the ARC system. A special value (e.g., -1) can be used to denote global, immutable constants that should not be deallocated.
* **Virtual Table Pointer**: A pointer to the type's virtual method table (v-table), enabling dynamic dispatch for polymorphic method calls like `to_string()`.
* **Destructor Pointer**: A function pointer to the object's specific destructor (`~Type()`) for proper cleanup.

The `Object` class defines the following core methods that can be overridden by subclasses:

* `func hash() -> int`: Returns a hash code for the object. The default implementation may be based on the object's memory address (identity hashing).
* `func equals(other: Object) -> bool`: Compares two objects. The default implementation performs a reference comparison (identity equality). Subclasses should override this to provide value equality.
* `func to_string() -> String`: Returns a string representation of the object. This is used by `println()` and for general serialization.

### 3. Built-in Types

#### 3.1. Fundamental Types

* **`bool`**
    * **Description**: Represents a boolean value.
    * **Semantics**: Has only two possible instances: the global constants `true` and `false`.

* **`String`**
    * **Description**: Represents a sequence of characters.
    * **Semantics**: `String` objects are **immutable**. Any operation that appears to modify a string (e.g., concatenation) will produce a new `String` object.
    * **Implementation**: Implemented as a hybrid type. The compiler has intrinsic knowledge of its memory layout (`ref_count`, `length`, `data*`) and provides high-performance implementations for core methods (e.g., `length()`). Higher-level methods are implemented in the standard library.

* **`nil`**
    * **Description**: Represents the absence of a value.
    * **Semantics**: `nil` is a singleton value. It can be assigned to any variable of any reference type.

#### 3.2. Numeric Types

* **`int`**
    * **Description**: A signed integer of arbitrary precision.
    * **Implementation**: The compiler may use optimized representations for common sizes (e.g., i64) and automatically promote to a heap-allocated "long int" representation when the value exceeds the optimized range.

* **`float`**
    * **Description**: A floating-point number.
    * **Implementation**: Can be backed by standard `float32` or `float64` representations.

* **`decimal`**
    * **Description**: A high-precision decimal type suitable for financial calculations, avoiding binary floating-point inaccuracies.
    * **Implementation**: Represented by a struct containing the sign and digits for the integer and fractional parts.

* **`complex`**
    * **Description**: Represents a complex number with real and imaginary parts.

#### 3.3. Container Types

MXScript provides four core built-in container types for organizing and storing data.

---

##### `List<T>`

* **Description**
    A dynamically-sized, ordered, homogeneous collection of elements of type `T`. It is the primary general-purpose sequence container.

* **Syntax**
    * **Type Declaration**: `List<T>`
    * **Literal**: `[element1, element2, ...]`
    * **Empty List**: `List<T>()`

    ```mxscript
    # Type annotation is mandatory
    let list1: List<int> = [1, 2, 3];
    let list2 = List<String>(); # Creates an empty list of strings
    ```

* **Methods and Operators**
    * `append(value: T)`: Adds an element to the end of the list.
    * `pop() -> T | Error`: Removes and returns the last element. Returns an `Error` if the list is empty.
    * `extend(other: List<T>)`: Appends all elements from another list to the current list.
    * `index_of(value: T) -> int | Nil`: Returns the index of the first occurrence of a value; returns `nil` if not found.
    * `length() -> int`: Returns the number of elements in the list.
    * `insert(index: int, value: T)`: Inserts an element at a specific index.
    * `remove(index: int) -> T | Error`: Removes and returns the element at a specific index.
    * `clear()`: Removes all elements from the list.
    * `operator+(other: List<T>) -> List<T>`: Concatenates two lists, returning a new list.
    * `operator*(count: int) -> List<T>`: Repeats the list's contents `count` times, returning a new list.
    * `operator[](index: int) -> T | Error`: Gets the element at a specific index (Get Item).
    * `operator[]=(index: int, value: T)`: Sets the element at a specific index (Set Item).

* **Storage Strategy**
    `List<T>` always uses a **boxed** (indirect storage) strategy. It internally maintains a dynamic array of pointers to `MXObject*` on the heap. This design allows the list to grow dynamically and unifies the management of elements of all types.

* **Runtime Implementation Guideline**
    * The `mxs_runtime::MXList` class in the Runtime should encapsulate a `std::vector<mxs_runtime::MXObject*>`.
    * All operations that modify the list (e.g., `append`, `insert`, `pop`) must correctly manage the reference counts of the `MXObject`s within. For instance, when appending an object, its reference count must be incremented; when popping an object, its reference count must be decremented.
    * When an `MXList` object itself is destroyed, it must decrement the reference counts of all elements it holds.

---

##### `Array<T>`

* **Description**
    A fixed-size, contiguous-memory, homogeneous array of elements of type `T`. The compiler employs an intelligent storage strategy based on whether the element type `T` is `POD` (Plain Old Data) to optimize for performance and memory layout.

* **Syntax**
    * **Type Declaration**: `[Size]Type` (e.g., `[10]int`)
    * **Initialization**:
        * Literal: `[element1, element2, ...]` (The number of elements must match `Size`)
        * Repetition: `[value; size]` (Creates an array by repeating `value` `size` times)

    ```mxscript
    # Initialization with a literal
    let array1: [3]int = [10, 20, 30];
        
    # Initialization with repetition syntax for an array of five zeros
    let array2: [5]int = [0; 5];
    ```

* **Methods and Operators**
    * `length() -> int`: Returns the fixed length of the array.
    * `operator[](index: int) -> T | Error`: Gets the element at a specific index (Get Item).
    * `operator[]=(index: int, value: T)`: Sets the element at a specific index (Set Item).

* **Storage Strategy**
    * **Direct Storage (Unboxed)**: For maximum performance and C-interoperability, when the element type `T` is **`POD`**, the array stores the values themselves directly in a contiguous memory block. Applicable types include:
        * Primitive numeric types (`int`, `float`, `bool`).
        * User-defined objects annotated with `@@POD`.
    * **Indirect Storage (Boxed)**: This is the default, safe strategy for all non-`POD` types. The array stores a contiguous sequence of pointers to `MXObject*` on the heap. Applicable types include:
        * The built-in `String` type.
        * Any user-defined object not marked as `@@POD`.

* **Runtime Implementation Guideline**
    * The Runtime needs to provide two array implementations: `MXPODArray` and `MXBoxedArray`.
    * `MXPODArray` can internally use `std::array` or a C-style array to store the unboxed values, making its memory layout compatible with C arrays.
    * `MXBoxedArray` internally stores an `std::array<mxs_runtime::MXObject*, Size>` and is responsible for managing the reference counts of its elements.
    * The `Compiler` must decide which `Runtime` array object to instantiate in the `LLVM IR` based on the type information (`is_pod` flag) provided by the `SemanticAnalyzer`.

---

##### `Tuple`

* **Description**
    A fixed-size, ordered collection of elements that can have different types. Tuples are **immutable**.

* **Syntax**
    * **Type Declaration**: `(Type1, Type2, ...)`
    * **Literal**: `(value1, value2, ...)`

    ```mxscript
    let my_tuple: (int, String, float) = (1, "hello", 3.14);
    print(my_tuple[1]); # Outputs: "hello"
    # my_tuple[0] = 2; # Compile error! Tuple elements are immutable.
    ```

* **Methods and Operators**
    * `length() -> int`: Returns the number of elements in the tuple.
    * `operator+(other: Tuple) -> Tuple`: Concatenates two tuples, returning a new tuple with the combined contents.
    * `operator[](index: int) -> T | Error`: Gets the element at a specific index (Get Item).

* **Storage Strategy**
    Because tuples are heterogeneous, they always use a **boxed** (indirect storage) strategy. A tuple object internally stores a fixed-size array of pointers to `MXObject*` on the heap.

* **Runtime Implementation Guideline**
    * The `mxs_runtime::MXTuple` class should contain an `std::array<mxs_runtime::MXObject*, Size>` or an `MXObject**` allocated at construction time.
    * The `Size` is determined at compile time.
    * The `MXTuple` object acquires the `MXObject*` pointers of all its elements upon construction and increments their reference counts.
    * Since tuples are immutable, no methods should be provided to modify their internal elements.
    * Upon destruction, it decrements the reference counts of all its elements.

---

##### `Dict<K, V>`

* **Description**
    An unordered collection of key-value (`K`, `V`) pairs, also known as a hash map or associative array. All keys must be of the same type, and all values must be of the same type. The key type must be hashable.

* **Syntax**
    * **Type Declaration**: `Dict<KeyType, ValueType>`
    * **Literal**: `[key1: value1, key2: value2, ...]`
    * **Empty Dictionary**: `Dict<K, V>()`

    ```mxscript
    let mut scores: Dict<String, int> = ["math": 95, "science": 88];
    scores["history"] = 92;
    print(scores["math"]); # Outputs: 95
    ```

* **Methods and Operators**
    * `get(key: K, default: V | Nil = nil) -> V | Nil`: Retrieves the value for a key; if the key is not found, returns the specified `default` value.
    * `remove(key: K) -> V | Error`: Removes and returns the value for a key. Returns an `Error` if the key is not found.
    * `keys() -> List<K>`: Returns a list of all keys.
    * `values() -> List<V>`: Returns a list of all values.
    * `items() -> List<(K, V)>`: Returns a list of all key-value `(key, value)` tuples.
    * `length() -> int`: Returns the number of key-value pairs in the dictionary.
    * `clear()`: Removes all elements from the dictionary.
    * `operator[](key: K) -> V | Error`: Gets the value for a key (Get Item). Returns an `Error` if the key is not found.
    * `operator[]=(key: K, value: V)`: Sets a key-value pair (Set Item).

* **Storage Strategy**
    `Dict<K, V>` always uses a **boxed** (indirect storage) strategy. It internally stores `MXObject*` pointers to the key and value objects.

* **Runtime Implementation Guideline**
    * The `mxs_runtime::MXDict` class should encapsulate a hash table, such as `std::unordered_map`.
    * To use `MXObject*` as keys, the base `MXObject` class needs to provide `hash()` and `equals()` virtual functions that can be overridden by subclasses.
    * The `Runtime` must provide implementations of `hash` and `equals` for all built-in types that can be used as keys (e.g., `int`, `String`).
    * All operations must correctly manage the reference counts of both keys and values.


### 4. User-Defined Types

#### 4.1. Classes

Classes are the primary mechanism for creating custom types with state and behavior.

```mxscript
class ClassName : SuperClass {
public:
    let pub_immutable_member: int;
    let mut pub_mutable_member: int;

    # Constructor
    ClassName(arg: int) : SuperClass() {
        # Immutable members can only be assigned within a constructor.
        self.pub_immutable_member = arg;
    }

    # Destructor
    ~ClassName() : ~SuperClass() {
        # Cleanup logic here. Superclass destructor is called automatically.
    }

    func method() {
        self.pub_mutable_member = 1;
    }

    override func to_string() -> String {
        return "An instance of ClassName";
    }

private:
    let _private_member: int;
}
```

* **Inheritance**: Classes support single inheritance using the `:` syntax.
* **Member Declaration**: Members are declared with `let` (immutable) or `let mut` (mutable).
* **Constructors**: Responsible for initializing the object. Immutable members can only be assigned within the scope of a constructor.
* **Destructors**: Called automatically by the ARC system when an object's reference count drops to zero. A subclass destructor automatically chains to its superclass's destructor.
* **Method Overriding**: The `override` keyword is mandatory when providing a new implementation for a method inherited from a superclass.

#### 4.2. Interfaces

Interfaces define a contract of methods that a class can choose to implement. They are a powerful tool for abstraction.

```mxscript
interface Printable {
    # All interface methods are implicitly public.
    func print_to_console();

    func get_debug_info() -> String {
        # Interfaces can provide default implementations.
        return "No debug info";
    }
}

class MyReport : Printable {
    override func print_to_console() {
        # Implementation of the interface method.
    }
}
```

* **Contract**: An interface defines a set of method signatures.
* **Implementation**: A class implements an interface by listing it in its inheritance clause and providing implementations for its methods.
* **Default Implementations**: Interfaces can provide default implementations for their methods, which can be used or overridden by conforming types.

#### 4.3 Class Methods and Operator Overloading

##### 4.3.1 Operator Overloading

Users can define custom behavior for standard operators in their classes by defining methods with a special name using the `operator` keyword. For example, an expression like `a + b` is translated by the compiler into `a.operator+(b)`.

**Syntax:**

```mxscript
class MyVector {
    func operator+(other: MyVector) -> MyVector {
        # Implementation for vector addition
    }
}
```

**Overloadable Operators:**

| Category          | Operators                                 |             |
| ----------------- | ----------------------------------------- | ----------- |
| Binary Arithmetic | +, -, \*, /, %, \*\*                      |             |
| Binary Bitwise    | &,                                        | , ^, <<, >> |
| Comparison        | ==, !=, <, <=, >, >=                      |             |
| Unary             | - (negation), + (unary plus), \~ (invert) |             |
| Container/Access  | \[] (getitem), \[]= (setitem)             |             |

> Operators not on this list (e.g., `.`, `is`, `and`, `or`, `not`, assignment operators like `+=`) are reserved and cannot be overloaded.

---

##### 4.3.2 Default Virtual Methods

The base `Object` class provides several virtual methods that can be overridden to customize a type's core behavior:

* `func hash() -> int:` Returns a hash code for the object. Used for dictionary keys and set elements.
* `func repr() -> String:` Returns a developer-facing string representation of the object. Used by `print()` by default.
* `static func from(source: Object) -> Self | Error:` A static-like conversion constructor used by the `cast` mechanism's dynamic path.


### 5. Generics (Templates)

Generics allow for writing flexible, reusable functions and types that can work with any type that satisfies the necessary constraints.

* **Generic Definition**: A generic function or type is defined using the `@@template` annotation.
    ```mxscript
    @@template(T)
    func swap(a: T, b: T) -> (T, T) {
        return (b, a);
    }
    ```

* **Generic Specialization**: The system can support providing specialized, high-performance implementations for specific types.
    ```mxscript
    # A specialized version of a generic function for when T is int.
    @@template<T=int>
    func do_something(val: T) {
        # Highly optimized code for integers.
    }
    ```
* **Compilation Strategy**: Generics are compiled using **Monomorphization**. The compiler creates a specialized version of the generic code for each concrete type it is used with, ensuring static type safety and eliminating runtime overhead.


### 6. Plain Old Data (POD)

To bridge the gap between high-level object-oriented abstractions and low-level performance, MXScript incorporates the concept of Plain Old Data (POD). A POD type is a type that has a simple, C-compatible memory layout and can be safely manipulated with direct memory operations, enabling significant optimizations, especially for arrays.

#### 6.1. The `@@POD` Annotation

A type is designated as POD through the `@@POD` annotation. This annotation is a contract with the compiler, declaring that instances of the class can be treated as simple, contiguous blocks of data without requiring complex object-oriented machinery like v-table lookups or ARC for their members.

```mxscript
# Vector3D is declared as a POD type.
@@POD
class Vector3D {
    let x: float;
    let y: float;
    let z: float;
}
```

#### 6.2. Rules for a POD Type

For the `@@POD` annotation to be valid, the compiler performs a strict, recursive static analysis at compile-time. A class is considered a valid POD type if and only if **all of its instance members are also POD types**.

The base cases for this recursive definition are:
* All primitive numeric types (`int`, `float`, `bool`, `decimal`) are inherently POD.
* An `Array<T>` is a POD type if and only if `T` is a POD type.
* Any class correctly marked with `@@POD` is considered a POD type.

If a class is marked `@@POD` but contains a non-POD member (like a `String` or a standard `Object` reference), the compiler will raise a compile-time error.

#### 6.3. Compiler-Managed Storage Strategy

The primary benefit of the POD system is its impact on the `Array<T>` storage strategy. The compiler uses the POD designation to choose the most efficient memory layout:

* **Direct Storage (Unboxed)**: If `T` is a POD type, an `Array<T>` will be a true, contiguous block of `T` values in memory. For example, `[10]Vector3D` will be a single memory block of `10 * sizeof(Vector3D)`. This layout is cache-friendly, offers maximum performance, and allows for zero-copy interoperability with C/C++ libraries.
* **Indirect Storage (Boxed)**: If `T` is not a POD type (the default for most classes), the `Array<T>` stores a contiguous sequence of pointers to the objects, which are individually allocated on the heap.


### 7. Type Operation Functions

Core functions for runtime type introspection and conversion are provided as built-ins.

| Function Name | Signature                                                                               | Description                                               |
| :------------ | :-------------------------------------------------------------------------------------- | :-------------------------------------------------------- |
| `type_of`     | `@@template(T) func type_of(instance: T) -> RTTIObject`                                 | Gets an object representing the runtime type information. |
| `is_instance` | `@@template(T) func is_instance(instance: Object, target_type: RTTIObject) -> bool`     | Checks if an object is an instance of a given type.       |
| `cast`        | `@@template(Target, Origin) func cast(target_type: RTTIObject, value: Origin) -> Target | Error`                                                    | Attempts to convert a value to a target type. |

### 7.1. `type_of` and `is_instance`

  * `type_of(instance)` returns a special `RTTIObject` which is a wrapper around the C++ `MXTypeInfo` structure. This `RTTIObject` can be stored and compared.
  * `is_instance(instance, target_type)` performs a runtime check. It traverses the inheritance chain of `instance` (by following the `parent` pointers in the `MXTypeInfo` structures) to see if it matches the type represented by `target_type`.



### 7.2. `cast` as a Hybrid Conversion Mechanism

The `cast(Target, value)` function is a powerful, high-level **conversion constructor**, not a low-level memory cast. It asks the `Target` type to attempt to construct an instance of itself from `value`. To achieve maximum performance and flexibility, the compiler implements this using a **hybrid dispatch model**.

#### 7.2.1. Static Dispatch ("Fast Path")

This is the default and preferred mechanism, used whenever the compiler can determine both the `Target` and `value` types at compile-time.

* **Behavior**: The `cast` expression is treated as **syntactic sugar**. The compiler translates `cast(String, my_integer)` directly into a call to a specific, high-performance C API function, such as `mxs_string_from_integer`.
* **Implementation**: This relies on a comprehensive set of C++ functions dedicated to specific type conversions. This path completely bypasses virtual function overhead.

#### 7.2.2. Dynamic Dispatch ("Polymorphic Path")

This is the fallback mechanism, employed only when the `Target` type cannot be resolved at compile-time (e.g., when it is a variable holding a type object).

* **Behavior**: When a static path is not available, the compiler generates a call to a generic C API function (`mxs_op_from`).
* **Implementation**: This generic function uses the C++ **`virtual` function mechanism**. It effectively calls `Target.op_from(value)`, asking the target type at **runtime** if it knows how to construct itself from the source value. Each type can `override` the `op_from` method to define its supported conversions.

This two-pronged approach ensures that `cast` is both extremely fast for the common, statically-known cases, and powerful enough to handle complex, dynamic polymorphism when required.



## 8. FFI for Built-in Classes and Fast Dispatch

To achieve both high performance and extensibility, MXScript employs a layered FFI (Foreign Function Interface) strategy. This allows the compiler to generate highly optimized code for built-in operations ("Fast Dispatch") while maintaining a clear and stable interface with the C++ runtime.

### Built-in Type Correspondence

The core built-in types in MXScript directly map to specific classes within the C++ runtime library:

| MXScript Type | C++ Runtime Class                               |
| :------------ | :---------------------------------------------- |
| `int`         | `mxs_runtime::MXInteger` or `mxs_runtime::MXBigInteger` |
| `float`       | `mxs_runtime::MXFloat`                          |
| `string`      | `mxs_runtime::MXString`                         |
| `bool`        | `mxs_runtime::MXBoolean`                        |
| `object`      | `mxs_runtime::MXObject` (Base Class)            |
| `List<T>`     | `mxs_runtime::MXList`                           |
| `Array<T>`    | `mxs_runtime::MXPODArray` or `mxs_runtime::MXBoxedArray` |
| `Dict<K, V>`  | `mxs_runtime::MXDict`                           |
| `Tuple`       | `mxs_runtime::MXTuple`                          |

### MXScript-Runtime Interaction Strategy

The interaction follows a strict, three-layered approach to ensure stability and clear separation of concerns:

**C++ Runtime Class -> `extern "C"` API -> MXS FFI Call**

1.  **C++ Runtime Class**: This is where the core logic resides. For example, the `mxs_runtime::MXInteger::add(other)` method contains the actual implementation for integer addition. This layer can use complex C++ features without exposing them to the outside world.
2.  **`extern "C"` API**: This is a stable, C-compatible wrapper around the C++ implementation. It exposes a simple function signature that is not subject to C++ name mangling, providing a stable ABI (Application Binary Interface). The compiler targets these functions directly.
    *Example*: `mxs_runtime::MXObject* mxs_integer_add(mxs_runtime::MXInteger* lhs, mxs_runtime::MXInteger* rhs);`
3.  **MXS FFI Call**: The MXScript compiler, when it can determine the types at compile time, generates a direct `call` instruction to the `extern "C"` API function. This is the "Fast Dispatch" path.

### Fast Dispatch vs. Dynamic Dispatch

The compiler can choose one of two paths when handling operations:

* **Dynamic Dispatch (Slow Path)**: This is the default, safe option. When the types of the operands are not known at compile time, the compiler generates a call to a generic operator function, like `mxs_op_add(lhs, rhs)`. This function must dynamically inspect the types of `lhs` and `rhs` at runtime (e.g., using a vtable or type tag) and then call the appropriate C++ method. This introduces runtime overhead.

* **Fast Dispatch (Optimized Path)**: When the `SemanticAnalyzer` can prove the types of both operands at compile time (e.g., for `let x: int = 1; let y: int = 2; x + y;`), the compiler can skip the generic `mxs_op_add` function. Instead, it generates a direct FFI call to the type-specific, high-performance C-API function, `mxs_integer_add`. This eliminates the overhead of dynamic type checking and results in significantly faster code.

The complete mapping of MXScript operations to their corresponding Fast Path C-API functions and runtime implementations should be defined in a dedicated `FFI-Responding.md` document. This document serves as the definitive contract between the compiler and the runtime library.