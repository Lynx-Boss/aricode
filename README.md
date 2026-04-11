# Aricode

**The first native compilation target for AI agents.**

Every AI coding system today generates Python, C, or JavaScript — languages designed for humans, compiled by heavyweight toolchains. Aricode is different: it's a language designed for **machines to write**, compiled directly to x86_64 machine code in **sub-millisecond**, producing binaries as small as **199 bytes** with **zero runtime dependencies**.

```
AI Agent → generates .ari → aric (0.3ms) → 199-byte ELF binary → runs
```

No LLVM. No linker. No libc. No runtime. Just raw machine code.

## Why Aricode?

**For AI agents, not humans.** Every feature exists to maximize machine efficiency.

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

### Builtins (23)
| Category | Functions |
|----------|-----------|
| **Console** | print_str, print_int, print_float, print_dec, read_int, read_float |
| **Strings** | str_new, str_len, str_eq, str_char_at, str_println, str_concat |
| **Arrays** | arr_new, arr_get, arr_set, arr_len |
| **Files** | file_open, file_read, file_write, file_close |
| **Convert** | int_to_float, float_to_int, dec |

### Compiler
- **Direct x86_64 codegen** - AST to machine code, no LLVM, no IR
- **Minimal ELF binaries** - 199 bytes (Hello World) to 3.3 KB (physics sim)
- **6 optimization passes** - constant folding, strength reduction, peephole, TCO, DCE, decimal folding
- **Semantic analyzer** - 5-level error hierarchy, always-on, zero false positives
- **Podium system** - compile-time code quality rating (Gold/Silver/Bronze/Iron)

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
