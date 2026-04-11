# aricode Error Levels Specification

## Overview

The aricode error system is built on a fundamental principle: **no error may be silently ignored**. Every possible failure in a program must be explicitly acknowledged and handled by the programmer. The compiler enforces this through static analysis, and the runtime enforces it for conditions that cannot be checked at compile time.

Errors are classified into 5 levels (0 through 4), each with distinct semantics, compiler behavior, and programmer obligations.

---

## Level 0 - SILENT_PASS (FORBIDDEN)

### Definition

Level 0 represents the act of silently ignoring an error. This level exists **only as documentation** to state that it is categorically banned from the language.

### What It Catches

Nothing -- this level does not catch errors. It represents the *absence* of error handling, which aricode makes structurally impossible.

### Language Mechanisms That Prevent Level 0

1. **Result<T, E> must be consumed.** A function that returns `Result` produces a Level 1 compile error if the return value is discarded.

```
// COMPILE ERROR: Result<void, SystemError> is not handled
file.write("out.txt", data);

// OK:
let _ = file.write("out.txt", data);
// Still ERROR: assigning Result to _ does not handle it

// OK:
match (file.write("out.txt", data)) {
  Ok(_) => {},
  Err(e) => log.error(e)
}
```

2. **Empty catch blocks are forbidden.**

```
// COMPILE ERROR: catch block is empty - error is silently ignored
try {
  risky_operation();
} catch (e: SystemError) {
}

// OK:
try {
  risky_operation();
} catch (e: SystemError) {
  log.error(e);
}
```

3. **Option<T> must be matched exhaustively.**

```
let val: Option<i32> = get_value();

// COMPILE ERROR: Option<i32> is used as i32 without unwrapping
let x: i32 = val;

// OK:
let x: i32 = match (val) {
  Some(v) => v,
  None => 0
};
```

4. **Division, array access, and other potentially-failing operations require guards.**

### Compiler Behavior

Level 0 is not a real error level. The compiler does not emit "Level 0" errors. Instead, the language design makes it structurally impossible to write code that silently ignores errors. Any attempt results in a Level 1 compile error.

---

## Level 1 - LOGIC_ERROR (INADMISSIBLE)

### Definition

Level 1 errors are **programmer logic mistakes** -- bugs that result from incorrect reasoning about the code. These are errors the programmer should never allow to happen.

### What It Catches

#### 1.1 Division by Zero

```
fn bad(a: i32, b: i32) -> i32 {
  return a / b;
  // COMPILE ERROR [L1-001]: Possible division by zero.
  // Parameter 'b' has type i32 which includes 0.
  // Fix: Add a guard check or use checked_div().
}

fn good(a: i32, b: i32) -> i32 {
  if (b == 0) {
    error.raise(Level.LOGIC, "Division by zero");
  }
  return a / b;  // OK: compiler proves b != 0 here
}

fn also_good(a: i32, b: i32) -> Option<i32> {
  return a.checked_div(b);  // Returns None if b == 0
}
```

#### 1.2 Null / None Access

```
let val: Option<i32> = get_value();

// COMPILE ERROR [L1-002]: Option<i32> may be None.
let x: i32 = val.unwrap();
// Note: unwrap() compiles but inserts a runtime panic if None.
// The compiler flags this because it prefers explicit match.

// OK:
match (val) {
  Some(v) => use_value(v),
  None => handle_missing()
}
```

#### 1.3 Array Out of Bounds

```
let arr: arr<i32> = [1, 2, 3];

// COMPILE ERROR [L1-003]: Index may be out of bounds.
// 'arr' has length 3, index 'i' has range i32 (unbounded).
fn get_item(arr: &arr<i32>, i: i32) -> i32 {
  return arr[i];
}

// OK:
fn get_item(arr: &arr<i32>, i: i32) -> Option<i32> {
  if (i < 0 || i >= arr.len() as i32) {
    return None;
  }
  return Some(arr[i as u64]);
}

// Also OK (bounds-checked access method):
fn get_item(arr: &arr<i32>, i: u64) -> Option<i32> {
  return arr.get(i);  // Returns Option<i32>
}
```

