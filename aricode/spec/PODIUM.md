# aricode Podium System Specification

## Overview

The Podium system is aricode's built-in performance rating engine. After compilation, every function in the program receives a medal rating based on the quality of the generated x86_64 machine code. This gives developers immediate, actionable feedback on performance without requiring external profiling tools.

The Podium system operates at compile time and analyzes the *generated machine code*, not the source code. Two different source-level implementations that produce the same machine code will receive the same rating.

---

## Medal Ratings

### GOLD - Optimal

The generated machine code is at or near the theoretical minimum for the operation. The compiler could not produce better code for this function.

**Criteria:**
- Instruction count is within 10% of the theoretical minimum for the algorithm
- No redundant loads, stores, or register moves
- Optimal register allocation (no register spills to stack)
- Branch prediction hints are correct
- SIMD instructions used where applicable
- No unnecessary memory allocations
- Loop bodies are tight with no wasted instructions
- Proper use of CPU cache (sequential access patterns)

**Display format:**
```
fn add(a: i32, b: i32) -> i32  ->  GOLD (6 bytes, 1 cycle) | Optimal
```

### SILVER - Good

The generated code is functionally correct and reasonably efficient, but the compiler has identified specific improvements that could be made. SILVER functions work well in practice but leave performance on the table.

**Criteria:**
- Code works correctly but has one or more of:
  - Unnecessary heap allocations that could be stack-allocated
  - Register spills that could be avoided with restructuring
  - Missed vectorization opportunities
  - Branch patterns that could be replaced with conditional moves
  - String formatting where concatenation would suffice (or vice versa)
  - Redundant bounds checks that the compiler could not eliminate
  - Suboptimal loop structure (could be unrolled or vectorized)

**Display format:**
```
fn format_name(first: str, last: str) -> str  ->  SILVER (48 bytes, 12 cycles) | Heap alloc for concatenation
```

The message after `|` describes the specific reason the function did not achieve GOLD.

### BRONZE - Inefficient

The code works but uses significantly more resources than necessary. The compiler strongly recommends restructuring.

**Criteria:**
- Code has one or more of:
  - Algorithmic inefficiency (O(n^2) where O(n) or O(n log n) is possible)
  - Excessive heap allocations in a loop
  - Unnecessary copies of large data structures
  - Contains `unsafe` blocks (automatic BRONZE cap)
  - Excessive function call overhead (deep call chains that could be inlined)
  - Poor cache locality (random access patterns on large data)
  - Redundant computation (same value computed multiple times)
  - Unoptimized string operations (repeated concatenation in loop)
  - Dynamic dispatch where static dispatch would work

**Display format:**
```
fn find_duplicates(items: &arr<str>) -> arr<str>  ->  BRONZE (256 bytes, O(n^2)) | Nested loop: use a Set for O(n)
```

### NO MEDAL - Rejected (optional, with `--podium-min`)

When `--podium-min` is set, functions that fall below the threshold will cause the build to fail.

```
vtc build --podium-min=silver src/main.vt

ERROR: Function 'find_duplicates' rated BRONZE, minimum is SILVER.
  Suggestion: Use a Set<str> instead of nested loop for O(n) lookup.
  Location: src/search.vt:42
```

---

## Rating Metrics

The Podium system evaluates functions on these metrics:

### 1. Code Size (bytes)

The size of the generated machine code for the function body.

```
GOLD threshold:   function body <= optimal_size * 1.1
SILVER threshold: function body <= optimal_size * 2.0
BRONZE threshold: function body > optimal_size * 2.0
```

"Optimal size" is computed by the compiler's internal model of the minimum instruction sequence for the function's operations.

### 2. Cycle Count (estimated)

Estimated CPU cycles for a single execution of the function, assuming L1 cache hits. For functions with loops, the cycle count is expressed as a function of the input size.

```
fn sum(arr: &arr<i32>) -> i64
  Cycles: O(n) where n = arr.len()
  Per-element: 1 cycle (vectorized with SIMD)
  Overhead: 8 cycles (setup + reduction)
  Total: 8 + n/4 cycles (4-wide SIMD)
```

### 3. Memory Operations

Count and type of memory operations:
- Stack reads/writes
- Heap allocations (`malloc`/`free` or equivalent)
- Cache line touches

