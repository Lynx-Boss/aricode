#define _POSIX_C_SOURCE 200809L

/*
 * cycle_estimator.c - Estimate CPU cycles for x86_64 functions
 *
 * Takes a binary and function name, disassembles via objdump, and estimates
 * execution cycles based on AMD Zen 3 (Ryzen 5800X) latency/throughput tables.
 *
 * Build: gcc -O2 -o cycle_estimator cycle_estimator.c
 * Usage: ./cycle_estimator <binary> [function_name]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_LINE     1024
#define MAX_MNEM     64
#define MAX_OPERANDS 256
#define MAX_INSNS    65536

/* Instruction categories for bottleneck analysis */
typedef enum {
    CAT_ALU,
    CAT_MUL,
    CAT_DIV,
    CAT_MOVE,
    CAT_MOVE_MEM,
    CAT_BRANCH,
    CAT_STACK,
    CAT_SYSCALL,
    CAT_LEA,
    CAT_CMP,
    CAT_NOP,
    CAT_DEP_BREAK,
    CAT_UNKNOWN
} insn_category_t;

typedef struct {
    char mnemonic[MAX_MNEM];
    char operands[MAX_OPERANDS];
    double latency;
    double throughput;
    insn_category_t category;
} insn_info_t;

static insn_info_t instructions[MAX_INSNS];
static int insn_count = 0;

/* Check if operands reference memory (contain brackets or parens) */
static bool has_memory_operand(const char *operands) {
    return strchr(operands, '(') != NULL || strchr(operands, '[') != NULL;
}

/* Check if both operands are the same register (e.g., xor eax,eax) */
static bool is_same_reg_operands(const char *operands) {
    /* AT&T syntax: %reg,%reg */
    const char *comma = strchr(operands, ',');
    if (!comma) return false;

    /* Get src (before comma) and dst (after comma) */
    char src[64] = {0}, dst[64] = {0};
    int slen = (int)(comma - operands);
    if (slen >= 64) slen = 63;
    strncpy(src, operands, slen);
    src[slen] = '\0';

    const char *d = comma + 1;
    while (*d == ' ') d++;
    strncpy(dst, d, 63);
    dst[63] = '\0';

    /* Trim trailing whitespace */
    char *end = dst + strlen(dst) - 1;
    while (end > dst && isspace(*end)) *end-- = '\0';
    end = src + strlen(src) - 1;
    while (end > src && isspace(*end)) *end-- = '\0';

    return strcmp(src, dst) == 0 && strlen(src) > 0;
}

