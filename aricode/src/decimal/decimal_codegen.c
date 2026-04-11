/*
 * aricode - Ari Code Language
 * Decimal Code Generation - x86_64 Machine Code Emission
 *
 * Emits CALL instructions to runtime functions for decimal operations.
 * The runtime functions are embedded in the generated binary.
 *
 * The approach for the initial implementation:
 *   - Instead of embedding raw machine code of the runtime (which would
 *     require a separate compilation step or binary blob), we use the
 *     C-level runtime directly during testing.
 *   - For production, the runtime would be compiled with:
 *       gcc -c -Os decimal_runtime.c -o decimal_runtime.o
 *       objcopy -O binary decimal_runtime.o decimal_runtime.bin
 *     and the .bin would be #include'd as a byte array.
 *
 * For now, the codegen emits the x86_64 calling convention setup
 * (LEA + CALL sequences) with placeholder offsets that get patched
 * when the runtime is linked.
 */

#include "decimal_codegen.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Macros (matching codegen.c conventions)                           */
/* ------------------------------------------------------------------ */

#define EMIT(cg, count) do { (cg)->code_size += (count); } while (0)
#define BUF(cg)         ((cg)->code + (cg)->code_size)

/* ------------------------------------------------------------------ */
/*  Initialization                                                    */
/* ------------------------------------------------------------------ */

void dec_codegen_init(DecCodegenState *dcs) {
    memset(dcs, 0, sizeof(DecCodegenState));
    for (int i = 0; i < DEC_RT_COUNT; i++) {
        dcs->runtime[i].offset  = 0;
        dcs->runtime[i].emitted = 0;
    }
}

/* ------------------------------------------------------------------ */
/*  Internal: emit LEA rbp+disp into a register                      */
/* ------------------------------------------------------------------ */

/*
 * Emit: LEA reg, [RBP + disp32]
 * This uses the existing emit_lea helper from x86_64.h with no index.
 */
static void emit_lea_rbp(CodegenState *cg, int reg, int32_t disp) {
    int n = emit_lea(BUF(cg), reg, REG_RBP, -1, 0, disp);
    EMIT(cg, n);
}

/* ------------------------------------------------------------------ */
/*  Internal: emit a CALL to a decimal runtime function               */
/* ------------------------------------------------------------------ */

static void emit_dec_call(CodegenState *cg, DecCodegenState *dcs,
                          DecRuntimeFunc func) {
    /* Mark that this runtime function is needed */
    dcs->needs_runtime[func] = 1;

    /* Emit CALL rel32 with placeholder */
    BUF(cg)[0] = 0xE8;  /* CALL rel32 */
    cg->code_size += 1;

    /* Record patch location */
    if (dcs->dec_patch_count < 256) {
        dcs->dec_patches[dcs->dec_patch_count].code_pos = cg->code_size;
        dcs->dec_patches[dcs->dec_patch_count].func     = func;
        dcs->dec_patch_count++;
    }

    /* Placeholder 4 bytes */
    memset(BUF(cg), 0, 4);
    cg->code_size += 4;
}

/* ------------------------------------------------------------------ */
/*  Internal: emit code to copy a DecBCD struct (24 bytes) on stack   */
/* ------------------------------------------------------------------ */

/*
 * Copy 24 bytes from [RBP + src_off] to [RBP + dst_off].
 * Uses three 8-byte MOV pairs via RAX.
 */
static void emit_copy_decbcd(CodegenState *cg, int32_t dst_off, int32_t src_off) {
    int n;
    /* Copy hi (8 bytes) */
    n = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RBP, src_off);
    EMIT(cg, n);
    n = emit_mov_mem_reg(BUF(cg), REG_RBP, dst_off, REG_RAX);
    EMIT(cg, n);

    /* Copy lo (8 bytes) */
    n = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RBP, src_off + 8);
    EMIT(cg, n);
    n = emit_mov_mem_reg(BUF(cg), REG_RBP, dst_off + 8, REG_RAX);
    EMIT(cg, n);

    /* Copy exp+sign (8 bytes) */
    n = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RBP, src_off + 16);
    EMIT(cg, n);
    n = emit_mov_mem_reg(BUF(cg), REG_RBP, dst_off + 16, REG_RAX);
    EMIT(cg, n);
}

