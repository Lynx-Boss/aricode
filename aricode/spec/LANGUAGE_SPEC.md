# aricode (Ari Code) - Language Specification v1.0

## 1. Overview

aricode is a statically typed, compiled programming language that produces optimized x86_64 machine code. It uses JavaScript-like syntax with strict type safety, mandatory error handling, and zero tolerance for silent failures.

**Target**: x86_64 native machine code (ELF binaries on Linux x86_64).

**File extension**: `.ari`

**Entry point**: Every aricode program must have a `main` function:

```
fn main() -> i32 {
  return 0;
}
```

---

## 2. Core Philosophy

1. **Familiar syntax**: JavaScript developers can read aricode immediately. Curly braces, semicolons, `let`/`const`, arrow-style return types.
2. **Compiled to metal**: No interpreter, no VM, no GC. aricode compiles directly to x86_64 machine code through its own backend.
3. **Zero silent errors**: Every possible failure path must be explicitly handled. The compiler performs exhaustive static analysis to detect unhandled error conditions. Code that can fail silently will not compile.
4. **Bad code does not ship**: The compiler rejects inefficient patterns, unused bindings, unreachable code, and ambiguous logic. The Podium system rates every function's machine code quality.

---

## 3. Lexical Structure

### 3.1 Character Set

Source files are UTF-8 encoded. Identifiers may contain ASCII letters, digits, and underscores. They must start with a letter or underscore.

### 3.2 Keywords

The following identifiers are reserved and cannot be used as variable or function names:

```
let       const     fn        return    if        else
for       while     loop      break     continue  match
try       catch     finally   throw     import    export
module    struct    enum      impl      trait     type
pub       priv      mut       ref       self      true
false     Some      None      as        in        typeof
sizeof    alignof   async     await     yield     spawn
```

### 3.3 Comments

```
// Single-line comment

/* 
   Multi-line comment.
   These do NOT nest.
*/

/// Documentation comment (attached to the next item).
/// Parsed by the doc generator.
```

### 3.4 Semicolons

Semicolons are **mandatory** at the end of every statement. The compiler will not insert them. Missing semicolons produce a Level 1 compile error.

### 3.5 Whitespace

Whitespace (spaces, tabs, newlines) is insignificant outside of string literals. Indentation is not syntactically meaningful but the compiler will emit a Level 2 warning for inconsistent indentation within a file.

---

## 4. Type System

### 4.1 Primitive Types

| Type   | Size    | Description                        | Range / Notes                          |
|--------|---------|------------------------------------|----------------------------------------|
| `i8`   | 1 byte  | Signed 8-bit integer               | -128 to 127                            |
| `i16`  | 2 bytes | Signed 16-bit integer              | -32,768 to 32,767                      |
| `i32`  | 4 bytes | Signed 32-bit integer              | -2^31 to 2^31-1                        |
| `i64`  | 8 bytes | Signed 64-bit integer              | -2^63 to 2^63-1                        |
| `u8`   | 1 byte  | Unsigned 8-bit integer             | 0 to 255                               |
| `u16`  | 2 bytes | Unsigned 16-bit integer            | 0 to 65,535                            |
| `u32`  | 4 bytes | Unsigned 32-bit integer            | 0 to 2^32-1                            |
| `u64`  | 8 bytes | Unsigned 64-bit integer            | 0 to 2^64-1                            |
| `f32`  | 4 bytes | 32-bit IEEE 754 float              | ~7 decimal digits precision            |
| `f64`  | 8 bytes | 64-bit IEEE 754 float              | ~15 decimal digits precision           |
| `bool` | 1 byte  | Boolean                            | `true` or `false`                      |
| `str`  | varies  | UTF-8 encoded string               | Immutable, heap-allocated              |
| `char` | 4 bytes | Single Unicode scalar value        | U+0000 to U+10FFFF (excluding surrogates) |
| `void` | 0 bytes | No value (function return only)    | Cannot be used as a variable type      |

### 4.2 Compound Types

#### Arrays: `arr<T>`

Fixed-type, dynamically-sized arrays. The element type must be specified.

```
let numbers: arr<i32> = [1, 2, 3, 4, 5];
let empty: arr<str> = [];

// Access (bounds-checked at runtime - Level 1 error if out of bounds)
let first: i32 = numbers[0];

// Length
let len: u64 = numbers.len();

// Append
numbers.push(6);

// Slice (returns a new array)
let sub: arr<i32> = numbers.slice(1, 3);  // [2, 3]
```

#### Fixed Arrays: `fixed_arr<T, N>`

Compile-time fixed-size arrays. Allocated on the stack.

```
let buffer: fixed_arr<u8, 1024> = [0; 1024];  // 1024 zeros
```

#### Maps: `map<K, V>`

Hash maps with typed keys and values. Keys must implement the `Hashable` trait.

```
let ages: map<str, i32> = {
  "alice": 30,
  "bob": 25
};

// Access returns Option<V> - never panics
let age: Option<i32> = ages.get("alice");

// Insert
ages.set("charlie", 35);

// Check existence
let exists: bool = ages.has("alice");

// Remove
ages.remove("bob");

// Length
let count: u64 = ages.len();
```

#### Tuples: `(T1, T2, ...)`

Fixed-size, heterogeneous ordered collections.

```
let pair: (i32, str) = (42, "hello");
let x: i32 = pair.0;
let s: str = pair.1;
```

### 4.3 The Null Problem: `Option<T>`

**There is no `null` in aricode.** Any value that might be absent is represented as `Option<T>`.

```
// Option<T> is an enum with two variants:
enum Option<T> {
  Some(T),
  None
}
```

The compiler **forces** exhaustive handling of `Option<T>` wherever it is used:

```
let maybe: Option<i32> = Some(42);

// This WON'T COMPILE - not all cases handled:
// let val: i32 = maybe;  // ERROR: Level 1 - Option<i32> is not i32

// This WILL compile:
match (maybe) {
  Some(val) => print(val),
  None => print("nothing")
}

// Shorthand with unwrap_or:
let val: i32 = maybe.unwrap_or(0);

// Explicit unwrap - compiles but inserts runtime check:
// Panics with Level 1 error if None
let val: i32 = maybe.unwrap();
```

### 4.4 Type Aliases

```
type UserId = u64;
type Callback = fn(i32) -> bool;
type Result<T> = Option<(T, Error)>;
```

### 4.5 Type Inference

aricode supports local type inference using `let` without an explicit type annotation, but ONLY when the type is unambiguous from the right-hand side:

