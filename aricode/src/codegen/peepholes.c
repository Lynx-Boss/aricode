/*
 * aricode - Code Generator: Peephole catalog (implementation)
 * =============================================================
 * See peepholes.h for contracts.
 */

#include "peepholes.h"
#include "codegen.h"
#include "x86_64.h"

#include <string.h>

int peephole_drop_movq_rax_xmm0(CodegenState *cg) {
    if (cg->code_size < 5) return 0;
    const uint8_t *p = cg->code + cg->code_size - 5;
    if (p[0] == 0x66 && p[1] == 0x48 && p[2] == 0x0F &&
        p[3] == 0x7E && p[4] == 0xC0) {
        cg->code_size -= 5;
        return 1;
    }
    return 0;
}

size_t cg_emit_cond_jump_skip(CodegenState *cg) {
    if (cg->code_size >= 7) {
        const uint8_t *p = cg->code + cg->code_size - 7;
        /* setCC al: 0F 9X C0    (X = condition code) */
        /* movzx rax, al: 48 0F B6 C0 */
        if (p[0] == 0x0F && (p[1] & 0xF0) == 0x90 && p[2] == 0xC0 &&
            p[3] == 0x48 && p[4] == 0x0F && p[5] == 0xB6 && p[6] == 0xC0) {
            uint8_t cc_true = p[1] & 0x0F;            /* the condition tested */
            cg->code_size -= 7;                        /* rewind setCC + movzx */
            /* JCC near with the INVERTED condition (skip when false).
             * Low bit of the condition code is the negation, so XOR 1. */
            uint8_t *b = BUF(cg);
            b[0] = 0x0F;
            b[1] = (uint8_t)(0x80 | (cc_true ^ 0x01));
            memset(b + 2, 0, 4);                       /* rel32 placeholder */
            size_t pos = cg->code_size;
            EMIT(cg, 6);
            return pos;
        }
    }
    /* Cold path: plain `cmp rax, 0; je rel32`. */
    int n = emit_cmp_reg_imm(BUF(cg), REG_RAX, 0); EMIT(cg, n);
    size_t pos = cg->code_size;
    n = emit_je(BUF(cg), 0); EMIT(cg, n);
    return pos;
}