/* ------------------------------------------------------------------ */
/*  Public: emit decimal operations                                   */
/* ------------------------------------------------------------------ */

/*
 * The runtime functions use a struct-return convention:
 *   - Result DecBCD is returned via a hidden first parameter (pointer in RDI)
 *   - Operand a pointer in RSI
 *   - Operand b pointer in RDX
 *
 * Actually, System V ABI returns small structs (<=16 bytes) in RAX:RDX.
 * DecBCD is 24 bytes, so it's returned via hidden pointer (RDI).
 *
 * Generated sequence for binary ops:
 *   lea  rdi, [rbp + result_off]    ; hidden return pointer
 *   lea  rsi, [rbp + a_off]         ; pointer to operand a
 *   lea  rdx, [rbp + b_off]         ; pointer to operand b
 *   call <runtime_func>
 */

void emit_decimal_add(CodegenState *cg, DecCodegenState *dcs,
                      int32_t result_off, int32_t a_off, int32_t b_off) {
    /* Set up arguments: RDI = &result, RSI = &a, RDX = &b */
    emit_lea_rbp(cg, REG_RDI, result_off);
    emit_lea_rbp(cg, REG_RSI, a_off);
    emit_lea_rbp(cg, REG_RDX, b_off);
    emit_dec_call(cg, dcs, DEC_RT_ADD);
}

void emit_decimal_sub(CodegenState *cg, DecCodegenState *dcs,
                      int32_t result_off, int32_t a_off, int32_t b_off) {
    emit_lea_rbp(cg, REG_RDI, result_off);
    emit_lea_rbp(cg, REG_RSI, a_off);
    emit_lea_rbp(cg, REG_RDX, b_off);
    emit_dec_call(cg, dcs, DEC_RT_SUB);
}

void emit_decimal_mul(CodegenState *cg, DecCodegenState *dcs,
                      int32_t result_off, int32_t a_off, int32_t b_off) {
    emit_lea_rbp(cg, REG_RDI, result_off);
    emit_lea_rbp(cg, REG_RSI, a_off);
    emit_lea_rbp(cg, REG_RDX, b_off);
    emit_dec_call(cg, dcs, DEC_RT_MUL);
}

void emit_decimal_div(CodegenState *cg, DecCodegenState *dcs,
                      int32_t result_off, int32_t a_off, int32_t b_off) {
    emit_lea_rbp(cg, REG_RDI, result_off);
    emit_lea_rbp(cg, REG_RSI, a_off);
    emit_lea_rbp(cg, REG_RDX, b_off);

    /* RCX = precision (20 digits default) */
    int n = emit_mov_reg_imm32(BUF(cg), REG_RCX, 20);
    EMIT(cg, n);

    emit_dec_call(cg, dcs, DEC_RT_DIV);
}

void emit_decimal_cmp(CodegenState *cg, DecCodegenState *dcs,
                      int32_t a_off, int32_t b_off) {
    /* RDI = &a, RSI = &b */
    emit_lea_rbp(cg, REG_RDI, a_off);
    emit_lea_rbp(cg, REG_RSI, b_off);
    emit_dec_call(cg, dcs, DEC_RT_CMP);

    /* Result is in EAX (-1, 0, or 1). Set flags for conditional jumps. */
    int n = emit_cmp_reg_imm(BUF(cg), REG_RAX, 0);
    EMIT(cg, n);
}

