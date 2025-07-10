# MxScript Language Guide

This document provides a comprehensive overview of the MxScript language syntax, covering fundamental concepts from variables to control flow and object-oriented programming.

### 1. Comments

Comments are used to annotate code and are ignored by the compiler.

* **Single-line comments**: Start with `#` and continue to the end of the line.

* **Multi-line comments**: Are enclosed between `!##!` and `!##!`.

```mxscript
# This is a single-line comment.

!##!
  This is a
  multi-line comment block.
!##!
```

### 2. Variables and Assignments

Variables are references to objects. They are declared using the `let` keyword. By default, variables are immutable.

#### 2.1. Immutable Variables

An immutable variable's reference cannot be changed after its initial assignment.

```mxscript
let x: int = 10; // Declared with an explicit type and initialized.
let y = "hello";  // Type is inferred from the assigned value.

// The following line would cause a compile-time error:
// x = 20; // Error: Cannot assign to immutable variable 'x'.
```

#### 2.2. Mutable Variables

To declare a variable whose reference can be changed, use the `let mut` keywords.

```mxscript
let mut count: int = 0;
count = 1; // This is valid.
```

#### 2.3. Assignment

Assignment is done using the `=` operator. It is an expression that changes the reference held by a mutable variable.

```mxscript
let mut a = 5;
a = 100; // 'a' now refers to the object representing 100.
```

### 3. Functions

Functions are first-class citizens in MxScript. They are defined using the `func` keyword.

#### 3.1. Function Definition

A function definition includes its name, a parameter list, an optional return type (`-> Type`), and a body block.

* **Parameters**: Multiple consecutive parameters of the same type can be grouped. Parameters can also be assigned a default value, making them optional at the call site.
* **Return Type**: If the return type is omitted, it implicitly defaults to `nil`.

```mxscript
// A function with an explicit return type.
func add(a: int, b: int) -> int {
    return a + b;
}

// A function demonstrating parameter grouping and default values.
// 'width' and 'height' share the type 'int' and default to 800.
func create_window(title: string, width, height: int, some_default_value: bool = true) {
    // ... function body ...
}

// A function with no explicit return type, so it implicitly returns nil.
func log_message(message: string) {
    println(message);
    // No 'return' statement needed for a nil return.
}
```

#### 3.2. Function Calls

Functions are called using their name followed by parentheses containing the arguments.

```mxscript
let sum = add(5, 10);
create_window("My App"); // Uses the default values for width and height.
```

### 4. Control Flow

MxScript provides a rich set of control flow statements.

#### 4.1. Conditional: `if-else`

Executes code based on a boolean condition. It supports `else if` for chaining conditions.

```mxscript
let score = 85;

if score >= 90 {
    println("Grade: A");
} else if score >= 80 {
    println("Grade: B");
} else {
    println("Grade: C or lower");
}
```

#### 4.2. Pre-test Loop: `until`

The `until` loop executes its body as long as its condition is **false**. The condition is checked *before* each iteration.

```mxscript
let mut counter = 0;

// This loop will run until counter is 5.
until (counter == 5) {
    print(counter); // Prints 0, 1, 2, 3, 4
    counter = counter + 1;
}
```

#### 4.3. Post-test Loop: `do-until`

The `do-until` loop executes its body at least once, and continues to loop as long as its condition is **false**. The condition is checked *after* each iteration.

```mxscript
let mut input = "";

// The body executes at least once.
do {
    input = read_line();
} until (input == "quit");
```

#### 4.4. Infinite Loop: `loop`

The `loop` statement creates an infinite loop that must be exited explicitly using a `break` statement.

```mxscript
loop {
    let command = get_command();
    if command == "exit" {
        break; // Exits the loop.
    }
    process_command(command);
}
```

#### 4.5. Iterator Loop: `for-in`

The `for-in` loop iterates over the elements of a collection. (Note: Requires a collection that supports the iterator protocol).

```mxscript
let names: [] List<string> = ["Alice", "Bob", "Charlie"];

for name in names {
    println(name);
}

for i in 1..100 {
    println(i);
}
```

#### 4.6 `match`

```mxscript


func foo_or_bar(s: string) -> int | string {
    return match (x) {
        case "foo" => {
            "bar";
        }
        case "bar" => {
            "foo"
        }
    }
}


let x: string = "s"; 


```

### 5. Class, interface, builtin classes, and type system
See ![](./type_system.md)

### 6. Error Handling

MxScript adopts a unified, value-based approach to error handling that is designed to be both safe and expressive. The system distinguishes between two fundamental categories of errors but handles them through a single, consistent mechanism: the `Error` object.

1.  **Recoverable Errors**: These are expected and handleable situations that occur during the normal flow of a program, such as invalid user input or a file not being found.
2.  **Unrecoverable Errors (Panics)**: These are critical, unexpected programming errors that indicate a bug, such as division by zero or an array index out of bounds. The only sensible action is to terminate the program.

#### 6.1. The Generic `Error` Object

All errors are represented as instances of the `std.Error` class. To ensure type safety, `std.Error` is a generic class.

* **Definition**: `@@template(T) class std.Error`
* **Generic Parameter `T`**: The type `T` corresponds to the type of the `alternative` value, providing a type-safe fallback mechanism.
* **Constructor**: `std.Error<T>(type: String, message: String, panic: bool = false, alternative: T = nil)`
    * `type`: A string identifying the class of error (e.g., `"ValueError"`, `"IOError"`).
    * `message`: A human-readable description of what went wrong.
    * `panic`: A boolean flag. If `true`, creating this `Error` object will immediately halt the program and print a stack trace to `stderr`. If `false` (the default), the `Error` object is treated as a normal value.
    * `alternative`: A type-safe fallback value of type `T` that can be used by the calling code for recovery.

