# Aricode

**A compiled language that produces the smallest, fastest binaries — with zero silent errors.**

Aricode is not an AI. It is a **compiler** — a tool that transforms human-readable code into minimal x86_64 machine code, directly, without LLVM, without a linker, without libc, without any runtime. The result: standalone ELF binaries as small as **255 bytes** that run on bare Linux.

```
Source (.ari) → aric compiler (0.3ms) → 255-byte ELF binary → runs
```

Where other languages need megabytes of runtime and toolchains, Aricode reduces that cost to **zero**. A neural network trainer compiles to 8.4 KB. An HTTP server to 1 KB. A PID motor controller to 3 KB. All with zero dependencies.

## Why Aricode?

**Maximum efficiency, minimum cost.** Every program compiles in sub-millisecond to a binary that needs nothing else to run — no interpreter, no VM, no shared libraries.

### Human-readable, machine-optimal

Aricode code is clear enough for humans to read and verify, but compiles to machine code that competes with hand-written assembly. Every division is guarded, every error path is handled, every type conversion is explicit — a human reviewer can quickly spot logic errors without deep domain expertise.

### Built for resource-constrained environments

Binaries under 3.5 KB with zero runtime dependencies make Aricode ideal for **robotics, embedded systems, and edge computing** — anywhere resources are scarce and every byte counts. A program can be generated, compiled, and executed in under 1 ms.

| Problem | C | Go | Rust | **Aricode** |
|---------|---|-----|------|-------------|
| Division by zero | crash | panic | panic | **won't compile** |
| Null without check | segfault | panic | `unwrap` panic | **won't compile** |
| Empty catch block | swallows error | - | - | **won't compile** |
| Data loss (i64->i32) | silent | silent | warning | **won't compile** |
| 0.1 + 0.2 = 0.3? | false | false | false | **true (dec type)** |

## Features

### Language
- **Types:** i32, f64 (SSE2), dec (exact decimal), str (heap strings), bool, arrays, structs
- **Control flow:** if/else, while, for, match, break, continue, return
- **Operators:** `+` `-` `*` `/` `%` `&` `|` `^` `<<` `>>` `==` `!=` `<` `>` `<=` `>=` `&&` `||` `!` `?:`
- **Compound:** `+=` `-=` `*=` `/=`
- **Functions:** parameters, return values, recursion, tail call optimization
- **Error handling:** try/catch across functions, error.raise
- **Data structures:** heap arrays (mmap), struct patterns
- **Imports:** `import "file.ari";` or `import "file.ari" as ns;` with namespace support
- **Networking:** TCP client/server via direct syscalls (HTTP server in 1,349 bytes)
- **Threading:** process-based parallelism via fork/waitpid syscalls
- **SIMD:** SSE2 vectorized array ops (default), AVX2 4x i64/cycle (`--avx2` flag)

### Builtins (57)
| Category | Functions |
|----------|-----------|
| **Console** | print_str, print_int, print_float, print_dec, read_int, read_float |
| **Strings** | str_new, str_len, str_eq, str_char_at, str_println, str_concat |
| **Arrays (i32)** | arr_new, arr_get, arr_set, arr_len, arr_sum, arr_fill, arr_scale, arr_dot |
| **Arrays (f64)** | arr_f64_new, arr_f64_get, arr_f64_set, arr_f64_sum, arr_f64_dot, arr_f64_scale |
| **Math** | math_sqrt, math_abs, math_exp, math_log |
| **Memory** | mem_free, buf_stack, buf_free |
| **Files** | file_open, file_read, file_write, file_close |
| **Networking** | socket_create, socket_connect, socket_send, socket_recv, socket_close, socket_bind, socket_listen, socket_accept, socket_opt, ip4 |
| **I/O Multiplex** | epoll_create, epoll_add, epoll_del, epoll_wait |
| **Threading** | thread_spawn, thread_wait, thread_exit |
| **Convert** | int_to_float, float_to_int, dec |