#### 1.4 Type Mismatch

```
let x: i32 = 42;
let y: f64 = x;
// COMPILE ERROR [L1-004]: Type mismatch. Cannot assign i32 to f64.
// Use explicit cast: x as f64

let z: f64 = x as f64;  // OK
```

#### 1.5 Integer Overflow

```
let x: i8 = 127;
let y: i8 = x + 1;
// COMPILE ERROR [L1-005]: Possible integer overflow.
// i8 max is 127, adding 1 would overflow.
// Use checked_add() or a wider type.

let y: i8 = x.checked_add(1).unwrap_or(0);  // OK
let y: i16 = x as i16 + 1;                   // OK (wider type)
```

#### 1.6 Use After Move

```
let name: str = "Alice";
let greeting: str = make_greeting(name);  // name moved
print(name);
// COMPILE ERROR [L1-006]: Use of moved value 'name'.
// 'name' was moved into make_greeting() on line 2.
// Use a borrow (&name) if you need to keep ownership.
```

#### 1.7 Uninitialized Variables

```
let x: i32;
print(x);
// COMPILE ERROR [L1-007]: Variable 'x' is used before initialization.
```

#### 1.8 Unreachable Code

```
fn example() -> i32 {
  return 5;
  let x: i32 = 10;  // COMPILE ERROR [L1-008]: Unreachable code after return.
}
```

#### 1.9 Non-Exhaustive Match

```
enum Status { Active, Inactive, Pending }

match (status) {
  Status.Active => handle_active(),
  Status.Inactive => handle_inactive()
  // COMPILE ERROR [L1-009]: Non-exhaustive match.
  // Missing variant: Status.Pending
}
```

#### 1.10 Variable Shadowing

```
let x: i32 = 10;
let x: str = "hello";
// COMPILE ERROR [L1-010]: Variable 'x' is already declared in this scope.
// Shadowing is not allowed in aricode.
```

#### 1.11 Unhandled Result

```
file.write("out.txt", data);
// COMPILE ERROR [L1-011]: Return type Result<void, SystemError> is not handled.
// This operation can fail. Handle the Result or propagate with ?.
```

#### 1.12 Purity Violation

```
pure fn compute(x: i32) -> i32 {
  io.println(x.to_str());
  // COMPILE ERROR [L1-012]: Pure function 'compute' has side effect.
  // io.println is not a pure operation.
  return x * 2;
}
```

### Error Codes

All Level 1 errors have codes in the format `L1-NNN`:

| Code     | Category                    |
|----------|-----------------------------|
| L1-001   | Division by zero            |
| L1-002   | None/null access            |
| L1-003   | Out of bounds               |
| L1-004   | Type mismatch               |
| L1-005   | Integer overflow            |
| L1-006   | Use after move              |
| L1-007   | Uninitialized variable      |
| L1-008   | Unreachable code            |
| L1-009   | Non-exhaustive match        |
| L1-010   | Variable shadowing          |
| L1-011   | Unhandled Result            |
| L1-012   | Purity violation            |
| L1-013   | Borrow conflict             |
| L1-014   | Missing return              |
| L1-015   | Invalid cast                |
| L1-016   | Dangling reference          |
| L1-017   | Double free                 |
| L1-018   | Data race potential         |
| L1-019   | Infinite recursion detected |
| L1-020   | Dead code (unreachable fn)  |

### Compiler Behavior

**Level 1 errors block compilation.** The compiler will not produce an executable if any Level 1 error exists. There is no flag to downgrade them.

### Programmer Response