#### 6.2. Signaling Errors with `raise`

The `raise` keyword is the primary mechanism for signaling an error. It is syntactic sugar for `return std.Error<T>(...)`.

**Compiler Rule**: The `raise` keyword can **only** be used inside a function whose return type is explicitly declared as a union type that includes an `Error` specialization (e.g., `-> int | Error<int>`). This rule ensures that all potential errors are visible in the function's signature.

```mxscript
// This function explicitly states it can return an int or an Error with an int alternative.
func process_data(data: string) -> int | Error<int> {
    if data.is_empty() {
        // Signal a recoverable error with a type-safe fallback value.
        raise std.Error<int>("ValueError", "Input data cannot be empty.", alternative=0);
    }
    // ...
}

// The signature MUST include Error because of the 'raise' statement.
func divide(a: int, b: int) -> int | Error<int> {
    if b == 0 {
        // Signal a fatal, unrecoverable error.
        raise std.Error<int>("ZeroDivisionError", "Divisor cannot be zero.", panic=true);
    }
    return a / b;
}
```

#### 6.3. Extending `Error` for Custom Types

You can create a more specific and structured error hierarchy by extending the `std.Error` class.

```mxscript
// Define a custom error type for network-related failures.
class NetworkError : std.Error<nil> {
    let host: string;
    let port: int;

    // Custom constructor
    NetworkError(host: string, port: int, msg: string) {
        super("NetworkError", msg); // Call the base class constructor
        self.host = host;
        self.port = port;
    }
}

func connect(host: string, port: int) -> Connection | NetworkError {
    if !is_reachable(host, port) {
        raise NetworkError(host, port, "Host is not reachable.");
    }
    // ...
}
```

#### 6.4. Handling Errors with `match`

Because functions that can fail return a union type, the `match` statement is the primary tool for safely handling the result. It forces the developer to explicitly consider both the success and error cases.

```mxscript
func main() -> int {
    let result = process_data("");

    let value = match (result) {
        // Case 1: Handle a specific, custom error type.
        case e: NetworkError => {
            println("Connection failed to host:", e.host);
            -1; // Return a default value
        }
        // Case 2: Handle any other generic Error. The compiler knows e.alternative is an int.
        case e: Error<int> => {
            println("A generic error occurred:", e.message);
            e.alternative; // Use the type-safe alternative value to recover.
        }
        // Case 3: The function returned an int.
        case i: int => {
            println("Success! The value is:", i);
            i;
        }
    };

    println("The final value is:", value); // Prints "The final value is: 0"
    return 0;
}


MxScript adopts a unified, value-based approach to error handling that is designed to be both safe and expressive. The system distinguishes between two fundamental categories of errors but handles them through a single, consistent mechanism: the `Error` object.

1.  **Recoverable Errors**: These are expected and handleable situations that occur during the normal flow of a program, such as invalid user input or a file not being found.
2.  **Unrecoverable Errors (Panics)**: These are critical, unexpected programming errors that indicate a bug, such as division by zero or an array index out of bounds. The only sensible action is to terminate the program.

#### 6.1. The `Error` Object

All errors are represented as instances of the `std.Error` class. This object encapsulates all information about an error event.

* **Constructor**: `std.Error(type: String, message: String, panic: bool = false, alternative: Any = nil)`
    * `type`: A string identifying the class of error (e.g., `"ValueError"`, `"IOError"`). This is useful for programmatic handling.
    * `message`: A human-readable description of what went wrong.
    * `panic`: A boolean flag. If `true`, creating this `Error` object will immediately halt the program and print a stack trace to `stderr`. If `false` (the default), the `Error` object is treated as a normal value.
    * `alternative`: A fallback value that can be used by the calling code as a default or recovery value if an error occurs.

#### 6.2. Signaling Errors with `raise`

The `raise` keyword is the primary mechanism for signaling an error. It is syntactic sugar for `return std.Error(...)`. This means that any function that can fail must explicitly declare `Error` as part of its return type using a union type.

```mxscript
// This function explicitly states it can return an int or an Error.
func process_data(data: string) -> int | Error {
    if data.is_empty() {
        // Signal a recoverable error with a fallback value.
        raise std.Error("ValueError", "Input data cannot be empty.", alternative=0);
    }
    // ...
}

func divide(a: int, b: int) -> int {
    if b == 0 {
        // Signal a fatal, unrecoverable error.
        raise std.Error("ZeroDivisionError", "Divisor cannot be zero.", panic=true);
    }
    return a / b;
}
```

#### 6.3. Handling Errors with `match`

Because functions that can fail return a union type (e.g., `int | Error`), the `match` statement is the primary tool for safely handling the result. It forces the developer to explicitly consider both the success case and the error case, preventing unhandled errors.

```mxscript
func main() -> int {
    let result = process_data("");

    let value = match (result) {
        // Case 1: The function returned an Error object.
        case e: Error => {
            println("An error occurred:", e.message);
            // Use the provided alternative value to recover.
            e.alternative;
        }
        // Case 2: The function returned an int.
        case i: int => {
            println("Success! The value is:", i);
            i;
        }
    };

    println("The final value is:", value); // Prints "The final value is: 0"

    // This call will terminate the program if the condition is met.
    let calculation = divide(10, 0);

    return 0;
}
```