```
let x = 10;           // Inferred as i32 (default integer type)
let y = 3.14;         // Inferred as f64 (default float type)
let s = "hello";      // Inferred as str
let b = true;         // Inferred as bool
let nums = [1, 2, 3]; // Inferred as arr<i32>

// Function return types are NEVER inferred - must be explicit
fn add(a: i32, b: i32) -> i32 { return a + b; }

// Ambiguous cases produce Level 1 error:
// let x = [];  // ERROR: cannot infer element type of empty array
```

### 4.6 Type Casting

Implicit casts are **forbidden**. All conversions must be explicit using the `as` keyword.

```
let x: i32 = 42;
let y: i64 = x as i64;   // Widening - always safe
let z: i16 = x as i16;   // Narrowing - compiler inserts overflow check

let f: f64 = 3.14;
let i: i32 = f as i32;   // Truncation - compiler emits Level 2 warning

// Lossy casts require explicit acknowledgment:
let big: i64 = 999999999999;
let small: i32 = big as! i32;  // as! = "I know this might overflow"
                                // Runtime Level 1 error if it does
```

---

## 5. Variables and Constants

### 5.1 Variable Declaration

```
let x: i32 = 10;          // Mutable by default
let mut y: i32 = 20;      // Explicitly mutable (same as above - let is mutable)
const PI: f64 = 3.14159;  // Immutable, value known at compile time
```

Wait -- design decision: `let` in aricode is **mutable by default** (matching JS familiarity), but a `const` binding is immutable. This departs from Rust's `let` being immutable.

```
let x: i32 = 10;
x = 20;       // OK - let bindings are mutable

const Y: i32 = 10;
Y = 20;       // ERROR: Level 1 - cannot assign to const binding
```

### 5.2 Shadowing

Variable shadowing is **forbidden**. Reusing a variable name in the same scope or a child scope produces a Level 1 error.

```
let x: i32 = 10;
let x: str = "hello";  // ERROR: Level 1 - variable 'x' already declared in this scope

if (true) {
  let x: i32 = 5;  // ERROR: Level 1 - 'x' shadows outer variable
}
```

### 5.3 Unused Variables

Unused variables produce a Level 2 warning. This can be configured to be a blocking error via compiler flags.

```
let x: i32 = 10;
// x is never used
// WARNING: Level 2 - variable 'x' is declared but never used
```

To suppress, prefix with underscore:

```
let _x: i32 = 10;  // OK - underscore prefix signals intentionally unused
```

---

## 6. Functions

### 6.1 Declaration

```
fn name(param1: Type1, param2: Type2) -> ReturnType {
  // body
  return value;
}
```

Return type is mandatory. Use `-> void` for functions that return nothing.

```
fn greet(name: str) -> void {
  print("Hello, " + name);
}
```

### 6.2 Multiple Return Values

Functions can return tuples:

```
fn divide(a: i32, b: i32) -> (i32, i32) {
  if (b == 0) {
    error.raise(Level.LOGIC, "Division by zero");
  }
  return (a / b, a % b);
}

let (quotient, remainder) = divide(10, 3);
```

### 6.3 Default Parameters

```
fn connect(host: str, port: u16 = 8080, timeout: u32 = 5000) -> Connection {
  // ...
}

connect("localhost");              // port=8080, timeout=5000
connect("localhost", 3000);        // port=3000, timeout=5000
connect("localhost", 3000, 10000); // port=3000, timeout=10000
```

### 6.4 Named Arguments

When calling functions with more than 3 parameters, named arguments are **required** (Level 2 warning otherwise):

```
fn create_user(name: str, age: i32, email: str, role: str) -> User {
  // ...
}

// WARNING: Level 2 - function with 4+ params should use named arguments
create_user("Alice", 30, "alice@example.com", "admin");

// OK:
create_user(name: "Alice", age: 30, email: "alice@example.com", role: "admin");
```

### 6.5 Function Overloading

aricode does **not** support function overloading. Each function name must be unique within its scope. Use generics or different names instead.

### 6.6 Closures / Anonymous Functions

```
let add = fn(a: i32, b: i32) -> i32 { return a + b; };

// Short form for single-expression closures:
let double = fn(x: i32) -> i32 => x * 2;

// As callback:
numbers.map(fn(x: i32) -> i32 => x * 2);
```

### 6.7 Pure Functions

Functions marked `pure` have no side effects. The compiler verifies this statically.

```
pure fn add(a: i32, b: i32) -> i32 {
  return a + b;
}

pure fn bad(a: i32) -> i32 {
  print(a);      // ERROR: Level 1 - pure function cannot have side effects
  return a + 1;
}
```

Pure functions enable aggressive compiler optimizations (memoization, reordering, parallelization).

---

## 7. Control Flow

### 7.1 Conditionals

```
if (condition) {
  // body
} else if (other_condition) {
  // body
} else {
  // body
}
```

The condition must be of type `bool`. No truthy/falsy coercion.

```
let x: i32 = 5;
if (x) { }       // ERROR: Level 1 - condition must be bool, got i32
if (x != 0) { }  // OK
```

### 7.2 Match (Pattern Matching)

```
match (value) {
  1 => do_one(),
  2 => do_two(),
  3 | 4 => do_three_or_four(),
  5..=10 => do_range(),
  _ => do_default()
}
```

**Match must be exhaustive.** The `_` (wildcard) pattern is required unless all possible values of the type are covered.

```
let b: bool = true;
match (b) {
  true => "yes",
  false => "no"
  // No _ needed - bool only has two values
}

let x: i32 = 5;
match (x) {
  1 => "one",
  2 => "two"
  // ERROR: Level 1 - match is not exhaustive, missing wildcard or remaining values
}
```

Match on enums:

```
enum Color { Red, Green, Blue }

let c: Color = Color.Red;
match (c) {
  Color.Red => "red",
  Color.Green => "green",
  Color.Blue => "blue"
  // Exhaustive - no _ needed
}
```

Match on Option:

```
let val: Option<i32> = Some(10);
match (val) {
  Some(x) => print(x),
  None => print("empty")
}
```

### 7.3 Loops

#### For Loop

```
// C-style
for (let i: i32 = 0; i < 10; i++) {
  print(i);
}

// Range-based
for (i in 0..10) {
  print(i);   // 0 through 9
}

for (i in 0..=10) {
  print(i);   // 0 through 10 inclusive
}

// Iterator-based
for (item in collection) {
  print(item);
}

// With index
for (index, item in collection.enumerate()) {
  print(index, item);
}
```

#### While Loop