### Compiler
- **Direct x86_64 codegen** - AST to machine code, no LLVM, no IR
- **Minimal ELF binaries** - 255 bytes (Hello World) to 3.3 KB (physics sim)
- **6 optimization passes** - constant folding, strength reduction, peephole, TCO, DCE, decimal folding
- **Semantic analyzer** - 5-level error hierarchy, always-on, zero false positives
- **Podium system** - compile-time code quality rating (Gold/Silver/Bronze/Iron)
- **Security** - NX stack (PT_GNU_STACK), bounds checking on arrays/strings, runtime div-by-zero guard

### Performance Tiers
| Mode | Flag | Throughput | Compatibility |
|------|------|-----------|---------------|
| Scalar | (default) | 1x baseline | All x86_64 |
| SSE2 | (default) | **5.2x** (arr_sum, arr_fill) | All x86_64 |
| AVX2 | `--avx2` | **10.5x** (arr_sum) | Desktop/server CPUs |

### Precision Control

First compiler to offer **per-compilation math precision** — no other compiler (GCC, LLVM, ICC, Rust) provides this.

```bash
aric program.ari --precision=6     # ~4 digits — IoT, sensors, ML inference
aric program.ari --precision=8     # ~8 digits — ML training, PID (default)
aric program.ari --precision=15    # ~12 digits — science, finance, orbital
```

| Level | Digits | math_exp terms | math_log terms | Binary cost | Use case |
|-------|--------|---------------|---------------|-------------|----------|
| `--precision=6` | ~4 | 6 squarings | 3 atanh | smallest | Robotics, edge, ML inference |
| `--precision=8` | ~8 | 8 squarings | 5 atanh | default | ML training, PID controllers |
| `--precision=15` | ~12 | 12 squarings | 8 atanh | +15% | Science, finance, orbital mechanics |

The programmer decides the precision-performance trade-off at compile time, not the language.

### Neural Network Training

Aricode can train neural networks with backpropagation in **zero-dependency binaries under 10 KB**.

**XOR Network** (2 → 2 hidden ReLU → 1 output):

```
Binary size:     8,665 bytes (8.4 KB)
Training time:   2ms (10,000 epochs)
Loss:            0.194 → 0.000000
Dependencies:    zero
```

| Input | Target | Prediction |
|-------|--------|------------|
| [0,0] | 0 | 0.000000 |
| [0,1] | 1 | 0.999999 |
| [1,0] | 1 | 0.999999 |
| [1,1] | 0 | 0.000000 |

**Comparison with other frameworks:**

| Framework | Binary/Runtime | Training time |
|-----------|---------------|---------------|
| **Aricode** | **8.4 KB** | **2ms** |
| C (manual) | 750 KB | 5ms |
| Python + NumPy | 50 MB | 100ms |
| Python + PyTorch | 2 GB | 500ms |

Built with: `arr_f64_new/get/set/dot`, `math_sqrt`, ReLU (inline), SSE2 f64 arithmetic. No libc, no BLAS, no runtime.

## Benchmarks

14 challenges, 154 binaries, 12 implementations:

```
  OVERALL SPARRING CHAMPION

  #1   NASM x86_64 asm    475 points
  #2   aricode            468 points
  #3   C (gcc -O2)        315 points
  #7   C (clang -O2)      262 points
  #11  Rust               100 points  (4.7x behind aricode)
  #12  Go                  84 points  (5.6x behind aricode)
```

**Binary size comparison:**

| Program | Aricode | GCC static | Rust | Go |
|---------|---------|------------|------|-----|
| Hello World | **199 B** | 754 KB | 11.3 MB | 1.8 MB |
| Bubble Sort | **1,315 B** | 754 KB | 11.3 MB | 1.8 MB |
| File I/O | **947 B** | 754 KB | 11.3 MB | 1.8 MB |

## Quick Start

```bash
# Build the compiler
cd aricode/src/compiler && make

# Hello World
./aric ../../examples/hello_world.ari -o hello
./hello

# Run all 23 automated tests
cd ../../tests && bash run_all.sh
```

## Examples

