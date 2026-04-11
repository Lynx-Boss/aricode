# Aricode

**A compiled language where silent errors are impossible.**

Aricode generates direct x86_64 machine code with no linker, no libc, no runtime. Hello World compiles to **199 bytes**.

```
fn main() -> i32 {
    print_str("Hello, World!");
    return 0;
}
```

## Why Aricode?

Every language lets bugs slip through. Aricode doesn't.

| Problem | C | Go | Rust | **Aricode** |
|---------|---|-----|------|-------------|
| Division by zero | crash | panic | panic | **won't compile** |
| Null without check | segfault | panic | `unwrap` panic | **won't compile** |
| Empty catch block | swallows error | - | - | **won't compile** |
| Data loss (i64->i32) | silent | silent | warning | **won't compile** |
| Unused variable | ignored | error | warning | **warning** |

Aricode classifies all errors into 5 levels:

| Level | Name | Action |
|-------|------|--------|
| 0 | SILENT | **Blocks compilation** - code that could fail silently |
| 1 | LOGIC | **Blocks compilation** - type errors, undefined vars |
| 2 | WARNING | Reports but compiles - unused vars, shadowing |
| 3 | SYSTEM | Must be caught at runtime - file I/O, network |
| 4 | CATASTROPHIC | Log and terminate - out of memory |

## Features

- **Direct x86_64 codegen** - AST to machine code, no LLVM, no IR
- **Minimal binaries** - 157 bytes (addition) to 2.2 KB (structs)
- **try/catch** - Cross-function error handling via callee-saved registers
- **error.raise** - Explicit error propagation, never silent
- **Optimizer** - Constant folding, strength reduction, tail call optimization, dead code elimination
- **f64 floats** - SSE2 instructions for double-precision arithmetic
- **Heap arrays** - mmap-allocated, length-prefixed
- **Structs** - Via array pattern with constructor/accessor functions
- **I/O** - print_str, print_int, read_int builtins
- **Loops** - while, for with full variable assignment
- **Bitwise** - &, |, ^, <<, >> operators
- **Podium system** - Compile-time code quality rating (Gold/Silver/Bronze/Iron)
- **Decimal arithmetic** - Native AriDecimal type (0.1 + 0.2 = 0.3 exactly)

## Benchmarks

Aricode competes with hand-written x86_64 assembly across 12 challenges:

```
  OVERALL SPARRING CHAMPION (108 binaries, 10 implementations)

  #1   aricode            334 points
  #2   NASM x86_64 asm    331 points
  #3   C (gcc -O3)        213 points
  #4   C (gcc -O2)        208 points
  ...
  #9   Rust               86 points
  #10  Go                 72 points
```

**Binary size comparison:**

| Program | Aricode | NASM | GCC static | Rust | Go |
|---------|---------|------|------------|------|-----|
| Hello World | **199 B** | - | 754 KB | 11.3 MB | 1.8 MB |
| Fibonacci | **268 B** | 408 B | 754 KB | 11.3 MB | 1.8 MB |
| Bubble Sort | **1,315 B** | - | 754 KB | 11.3 MB | 1.8 MB |
| Mersenne prime | **374 B** | 400 B | 754 KB | 11.3 MB | 1.8 MB |

Aricode binaries are **72,000x smaller than Rust** and **12,000x smaller than Go**.

## Quick Start

```bash
# Build the compiler
cd aricode/src/compiler && make

# Compile and run Hello World
./aric ../../examples/hello_world.ari -o hello
./hello
# Output: Hello, World!

# Interactive calculator
./aric ../../examples/calculator.ari -o calc
./calc

# Error handling demo
./aric ../../examples/error_handling.ari -o errors
./errors
```

## Examples

| File | What it does | Binary size |
|------|-------------|-------------|
| [hello_world.ari](aricode/examples/hello_world.ari) | Hello World | 199 B |
| [fibonacci.ari](aricode/examples/fibonacci.ari) | First 20 Fibonacci numbers | 422 B |
| [primes.ari](aricode/examples/primes.ari) | All primes below 100 | 674 B |
| [calculator.ari](aricode/examples/calculator.ari) | Interactive calculator | 1,480 B |
| [circle.ari](aricode/examples/circle.ari) | f64 circle area (SSE2) | 1,069 B |
| [sort.ari](aricode/examples/sort.ari) | Bubble sort with arrays | 1,315 B |
| [structs.ari](aricode/examples/structs.ari) | Point and Rectangle structs | 2,270 B |
| [error_handling.ari](aricode/examples/error_handling.ari) | try/catch across functions | 1,605 B |
| [demo.ari](aricode/examples/demo.ari) | Full language showcase | 1,538 B |

## Compiler Architecture

```
Source (.ari)
    |
    v
 [Lexer] --> tokens (40+ types)
    |
    v
 [Parser] --> AST (recursive descent)
    |
    v
 [Semantic Analyzer] --> 5-level error enforcement
    |
    v
 [Optimizer] --> constant fold, strength reduce, TCO, DCE
    |
    v
 [Codegen] --> raw x86_64 machine code
    |
    v
 [ELF Writer] --> minimal Linux binary (120 byte header)
```

## Sparring Challenges

12 challenges across 5 categories, benchmarked against C, Rust, Go, NASM, Python:

| Category | Challenges |
|----------|-----------|
| Arithmetic | Addition, Factorial |
| Recursion | Fibonacci, Ackermann A(3,4) |
| Number Theory | Collatz conjecture, Mersenne M31 prime |
| Algorithms | GCD, Prime counting, Integer sqrt, Powmod |
| AI/ML | Perceptron neural network, Minimax game AI |

Run the sparring suite:
```bash
cd aricode/sparring
bash build_all.sh      # Build all 108 binaries
bash benchmark.sh      # Run benchmarks
bash sparring_report.sh # Generate podium report
```

## Language Syntax

```javascript
// Functions
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

// Variables and loops
let sum: i32 = 0;
for (let i: i32 = 0; i < 10; i = i + 1) {
    sum = sum + i;
}

// Error handling
fn safe_divide(a: i32, b: i32) -> i32 {
    if (b == 0) {
        error.raise(1, "division by zero");
    }
    return a / b;
}

try {
    let result: i32 = safe_divide(10, 0);
} catch (e: Error) {
    print_str("Caught error!");
    print_int(e);
}

// Arrays and structs
let data: i32 = arr_new(5);
arr_set(data, 0, 42);
print_int(arr_get(data, 0));

// Floats (SSE2)
let pi: f64 = 3.14159265;
let area: f64 = pi * r * r;
```

## Paper

A scientific paper describing aricode's design is available:

**"Aricode: A Compiled Language with Mandatory Error Handling and Direct x86_64 Code Generation"**

See [paper/aricode_paper.tex](paper/aricode_paper.tex) for the full LaTeX source.

## License

Copyright (c) 2026 Edwin F. Veliz Jaramillo. All rights reserved. See [LICENSE](LICENSE).

## Author

Edwin F. Veliz Jaramillo (lynxcraft) - Independent Researcher
