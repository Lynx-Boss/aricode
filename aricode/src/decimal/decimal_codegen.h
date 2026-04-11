/*
 * aricode - Ari Code Language
 * Decimal Code Generation - x86_64 Machine Code for BCD Operations
 *
 * Since arbitrary precision decimals cannot use hardware FPU instructions
 * (they fail at precision), the codegen emits CALL instructions to runtime
 * functions embedded in the generated binary.
 *
 * Strategy:
 *   1. Decimal values are 24 bytes on the stack: hi(8) + lo(8) + exp|sign(8)
 *   2. Arithmetic operations call runtime functions (dec_bcd_add, etc.)
 *   3. The runtime is compiled once and its machine code is embedded in
 *      every aricode binary that uses decimals
 *   4. Each emit_decimal_* function generates x86_64 instructions that:
 *      - Set up arguments per System V ABI (pointers in RDI, RSI, RDX)
 *      - CALL the runtime function
 *      - Store the result
 *
 * Stack layout for a decimal local variable at [RBP + offset]:
 *   [RBP + offset +  0]  = hi   (uint64_t)
 *   [RBP + offset +  8]  = lo   (uint64_t)
 *   [RBP + offset + 16]  = exp  (int32_t) | sign (int32_t)
 *
 * The codegen state tracks which runtime functions are needed and
 * appends their machine code to the output buffer after all user
 * functions are emitted.
 */

#ifndef ARI_DECIMAL_CODEGEN_H
#define ARI_DECIMAL_CODEGEN_H

#include "../codegen/codegen.h"
#include "../codegen/x86_64.h"
#include "decimal_runtime.h"

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                         */
/* ------------------------------------------------------------------ */

/* Size of a DecBCD on the stack (padded to 8-byte alignment) */
#define DEC_STACK_SIZE  24  /* 8 (hi) + 8 (lo) + 4 (exp) + 4 (sign) */

/* Maximum runtime function entries */
#define DEC_MAX_RUNTIME_FUNCS  8

/* ------------------------------------------------------------------ */
/*  Runtime function table                                            */
/* ------------------------------------------------------------------ */

/*
 * Each runtime function that gets embedded in the binary is tracked here.
 * The codegen records the offset where each runtime function is placed
 * in the code buffer, so CALL instructions can be patched.
 */
typedef enum {
    DEC_RT_ADD = 0,
    DEC_RT_SUB,
    DEC_RT_MUL,
    DEC_RT_DIV,
    DEC_RT_CMP,
    DEC_RT_FROM_LITERAL,
    DEC_RT_COUNT
} DecRuntimeFunc;

typedef struct {
    size_t   offset;    /* byte offset in code buffer (-1 = not emitted) */
    int      emitted;   /* has this function been emitted?               */
} DecRuntimeEntry;

/*
 * Decimal codegen state. Extends the main CodegenState.
 */
typedef struct {
    /* Which runtime functions have been used (and need embedding) */
    int               needs_runtime[DEC_RT_COUNT];

    /* Offsets of embedded runtime functions in the code buffer */
    DecRuntimeEntry   runtime[DEC_RT_COUNT];

    /* Patch list for decimal runtime CALLs */
    struct {
        size_t        code_pos;    /* position of rel32 in code buffer */
        DecRuntimeFunc func;       /* which runtime function */
    } dec_patches[256];
    size_t            dec_patch_count;

    /* Data section: string literals for decimal constants */
    struct {
        size_t        data_offset; /* offset in a data buffer */
        const char   *literal;     /* the decimal string literal */
    } literals[256];
    size_t            literal_count;
} DecCodegenState;

/* ------------------------------------------------------------------ */
/*  Initialization                                                    */
/* ------------------------------------------------------------------ */

/*
 * Initialize the decimal codegen state. Call once before generating.
 */
void dec_codegen_init(DecCodegenState *dcs);

/* ------------------------------------------------------------------ */
/*  Code emission functions                                           */
/* ------------------------------------------------------------------ */