```
while (condition) {
  // body
}
```

The compiler checks for infinite loops. A `while (true)` without a reachable `break` or `return` produces a Level 2 warning.

#### Loop (Infinite)

```
loop {
  // runs forever until break/return
  if (done) {
    break;
  }
}
```

### 7.4 Break and Continue

```
for (i in 0..100) {
  if (i == 50) { break; }
  if (i % 2 == 0) { continue; }
  print(i);
}
```

Labeled loops for nested break:

```
outer: for (i in 0..10) {
  inner: for (j in 0..10) {
    if (i + j == 15) {
      break outer;
    }
  }
}
```

---

## 8. Error Handling

This is the defining feature of aricode. See `ERROR_LEVELS.md` for the complete error level specification.

### 8.1 Error Levels

| Level | Name           | Meaning                           | Compiler Behavior                         |
|-------|----------------|-----------------------------------|-------------------------------------------|
| 0     | SILENT_PASS    | Silent error ignoring             | **FORBIDDEN** - this level cannot exist   |
| 1     | LOGIC_ERROR    | Programmer logic mistake          | **BLOCKS COMPILATION** if unhandled       |
| 2     | RUNTIME_WARNING| Suspicious but functional code    | Logged, optionally blocks compilation     |
| 3     | SYSTEM_ERROR   | External system failure           | Must be caught with try/catch             |
| 4     | CATASTROPHIC   | Unrecoverable failure             | Log and graceful exit                     |

### 8.2 Level 1: Compile-Time Safety Analysis

The compiler performs static analysis to detect all possible Level 1 errors. If a code path can produce a Level 1 error and it is not explicitly handled, the code **will not compile**.

The compiler detects:
- Division where the divisor could be zero
- Array access where the index could be out of bounds
- Option unwrap without prior None check
- Integer overflow in arithmetic
- Unreachable pattern match arms
- Type mismatches

```
// WON'T COMPILE:
fn bad(a: i32, b: i32) -> i32 {
  return a / b;
  // ERROR: Level 1 - possible division by zero. 'b' could be 0.
  // Handle with: guard check, or use checked_div()
}

// WILL COMPILE:
fn good(a: i32, b: i32) -> i32 {
  if (b == 0) {
    error.raise(Level.LOGIC, "Division by zero");
  }
  return a / b;
}

// ALSO WILL COMPILE (using checked arithmetic):
fn also_good(a: i32, b: i32) -> Option<i32> {
  return a.checked_div(b);
}
```

### 8.3 error.raise()

The `error.raise()` function terminates the current function's execution and propagates the error up the call stack. It is the primary mechanism for reporting Level 1 errors.

```
error.raise(Level.LOGIC, "descriptive message");
error.raise(Level.LOGIC, "value out of range: " + value.to_str());
```

When `error.raise` is called:
1. The error is logged to the error registry (`.aricode/errors.log`)
2. The current function's stack frame is unwound
3. If the caller has a `try/catch`, the error is caught there
4. If no `try/catch` exists up the call stack, the program terminates with a full stack trace

### 8.4 try/catch/finally

```
try {
  let data: str = file.read("config.txt");
  let parsed: Config = parse_config(data);
} catch (e: LogicError) {
  // Handle Level 1 errors
  log.error("Config parse failed: " + e.message);
  return default_config();
} catch (e: SystemError) {
  // Handle Level 3 errors (file not found, permission denied, etc.)
  log.error("Cannot read config: " + e.message);
  return default_config();
} finally {
  // Always runs
  cleanup();
}
```

**Rules:**
- Empty `catch` blocks are **forbidden** (Level 1 error). You must do something with the error.
- Catching an error and doing nothing produces: `ERROR: Level 1 - caught error is not handled`
- At minimum, the error must be logged: `log.error(e);`

### 8.5 Error Propagation with `?`

Functions that can produce errors declare so in their return type using `Result<T, E>`:

```
fn read_number(path: str) -> Result<i32, SystemError> {
  let content: str = file.read(path)?;  // ? propagates error to caller
  let num: i32 = content.parse_i32()?;
  return Ok(num);
}

// Caller must handle:
match (read_number("num.txt")) {
  Ok(n) => print(n),
  Err(e) => log.error(e)
}
```

### 8.6 Result<T, E> Type

```
enum Result<T, E> {
  Ok(T),
  Err(E)
}
```

Like `Option<T>`, `Result<T, E>` must be exhaustively matched. Ignoring a `Result` is a Level 1 error.

```
file.write("out.txt", data);
// ERROR: Level 1 - Result<void, SystemError> is not handled.
// The write could fail and you are ignoring it.

// Fix:
let result: Result<void, SystemError> = file.write("out.txt", data);
match (result) {
  Ok(_) => {},
  Err(e) => log.error(e)
}

// Or with ?:
file.write("out.txt", data)?;
```

### 8.7 Error Registry

All errors (compile-time and runtime) are logged to `.aricode/errors.log` in the project root.

Log entry format:
```
[2026-04-11T14:30:00.000Z] LEVEL=1 FILE=src/main.ari LINE=42 COL=15
  MESSAGE: Division by zero
  FUNCTION: calculate_average
  STACK:
    at calculate_average (src/main.ari:42:15)
    at process_data (src/data.ari:108:3)
    at main (src/main.ari:12:5)
```

Querying the error log programmatically:

```
import aricode.errors;

let recent: arr<ErrorEntry> = errors.query(
  level: Level.LOGIC,
  since: "2026-04-11",
  file: "src/main.ari"
);

for (entry in recent) {
  print(entry.timestamp, entry.message);
}
```

---

## 9. Structs

### 9.1 Declaration

```
struct User {
  name: str,
  age: i32,
  email: str,
  active: bool
}
```

### 9.2 Instantiation

```
let user: User = User {
  name: "Alice",
  age: 30,
  email: "alice@example.com",
  active: true
};
```

All fields must be initialized. There are no default values unless specified:

```
struct Config {
  host: str = "localhost",
  port: u16 = 8080,
  debug: bool = false
}

let cfg: Config = Config {};  // Uses all defaults
let cfg2: Config = Config { port: 3000 };  // Override port only
```

### 9.3 Methods (impl blocks)

```
impl User {
  fn new(name: str, age: i32, email: str) -> User {
    return User {
      name: name,
      age: age,
      email: email,
      active: true
    };
  }

  fn greet(self) -> str {
    return "Hello, " + self.name;
  }

  fn set_age(self, new_age: i32) -> void {
    if (new_age < 0 || new_age > 150) {
      error.raise(Level.LOGIC, "Invalid age: " + new_age.to_str());
    }
    self.age = new_age;
  }
}

let user: User = User.new("Alice", 30, "alice@example.com");
print(user.greet());
```