Fix the code. Level 1 errors indicate bugs. The options are:
1. Add a guard check before the dangerous operation
2. Use a safe alternative (`checked_div`, `get`, `unwrap_or`)
3. Propagate the error with `?` to the caller
4. Use `error.raise()` to explicitly signal the failure

---

## Level 2 - RUNTIME_WARNING (SUSPICIOUS)

### Definition

Level 2 errors are **code quality warnings** -- things that compile and run correctly but indicate suspicious patterns, potential inefficiency, or likely unintended behavior.

### What It Catches

#### 2.1 Unused Variables

```
fn process(data: arr<i32>) -> i32 {
  let temp: i32 = 42;  // WARNING [L2-001]: Variable 'temp' is declared but never used.
  return data[0];
}
```

Suppression: prefix with underscore (`let _temp: i32 = 42;`).

#### 2.2 Unused Imports

```
import std.math;   // WARNING [L2-002]: Module 'std.math' is imported but not used.
import std.io;

fn main() -> i32 {
  io.println("hello");
  return 0;
}
```

#### 2.3 Unreachable Code (non-blocking)

Code after an `if/else` where both branches return:

```
fn classify(x: i32) -> str {
  if (x > 0) {
    return "positive";
  } else {
    return "non-positive";
  }
  // WARNING [L2-003]: Code after this point is unreachable.
  // (This is L2 when it's clearly dead code but not after a bare return)
}
```

#### 2.4 Implicit Narrowing in Expressions

```
let x: i64 = 1000000;
let y: i32 = x as i32;
// WARNING [L2-004]: Narrowing cast from i64 to i32.
// Value may be truncated. Use as! for explicit acknowledgment.
```

#### 2.5 Wildcard Import

```
import std.collections.*;
// WARNING [L2-005]: Wildcard import. Prefer importing specific items.
```

#### 2.6 Functions With 4+ Parameters Without Named Arguments

```
create_user("Alice", 30, "alice@example.com", "admin");
// WARNING [L2-006]: Function call with 4+ arguments should use named parameters.
```

#### 2.7 Large Stack Allocation

```
let buffer: fixed_arr<u8, 10_000_000> = [0; 10_000_000];
// WARNING [L2-007]: Stack allocation of 10MB. Consider heap allocation with arr<u8>.
```

#### 2.8 Suspicious Comparison

```
let x: f64 = 0.1 + 0.2;
if (x == 0.3) {
  // WARNING [L2-008]: Floating-point equality comparison.
  // Due to IEEE 754, 0.1 + 0.2 != 0.3 exactly.
  // Use math.approx_equal(x, 0.3, epsilon) instead.
}
```

#### 2.9 Deprecated API Usage

```
let result = old_function();
// WARNING [L2-009]: 'old_function' is deprecated. Use 'new_function' instead.
```

#### 2.10 Inconsistent Indentation

```
fn example() -> void {
    let x = 1;  // 4 spaces
  let y = 2;    // 2 spaces
// WARNING [L2-010]: Inconsistent indentation in function 'example'.
}
```

#### 2.11 Infinite Loop Without Break

```
while (true) {
  process();
}
// WARNING [L2-011]: Possible infinite loop. No reachable break or return found.
```

### Error Codes

| Code     | Category                        |
|----------|---------------------------------|
| L2-001   | Unused variable                 |
| L2-002   | Unused import                   |
| L2-003   | Unreachable code                |
| L2-004   | Implicit narrowing              |
| L2-005   | Wildcard import                 |
| L2-006   | Unnamed arguments (4+ params)   |
| L2-007   | Large stack allocation          |
| L2-008   | Float equality comparison       |
| L2-009   | Deprecated API                  |
| L2-010   | Inconsistent indentation        |
| L2-011   | Possible infinite loop          |
| L2-012   | Redundant clone                 |
| L2-013   | Unnecessary type annotation     |
| L2-014   | Single-arm match (use if)       |
| L2-015   | Empty loop body                 |

### Compiler Behavior