```
fn example():
  Stack:  2 reads, 1 write
  Heap:   1 allocation (32 bytes)
  Cache:  3 lines touched
```

### 4. Register Pressure

How efficiently the function uses CPU registers:

```
Registers used: 6 / 16 general purpose
Register spills: 0     (GOLD)
Register spills: 1-2   (SILVER)
Register spills: 3+    (BRONZE)
```

### 5. Branch Prediction Score

Estimated branch prediction hit rate:

```
Branches: 3
Predictable: 3/3 (100%)  -> GOLD
Predictable: 2/3 (67%)   -> SILVER
Predictable: 1/3 (33%)   -> BRONZE
```

---

## Compiler Suggestions

When a function receives SILVER or BRONZE, the compiler provides specific, actionable suggestions for improvement.

### Suggestion Format

```
fn process_data(data: &arr<Record>) -> arr<Result>  ->  SILVER (128 bytes, O(n))
  | Suggestions:
  |   1. Line 45: Clone of 'record' can be replaced with borrow (saves 1 alloc/iter)
  |   2. Line 52: Loop can be vectorized if array length check is hoisted
  |   3. Line 58: match arms 3-5 have identical bodies - merge with | pattern
```

### Common Suggestions

| Pattern Detected | Medal Impact | Suggestion |
|-----------------|-------------|------------|
| Heap alloc in loop | SILVER | "Move allocation outside loop" or "Use stack buffer" |
| Nested loops on same data | BRONZE | "Use a map/set for O(1) inner lookup" |
| Repeated string concatenation | BRONZE | "Use string builder or f-string" |
| Clone where borrow works | SILVER | "Use &T instead of cloning" |
| Unvectorized numeric loop | SILVER | "Restructure for SIMD vectorization" |
| Branch in hot loop | SILVER | "Use conditional move or branchless logic" |
| Large struct passed by value | SILVER | "Pass by reference (&T)" |
| Virtual dispatch in loop | BRONZE | "Use monomorphized generic instead of trait object" |
| Redundant bounds check | SILVER | "Use iterator instead of index access" |
| Unnecessary mutex lock scope | SILVER | "Narrow lock scope to critical section only" |

---

## Podium Output Modes

### Standard Build Output

```
vtc build src/main.vt --release

[BUILD] src/main.vt -> main
[CHECK] All checks passed.
[COMPILE] Generating x86_64...

PODIUM RESULTS:
  fn main()                    -> SILVER (148 bytes, ~80 cycles) | 2 heap allocs
  fn Point.new()               -> GOLD   (12 bytes, 3 cycles)   | Optimal
  fn Point.distance()          -> GOLD   (28 bytes, 8 cycles)   | SSE2 sqrt
  fn Shape.area()              -> GOLD   (20 bytes, 5 cycles)   | Branch-free
  fn find_largest()            -> GOLD   (36 bytes, O(n))       | Optimal scan

SUMMARY: 4 GOLD, 1 SILVER, 0 BRONZE
[LINK] main -> 8.2 KB
[DONE] Build successful.
```

### Detailed Podium Report

```
vtc podium src/main.vt --detail

PODIUM DETAILED REPORT
======================

fn Point.distance(self, other: &Point) -> f64
  Rating:      GOLD
  Code size:   28 bytes
  Cycles:      8 (estimated, single call)
  Registers:   4 / 16 (xmm0-xmm3)
  Spills:      0
  Heap allocs: 0
  Branches:    0
  SIMD:        Yes (SSE2 mulsd, addsd, sqrtsd)
  Assembly:
    movsd  xmm0, [rdi]        ; self.x
    subsd  xmm0, [rsi]        ; dx = self.x - other.x
    mulsd  xmm0, xmm0         ; dx * dx
    movsd  xmm1, [rdi+8]      ; self.y
    subsd  xmm1, [rsi+8]      ; dy = self.y - other.y
    mulsd  xmm1, xmm1         ; dy * dy
    addsd  xmm0, xmm1         ; dx*dx + dy*dy
    sqrtsd xmm0, xmm0         ; sqrt(...)
    ret

---

fn main() -> i32
  Rating:      SILVER
  Code size:   148 bytes
  Cycles:      ~80 (estimated)
  Registers:   8 / 16
  Spills:      0
  Heap allocs: 2 (array of shapes, format strings)
  Branches:    4
  Suggestions:
    1. Pre-allocate shapes array with known size: arr<Shape>.with_capacity(3)
    2. Format string in loop body allocates each iteration - consider print args
  Assembly:    [omitted for brevity, use --emit-asm for full listing]
```