### 9.4 Visibility

By default, struct fields and methods are **private** to the module. Use `pub` for public access.

```
pub struct User {
  pub name: str,
  pub age: i32,
  email: str,       // private - only accessible within this module
  active: bool      // private
}

impl User {
  pub fn greet(self) -> str { return "Hello, " + self.name; }
  fn internal(self) -> void { }  // private method
}
```

---

## 10. Enums

### 10.1 Simple Enums

```
enum Direction {
  North,
  South,
  East,
  West
}

let d: Direction = Direction.North;
```

### 10.2 Enums with Data

```
enum Shape {
  Circle(f64),                    // radius
  Rectangle(f64, f64),            // width, height
  Triangle(f64, f64, f64)         // three sides
}

let s: Shape = Shape.Circle(5.0);

match (s) {
  Shape.Circle(r) => print("Circle with radius " + r.to_str()),
  Shape.Rectangle(w, h) => print("Rectangle " + w.to_str() + "x" + h.to_str()),
  Shape.Triangle(a, b, c) => print("Triangle")
}
```

### 10.3 Enum Methods

```
impl Shape {
  fn area(self) -> f64 {
    match (self) {
      Shape.Circle(r) => return 3.14159 * r * r,
      Shape.Rectangle(w, h) => return w * h,
      Shape.Triangle(a, b, c) => {
        let s: f64 = (a + b + c) / 2.0;
        return (s * (s - a) * (s - b) * (s - c)).sqrt();
      }
    }
  }
}
```

---

## 11. Traits (Interfaces)

### 11.1 Declaration

```
trait Printable {
  fn to_string(self) -> str;
}

trait Comparable {
  fn compare(self, other: Self) -> i32;
  
  // Default implementation
  fn equals(self, other: Self) -> bool {
    return self.compare(other) == 0;
  }
}
```

### 11.2 Implementation

```
impl Printable for User {
  fn to_string(self) -> str {
    return "User(" + self.name + ", " + self.age.to_str() + ")";
  }
}
```

### 11.3 Trait Bounds

```
fn print_all<T: Printable>(items: arr<T>) -> void {
  for (item in items) {
    print(item.to_string());
  }
}

// Multiple bounds:
fn process<T: Printable + Comparable>(a: T, b: T) -> void {
  if (a.equals(b)) {
    print(a.to_string());
  }
}
```

---

## 12. Generics

### 12.1 Generic Functions

```
fn max<T: Comparable>(a: T, b: T) -> T {
  if (a.compare(b) > 0) {
    return a;
  }
  return b;
}
```

### 12.2 Generic Structs

```
struct Stack<T> {
  items: arr<T>,
  size: u64
}

impl<T> Stack<T> {
  fn new() -> Stack<T> {
    return Stack { items: [], size: 0 };
  }

  fn push(self, item: T) -> void {
    self.items.push(item);
    self.size = self.size + 1;
  }

  fn pop(self) -> Option<T> {
    if (self.size == 0) {
      return None;
    }
    self.size = self.size - 1;
    return Some(self.items.remove_last());
  }
}
```

### 12.3 Monomorphization

Generics in aricode are monomorphized at compile time. `Stack<i32>` and `Stack<str>` produce separate, specialized machine code. There is no runtime generic dispatch overhead.

---

## 13. Modules and Imports

### 13.1 Module Declaration

Each `.ari` file is a module. The module name is derived from the file path.

```
// File: src/math/vector.ari
module math.vector;

pub struct Vec2 {
  pub x: f64,
  pub y: f64
}

pub fn dot(a: Vec2, b: Vec2) -> f64 {
  return a.x * b.x + a.y * b.y;
}
```

### 13.2 Imports

```
import math.vector;                    // Import module
import math.vector.Vec2;              // Import specific item
import math.vector.{Vec2, dot};       // Import multiple items
import math.vector.*;                 // Import all public items (Level 2 warning)
```

### 13.3 Standard Library Modules

```
import std.io;          // File I/O, stdin/stdout
import std.net;         // Networking (TCP, UDP, HTTP)
import std.math;        // Math functions
import std.collections; // Additional data structures
import std.string;      // String utilities
import std.time;        // Date/time
import std.os;          // OS interaction
import std.thread;      // Threading
import std.sync;        // Synchronization primitives
import std.crypto;      // Cryptographic functions
import std.json;        // JSON parsing/serialization
```

---

## 14. Memory Management

### 14.1 Ownership Model

aricode uses a **compile-time ownership model** (similar in concept to Rust, but simplified for JS-like ergonomics).

Rules:
1. Every value has exactly one owner.
2. When the owner goes out of scope, the value is dropped (memory freed).
3. Values can be **borrowed** (referenced) without transferring ownership.

```
let name: str = "Alice";
let greeting: str = make_greeting(name);  // name is MOVED into the function
// print(name);  // ERROR: Level 1 - 'name' was moved and is no longer valid

fn make_greeting(n: str) -> str {
  return "Hello, " + n;
}
```

### 14.2 Borrowing

```
let name: str = "Alice";
let greeting: str = make_greeting(&name);  // borrow - name is still valid
print(name);  // OK

fn make_greeting(n: &str) -> str {
  return "Hello, " + n;
}
```

### 14.3 Mutable Borrowing

```
fn increment(val: &i32) -> void {
  val = val + 1;  // ERROR: Level 1 - cannot mutate immutable borrow
}

fn increment(val: &mut i32) -> void {
  val = val + 1;  // OK - mutable borrow
}

let x: i32 = 10;
increment(&mut x);
// x is now 11
```

Rules:
- You can have many `&T` (immutable borrows) at the same time.
- You can have exactly ONE `&mut T` (mutable borrow) at a time.
- You cannot have `&T` and `&mut T` at the same time.

### 14.4 Stack vs Heap

- Primitives (`i32`, `f64`, `bool`, etc.) live on the stack.
- `str`, `arr<T>`, `map<K,V>` are heap-allocated with a stack pointer.
- Structs live on the stack by default. Use `Box<T>` for heap allocation.

```
let x: i32 = 42;                  // Stack
let s: str = "hello";             // Pointer on stack, data on heap
let boxed: Box<User> = Box.new(User { ... });  // Heap-allocated struct
```

---

## 15. Concurrency

### 15.1 Threads

