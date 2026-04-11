# aricode Sparring Benchmark Framework

A benchmarking system that compares aricode's compiled output against established
programming languages for identical computational tasks.

## Competitors

| Language | Compiler/Runtime | Notes |
|----------|-----------------|-------|
| **aricode** | `aric` (direct x86_64 codegen) | No libc, no linker, raw ELF |
| **C** | `gcc -O0/-O2/-O3/-Os` | Static and dynamic linking |
| **C** | `clang -O2` | LLVM backend |
| **Rust** | `rustc -C opt-level=2` | If available |
| **Go** | `go build` | If available |
| **Assembly** | `nasm -f elf64` + `ld -s -n` | Hand-written x86_64 |
| **Python** | `python3` | Interpreted baseline |

## Challenges

1. **01_add** - Simple Addition: `37 + 5 = 42` (return as exit code)
2. **02_fib** - Fibonacci: `fib(10) = 55` (recursive, return as exit code)
3. **03_factorial** - Factorial: `5! = 120` (recursive, return as exit code)

## Metrics

- **Binary Size** - Total file size in bytes
- **Instruction Count** - Number of machine instructions (via objdump)
- **Execution Time** - Average nanoseconds per run (1000 iterations)
- **Startup Time** - Process exec-to-exit latency

## Usage

```bash
# Step 1: Build all challenges in all available languages
./build_all.sh

# Step 2: Run benchmarks (default: 1000 iterations)
./benchmark.sh          # or: ./benchmark.sh 5000

# Step 3: Generate the podium report
./sparring_report.sh

# Or run everything at once:
./build_all.sh && ./benchmark.sh && ./sparring_report.sh
```

## Directory Structure

```
sparring/
  build_all.sh           - Build all challenges
  benchmark.sh           - Run benchmarks and collect metrics
  sparring_report.sh     - Generate visual podium report
  challenges/
    01_add/              - Addition challenge sources
    02_fib/              - Fibonacci challenge sources
    03_factorial/        - Factorial challenge sources
  build/                 - Compiled binaries (generated)
  results/               - CSV benchmark results (generated)
```

## How aricode Competes

aricode generates native x86_64 machine code directly, producing minimal ELF
binaries with no libc dependency, no linker overhead, and no runtime. This gives
it a significant advantage in binary size and startup time compared to languages
that link against system libraries.

The framework detects which compilers are available and only benchmarks those
that exist on the system. Missing compilers are skipped gracefully.