| File | What it does | Binary |
|------|-------------|--------|
| [hello_world.ari](aricode/examples/hello_world.ari) | Hello World | 199 B |
| [fibonacci.ari](aricode/examples/fibonacci.ari) | First 20 Fibonacci numbers | 422 B |
| [primes.ari](aricode/examples/primes.ari) | All primes below 100 | 674 B |
| [calculator.ari](aricode/examples/calculator.ari) | Interactive calculator | 1,480 B |
| [circle.ari](aricode/examples/circle.ari) | f64 circle area (SSE2) | 1,069 B |
| [sort.ari](aricode/examples/sort.ari) | Bubble sort with arrays | 1,315 B |
| [structs.ari](aricode/examples/structs.ari) | Point and Rectangle structs | 2,270 B |
| [structs2.ari](aricode/examples/structs2.ari) | Vector collection with for loops | 1,845 B |
| [error_handling.ari](aricode/examples/error_handling.ari) | try/catch across functions | 1,605 B |
| [demo.ari](aricode/examples/demo.ari) | Full language showcase | 1,538 B |
| [physics.ari](aricode/examples/physics.ari) | Projectile motion simulation | 3,347 B |
| [exact_math.ari](aricode/examples/exact_math.ari) | 0.1 + 0.2 = 0.3 (exact decimal) | 1,290 B |
| [strings.ari](aricode/examples/strings.ari) | String operations | 2,272 B |
| [guessing_game.ari](aricode/examples/guessing_game.ari) | Interactive number guessing | 1,021 B |
| [fileio.ari](aricode/examples/fileio.ari) | File read/write | 947 B |

## Test Suite

23 automated tests covering all language features:

```bash
cd aricode/tests && bash run_all.sh
```

```
  --- Basic Output ---       3/3 PASS
  --- Arithmetic ---         2/2 PASS
  --- Control Flow ---       6/6 PASS
  --- Functions ---          2/2 PASS
  --- Types ---              2/2 PASS (f64 + exact decimal)
  --- Data Structures ---    5/5 PASS
  --- Error Handling ---     1/1 PASS
  --- I/O ---                2/2 PASS
  Total: 23/23 PASS
```

## Compiler Architecture

```
Source (.ari)
    |
    v
 [Lexer] ──────── 40+ token types, keywords, operators
    |
    v
 [Parser] ─────── recursive descent, AST generation
    |
    v
 [Semantic] ───── 5-level error enforcement (THE GUARDIAN)
    |
    v
 [Optimizer] ──── constant fold, strength reduce, TCO, DCE, decimal fold
    |
    v
 [Codegen] ────── x86_64 + SSE2, 23 builtins, try/catch via R12-R15
    |
    v
 [ELF Writer] ─── minimal Linux binary (120 byte header)
```

## Error Levels

| Level | Name | Action | Example |
|-------|------|--------|---------|
| 0 | SILENT | **Blocks compilation** | `10 / 0` |
| 1 | LOGIC | Warning | Type mismatch |
| 2 | WARNING | Informational | Unused variable |
| 3 | SYSTEM | Must catch at runtime | File not found |
| 4 | CATASTROPHIC | Log and terminate | Out of memory |

## Sparring Challenges

14 challenges across 7 categories:

| Category | Challenges |
|----------|-----------|
| Arithmetic | Addition, Factorial |
| Recursion | Fibonacci, Ackermann A(3,4) |
| Number Theory | Collatz conjecture, Mersenne M31 prime |
| Algorithms | GCD, Prime counting, Integer sqrt, Powmod |
| AI/ML | Perceptron neural network, Minimax game AI |
| Float | Leibniz pi approximation |
| Arrays | Array sum |

```bash
cd aricode/sparring
bash build_all.sh        # Build 154 binaries
bash benchmark.sh        # Run benchmarks
bash sparring_report.sh  # Podium report
```

## Paper

**"Aricode: A Compiled Language with Mandatory Error Handling and Direct x86_64 Code Generation"**

See [paper/aricode_paper.tex](paper/aricode_paper.tex)

## License

Copyright (c) 2026 Edwin F. Veliz Jaramillo. All rights reserved. See [LICENSE](LICENSE).