```
import std.thread;

let handle: Thread = thread.spawn(fn() -> void {
  print("Hello from thread");
});

handle.join();  // Wait for thread to finish - returns Result
```

### 15.2 Channels

```
import std.sync;

let (tx, rx): (Sender<i32>, Receiver<i32>) = sync.channel();

thread.spawn(fn() -> void {
  tx.send(42);
});

let value: i32 = rx.recv().unwrap();
```

### 15.3 Mutex

```
import std.sync;

let counter: Mutex<i32> = Mutex.new(0);

for (i in 0..10) {
  let c = &counter;
  thread.spawn(fn() -> void {
    let lock = c.lock();
    lock.value = lock.value + 1;
    // lock is automatically released when it goes out of scope
  });
}
```

### 15.4 Async/Await

```
async fn fetch_data(url: str) -> Result<str, SystemError> {
  let response = await http.get(url)?;
  return Ok(response.body);
}

async fn main() -> i32 {
  let data = await fetch_data("https://api.example.com/data");
  match (data) {
    Ok(body) => print(body),
    Err(e) => log.error(e)
  }
  return 0;
}
```

---

## 16. Operators

### 16.1 Arithmetic

| Operator | Description    | Types                    |
|----------|---------------|--------------------------|
| `+`      | Addition      | Integers, floats, str (concatenation) |
| `-`      | Subtraction   | Integers, floats         |
| `*`      | Multiplication| Integers, floats         |
| `/`      | Division      | Integers, floats (Level 1 if divisor could be 0) |
| `%`      | Modulo        | Integers (Level 1 if divisor could be 0) |
| `**`     | Power         | Integers, floats         |

### 16.2 Comparison

| Operator | Description       |
|----------|-------------------|
| `==`     | Equal             |
| `!=`     | Not equal         |
| `<`      | Less than         |
| `>`      | Greater than      |
| `<=`     | Less or equal     |
| `>=`     | Greater or equal  |

All comparisons return `bool`. Both operands must be the same type. No implicit coercion.

### 16.3 Logical

| Operator | Description |
|----------|-------------|
| `&&`     | Logical AND |
| `\|\|`  | Logical OR  |
| `!`      | Logical NOT |

Operands must be `bool`. Short-circuit evaluation applies.

### 16.4 Bitwise

| Operator | Description     |
|----------|-----------------|
| `&`      | Bitwise AND     |
| `\|`     | Bitwise OR      |
| `^`      | Bitwise XOR     |
| `~`      | Bitwise NOT     |
| `<<`     | Left shift      |
| `>>`     | Right shift     |

### 16.5 Assignment

| Operator | Description         |
|----------|---------------------|
| `=`      | Assign              |
| `+=`     | Add and assign      |
| `-=`     | Subtract and assign |
| `*=`     | Multiply and assign |
| `/=`     | Divide and assign   |
| `%=`     | Modulo and assign   |
| `&=`     | AND and assign      |
| `\|=`   | OR and assign       |
| `^=`    | XOR and assign      |
| `<<=`    | Left shift assign   |
| `>>=`    | Right shift assign  |

### 16.6 Operator Overloading

Operators can be overloaded by implementing the corresponding trait:

```
trait Add<Rhs, Output> {
  fn add(self, rhs: Rhs) -> Output;
}

impl Add<Vec2, Vec2> for Vec2 {
  fn add(self, rhs: Vec2) -> Vec2 {
    return Vec2 { x: self.x + rhs.x, y: self.y + rhs.y };
  }
}

let a: Vec2 = Vec2 { x: 1.0, y: 2.0 };
let b: Vec2 = Vec2 { x: 3.0, y: 4.0 };
let c: Vec2 = a + b;  // Uses Add trait
```

---

## 17. String Handling

### 17.1 String Literals

```
let s: str = "hello world";
let multiline: str = "line one\nline two";
let raw: str = r"no \n escaping here";
let interpolated: str = f"Hello, {name}! You are {age} years old.";
```

### 17.2 Escape Sequences

| Sequence | Character        |
|----------|------------------|
| `\n`     | Newline          |
| `\t`     | Tab              |
| `\r`     | Carriage return  |
| `\\`     | Backslash        |
| `\"`     | Double quote     |
| `\0`     | Null byte        |
| `\x41`   | Hex byte (A)     |
| `\u{1F600}` | Unicode scalar |

### 17.3 String Methods

```
let s: str = "Hello, World!";

s.len()           // 13 (byte length)
s.char_count()    // 13 (Unicode scalar count)
s.to_upper()      // "HELLO, WORLD!"
s.to_lower()      // "hello, world!"
s.trim()          // Remove leading/trailing whitespace
s.split(",")      // arr<str>: ["Hello", " World!"]
s.contains("World") // true
s.starts_with("He") // true
s.ends_with("!")     // true
s.replace("World", "aricode")  // "Hello, aricode!"
s.substring(0, 5)   // "Hello"
s.char_at(0)         // Some('H')
s.index_of("World")  // Some(7)
s.repeat(3)          // "Hello, World!Hello, World!Hello, World!"
```

Strings are immutable. All methods return new strings.

---

## 18. Standard Library

### 18.1 I/O

```
import std.io;

// Console
io.print("hello");           // Print without newline
io.println("hello");         // Print with newline
let input: str = io.read_line();  // Read line from stdin

// File I/O (all return Result)
let content: str = io.file.read("path.txt")?;
io.file.write("path.txt", content)?;
io.file.append("path.txt", "more data")?;
let exists: bool = io.file.exists("path.txt");
io.file.delete("path.txt")?;
io.file.copy("src.txt", "dst.txt")?;

// Buffered I/O
let reader: BufferedReader = io.file.open_read("large.txt")?;
while (let line = reader.read_line()) {
  match (line) {
    Some(l) => process(l),
    None => break
  }
}
reader.close();
```

### 18.2 Math

```
import std.math;

math.abs(-5)          // 5
math.max(3, 7)        // 7
math.min(3, 7)        // 3
math.floor(3.7)       // 3.0
math.ceil(3.2)        // 4.0
math.round(3.5)       // 4.0
math.sqrt(16.0)       // 4.0
math.pow(2.0, 10.0)   // 1024.0
math.log(100.0, 10.0) // 2.0
math.sin(0.0)         // 0.0
math.cos(0.0)         // 1.0
math.PI               // 3.14159265358979...
math.E                // 2.71828182845904...
math.random()         // Random f64 in [0.0, 1.0)
math.random_range(1, 100)  // Random i32 in [1, 100]
```