- **Default**: Warnings are displayed after compilation but do not block it. The warning count is shown in the build summary.
- **`--warn-as-error` flag**: Promotes all Level 2 warnings to Level 1 errors, blocking compilation.
- **`#[allow(unused)]`**: Suppresses specific warnings on individual items.
- **aricode.toml configuration**: Fine-grained control per warning code.

```toml
[warnings]
L2-001 = "error"    # Unused variables block compilation
L2-005 = "ignore"   # Allow wildcard imports
L2-006 = "warn"     # Default behavior for unnamed args
```

### Programmer Response

Review the warning and either:
1. Fix the code (recommended)
2. Suppress with an attribute (`#[allow(unused)]`)
3. Configure the warning level in `aricode.toml`

---

## Level 3 - SYSTEM_ERROR (EXTERNAL)

### Definition

Level 3 errors are **external system failures** -- things that go wrong outside the programmer's control. The code logic is correct, but the operating system, network, filesystem, or hardware did not cooperate.

### What It Catches

#### 3.1 File System Errors

```
try {
  let content: str = io.file.read("config.txt");
} catch (e: SystemError) {
  // Possible causes:
  // - File not found (FileNotFound)
  // - Permission denied (PermissionDenied)
  // - Disk full (DiskFull)
  // - File locked (FileLocked)
  // - I/O error (IoError)
  match (e.kind) {
    ErrorKind.FileNotFound => {
      log.warn("Config file missing, using defaults");
      return default_config();
    },
    ErrorKind.PermissionDenied => {
      log.error("Cannot read config: permission denied");
      error.raise(Level.LOGIC, "Fix file permissions for config.txt");
    },
    _ => {
      log.error(f"Unexpected file error: {e.message}");
      error.raise(Level.SYSTEM, e.message);
    }
  }
}
```

#### 3.2 Network Errors

```
try {
  let response = await http.get("https://api.example.com/data");
} catch (e: SystemError) {
  // Possible causes:
  // - Connection refused (ConnectionRefused)
  // - Timeout (Timeout)
  // - DNS resolution failed (DnsError)
  // - Connection reset (ConnectionReset)
  // - TLS handshake failed (TlsError)
  match (e.kind) {
    ErrorKind.Timeout => {
      log.warn("API request timed out, retrying...");
      return retry(3);
    },
    ErrorKind.ConnectionRefused => {
      log.error("API server is down");
      return cached_response();
    },
    _ => {
      log.error(f"Network error: {e.message}");
      return Err(e);
    }
  }
}
```

#### 3.3 Process Errors

```
try {
  let result = os.exec("external_tool", ["--flag"]);
} catch (e: SystemError) {
  // Possible causes:
  // - Command not found (CommandNotFound)
  // - Process crashed (ProcessCrash)
  // - Signal received (SignalReceived)
  log.error(f"External process failed: {e.message}");
}
```

#### 3.4 Database / External Service Errors

```
try {
  let conn = db.connect("postgres://localhost/mydb");
  let rows = conn.query("SELECT * FROM users")?;
} catch (e: SystemError) {
  // Connection pool exhausted, query timeout, deadlock, etc.
  log.error(f"Database error: {e.message}");
  return Err(e);
}
```

### SystemError Structure

```
struct SystemError {
  kind: ErrorKind,
  message: str,
  source: Option<str>,       // Underlying OS error message
  code: Option<i32>,         // OS error code (errno)
  timestamp: str,            // ISO 8601
  file: str,                 // Source file where error occurred
  line: u32,                 // Line number
  stack: arr<StackFrame>     // Stack trace
}

enum ErrorKind {
  // File system
  FileNotFound,
  PermissionDenied,
  DiskFull,
  FileLocked,
  IoError,
  
  // Network
  ConnectionRefused,
  ConnectionReset,
  Timeout,
  DnsError,
  TlsError,
  
  // Process
  CommandNotFound,
  ProcessCrash,
  SignalReceived,
  
  // General
  Unknown
}
```