/* Classify an instruction and assign latency/throughput estimates (Zen 3) */
static void classify_instruction(insn_info_t *info) {
    const char *m = info->mnemonic;
    const char *ops = info->operands;

    /* NOP */
    if (strcmp(m, "nop") == 0 || strcmp(m, "nopw") == 0 ||
        strcmp(m, "nopl") == 0 || strcmp(m, "nopq") == 0 ||
        strncmp(m, "data16", 6) == 0) {
        info->latency = 0;
        info->throughput = 0;
        info->category = CAT_NOP;
        return;
    }

    /* XOR/SUB with same register = dependency breaker */
    if ((strcmp(m, "xor") == 0 || strcmp(m, "xorl") == 0 ||
         strcmp(m, "xorq") == 0 || strcmp(m, "xorw") == 0 ||
         strcmp(m, "sub") == 0 || strcmp(m, "subl") == 0 ||
         strcmp(m, "subq") == 0) && is_same_reg_operands(ops)) {
        info->latency = 0;
        info->throughput = 0;
        info->category = CAT_DEP_BREAK;
        return;
    }

    /* SYSCALL */
    if (strcmp(m, "syscall") == 0) {
        info->latency = 100;
        info->throughput = 100;
        info->category = CAT_SYSCALL;
        return;
    }

    /* DIV / IDIV */
    if (strcmp(m, "div") == 0 || strcmp(m, "divl") == 0 ||
        strcmp(m, "divq") == 0 || strcmp(m, "divw") == 0 ||
        strcmp(m, "divb") == 0 ||
        strcmp(m, "idiv") == 0 || strcmp(m, "idivl") == 0 ||
        strcmp(m, "idivq") == 0 || strcmp(m, "idivw") == 0 ||
        strcmp(m, "idivb") == 0) {
        /* Depends on operand size - use average */
        if (strstr(m, "q")) {
            info->latency = 41;
            info->throughput = 41;
        } else if (strstr(m, "l") || strlen(m) <= 4) {
            info->latency = 25;
            info->throughput = 25;
        } else {
            info->latency = 8;
            info->throughput = 8;
        }
        info->category = CAT_DIV;
        return;
    }

    /* MUL / IMUL */
    if (strcmp(m, "mul") == 0 || strcmp(m, "mull") == 0 ||
        strcmp(m, "mulq") == 0 || strcmp(m, "mulw") == 0 ||
        strcmp(m, "imul") == 0 || strcmp(m, "imull") == 0 ||
        strcmp(m, "imulq") == 0 || strcmp(m, "imulw") == 0) {
        info->latency = 3;
        info->throughput = 1;
        info->category = CAT_MUL;
        return;
    }

    /* LEA */
    if (strcmp(m, "lea") == 0 || strcmp(m, "leaq") == 0 ||
        strcmp(m, "leal") == 0 || strcmp(m, "leaw") == 0) {
        info->latency = 1;
        info->throughput = 0.25;
        info->category = CAT_LEA;
        return;
    }

    /* ADD / SUB / INC / DEC / NEG / ADC / SBB */
    if (strncmp(m, "add", 3) == 0 || strncmp(m, "sub", 3) == 0 ||
        strncmp(m, "inc", 3) == 0 || strncmp(m, "dec", 3) == 0 ||
        strncmp(m, "neg", 3) == 0 || strncmp(m, "adc", 3) == 0 ||
        strncmp(m, "sbb", 3) == 0) {
        if (has_memory_operand(ops)) {
            info->latency = 5;  /* 1 ALU + 4 mem */
            info->throughput = 1;
        } else {
            info->latency = 1;
            info->throughput = 0.25;
        }
        info->category = CAT_ALU;
        return;
    }

    /* Logic: AND, OR, XOR, NOT, SHL, SHR, SAR, ROL, ROR, TEST */
    if (strncmp(m, "and", 3) == 0 || strncmp(m, "or", 2) == 0 ||
        strncmp(m, "xor", 3) == 0 || strncmp(m, "not", 3) == 0 ||
        strncmp(m, "shl", 3) == 0 || strncmp(m, "shr", 3) == 0 ||
        strncmp(m, "sar", 3) == 0 || strncmp(m, "sal", 3) == 0 ||
        strncmp(m, "rol", 3) == 0 || strncmp(m, "ror", 3) == 0 ||
        strncmp(m, "test", 4) == 0) {
        if (has_memory_operand(ops)) {
            info->latency = 5;
            info->throughput = 1;
        } else {
            info->latency = 1;
            info->throughput = 0.25;
        }
        info->category = CAT_ALU;
        return;
    }

    /* CMP */
    if (strncmp(m, "cmp", 3) == 0) {
        info->latency = 1;
        info->throughput = 0.25;
        info->category = CAT_CMP;
        return;
    }

    /* MOV variants */
    if (strncmp(m, "mov", 3) == 0 || strncmp(m, "cmov", 4) == 0 ||
        strcmp(m, "xchg") == 0 || strncmp(m, "xchg", 4) == 0 ||
        strncmp(m, "movzx", 5) == 0 || strncmp(m, "movsx", 5) == 0 ||
        strncmp(m, "movzb", 5) == 0 || strncmp(m, "movsb", 5) == 0 ||
        strncmp(m, "movsl", 5) == 0 || strncmp(m, "movabs", 6) == 0) {
        if (has_memory_operand(ops)) {
            info->latency = 4;   /* L1 hit */
            info->throughput = 0.5;
            info->category = CAT_MOVE_MEM;
        } else {
            info->latency = 0;   /* register renaming */
            info->throughput = 0;
            info->category = CAT_MOVE;
        }
        return;
    }

    /* PUSH / POP */
    if (strncmp(m, "push", 4) == 0 || strncmp(m, "pop", 3) == 0) {
        info->latency = 1;
        info->throughput = 0.5;
        info->category = CAT_STACK;
        return;
    }

    /* JMP, Jcc, CALL, RET */
    if (m[0] == 'j' || strcmp(m, "call") == 0 || strcmp(m, "callq") == 0 ||
        strcmp(m, "ret") == 0 || strcmp(m, "retq") == 0 ||
        strncmp(m, "loop", 4) == 0) {
        info->latency = 1;
        info->throughput = 0.5;
        info->category = CAT_BRANCH;
        return;
    }

    /* INT */
    if (strcmp(m, "int") == 0) {
        info->latency = 100;
        info->throughput = 100;
        info->category = CAT_SYSCALL;
        return;
    }

    /* ENDBR64 / ENDBR32 (CET) */
    if (strncmp(m, "endbr", 5) == 0) {
        info->latency = 0;
        info->throughput = 0;
        info->category = CAT_NOP;
        return;
    }

    /* Default: unknown instruction */
    info->latency = 1;
    info->throughput = 1;
    info->category = CAT_UNKNOWN;
}

