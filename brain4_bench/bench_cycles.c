/*
 * bench_cycles.c - CPU cycle-level benchmarking of addition implementations
 * Target: AMD Ryzen 7 5800X (Zen 3), Linux x86_64
 *
 * Measures RDTSC cycles for:
 *   1. ADD reg, reg
 *   2. ADD reg, imm
 *   3. LEA reg, [reg+reg]
 *   4. ADD with memory operand
 *   5. XOR + shift based addition (bitwise full adder)
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------- RDTSC helpers --------------- */

static inline uint64_t rdtsc_start(void) {
    uint32_t lo, hi;
    /* CPUID serializes; then RDTSC reads the counter */
    __asm__ __volatile__(
        "cpuid\n\t"
        "rdtsc\n\t"
        : "=a"(lo), "=d"(hi)
        : "a"(0)
        : "rbx", "rcx");
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtsc_end(void) {
    uint32_t lo, hi;
    /* RDTSCP serializes reads; CPUID serializes after */
    __asm__ __volatile__(
        "rdtscp\n\t"
        "mov %%eax, %0\n\t"
        "mov %%edx, %1\n\t"
        "cpuid\n\t"
        : "=r"(lo), "=r"(hi)
        :
        : "rax", "rbx", "rcx", "rdx");
    return ((uint64_t)hi << 32) | lo;
}

/* --------------- Constants --------------- */

#define ITERATIONS   100000000ULL   /* 100 million per run */
#define WARMUP       10000000ULL    /* 10 million warmup   */
#define RUNS         5              /* repeat and take min  */

/* --------------- Benchmark: ADD reg, reg --------------- */

static uint64_t bench_add_reg_reg(void) {
    uint64_t start, end;
    uint64_t a = 1, b = 2;

    /* warmup */
    for (uint64_t i = 0; i < WARMUP; i++) {
        __asm__ __volatile__("add %1, %0" : "+r"(a) : "r"(b));
    }

    start = rdtsc_start();
    for (uint64_t i = 0; i < ITERATIONS; i++) {
        __asm__ __volatile__("add %1, %0" : "+r"(a) : "r"(b));
    }
    end = rdtsc_end();

    /* prevent dead-code elimination */
    volatile uint64_t sink = a;
    (void)sink;

    return end - start;
}

/* --------------- Benchmark: ADD reg, imm --------------- */

static uint64_t bench_add_reg_imm(void) {
    uint64_t start, end;
    uint64_t a = 1;

    for (uint64_t i = 0; i < WARMUP; i++) {
        __asm__ __volatile__("addq $42, %0" : "+r"(a));
    }

    start = rdtsc_start();
    for (uint64_t i = 0; i < ITERATIONS; i++) {
        __asm__ __volatile__("addq $42, %0" : "+r"(a));
    }
    end = rdtsc_end();

    volatile uint64_t sink = a;
    (void)sink;
    return end - start;
}

/* --------------- Benchmark: LEA reg, [reg+reg] --------------- */

static uint64_t bench_lea(void) {
    uint64_t start, end;
    uint64_t a = 1, b = 2, result;

    for (uint64_t i = 0; i < WARMUP; i++) {
        __asm__ __volatile__("lea (%1,%2), %0" : "=r"(result) : "r"(a), "r"(b));
        a = result;
    }

    start = rdtsc_start();
    for (uint64_t i = 0; i < ITERATIONS; i++) {
        __asm__ __volatile__("lea (%1,%2), %0" : "=r"(result) : "r"(a), "r"(b));
        a = result;
    }
    end = rdtsc_end();

    volatile uint64_t sink = result;
    (void)sink;
    return end - start;
}

/* --------------- Benchmark: ADD with memory operand --------------- */

static uint64_t bench_add_mem(void) {
    uint64_t start, end;
    volatile uint64_t mem_val = 42;
    uint64_t a = 1;

    for (uint64_t i = 0; i < WARMUP; i++) {
        __asm__ __volatile__("addq %1, %0" : "+r"(a) : "m"(mem_val));
    }

    start = rdtsc_start();
    for (uint64_t i = 0; i < ITERATIONS; i++) {
        __asm__ __volatile__("addq %1, %0" : "+r"(a) : "m"(mem_val));
    }
    end = rdtsc_end();

    volatile uint64_t sink = a;
    (void)sink;
    return end - start;
}

/* --------------- Benchmark: XOR/AND/SHIFT addition --------------- */

/* Add two 64-bit integers using only bitwise ops (ripple-carry via loop) */
static inline uint64_t bitwise_add(uint64_t x, uint64_t y) {
    uint64_t carry;
    while (y != 0) {
        carry = x & y;
        x = x ^ y;
        y = carry << 1;
    }
    return x;
}

static uint64_t bench_bitwise_add(void) {
    uint64_t start, end;
    uint64_t a = 1, b = 2;

    for (uint64_t i = 0; i < WARMUP; i++) {
        a = bitwise_add(a, b);
    }

    start = rdtsc_start();
    for (uint64_t i = 0; i < ITERATIONS; i++) {
        a = bitwise_add(a, b);
    }
    end = rdtsc_end();

    volatile uint64_t sink = a;
    (void)sink;
    return end - start;
}

/* --------------- Main --------------- */

typedef struct {
    const char *name;
    uint64_t (*fn)(void);
} bench_entry;

int main(void) {
    bench_entry benches[] = {
        {"ADD reg, reg  ", bench_add_reg_reg},
        {"ADD reg, imm  ", bench_add_reg_imm},
        {"LEA [reg+reg] ", bench_lea},
        {"ADD mem, reg  ", bench_add_mem},
        {"Bitwise add   ", bench_bitwise_add},
    };
    int n = sizeof(benches) / sizeof(benches[0]);

    printf("=============================================================\n");
    printf("  CPU Cycle-Level Addition Benchmark (RDTSC)\n");
    printf("  Iterations per run: %llu   Runs: %d (taking minimum)\n",
           (unsigned long long)ITERATIONS, RUNS);
    printf("=============================================================\n\n");
    printf("%-18s %12s %12s %12s\n", "Method", "Min Cycles", "Avg Cycles", "Cyc/Op");
    printf("--------------------------------------------------------------\n");

    for (int i = 0; i < n; i++) {
        uint64_t min_cyc = UINT64_MAX;
        uint64_t total = 0;

        for (int r = 0; r < RUNS; r++) {
            uint64_t cyc = benches[i].fn();
            if (cyc < min_cyc) min_cyc = cyc;
            total += cyc;
        }

        double avg = (double)total / RUNS;
        double per_op = (double)min_cyc / ITERATIONS;

        printf("%-18s %12llu %12.0f %12.4f\n",
               benches[i].name,
               (unsigned long long)min_cyc,
               avg,
               per_op);
    }

    printf("--------------------------------------------------------------\n");
    printf("\nNotes:\n");
    printf("  - Cycles measured with RDTSC/RDTSCP + CPUID serialization\n");
    printf("  - 'Min' is the best (lowest) across %d runs\n", RUNS);
    printf("  - Cyc/Op = Min_Cycles / %llu iterations\n",
           (unsigned long long)ITERATIONS);
    printf("  - Zen 3 ADD/LEA: 1 cycle latency, 4/cycle throughput\n");

    return 0;
}