### Compiler Behavior

- Functions that can produce Level 3 errors must declare `Result<T, SystemError>` as their return type, or be called inside a `try` block.
- The compiler knows which standard library functions can produce system errors and enforces handling.
- Level 3 errors are always logged to the error registry.

### Programmer Response

1. **Catch and handle**: Use `try/catch` to catch the error and respond appropriately (retry, fallback, propagate).
2. **Propagate**: Use `?` to pass the error to the caller.
3. **Log**: At minimum, log the error. Empty catch blocks are forbidden.

---

## Level 4 - CATASTROPHIC (UNRECOVERABLE)

### Definition

Level 4 errors are **unrecoverable system failures** -- situations where the program cannot meaningfully continue. These are events that no amount of error handling can fix at the application level.

### What It Catches

#### 4.1 Out of Memory

```
// The runtime detects OOM when:
// - arr<T>.push() cannot allocate
// - str concatenation cannot allocate
// - Box.new() cannot allocate
// - Any heap allocation fails

// OOM handler (registered at startup):
fn main() -> i32 {
  aricode.on_catastrophic(fn(e: CatastrophicError) -> void {
    // This runs in a pre-allocated emergency buffer
    log.fatal(f"CATASTROPHIC: {e.message}");
    log.fatal("Dumping state to crash.log...");
    crash_dump("crash.log");
    // Program exits after this handler returns
  });

  // ... rest of program
  return 0;
}
```

#### 4.2 Stack Overflow

```
// Detected by guard pages on the stack.
// The runtime catches the segfault and converts it to a Level 4 error.
// The catastrophic handler runs on an alternate signal stack.

fn recursive_disaster(n: i32) -> i32 {
  return recursive_disaster(n + 1);
  // Note: the compiler may catch obvious infinite recursion as L1-019
  // But indirect or conditional recursion that overflows is Level 4.
}
```

#### 4.3 Hardware Failures

```
// Detected conditions:
// - ECC memory errors (if reported by OS)
// - Disk controller failures
// - CPU exceptions (illegal instruction, bus error)
// - Corrupted memory (detected by runtime canaries)
```

#### 4.4 Assertion Failures in Release Mode

```
// aricode.assert() in release mode:
aricode.assert(critical_invariant(), "Data structure corrupted");
// If this fails in release mode, it's Level 4 - program state is invalid.
```

#### 4.5 Double Panic

```
// If an error.raise() occurs during the handling of another error.raise(),
// this is a double panic and is escalated to Level 4.
```

### CatastrophicError Structure

```
struct CatastrophicError {
  kind: CatastrophicKind,
  message: str,
  timestamp: str,
  stack: arr<StackFrame>,    // May be incomplete
  memory_usage: u64,         // Bytes at time of crash
  thread_id: u64
}

enum CatastrophicKind {
  OutOfMemory,
  StackOverflow,
  HardwareFailure,
  AssertionFailure,
  DoublePanic,
  KernelPanic,              // Detected via OS signal
  CorruptedState
}
```

### Runtime Behavior

1. The runtime halts all threads except the crash handler thread.
2. The registered `on_catastrophic` handler runs on a pre-allocated emergency stack (8 KB, allocated at startup).
3. The error is logged to the error registry (using a pre-allocated log buffer).
4. If no handler is registered, the runtime prints the error to stderr and exits with code 255.
5. After the handler returns, the runtime calls `exit(255)`.
6. The handler has a 5-second timeout. If it does not return, the process is forcibly killed.

### Programmer Response

Level 4 errors cannot be "fixed" at runtime. The programmer's responsibility is:
1. Register a `on_catastrophic` handler to log diagnostic information.
2. Design the system architecture so that a crash of one component does not bring down the entire system (process supervision, restart policies).
3. Write crash dumps that can be analyzed post-mortem.