### 18.3 Collections

```
import std.collections;

// Linked list
let list: LinkedList<i32> = LinkedList.new();

// Set (hash set)
let set: Set<str> = Set.from(["a", "b", "c"]);
set.insert("d");
set.contains("a");  // true
set.remove("b");

// Deque (double-ended queue)
let dq: Deque<i32> = Deque.new();
dq.push_front(1);
dq.push_back(2);

// Priority queue
let pq: PriorityQueue<i32> = PriorityQueue.new();
pq.push(5);
pq.push(1);
pq.push(3);
let min: Option<i32> = pq.pop();  // Some(1)
```

---

## 19. Compilation Model

### 19.1 Compiler Phases

```
Source (.ari files)
    |
    v
[1. Lexer] --> Token stream
    |
    v
[2. Parser] --> Abstract Syntax Tree (AST)
    |
    v
[3. Type Checker] --> Typed AST
    |
    v
[4. Error Analysis] --> Verify all error paths handled
    |
    v
[5. Ownership Analysis] --> Verify memory safety
    |
    v
[6. IR Generation] --> aricode Intermediate Representation
    |
    v
[7. Optimization] --> Optimized IR
    |
    v
[8. x86_64 Code Gen] --> Assembly / Machine code
    |
    v
[9. Podium Rating] --> Performance analysis per function
    |
    v
[10. Linking] --> Executable binary
```

### 19.2 Compiler Invocation

```bash
aric build src/main.ari              # Compile to executable
aric build src/main.ari -o myapp     # Specify output name
aric build src/main.ari --release    # Optimized release build
aric build src/main.ari --debug      # Debug build with symbols
aric check src/main.ari              # Type check without compiling
aric run src/main.ari                # Compile and run
aric podium src/main.ari             # Show podium ratings only
aric errors                         # Query error log
aric fmt src/                       # Format source files
aric test src/                      # Run tests
```

### 19.3 Compiler Flags

| Flag                     | Description                                    |
|--------------------------|------------------------------------------------|
| `--release`              | Enable all optimizations (O3 equivalent)       |
| `--debug`                | Include debug symbols, disable optimizations   |
| `--warn-as-error`        | Treat Level 2 warnings as Level 1 errors       |
| `--podium-min=GOLD`      | Reject functions below specified podium level   |
| `--target=x86_64-linux`  | Cross-compilation target                       |
| `--emit-asm`             | Also output assembly listing                   |
| `--emit-ir`              | Also output aricode IR                          |
| `--no-std`               | Compile without standard library               |
| `--opt-level=0\|1\|2\|3` | Optimization level                             |
| `--error-log=path`       | Custom error log path                          |

### 19.4 Project Structure

```
myproject/
  aricode.toml          # Project manifest
  src/
    main.ari            # Entry point
    lib.ari             # Library root (for libraries)
    utils/
      math.ari
      string.ari
  test/
    test_main.ari
    test_utils.ari
  .aricode/
    errors.log         # Error registry
    podium.json        # Cached podium ratings
```

### 19.5 aricode.toml (Project Manifest)

```toml
[project]
name = "myapp"
version = "1.0.0"
authors = ["Alice <alice@example.com>"]
edition = "2026"

[build]
target = "x86_64-linux"
opt_level = 3
warn_as_error = true
podium_min = "silver"

[dependencies]
http = "2.1.0"
json = "1.3.0"

[dev-dependencies]
test_utils = "0.5.0"
```

---

## 20. Testing

### 20.1 Test Functions

```
import std.test;

#[test]
fn test_addition() -> void {
  assert.equal(add(2, 3), 5);
}

#[test]
fn test_division_by_zero() -> void {
  assert.raises(Level.LOGIC, fn() -> void {
    divide(10, 0);
  });
}

#[test]
fn test_option_handling() -> void {
  let val: Option<i32> = Some(42);
  assert.is_some(val);
  assert.equal(val.unwrap(), 42);

  let none_val: Option<i32> = None;
  assert.is_none(none_val);
}
```

### 20.2 Test Assertions

```
assert.equal(a, b)              // a == b
assert.not_equal(a, b)          // a != b
assert.true(expr)               // expr is true
assert.false(expr)              // expr is false
assert.is_some(option)          // option is Some
assert.is_none(option)          // option is None
assert.is_ok(result)            // result is Ok
assert.is_err(result)           // result is Err
assert.raises(level, fn)        // fn raises an error at the given level
assert.approx(a, b, epsilon)   // |a - b| < epsilon (for floats)
```

### 20.3 Benchmarks

```
#[bench]
fn bench_sort() -> void {
  let data: arr<i32> = generate_random(10000);
  bench.iter(fn() -> void {
    sort(data.clone());
  });
}
```

---

## 21. Attributes

Attributes provide metadata to the compiler:

```
#[test]           // Mark as test function
#[bench]          // Mark as benchmark
#[inline]         // Suggest inlining
#[inline(always)] // Force inlining
#[inline(never)]  // Prevent inlining
#[deprecated("use new_fn instead")]  // Mark as deprecated
#[allow(unused)]  // Suppress unused warning for this item
#[cold]           // Mark as rarely called (affects code layout)
#[hot]            // Mark as frequently called (affects code layout)
#[pure]           // Assert function is pure (same as pure keyword)
#[no_mangle]      // Preserve symbol name for FFI
#[export]         // Export for C ABI interop
```

---

## 22. Foreign Function Interface (FFI)

### 22.1 Calling C from aricode

```
#[extern("C")]
fn printf(fmt: *u8, ...) -> i32;

#[extern("C")]
fn malloc(size: u64) -> *void;

#[extern("C")]
fn free(ptr: *void) -> void;
```

### 22.2 Exposing aricode to C

```
#[export]
#[no_mangle]
pub fn aricode_add(a: i32, b: i32) -> i32 {
  return a + b;
}
```

### 22.3 Raw Pointers

Raw pointers are allowed only inside `unsafe` blocks:

```
unsafe {
  let ptr: *i32 = malloc(4) as *i32;
  *ptr = 42;
  let val: i32 = *ptr;
  free(ptr as *void);
}
```

`unsafe` blocks bypass ownership checks. The Podium system automatically rates any function containing `unsafe` as BRONZE at best.

---

## 23. Compiler Guarantees

The aricode compiler guarantees the following for safe code (no `unsafe` blocks):