/*
 * emit_decimal_add - Emit x86_64 code for decimal addition.
 *
 * Emits a CALL to the embedded dec_bcd_add runtime function.
 * Arguments are pointers to DecBCD structs on the stack.
 *
 * Parameters:
 *   cg         - main codegen state (code buffer)
 *   dcs        - decimal codegen state (runtime tracking)
 *   result_off - stack offset (from RBP) for result DecBCD
 *   a_off      - stack offset for operand a
 *   b_off      - stack offset for operand b
 *
 * Generated code:
 *   lea rdi, [rbp + result_off]   ; pointer to result
 *   lea rsi, [rbp + a_off]        ; pointer to a
 *   lea rdx, [rbp + b_off]        ; pointer to b
 *   call dec_bcd_add_rt           ; runtime function
 */
void emit_decimal_add(CodegenState *cg, DecCodegenState *dcs,
                      int32_t result_off, int32_t a_off, int32_t b_off);

/*
 * emit_decimal_sub - Emit x86_64 code for decimal subtraction.
 * Same calling convention as emit_decimal_add.
 */
void emit_decimal_sub(CodegenState *cg, DecCodegenState *dcs,
                      int32_t result_off, int32_t a_off, int32_t b_off);

/*
 * emit_decimal_mul - Emit x86_64 code for decimal multiplication.
 */
void emit_decimal_mul(CodegenState *cg, DecCodegenState *dcs,
                      int32_t result_off, int32_t a_off, int32_t b_off);

/*
 * emit_decimal_div - Emit x86_64 code for decimal division.
 * Uses default precision (20 digits).
 */
void emit_decimal_div(CodegenState *cg, DecCodegenState *dcs,
                      int32_t result_off, int32_t a_off, int32_t b_off);

/*
 * emit_decimal_cmp - Emit x86_64 code for decimal comparison.
 *
 * After this instruction sequence, the CPU flags are set:
 *   ZF=1 if a == b
 *   SF=1 if a < b  (result in RAX: -1, 0, or 1)
 *
 * Parameters:
 *   a_off - stack offset for operand a
 *   b_off - stack offset for operand b
 *
 * Generated code:
 *   lea rdi, [rbp + a_off]
 *   lea rsi, [rbp + b_off]
 *   call dec_bcd_cmp_rt
 *   test eax, eax              ; sets flags based on result
 */
void emit_decimal_cmp(CodegenState *cg, DecCodegenState *dcs,
                      int32_t a_off, int32_t b_off);

/*
 * emit_decimal_from_literal - Load a decimal constant from a string.
 *
 * Emits code that calls dec_bcd_from_string with a pointer to the
 * string literal embedded in the binary's data section.
 *
 * Parameters:
 *   dest_off - stack offset for the destination DecBCD
 *   literal  - the decimal string (e.g., "0.1", "3.14")
 *
 * Generated code:
 *   lea rdi, [rip + literal_data]   ; pointer to string in .rodata
 *   call dec_bcd_from_string_rt
 *   ; copy result struct to [rbp + dest_off]
 */
void emit_decimal_from_literal(CodegenState *cg, DecCodegenState *dcs,
                               int32_t dest_off, const char *literal);

/* ------------------------------------------------------------------ */
/*  Runtime embedding                                                 */
/* ------------------------------------------------------------------ */

/*
 * Emit all needed runtime functions into the code buffer.
 * Call this after all user code has been generated but before
 * the _start stub.
 *
 * This embeds the compiled machine code of the runtime functions
 * that were actually used (tracked via dcs->needs_runtime[]).
 *
 * After embedding, patches all decimal CALL sites to point to
 * the correct offsets.
 */
void dec_codegen_emit_runtime(CodegenState *cg, DecCodegenState *dcs);

/*
 * Patch all decimal CALL rel32 targets.
 * Called automatically by dec_codegen_emit_runtime, but can be
 * called separately if needed.
 */
void dec_codegen_patch_calls(CodegenState *cg, DecCodegenState *dcs);

/* ------------------------------------------------------------------ */
/*  Helper: allocate a decimal local                                  */
/* ------------------------------------------------------------------ */

/*
 * Allocate stack space for a decimal local variable.
 * Returns the RBP offset for the new DecBCD slot.
 * Adjusts cg->stack_offset by DEC_STACK_SIZE.
 */
static inline int32_t dec_alloc_local(CodegenState *cg) {
    cg->stack_offset -= DEC_STACK_SIZE;
    return cg->stack_offset;
}

#endif /* ARI_DECIMAL_CODEGEN_H */