---

## Error Registry

### Location

All errors are logged to `.aricode/errors.log` in the project root directory. This file is created automatically on the first error.

### Log Format

Each entry is a structured record:

```
================================================================================
TIMESTAMP: 2026-04-11T14:30:00.000Z
LEVEL:     1 (LOGIC_ERROR)
CODE:      L1-001
FILE:      src/math/calc.vt
LINE:      42
COLUMN:    15
FUNCTION:  calculate_average
MESSAGE:   Division by zero: parameter 'count' is 0
PHASE:     compile    (or: runtime)
STACK:
  at calculate_average (src/math/calc.vt:42:15)
  at process_data (src/data.vt:108:3)
  at main (src/main.vt:12:5)
================================================================================
```

### Programmatic Access

```
import aricode.errors;

// Query errors
let recent: arr<ErrorEntry> = errors.query(
  level: Level.LOGIC,
  since: "2026-04-11",
  limit: 50
);

// Filter by file
let file_errors: arr<ErrorEntry> = errors.query(
  file: "src/main.vt"
);

// Filter by code
let div_errors: arr<ErrorEntry> = errors.query(
  code: "L1-001"
);

// Clear old entries
errors.clear(before: "2026-01-01");

// Error statistics
let stats: ErrorStats = errors.stats();
print(f"Total: {stats.total}, Level 1: {stats.level1}, Level 2: {stats.level2}");
```

### ErrorEntry Structure

```
struct ErrorEntry {
  timestamp: str,
  level: i32,
  code: str,
  file: str,
  line: u32,
  column: u32,
  function_name: str,
  message: str,
  phase: str,           // "compile" or "runtime"
  stack: arr<StackFrame>
}

struct StackFrame {
  function_name: str,
  file: str,
  line: u32,
  column: u32
}
```

---

## Error Handling Decision Tree

When writing aricode code, follow this decision tree:

```
Can this operation fail?
|
+-- NO --> No special handling needed.
|
+-- YES --> What kind of failure?
    |
    +-- Programmer mistake (wrong input, bad logic)
    |   --> Level 1. Guard with if-check, use checked_*, or error.raise().
    |       The compiler forces you to handle it.
    |
    +-- External system (file, network, OS)
    |   --> Level 3. Wrap in try/catch or propagate with ?.
    |       Log the error. Retry or fallback.
    |
    +-- Hardware / unrecoverable
        --> Level 4. Register on_catastrophic handler.
            Log diagnostics. Let the process exit.
```

---

## Configuration Summary

### aricode.toml Error Configuration

```toml
[errors]
# Error registry location (default: .aricode/errors.log)
log_path = ".aricode/errors.log"

# Maximum log file size before rotation (default: 10MB)
max_log_size = "10MB"

# Number of rotated log files to keep (default: 5)
log_rotation = 5

# Whether to include compile-time errors in the log (default: true)
log_compile_errors = true

[warnings]
# Per-warning-code configuration: "warn" | "error" | "ignore"
L2-001 = "warn"       # Unused variables
L2-002 = "warn"       # Unused imports
L2-003 = "warn"       # Unreachable code
L2-004 = "error"      # Implicit narrowing -> block compilation
L2-005 = "ignore"     # Wildcard imports -> allow
L2-006 = "warn"       # Unnamed arguments
L2-007 = "error"      # Large stack allocation -> block
L2-008 = "error"      # Float equality -> block
```

### Compiler Flags

```
vtc build --warn-as-error          # All L2 warnings become L1 errors
vtc build --warn-as-error=L2-001   # Only specific warning becomes error
vtc errors                          # Show error log summary
vtc errors --level=1               # Show only Level 1 errors
vtc errors --since=2026-04-01      # Show errors since date
vtc errors --file=src/main.vt      # Show errors in specific file
vtc errors --clear                 # Clear the error log
```