1. **No null pointer dereferences** - enforced by Option<T>
2. **No buffer overflows** - all array access is bounds-checked
3. **No use-after-free** - enforced by ownership model
4. **No data races** - enforced by borrow checker across threads
5. **No unhandled errors** - enforced by exhaustive error analysis
6. **No uninitialized memory** - all variables must be initialized
7. **No integer overflow in release mode** - checked arithmetic by default (use `wrapping_add` etc. for intentional wrapping)
8. **No implicit type coercion** - all casts are explicit
9. **No memory leaks** (for stack and owned heap values) - deterministic destruction

---

## 24. Grammar (EBNF Summary)

```ebnf
program        = { import_decl | item_decl } ;
import_decl    = "import" module_path ";" ;
module_path    = IDENT { "." IDENT } [ ".{" IDENT { "," IDENT } "}" | ".*" ] ;

item_decl      = fn_decl | struct_decl | enum_decl | trait_decl
               | impl_decl | type_alias | const_decl ;

fn_decl        = [ "pub" ] [ "async" ] [ "pure" ] "fn" IDENT
                 [ generic_params ] "(" [ param_list ] ")" "->" type block ;
param_list     = param { "," param } ;
param          = IDENT ":" type [ "=" expr ] ;

struct_decl    = [ "pub" ] "struct" IDENT [ generic_params ]
                 "{" field_list "}" ;
field_list     = field { "," field } ;
field          = [ "pub" ] IDENT ":" type [ "=" expr ] ;

enum_decl      = [ "pub" ] "enum" IDENT [ generic_params ]
                 "{" variant_list "}" ;
variant_list   = variant { "," variant } ;
variant        = IDENT [ "(" type_list ")" ] ;

trait_decl     = [ "pub" ] "trait" IDENT [ generic_params ]
                 "{" { fn_sig | fn_decl } "}" ;
fn_sig         = "fn" IDENT "(" [ param_list ] ")" "->" type ";" ;

impl_decl      = "impl" [ generic_params ] [ type "for" ] type
                 "{" { fn_decl } "}" ;

type_alias     = "type" IDENT [ generic_params ] "=" type ";" ;
const_decl     = [ "pub" ] "const" IDENT ":" type "=" expr ";" ;

generic_params = "<" IDENT [ ":" trait_bound ] { "," IDENT [ ":" trait_bound ] } ">" ;
trait_bound    = type { "+" type } ;

type           = primitive_type | IDENT [ "<" type_list ">" ]
               | "arr" "<" type ">" | "fixed_arr" "<" type "," INTEGER ">"
               | "map" "<" type "," type ">"
               | "Option" "<" type ">" | "Result" "<" type "," type ">"
               | "Box" "<" type ">" | "fn" "(" [ type_list ] ")" "->" type
               | "(" type_list ")" | "&" [ "mut" ] type | "*" type | "void" ;
primitive_type = "i8" | "i16" | "i32" | "i64"
               | "u8" | "u16" | "u32" | "u64"
               | "f32" | "f64" | "bool" | "str" | "char" ;
type_list      = type { "," type } ;

block          = "{" { statement } "}" ;
statement      = let_stmt | const_stmt | expr_stmt | return_stmt
               | if_stmt | for_stmt | while_stmt | loop_stmt
               | match_stmt | try_stmt | break_stmt | continue_stmt
               | unsafe_block | ";" ;

let_stmt       = "let" [ "mut" ] IDENT [ ":" type ] "=" expr ";" ;
const_stmt     = "const" IDENT ":" type "=" expr ";" ;
return_stmt    = "return" [ expr ] ";" ;
break_stmt     = "break" [ IDENT ] ";" ;
continue_stmt  = "continue" [ IDENT ] ";" ;
expr_stmt      = expr ";" ;

if_stmt        = "if" "(" expr ")" block { "else" "if" "(" expr ")" block }
                 [ "else" block ] ;
for_stmt       = "for" "(" ( let_stmt expr ";" expr | IDENT "in" expr ) ")" block ;
while_stmt     = "while" "(" expr ")" block ;
loop_stmt      = "loop" block ;
match_stmt     = "match" "(" expr ")" "{" { match_arm } "}" ;
match_arm      = pattern "=>" ( expr "," | block ) ;
pattern        = literal | IDENT | IDENT "(" pattern_list ")"
               | pattern "|" pattern | "_" | range_pattern ;
range_pattern  = literal ".." literal | literal "..=" literal ;

try_stmt       = "try" block { catch_clause } [ "finally" block ] ;
catch_clause   = "catch" "(" IDENT ":" type ")" block ;

unsafe_block   = "unsafe" block ;

expr           = assignment ;
assignment     = or_expr [ assign_op assignment ] ;
assign_op      = "=" | "+=" | "-=" | "*=" | "/=" | "%="
               | "&=" | "|=" | "^=" | "<<=" | ">>=" ;
or_expr        = and_expr { "||" and_expr } ;
and_expr       = bitor_expr { "&&" bitor_expr } ;
bitor_expr     = xor_expr { "|" xor_expr } ;
xor_expr       = bitand_expr { "^" bitand_expr } ;
bitand_expr    = equality_expr { "&" equality_expr } ;
equality_expr  = relational_expr { ( "==" | "!=" ) relational_expr } ;
relational_expr= shift_expr { ( "<" | ">" | "<=" | ">=" ) shift_expr } ;
shift_expr     = additive_expr { ( "<<" | ">>" ) additive_expr } ;
additive_expr  = mult_expr { ( "+" | "-" ) mult_expr } ;
mult_expr      = power_expr { ( "*" | "/" | "%" ) power_expr } ;
power_expr     = unary_expr { "**" unary_expr } ;
unary_expr     = ( "-" | "!" | "~" | "&" [ "mut" ] | "*" ) unary_expr
               | postfix_expr ;
postfix_expr   = primary { "." IDENT [ "(" [ arg_list ] ")" ]
               | "[" expr "]" | "(" [ arg_list ] ")" | "?" | "as" [ "!" ] type } ;
arg_list       = arg { "," arg } ;
arg            = [ IDENT ":" ] expr ;
primary        = literal | IDENT | "(" expr ")" | "(" expr "," expr_list ")"
               | "[" [ expr_list ] "]" | "{" [ field_init_list ] "}"
               | "fn" "(" [ param_list ] ")" "->" type ( block | "=>" expr )
               | "await" expr | IDENT "::" IDENT ;
literal        = INTEGER | FLOAT | STRING | CHAR | "true" | "false"
               | "None" | "Some" "(" expr ")" ;
expr_list      = expr { "," expr } ;
field_init_list= field_init { "," field_init } ;
field_init     = IDENT ":" expr ;

INTEGER        = DIGIT { DIGIT | "_" } [ integer_suffix ] ;
integer_suffix = "i8" | "i16" | "i32" | "i64"
               | "u8" | "u16" | "u32" | "u64" ;
FLOAT          = DIGIT { DIGIT } "." DIGIT { DIGIT } [ float_suffix ] ;
float_suffix   = "f32" | "f64" ;
STRING         = '"' { any_char | escape } '"'
               | 'r"' { any_char } '"'
               | 'f"' { any_char | "{" expr "}" } '"' ;
CHAR           = "'" ( any_char | escape ) "'" ;
escape         = "\\" ( "n" | "t" | "r" | "\\" | '"' | "0"
               | "x" HEX HEX | "u{" HEX { HEX } "}" ) ;
IDENT          = ( LETTER | "_" ) { LETTER | DIGIT | "_" } ;
```