### JSON Output

```
vtc podium src/main.vt --json
```

Produces `.aricode/podium.json`:

```json
{
  "timestamp": "2026-04-11T14:30:00.000Z",
  "file": "src/main.vt",
  "target": "x86_64-linux",
  "opt_level": 3,
  "functions": [
    {
      "name": "Point.distance",
      "file": "src/main.vt",
      "line": 15,
      "rating": "gold",
      "code_size_bytes": 28,
      "estimated_cycles": 8,
      "registers_used": 4,
      "register_spills": 0,
      "heap_allocations": 0,
      "branches": 0,
      "simd": true,
      "suggestions": [],
      "reason": "Optimal"
    },
    {
      "name": "main",
      "file": "src/main.vt",
      "line": 50,
      "rating": "silver",
      "code_size_bytes": 148,
      "estimated_cycles": 80,
      "registers_used": 8,
      "register_spills": 0,
      "heap_allocations": 2,
      "branches": 4,
      "simd": false,
      "suggestions": [
        "Pre-allocate shapes array with known size",
        "Format string in loop body allocates each iteration"
      ],
      "reason": "Heap allocations in loop"
    }
  ],
  "summary": {
    "gold": 4,
    "silver": 1,
    "bronze": 0,
    "total": 5
  }
}
```

---

## Podium Thresholds Configuration

### aricode.toml

```toml
[podium]
# Minimum medal for build to succeed (none | bronze | silver | gold)
minimum = "none"

# Show podium results during build (true | false)
show_results = true

# Show detailed suggestions for non-gold functions (true | false)
show_suggestions = true

# Generate JSON report (true | false)
generate_json = false

# Functions to exclude from podium analysis (e.g., test helpers)
exclude = ["test_*", "bench_*", "debug_*"]

# Custom thresholds (advanced)
[podium.thresholds]
# Maximum code size multiplier over optimal before downgrade
silver_size_factor = 2.0
bronze_size_factor = 4.0

# Maximum register spills before downgrade
silver_max_spills = 2
bronze_max_spills = 5

# Maximum heap allocations for gold (per invocation)
gold_max_heap_allocs = 0
silver_max_heap_allocs = 3
```

### Per-Function Override

```
#[podium(ignore)]
fn debug_dump(state: &ProgramState) -> void {
  // This function is intentionally verbose - don't rate it
}

#[podium(min = "silver")]
fn critical_path(data: &arr<f64>) -> f64 {
  // This function MUST be at least silver
}
```

---

## How the Podium System Works Internally

### Phase 9 in the Compilation Pipeline

The Podium analysis runs after x86_64 code generation (Phase 8) and before linking (Phase 10). It operates on the generated machine code, not the source or IR.

### Analysis Steps

1. **Instruction Counting**: Count total instructions, categorize by type (arithmetic, memory, branch, SIMD).

2. **Optimal Baseline Computation**: For the function's operations (as described by the IR), compute the theoretical minimum instruction count. This uses a database of known optimal instruction sequences for common patterns:
   - Integer addition: 1 instruction (`add`)
   - Function prologue/epilogue: 2-4 instructions
   - Array sum: n/4 instructions with AVX2
   - etc.

3. **Ratio Computation**: Compare actual instruction count to baseline.

4. **Penalty Assessment**: Apply penalties for:
   - Each heap allocation: -1 level if any (GOLD becomes SILVER at minimum)
   - Each register spill: -1 level per 3 spills
   - Each unpredictable branch: -1 level per 2 unpredictable branches
   - Use of `unsafe`: cap at BRONZE
   - O(n^2) or worse when better is possible: cap at BRONZE

5. **Medal Assignment**: Based on ratio and penalties.

6. **Suggestion Generation**: For non-GOLD functions, analyze each penalty source and generate a human-readable suggestion for improvement.

### Limitations

The Podium system has known limitations:

- **Cycle estimates are approximate.** They assume L1 cache hits and no contention. Real-world performance depends on data, cache state, and other threads.
- **Algorithmic complexity detection is heuristic.** The compiler recognizes common patterns (nested loops, repeated search) but cannot prove algorithmic complexity in all cases.
- **GOLD does not mean fast.** A function that does inherently expensive work (e.g., computing a cryptographic hash) can be GOLD -- it means the implementation is optimal, not that the operation is cheap.
- **Cross-function optimization is not rated.** Inlining decisions affect the caller's rating, not the callee's. A GOLD function inlined into a loop may make the caller SILVER if it increases register pressure.

---

## Examples

### Example 1: Simple Arithmetic (GOLD)

```
fn add(a: i32, b: i32) -> i32 {
  return a + b;
}
```

Podium output:
```
fn add(a: i32, b: i32) -> i32  ->  GOLD (6 bytes, 1 cycle) | Optimal
  Assembly:
    lea eax, [rdi + rsi]   ; add using LEA (single instruction)
    ret
```

### Example 2: String Building in Loop (BRONZE)

```
fn build_csv(items: &arr<str>) -> str {
  let result: str = "";
  for (item in items) {
    result = result + item + ",";    // Reallocates every iteration
  }
  return result;
}
```

Podium output:
```
fn build_csv(items: &arr<str>) -> str  ->  BRONZE (96 bytes, O(n^2)) | String reallocation in loop
  | Suggestions:
  |   1. Use a StringBuilder: let sb = StringBuilder.new(); sb.append(item); sb.append(",");
  |   2. Alternative: items.join(",")
  |   Current: O(n^2) due to repeated string copying
  |   Fixed:   O(n) with StringBuilder or join()
```

### Example 3: Improved String Building (GOLD)

```
fn build_csv(items: &arr<str>) -> str {
  return items.join(",");
}
```

Podium output:
```
fn build_csv(items: &arr<str>) -> str  ->  GOLD (32 bytes, O(n)) | Optimal - single allocation
```

### Example 4: Search With Suboptimal Structure (BRONZE to GOLD)

```
// BRONZE version:
fn has_duplicate(items: &arr<i32>) -> bool {
  for (i in 0..items.len()) {
    for (j in (i+1)..items.len()) {
      if (items[i] == items[j]) {
        return true;
      }
    }
  }
  return false;
}
// BRONZE (O(n^2)) | Nested loop: use Set<i32> for O(n)

// GOLD version:
fn has_duplicate(items: &arr<i32>) -> bool {
  let seen: Set<i32> = Set.new();
  for (item in items) {
    if (seen.contains(item)) {
      return true;
    }
    seen.insert(item);
  }
  return false;
}
// GOLD (O(n) average) | Optimal hash-based dedup
```

### Example 5: Unsafe Code (BRONZE cap)

```
fn raw_copy(src: *u8, dst: *u8, len: u64) -> void {
  unsafe {
    for (let i: u64 = 0; i < len; i++) {
      *(dst + i) = *(src + i);
    }
  }
}
```

Podium output:
```
fn raw_copy(src: *u8, dst: *u8, len: u64) -> void  ->  BRONZE (24 bytes, O(n)) | Contains unsafe block
  | Note: Functions with unsafe blocks are capped at BRONZE.
  | Consider using safe alternatives: arr.copy_from() or std.mem.copy()
```

---

## Querying Podium History

Podium results are stored in `.aricode/podium.json` after each build. This enables tracking performance over time.

```
vtc podium --history

PODIUM HISTORY: fn Point.distance()
  2026-04-01  GOLD   (28 bytes, 8 cycles)
  2026-04-05  SILVER (36 bytes, 10 cycles)  <- regression
  2026-04-06  GOLD   (28 bytes, 8 cycles)   <- fixed

vtc podium --regressions

REGRESSIONS SINCE LAST BUILD:
  fn process_data()  GOLD -> SILVER  (added heap allocation on line 45)
```

### CI Integration

```toml
# aricode.toml - enforce podium in CI
[podium]
minimum = "silver"

# Fail CI if any function regresses
[podium.ci]
fail_on_regression = true
baseline = ".aricode/podium-baseline.json"
```

```bash
# In CI pipeline:
vtc build --release src/main.vt
vtc podium --check-regressions --baseline=.aricode/podium-baseline.json
# Exit code 1 if any regressions detected
```
