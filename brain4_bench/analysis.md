# Brain 4: CPU Cycle-Level Addition Benchmark Analysis

## Target System
- **CPU**: AMD Ryzen 7 5800X (Zen 3 microarchitecture)
- **Cores**: 8C/16T
- **Compiler**: GCC 14.2.0 with `-O2 -march=znver3`
- **OS**: Linux 6.12.74 (Debian), x86_64

## Test 1: Addition Instruction Variants

Measured with RDTSC/RDTSCP + CPUID serialization, 100M iterations, best of 5 runs.

| Method           | Min Cycles   | Cycles/Op |
|------------------|-------------|-----------|
| ADD reg, reg     | 78,461,488  | 0.7846    |
| ADD reg, imm     | 78,449,480  | 0.7845    |
| LEA [reg+reg]    | 78,469,696  | 0.7847    |
| ADD mem, reg     | 78,436,978  | 0.7844    |
| Bitwise add      | 285,927,162 | 2.8593    |

### Key Finding: Sub-Cycle Throughput

All native addition forms measure at **~0.78 cycles/op**, which is *below* 1.0 cycle. This is not a measurement error -- it demonstrates **superscalar execution**. The Zen 3 has **4 integer ALU pipes** and can retire up to 4 ADD/LEA operations per cycle when there are no data dependencies between iterations. The loop overhead (counter increment, branch) gets overlapped with the ADD, yielding a measured throughput below 1.0 cyc/op for the payload instruction.

### All Native Forms Are Identical

ADD reg,reg / ADD reg,imm / LEA / ADD mem,reg all measure within noise of each other (~0.784 cyc/op). This is because:

1. **ADD reg,reg**: 1 uop, 1 cycle latency, port 0/1/2/3 on Zen 3
2. **ADD reg,imm**: 1 uop, 1 cycle latency, same ports
3. **LEA [reg+reg]**: 1 uop, 1 cycle latency on Zen 3's AGU, simple LEA is as fast as ADD
4. **ADD mem,reg**: L1d-cached memory operand is micro-fused, fully pipelined

### Bitwise Add: 2.86 cyc/op (3.6x slower)

The ripple-carry bitwise add needs ~2.86 cycles per addition due to data-dependent carry chain serialization.

## Test 2: Bitwise Addition Methods vs Native ADD

50M iterations, best of 5 runs.

| Method              | Cycles/Op | Slowdown vs Native |
|---------------------|-----------|-------------------|
| Native ADD          | 3.92      | 1.0x (baseline)   |
| Ripple-carry (loop) | 5.27      | 1.3x              |
| Parallel prefix     | 16.47     | 4.2x              |
| Bit-serial (1-bit)  | 173.37    | 44.2x             |

### Why These Numbers?

**Native ADD (3.92 cyc/op with function pointer overhead):**
The function pointer call prevents inlining, adding call/ret overhead (~3 cycles). The actual ADD is ~1 cycle.

**Ripple-carry (5.27 cyc/op, 1.3x):**
With input b=2 (binary 10), carry propagation terminates after 1-2 loop iterations.

**Parallel prefix / Kogge-Stone (16.47 cyc/op, 4.2x):**
Despite O(log n) depth, 6 stages x 3 dependent operations = 18 cycle critical path.

**Bit-serial (173.37 cyc/op, 44.2x):**
64 iterations x 6+ operations each = 384+ ALU operations per addition.

## Microarchitecture-Level Explanation

### Why Is Hardware ADD So Fast?

The Zen 3 ALU uses a **carry-lookahead adder (CLA)** in silicon:

1. **Gate-level parallelism**: 64-bit CLA computes all carries in O(log2(64)) = 6 gate levels, each ~10-50ps in 7nm. Total < 300ps.
2. **Single-cycle at 4.7 GHz**: One cycle = ~213ps. Full add fits in one pipeline stage.
3. **4-wide throughput**: 4 ALU pipes with independent adders = 0.25 cyc/op theoretical throughput.
4. **Zero-cost variants**: ADD-imm, LEA, ADD-mem all decode to single micro-ops using the same unit.

### The Theoretically Optimal Number of Cycles for an Addition

| Metric | Value | Explanation |
|--------|-------|-------------|
| **Latency** | **1 cycle** | Minimum time from input ready to output ready |
| **Throughput** | **0.25 cycles/op** | 4 independent ALU pipes, 4 ADDs per cycle |
| **Our measured throughput** | **0.78 cyc/op** | Loop overhead consumes some pipeline bandwidth |

### Why Software Bitwise Addition Can Never Match Hardware

- **Hardware CLA**: All 64 carry bits computed *simultaneously* across physical wires in ~300ps
- **Software**: Must serialize carry computation through sequential instructions. Even Kogge-Stone needs 18 dependent instructions = 18 cycles minimum

The hardware adder achieves in 1 cycle what takes the best software approach 4-17 cycles. This is the **cost of abstraction** -- going from transistor-level spatial parallelism to instruction-level sequential execution.

## Summary

| Question | Answer |
|----------|--------|
| Which is fastest on Zen 3? | All native forms (ADD/LEA) tied at 1 cycle latency, ~0.78 cyc/op throughput |
| Theoretical minimum? | 1 cycle latency, 0.25 cyc/op throughput (4 ALU pipes) |
| ADD reg,reg vs ADD reg,imm? | Identical -- same uop, same ports |
| LEA vs ADD? | Identical for simple forms |
| Memory operand penalty? | None when L1d-resident |
| Software bitwise add cost? | 1.3x to 44x slower depending on algorithm |
| Why is HW ADD unbeatable? | Silicon carry-lookahead computes 64 carry bits in parallel in ~300ps; software must serialize |