---

## 25. Complete Example Program

```
/// A complete aricode program demonstrating core features.
module main;

import std.io;
import std.math;

struct Point {
  x: f64,
  y: f64
}

impl Point {
  fn new(x: f64, y: f64) -> Point {
    return Point { x: x, y: y };
  }

  fn distance(self, other: &Point) -> f64 {
    let dx: f64 = self.x - other.x;
    let dy: f64 = self.y - other.y;
    return math.sqrt(dx * dx + dy * dy);
  }

  fn to_string(self) -> str {
    return f"({self.x}, {self.y})";
  }
}

enum Shape {
  Circle(Point, f64),
  Rectangle(Point, f64, f64)
}

impl Shape {
  fn area(self) -> f64 {
    match (self) {
      Shape.Circle(_, radius) => return math.PI * radius * radius,
      Shape.Rectangle(_, w, h) => return w * h
    }
  }

  fn describe(self) -> str {
    match (self) {
      Shape.Circle(center, r) => {
        return f"Circle at {center.to_string()} with radius {r}";
      },
      Shape.Rectangle(origin, w, h) => {
        return f"Rectangle at {origin.to_string()} size {w}x{h}";
      }
    }
  }
}

fn find_largest(shapes: &arr<Shape>) -> Option<(u64, f64)> {
  if (shapes.len() == 0) {
    return None;
  }

  let max_idx: u64 = 0;
  let max_area: f64 = shapes[0].area();

  for (i in 1..shapes.len()) {
    let a: f64 = shapes[i].area();
    if (a > max_area) {
      max_area = a;
      max_idx = i;
    }
  }

  return Some((max_idx, max_area));
}

fn main() -> i32 {
  let shapes: arr<Shape> = [
    Shape.Circle(Point.new(0.0, 0.0), 5.0),
    Shape.Rectangle(Point.new(1.0, 1.0), 10.0, 20.0),
    Shape.Circle(Point.new(3.0, 4.0), 7.0)
  ];

  for (index, shape in shapes.enumerate()) {
    io.println(f"[{index}] {shape.describe()} -> area = {shape.area()}");
  }

  match (find_largest(&shapes)) {
    Some((idx, area)) => {
      io.println(f"Largest shape is [{idx}] with area {area}");
    },
    None => {
      io.println("No shapes to compare");
    }
  }

  return 0;
}
```

Expected compiler output:

```
aric build src/main.ari --release

[BUILD] src/main.ari -> main
[CHECK] Type checking... OK
[CHECK] Error analysis... OK
[CHECK] Ownership analysis... OK
[COMPILE] Generating x86_64...

PODIUM RESULTS:
  fn Point.new()        -> GOLD   (12 bytes, 3 cycles)   | Optimal
  fn Point.distance()   -> GOLD   (28 bytes, 8 cycles)   | Optimal - uses SSE2 sqrt
  fn Point.to_string()  -> SILVER (64 bytes, 15 cycles)  | Heap alloc for format string
  fn Shape.area()       -> GOLD   (20 bytes, 5 cycles)   | Optimal - branch-free
  fn Shape.describe()   -> SILVER (96 bytes, 22 cycles)  | Heap alloc for format string
  fn find_largest()     -> GOLD   (36 bytes, N*4 cycles) | Optimal linear scan
  fn main()             -> SILVER (148 bytes, ~80 cycles) | Multiple heap allocs

[LINK] main -> 8.2 KB
[DONE] Build successful. 0 errors, 0 warnings.
```

---

## Appendices

### A. Precedence Table (highest to lowest)

| Precedence | Operators                          | Associativity |
|------------|------------------------------------|---------------|
| 1          | `()` `[]` `.` `::` postfix `?`    | Left          |
| 2          | `-` `!` `~` `&` `&mut` `*` (unary)| Right         |
| 3          | `**`                               | Right         |
| 4          | `*` `/` `%`                        | Left          |
| 5          | `+` `-`                            | Left          |
| 6          | `<<` `>>`                          | Left          |
| 7          | `&`                                | Left          |
| 8          | `^`                                | Left          |
| 9          | `\|`                               | Left          |
| 10         | `==` `!=` `<` `>` `<=` `>=`       | Left          |
| 11         | `&&`                               | Left          |
| 12         | `\|\|`                             | Left          |
| 13         | `as` `as!`                         | Left          |
| 14         | `=` `+=` `-=` `*=` `/=` etc.      | Right         |

### B. Built-in Traits

| Trait         | Methods                  | Description                    |
|---------------|--------------------------|--------------------------------|
| `Printable`   | `to_string() -> str`     | String representation          |
| `Comparable`  | `compare(Self) -> i32`   | Ordering comparison            |
| `Hashable`    | `hash() -> u64`          | Hash value for maps/sets       |
| `Copyable`    | `copy() -> Self`         | Bitwise copy (for small types) |
| `Cloneable`   | `clone() -> Self`        | Deep clone                     |
| `Iterable<T>` | `iter() -> Iterator<T>`  | Iteration support              |
| `Default`     | `default() -> Self`      | Default value                  |
| `Drop`        | `drop(self) -> void`     | Custom destructor              |

### C. Integer Literal Forms

```
let dec: i32 = 1_000_000;    // Decimal (underscores for readability)
let hex: i32 = 0xFF;         // Hexadecimal
let oct: i32 = 0o77;         // Octal
let bin: i32 = 0b1010_0101;  // Binary
```

### D. Reserved for Future

The following features are planned but not part of v1.0:

- Macros / metaprogramming
- Compile-time evaluation (`comptime`)
- SIMD intrinsics as first-class types
- GPU compute shaders
- Hot code reloading
- Package manager (`aripkg`)