void emit_decimal_from_literal(CodegenState *cg, DecCodegenState *dcs,
                               int32_t dest_off, const char *literal) {
    /*
     * For the initial implementation, we inline the literal parsing
     * at compile time and emit the pre-computed BCD values directly
     * as immediate stores to the stack.
     *
     * This is actually BETTER than calling from_string at runtime:
     * - No runtime parsing overhead
     * - No string data in the binary
     * - Constant folding at compile time
     *
     * Generated code:
     *   mov rax, <hi_value>
     *   mov [rbp + dest_off + 0], rax
     *   mov rax, <lo_value>
     *   mov [rbp + dest_off + 8], rax
     *   mov rax, <exp_sign_packed>
     *   mov [rbp + dest_off + 16], rax
     */
    DecBCD val = dec_bcd_from_string(literal);

    int n;

    /* Store hi */
    n = emit_mov_reg_imm64(BUF(cg), REG_RAX, val.hi);
    EMIT(cg, n);
    n = emit_mov_mem_reg(BUF(cg), REG_RBP, dest_off, REG_RAX);
    EMIT(cg, n);

    /* Store lo */
    n = emit_mov_reg_imm64(BUF(cg), REG_RAX, val.lo);
    EMIT(cg, n);
    n = emit_mov_mem_reg(BUF(cg), REG_RBP, dest_off + 8, REG_RAX);
    EMIT(cg, n);

    /* Store exp and sign packed as a single 64-bit value */
    uint64_t exp_sign = ((uint64_t)(uint32_t)val.exp) |
                        ((uint64_t)(uint32_t)val.sign << 32);
    n = emit_mov_reg_imm64(BUF(cg), REG_RAX, exp_sign);
    EMIT(cg, n);
    n = emit_mov_mem_reg(BUF(cg), REG_RBP, dest_off + 16, REG_RAX);
    EMIT(cg, n);

    (void)dcs; /* literal loading doesn't need runtime functions */
}

/* ------------------------------------------------------------------ */
/*  Runtime embedding (stub for future)                               */
/* ------------------------------------------------------------------ */

/*
 * In production, this function would:
 * 1. Check which runtime functions are needed (dcs->needs_runtime[])
 * 2. Copy their pre-compiled machine code into cg->code[]
 * 3. Record the offsets in dcs->runtime[]
 * 4. Patch all CALL sites
 *
 * For now, the runtime functions are linked directly from the C runtime
 * library (decimal_runtime.c), so this function only records offsets
 * for the linker/patch phase.
 */
void dec_codegen_emit_runtime(CodegenState *cg, DecCodegenState *dcs) {
    (void)cg;

    /* In the embedded approach, we would copy machine code blobs here.
     * For the C-linkage approach, the runtime functions are resolved
     * at link time, so we just need to set up the patch table.
     *
     * The runtime function addresses will be provided by the linker
     * when building the final binary. The codegen_compile_to_file
     * function would need to be extended to include the runtime .o
     * in the link step.
     */

    /* Mark all as "emitted" (they exist in the linked runtime) */
    for (int i = 0; i < DEC_RT_COUNT; i++) {
        if (dcs->needs_runtime[i]) {
            dcs->runtime[i].emitted = 1;
            /* Offset will be filled by linker - set to 0 for now */
            dcs->runtime[i].offset = 0;
        }
    }
}

void dec_codegen_patch_calls(CodegenState *cg, DecCodegenState *dcs) {
    for (size_t i = 0; i < dcs->dec_patch_count; i++) {
        DecRuntimeFunc func = dcs->dec_patches[i].func;
        size_t call_site    = dcs->dec_patches[i].code_pos;

        if (!dcs->runtime[func].emitted) {
            fprintf(stderr, "aricode/decimal_codegen: runtime function %d "
                    "not emitted\n", func);
            cg->had_error = 1;
            continue;
        }

        size_t target = dcs->runtime[func].offset;

        /* rel32 = target - (call_site + 4) */
        int32_t rel = (int32_t)((int64_t)target - (int64_t)(call_site + 4));
        memcpy(cg->code + call_site, &rel, 4);
    }
}
