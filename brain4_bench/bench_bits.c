/*
 * bench_bits.c - Addition using ONLY bitwise operations vs native ADD
 * Target: AMD Ryzen 7 5800X (Zen 3), Linux x86_64
 *
 * Implements a full adder at the bit level and benchmarks it against
 * the native ADD instruction to quantify the cost difference.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------- RDTSC helpers --------------- */

static inline uint64_t rdtsc_start(void) {
    uint32_t lo, hi;
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

#define ITERATIONS   50000000ULL    /* 50 million */
#define WARMUP       5000000ULL
#define RUNS         5

/* ===============================================================
 * Method 1: Iterative ripple-carry (XOR + AND + SHIFT loop)
 * Average case ~3-6 iterations for random 64-bit numbers
 * Best case (no carry) = 1 iteration
 * Worst case (all carries) = 64 iterations
 * =============================================================== */
static inline uint64_t add_ripple_carry(uint64_t a, uint64_t b) {
    uint64_t carry;
    while (b != 0) {
        carry = a & b;       /* carry bits */
        a     = a ^ b;       /* sum without carry */
        b     = carry << 1;  /* carry shifted left */
    }
    return a;
}

/* ===============================================================
 * Method 2: Unrolled carry-lookahead style (parallel prefix)
 * Process carries in O(log2(64)) = 6 steps instead of up to 64
 * =============================================================== */
static inline uint64_t add_parallel_prefix(uint64_t a, uint64_t b) {
    uint64_t p = a ^ b;    /* propagate */
    uint64_t g = a & b;    /* generate  */

    /* Kogge-Stone parallel prefix - 6 stages for 64 bits */
    g = g | (p & (g << 1));
    p = p & (p << 1);

    g = g | (p & (g << 2));
    p = p & (p << 2);

    g = g | (p & (g << 4));
    p = p & (p << 4);

    g = g | (p & (g << 8));
    p = p & (p << 8);

    g = g | (p & (g << 16));
    p = p & (p << 16);

    g = g | (p & (g << 32));
    /* p not needed after last stage */

    /* carry_in[i] = g[i-1], sum[i] = a[i] ^ b[i] ^ carry_in[i] */
    return (a ^ b) ^ (g << 1);
}

/* ===============================================================
 * Method 3: Bit-by-bit full adder (pedagogical, very slow)
 * Processes one bit at a time, like a hardware ripple-carry adder
 * =============================================================== */
static inline uint64_t add_bitserial(uint64_t a, uint64_t b) {
    uint64_t result = 0;
    uint64_t carry = 0;

    for (int i = 0; i < 64; i++) {
        uint64_t bit_a = (a >> i) & 1;
        uint64_t bit_b = (b >> i) & 1;

        /* Full adder: sum = a ^ b ^ carry, carry_out = (a & b) | (carry & (a ^ b)) */
        uint64_t sum = bit_a ^ bit_b ^ carry;
        carry = (bit_a & bit_b) | (carry & (bit_a ^ bit_b));

        result = result | (sum << i);
    }
    return result;
}

/* ===============================================================
 * Method 4: Native ADD (baseline reference)
 * =============================================================== */
static inline uint64_t add_native(uint64_t a, uint64_t b) {
    uint64_t result;
    __asm__ __volatile__("add %2, %0" : "=r"(result) : "0"(a), "r"(b));
    return result;
}

/* --------------- Correctness check --------------- */

static int verify_all(void) {
    uint64_t test_pairs[][2] = {
        {0, 0}, {1, 1}, {0xFFFFFFFF, 1}, {0x8000000000000000ULL, 1},
        {0xFFFFFFFFFFFFFFFFULL, 0}, {123456789, 987654321},
        {0xDEADBEEF, 0xCAFEBABE}, {0x5555555555555555ULL, 0xAAAAAAAAAAAAAAAAULL},
    };
    int n = sizeof(test_pairs) / sizeof(test_pairs[0]);

    for (int i = 0; i < n; i++) {
        uint64_t a = test_pairs[i][0], b = test_pairs[i][1];
        uint64_t expected = a + b;

        if (add_ripple_carry(a, b) != expected) {
            printf("FAIL: ripple_carry(%llu, %llu)\n", (unsigned long long)a, (unsigned long long)b);
            return 0;
        }
        if (add_parallel_prefix(a, b) != expected) {
            printf("FAIL: parallel_prefix(%llu, %llu)\n", (unsigned long long)a, (unsigned long long)b);
            return 0;
        }
        if (add_bitserial(a, b) != expected) {
            printf("FAIL: bitserial(%llu, %llu)\n", (unsigned long long)a, (unsigned long long)b);
            return 0;
        }
    }
    return 1;
}

/* --------------- Benchmark harness --------------- */

typedef uint64_t (*add_fn)(uint64_t, uint64_t);

static uint64_t run_bench(add_fn fn, uint64_t a_init, uint64_t b_val) {
    uint64_t a = a_init;

    /* warmup */
    for (uint64_t i = 0; i < WARMUP; i++) {
        a = fn(a, b_val);
    }

    uint64_t start = rdtsc_start();
    for (uint64_t i = 0; i < ITERATIONS; i++) {
        a = fn(a, b_val);
    }
    uint64_t end = rdtsc_end();

    volatile uint64_t sink = a;
    (void)sink;
    return end - start;
}

int main(void) {
    printf("=============================================================\n");
    printf("  Bitwise Addition vs Native ADD Benchmark\n");
    printf("  Iterations: %llu   Runs: %d (taking minimum)\n",
           (unsigned long long)ITERATIONS, RUNS);
    printf("=============================================================\n\n");

    /* Verify correctness first */
    printf("Correctness check: ");
    if (!verify_all()) {
        printf("FAILED - aborting\n");
        return 1;
    }
    printf("PASSED\n\n");

    struct {
        const char *name;
        add_fn fn;
    } methods[] = {
        {"Native ADD         ", add_native},
        {"Ripple-carry (loop)", add_ripple_carry},
        {"Parallel prefix    ", add_parallel_prefix},
        {"Bit-serial (1-bit) ", add_bitserial},
    };
    int n = sizeof(methods) / sizeof(methods[0]);

    printf("%-22s %12s %12s %12s %10s\n",
           "Method", "Min Cycles", "Avg Cycles", "Cyc/Op", "Slowdown");
    printf("----------------------------------------------------------------------\n");

    double native_per_op = 0;

    for (int i = 0; i < n; i++) {
        uint64_t min_cyc = UINT64_MAX;
        uint64_t total = 0;

        for (int r = 0; r < RUNS; r++) {
            uint64_t cyc = run_bench(methods[i].fn, 1, 2);
            if (cyc < min_cyc) min_cyc = cyc;
            total += cyc;
        }

        double avg = (double)total / RUNS;
        double per_op = (double)min_cyc / ITERATIONS;

        if (i == 0) native_per_op = per_op;

        double slowdown = (native_per_op > 0) ? per_op / native_per_op : 0;

        printf("%-22s %12llu %12.0f %12.4f %9.1fx\n",
               methods[i].name,
               (unsigned long long)min_cyc,
               avg,
               per_op,
               slowdown);
    }

    printf("----------------------------------------------------------------------\n");
    printf("\nAnalysis:\n");
    printf("  - Native ADD: 1 uop, 1 cycle latency on Zen 3\n");
    printf("  - Ripple-carry: ~3-6 loop iterations per add (XOR+AND+SHL per iter)\n");
    printf("  - Parallel prefix: 6 stages x (XOR+AND+SHL) = ~18 ops, no branch\n");
    printf("  - Bit-serial: 64 iterations x 6 ops each = ~384 ops per add\n");
    printf("  - The hardware adder does in 1 cycle what software needs hundreds\n");
    printf("    of cycles to emulate — the ALU's carry-lookahead circuit is\n");
    printf("    the reason addition is so fast.\n");

    return 0;
}