static const char *category_name(insn_category_t cat) {
    switch (cat) {
        case CAT_ALU:       return "ALU";
        case CAT_MUL:       return "Multiply";
        case CAT_DIV:       return "Divide";
        case CAT_MOVE:      return "Reg Move";
        case CAT_MOVE_MEM:  return "Mem Move";
        case CAT_BRANCH:    return "Branch";
        case CAT_STACK:     return "Stack";
        case CAT_SYSCALL:   return "Syscall";
        case CAT_LEA:       return "LEA";
        case CAT_CMP:       return "Compare";
        case CAT_NOP:       return "NOP";
        case CAT_DEP_BREAK: return "Dep Break";
        case CAT_UNKNOWN:   return "Unknown";
    }
    return "?";
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <binary> [function_name]\n", argv[0]);
        fprintf(stderr, "\nEstimates CPU cycles for x86_64 functions based on Zen 3 timings.\n");
        return 1;
    }

    const char *binary = argv[1];
    const char *func = argc > 2 ? argv[2] : "main";

    /* Build objdump command */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "objdump -d '%s' 2>/dev/null", binary);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        fprintf(stderr, "Error: failed to run objdump on '%s'\n", binary);
        return 1;
    }

    /* Parse objdump output - find target function */
    char line[MAX_LINE];
    bool in_function = false;
    char func_pattern[128];
    snprintf(func_pattern, sizeof(func_pattern), "<%s>:", func);
    char func_pattern2[128];
    snprintf(func_pattern2, sizeof(func_pattern2), "<%s()>:", func);

    insn_count = 0;

    while (fgets(line, sizeof(line), pipe)) {
        /* Check for function start */
        if (strstr(line, func_pattern) || strstr(line, func_pattern2)) {
            in_function = true;
            continue;
        }

        /* Check for function end (blank line after we started) */
        if (in_function) {
            /* Blank line or next function = end */
            if (line[0] == '\n' || line[0] == '\r') {
                break;
            }

            /* Parse instruction line: "  addr: hex_bytes  mnemonic operands" */
            char *colon = strchr(line, ':');
            if (!colon) continue;

            /* Skip the hex bytes to find the mnemonic */
            char *p = colon + 1;
            /* Skip hex bytes (tab-separated from mnemonic in objdump) */
            char *tab = strchr(p, '\t');
            if (!tab) continue;
            tab++;
            /* There might be another tab separating hex from mnemonic */
            char *tab2 = strchr(tab, '\t');
            if (tab2) tab = tab2 + 1;

            /* Now tab points to "mnemonic operands\n" */
            while (*tab == ' ' || *tab == '\t') tab++;

            if (insn_count >= MAX_INSNS) break;

            insn_info_t *info = &instructions[insn_count];
            memset(info, 0, sizeof(*info));

            /* Extract mnemonic */
            int mi = 0;
            char *s = tab;
            while (*s && !isspace(*s) && mi < MAX_MNEM - 1) {
                info->mnemonic[mi++] = *s++;
            }
            info->mnemonic[mi] = '\0';

            /* Skip whitespace, extract operands */
            while (*s && isspace(*s)) s++;
            int oi = 0;
            while (*s && *s != '\n' && *s != '#' && oi < MAX_OPERANDS - 1) {
                info->operands[oi++] = *s++;
            }
            info->operands[oi] = '\0';
            /* Trim trailing spaces */
            while (oi > 0 && isspace(info->operands[oi-1])) {
                info->operands[--oi] = '\0';
            }

            if (strlen(info->mnemonic) > 0) {
                classify_instruction(info);
                insn_count++;
            }
        }
    }
    pclose(pipe);

    if (insn_count == 0) {
        fprintf(stderr, "Error: function '%s' not found or empty in '%s'\n", func, binary);
        return 1;
    }

    /* === Output Report === */
    printf("================================================================================\n");
    printf("  CYCLE ESTIMATION: %s() in %s\n", func, binary);
    printf("  Microarchitecture: AMD Zen 3 (Ryzen 5800X)\n");
    printf("================================================================================\n\n");

    /* Per-instruction breakdown */
    printf("--- INSTRUCTION-BY-INSTRUCTION BREAKDOWN ---\n\n");
    printf("  %-4s  %-12s  %-30s  %-8s  %-8s  %s\n",
           "#", "Mnemonic", "Operands", "Latency", "Thruput", "Category");
    printf("  %-4s  %-12s  %-30s  %-8s  %-8s  %s\n",
           "----", "------------", "------------------------------",
           "--------", "--------", "--------");

    double total_latency = 0;
    double total_throughput = 0;
    int cat_counts[13] = {0};
    double cat_cycles[13] = {0};

    for (int i = 0; i < insn_count; i++) {
        insn_info_t *info = &instructions[i];
        char ops_short[31];
        strncpy(ops_short, info->operands, 30);
        ops_short[30] = '\0';

        printf("  %-4d  %-12s  %-30s  %-8.1f  %-8.2f  %s\n",
               i + 1,
               info->mnemonic,
               ops_short,
               info->latency,
               info->throughput,
               category_name(info->category));

        total_latency += info->latency;
        total_throughput += info->throughput;
        cat_counts[info->category]++;
        cat_cycles[info->category] += info->latency;
    }

    printf("\n");

    /* Summary */
    printf("--- SUMMARY ---\n\n");
    printf("  Total instructions:       %d\n", insn_count);
    printf("  Total latency (serial):   %.0f cycles\n", total_latency);
    printf("  Total throughput cost:     %.1f cycles\n", total_throughput);
    printf("  Estimated execution:      %.0f - %.0f cycles\n",
           total_throughput, total_latency);
    printf("  IPC estimate:             %.2f\n",
           total_throughput > 0 ? insn_count / total_throughput : 0);
    printf("\n");

    /* Category breakdown */
    printf("--- CATEGORY BREAKDOWN ---\n\n");
    printf("  %-14s  %5s  %8s  %8s\n", "Category", "Count", "Cycles", "%%Total");
    printf("  %-14s  %5s  %8s  %8s\n", "--------------", "-----", "--------", "--------");

    for (int c = 0; c < 13; c++) {
        if (cat_counts[c] > 0) {
            double pct = total_latency > 0 ? (cat_cycles[c] / total_latency) * 100 : 0;
            printf("  %-14s  %5d  %8.0f  %7.1f%%\n",
                   category_name((insn_category_t)c),
                   cat_counts[c],
                   cat_cycles[c],
                   pct);
        }
    }
    printf("\n");

    /* Bottleneck identification */
    printf("--- BOTTLENECK ANALYSIS ---\n\n");

    /* Find the biggest cycle consumer */
    double max_cat_cycles = 0;
    int bottleneck_cat = -1;
    for (int c = 0; c < 13; c++) {
        if (cat_cycles[c] > max_cat_cycles) {
            max_cat_cycles = cat_cycles[c];
            bottleneck_cat = c;
        }
    }

    if (bottleneck_cat >= 0) {
        printf("  Primary bottleneck: %s (%.0f cycles, %.1f%% of total)\n",
               category_name((insn_category_t)bottleneck_cat),
               max_cat_cycles,
               total_latency > 0 ? (max_cat_cycles / total_latency) * 100 : 0);
    }

    /* Specific warnings */
    if (cat_counts[CAT_DIV] > 0) {
        printf("  WARNING: %d division instruction(s) detected - very expensive!\n",
               cat_counts[CAT_DIV]);
        printf("           Consider replacing with multiplication by reciprocal or shifts.\n");
    }
    if (cat_counts[CAT_SYSCALL] > 0) {
        printf("  WARNING: %d syscall(s) detected - each costs ~100 cycles.\n",
               cat_counts[CAT_SYSCALL]);
        printf("           Consider batching I/O operations.\n");
    }
    if (cat_counts[CAT_MOVE_MEM] > (insn_count / 2)) {
        printf("  WARNING: Memory-bound code - %d/%d instructions access memory.\n",
               cat_counts[CAT_MOVE_MEM], insn_count);
        printf("           Consider keeping more values in registers.\n");
    }
    if (cat_counts[CAT_NOP] > 0) {
        printf("  NOTE: %d NOP/padding instructions (zero-cost but waste I-cache).\n",
               cat_counts[CAT_NOP]);
    }
    if (cat_counts[CAT_DEP_BREAK] > 0) {
        printf("  GOOD: %d dependency-breaking instruction(s) detected (zero-cost).\n",
               cat_counts[CAT_DEP_BREAK]);
    }

    printf("\n");
    printf("================================================================================\n");
    printf("  NOTE: These are estimates. Actual performance depends on:\n");
    printf("  - Branch prediction accuracy\n");
    printf("  - Cache hit rates (L1/L2/L3)\n");
    printf("  - Instruction-level parallelism\n");
    printf("  - Memory access patterns\n");
    printf("  - Micro-op fusion opportunities\n");
    printf("================================================================================\n");

    return 0;
}
