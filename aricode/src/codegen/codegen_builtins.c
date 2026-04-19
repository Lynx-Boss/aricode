/*
 * aricode - Ari Code Language
 * Builtin Function Code Generation
 *
 * All builtin function implementations extracted from codegen.c.
 * Each builtin is dispatched via emit_builtin() which is called
 * from emit_call_expr() in codegen.c.
 */

#include "codegen.h"
#include "codegen_builtins.h"
#include "x86_64.h"

#include <stdio.h>
#include <string.h>

/* =====================================================================
 *  SSE2 minimax polynomial helpers for sin / cos
 * =====================================================================
 *
 * The coefficients below were originally fit by the aricode build for
 * |x| <= π/4.  They deliver ~10^-11 relative accuracy in that range;
 * applied outside it the 5-term truncation error explodes (4e-4 at ±π,
 * 1.8e-3 for cos).  The shared octant reducer below (emit_octant_reduce)
 * is what keeps us inside the designed range.
 *
 * Both helpers assume: x (the reduced r, in [-π/4, π/4]) is in xmm0.
 * Both leave: sin_poly(r) or cos_poly(r) in xmm0 (RAX is NOT written
 * here — the caller takes care of that after sign-flipping).
 */
/*
 * Helper: emit a single Horner step using FMA3.
 *
 * Replaces the classical two-instruction pattern
 *    mulsd xmm_acc, xmm_y     ; 3c
 *    addsd xmm_acc, xmm_c     ; 3c  (chained = 6c)
 * with
 *    vfmadd213sd xmm_acc, xmm_y, xmm_c   ; 4c
 * saving 2 cycles per Horner step on Zen 3 (and emitting fewer bytes).
 *
 * xmm_acc must already hold the running accumulator, xmm_y the y
 * multiplier.  This helper loads the constant `coef_bits` into xmm_c
 * and fuses the `acc = acc·y + c` step.
 *
 * VEX encoding of vfmadd213sd xmm_acc, xmm_y, xmm_c :
 *   C4 E2 <W vvvv L pp> A9 <modrm>
 *     W = 1, L = 0, pp = 01        (fixed for sd)
 *     vvvv = ~xmm_y                (second source operand)
 *     modrm = 11 xmm_acc xmm_c
 */
static void emit_fma_horner_step(CodegenState *cg,
                                 int acc, int y, int c_reg,
                                 uint64_t coef_bits) {
    int pn; uint8_t *b;
    /* Load coefficient into xmm_c via RCX. */
    pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, coef_bits); EMIT(cg, pn);
    b = BUF(cg);
    b[0] = 0x66; b[1] = 0x48; b[2] = 0x0F; b[3] = 0x6E;
    b[4] = 0xC0 | ((c_reg & 7) << 3) | (REG_RCX & 7);   /* movq xmm_c, rcx */
    EMIT(cg, 5);

    /* vfmadd213sd xmm_acc, xmm_y, xmm_c  →  acc = y·acc + c */
    b = BUF(cg);
    b[0] = 0xC4;
    b[1] = 0xE2;                                         /* RXB.mmmmm = 1.1.1.00010 */
    b[2] = 0x81 | ((~y & 0xF) << 3);                     /* W=1, vvvv=~y, L=0, pp=01 */
    b[3] = 0xA9;
    b[4] = 0xC0 | ((acc & 7) << 3) | (c_reg & 7);        /* mod=11, reg=acc, r/m=c */
    EMIT(cg, 5);
}

/* Emit `dst += src1·src2` as vfmadd231sd dst, src1, src2  (231 form:
 * dst = dst + src1·src2 with single rounding).  Used for final
 * recombination where we don't want to overwrite dst's current value. */
static void emit_fma_add(CodegenState *cg, int dst, int src1, int src2) {
    uint8_t *b = BUF(cg);
    b[0] = 0xC4;
    b[1] = 0xE2;
    b[2] = 0x81 | ((~src1 & 0xF) << 3);
    b[3] = 0xB9;                                 /* 231 form opcode */
    b[4] = 0xC0 | ((dst & 7) << 3) | (src2 & 7);
    EMIT(cg, 5);
}

/* vfmadd213sd dst, src1, src2  →  dst = dst·src1 + src2.
 * Used inside Estrin's scheme to build parallel "pair = Ck·y + Ck-1"
 * accumulators from existing register values (no constant load). */
static void emit_fma213_reg(CodegenState *cg, int dst, int src1, int src2) {
    uint8_t *b = BUF(cg);
    b[0] = 0xC4;
    b[1] = 0xE2;
    b[2] = 0x81 | ((~src1 & 0xF) << 3);
    b[3] = 0xA9;                                 /* 213 form opcode */
    b[4] = 0xC0 | ((dst & 7) << 3) | (src2 & 7);
    EMIT(cg, 5);
}

/* Load a 64-bit immediate (IEEE 754 double bits) into xmm_dst via RCX.
 *   mov rcx, imm64            (10 B)
 *   movq xmm_dst, rcx         ( 5 B)
 * The current codegen has no rodata-pool infrastructure, so we stay
 * with the imm64 path — costs a few more bytes than `movsd xmm, [rip]`
 * but keeps the emitter self-contained.
 */
static void emit_load_f64(CodegenState *cg, int xmm, uint64_t bits) {
    int pn; uint8_t *b;
    pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, bits); EMIT(cg, pn);
    b = BUF(cg);
    b[0] = 0x66; b[1] = 0x48; b[2] = 0x0F; b[3] = 0x6E;
    b[4] = 0xC0 | ((xmm & 7) << 3) | (REG_RCX & 7);   /* movq xmm, rcx */
    EMIT(cg, 5);
}

/* movapd xmm_dst, xmm_src  — zero-latency register copy (renamed). */
static void emit_mov_xmm(CodegenState *cg, int dst, int src) {
    uint8_t *b = BUF(cg);
    b[0] = 0x66; b[1] = 0x0F; b[2] = 0x28;
    b[3] = 0xC0 | ((dst & 7) << 3) | (src & 7);
    EMIT(cg, 4);
}

/* mulsd xmm_dst, xmm_src  —  dst = dst · src. */
static void cg_mulsd(CodegenState *cg, int dst, int src) {
    uint8_t *b = BUF(cg);
    b[0] = 0xF2; b[1] = 0x0F; b[2] = 0x59;
    b[3] = 0xC0 | ((dst & 7) << 3) | (src & 7);
    EMIT(cg, 4);
}

/* =====================================================================
 *  Counted-loop bracket — abstracts the cmp/jae/…/add/jmp/patch idiom
 *  that every vectorised array builtin opened with.  Use by capturing
 *  the handle returned by cg_loop_begin, emitting the body, then
 *  closing with cg_loop_end (which knows whether to step by 4 for the
 *  AVX2 path or by 1 for the scalar tail).
 * ===================================================================== */

typedef struct {
    size_t top;          /* code offset of the `cmp idx, limit` */
    size_t jae_patch;    /* code offset of the 6-byte jae rel32  */
    int    idx_reg;      /* e.g. REG_RSI or R12 (low 3 bits used) */
    int    limit_is_high;/* limit register (RDX, R8..R11, RCX, …) */
    int    limit_reg;    /* full 0..15                           */
} CgCountedLoop;

/* Emit:
 *     .top:
 *        cmp <idx>, <limit>         ; 3-byte REX.W 0x39 modrm
 *        jae .done (rel32 placeholder, patched in cg_loop_end)
 * Caller emits the body between this and cg_loop_end. */
static CgCountedLoop cg_loop_begin(CodegenState *cg, int idx_reg, int limit_reg) {
    CgCountedLoop lp;
    lp.top       = cg->code_size;
    lp.idx_reg   = idx_reg;
    lp.limit_reg = limit_reg;
    lp.limit_is_high = (limit_reg >= 8);

    /* REX.W + opcode 0x39 + modrm = cmp r64, r64. */
    uint8_t rex = 0x48;
    if (idx_reg >= 8)   rex |= 0x01;   /* REX.B on r/m */
    if (limit_reg >= 8) rex |= 0x04;   /* REX.R on reg */

    uint8_t *b = BUF(cg);
    b[0] = rex;
    b[1] = 0x39;
    /* mod=11, reg = limit (in modrm.reg), r/m = idx */
    b[2] = 0xC0 | ((limit_reg & 7) << 3) | (idx_reg & 7);
    EMIT(cg, 3);

    /* jae rel32 placeholder — we patch it in cg_loop_end. */
    lp.jae_patch = cg->code_size;
    b = BUF(cg);
    b[0] = 0x0F; b[1] = 0x83;
    memset(b + 2, 0, 4);
    EMIT(cg, 6);
    return lp;
}

/* Emit:
 *        add idx, step    ; (or `inc idx` when step == 1)
 *        jmp .top         ; rel32
 *     .done:               ; patched into the jae above
 *
 * `step` must be 1 (scalar tail) or 4 (4-wide AVX2 body).
 */
static void cg_loop_end(CodegenState *cg, CgCountedLoop lp, int step) {
    uint8_t *b;

    /* Index advance: `add idx, 4` or `inc idx`. */
    if (step == 1) {
        /* inc r64. REX.W = 1; opcode FF /0. */
        uint8_t rex = 0x48 | ((lp.idx_reg >= 8) ? 0x01 : 0);
        b = BUF(cg);
        b[0] = rex;
        b[1] = 0xFF;
        b[2] = 0xC0 | (lp.idx_reg & 7);
        EMIT(cg, 3);
    } else {
        /* add r64, imm8. REX.W = 1; opcode 83 /0. */
        uint8_t rex = 0x48 | ((lp.idx_reg >= 8) ? 0x01 : 0);
        b = BUF(cg);
        b[0] = rex;
        b[1] = 0x83;
        b[2] = 0xC0 | (lp.idx_reg & 7);
        b[3] = (uint8_t)step;
        EMIT(cg, 4);
    }

    /* jmp rel32 back to top. */
    int32_t back = (int32_t)((int64_t)lp.top - (int64_t)(cg->code_size + 5));
    int n = emit_jmp(BUF(cg), back);
    EMIT(cg, n);

    /* Patch the jae at loop entry to land here (.done). */
    int32_t done_off = (int32_t)(cg->code_size - (lp.jae_patch + 6));
    memcpy(cg->code + lp.jae_patch + 2, &done_off, 4);
}

/* =====================================================================
 *  SIB-addressed load/store helpers:  [base + idx*8] addressing
 * =====================================================================
 *
 * Every AVX2 kernel load/stores through [base + idx*8] with i = rsi or
 * r12.  Both `vmovupd ymm, [base+idx*8]` and its scalar `movsd xmm,
 * [base+idx*8]` cousin have enough VEX / REX plumbing that inlining
 * them at every call site is a readability trap.
 *
 * These helpers pick 2- vs 3-byte VEX (for vmovupd) or REX (for movsd)
 * automatically, and handle the SIB-base=rbp/r13 quirk where mod=00
 * silently means "absolute disp32" — any rbp/r13-based addressing
 * must use mod=01 with a zero disp8.  Callers pass the register IDs;
 * the encoding is fully derived. */

/* vmovupd ymm, [base + idx*8]   (opcode 0x10)
 * vmovupd [base + idx*8], ymm   (opcode 0x11) */
static void cg_vmovupd_ymm_base_idx(CodegenState *cg, int ymm,
                                    int base_reg, int idx_reg,
                                    int opcode /*0x10 or 0x11*/) {
    int R_high = (ymm      >= 8);
    int X_high = (idx_reg  >= 8);
    int B_high = (base_reg >= 8);
    int needs_disp8 = ((base_reg & 7) == 5);   /* rbp/r13 SIB quirk */

    uint8_t *b = BUF(cg);
    int off = 0;
    if (R_high || X_high || B_high) {
        /* 3-byte VEX: C4 [R~X~B~.00001] [W=0.1111.L=1.pp=01] */
        b[off++] = 0xC4;
        b[off++] = (uint8_t)((R_high ? 0 : 0x80) |
                             (X_high ? 0 : 0x40) |
                             (B_high ? 0 : 0x20) | 0x01);
        b[off++] = 0x7D;
    } else {
        /* 2-byte VEX: C5 [R~=1.vvvv=1111.L=1.pp=01] = C5 FD */
        b[off++] = 0xC5;
        b[off++] = 0xFD;
    }
    b[off++] = (uint8_t)opcode;
    b[off++] = (uint8_t)(((needs_disp8 ? 1 : 0) << 6) |
                         ((ymm & 7) << 3) | 4);          /* modrm: reg=ymm r/m=SIB */
    b[off++] = (uint8_t)((3 << 6) | ((idx_reg & 7) << 3) |
                         (base_reg & 7));                /* sib: scale=8 */
    if (needs_disp8) b[off++] = 0;
    EMIT(cg, off);
}

/* movsd xmm, [base + idx*8]   (opcode 0x10)
 * movsd [base + idx*8], xmm   (opcode 0x11)
 *
 * Legacy SSE encoding with F2 prefix + optional REX, not VEX — matches
 * what the existing builtins emit bit-for-bit. */
static void cg_movsd_xmm_base_idx(CodegenState *cg, int xmm,
                                  int base_reg, int idx_reg,
                                  int opcode /*0x10 or 0x11*/) {
    int R = (xmm      >= 8) ? 1 : 0;
    int X = (idx_reg  >= 8) ? 1 : 0;
    int B = (base_reg >= 8) ? 1 : 0;
    int needs_disp8 = ((base_reg & 7) == 5);

    uint8_t *b = BUF(cg);
    int off = 0;
    b[off++] = 0xF2;
    if (R || X || B) {
        b[off++] = (uint8_t)(0x40 | (R << 2) | (X << 1) | B);
    }
    b[off++] = 0x0F;
    b[off++] = (uint8_t)opcode;
    b[off++] = (uint8_t)(((needs_disp8 ? 1 : 0) << 6) |
                         ((xmm & 7) << 3) | 4);
    b[off++] = (uint8_t)((3 << 6) | ((idx_reg & 7) << 3) |
                         (base_reg & 7));
    if (needs_disp8) b[off++] = 0;
    EMIT(cg, off);
}

/* =====================================================================
 *  Packed AVX2 helpers (ymm, 4 doubles per register)
 * =====================================================================
 *
 * These emit the 3-operand VEX forms so the source registers are never
 * clobbered — important because we want to keep `x` (the original input
 * vector) alive across the whole exp/sigmoid/tanh pipeline.
 *
 * Only ymm0..ymm7 are exercised; we never need the REX.B/X/R high bits.
 */

/* Emit a 3-byte VEX prefix.  This variant supports ymm0..ymm15 for
 * any operand: the B bit extends the r/m register (source2) and R
 * extends the reg field (destination).
 *
 *   mmmmm chooses the opcode-escape map:
 *     01 = 0F,  02 = 0F 38,  03 = 0F 3A
 */
static void cg_vex3(CodegenState *cg, int dst, int a, int b_reg,
                    int W, int L, int pp, int mmmmm) {
    uint8_t *b = BUF(cg);
    int R_bar = (dst < 8) ? 1 : 0;
    int X_bar = 1;                              /* no SIB in register-only forms */
    int B_bar = (b_reg < 8) ? 1 : 0;
    b[0] = 0xC4;
    b[1] = (uint8_t)((R_bar << 7) | (X_bar << 6) | (B_bar << 5) | (mmmmm & 0x1F));
    b[2] = (uint8_t)(((W & 1) << 7) | (((~a) & 0xF) << 3) | ((L & 1) << 2) | (pp & 3));
    EMIT(cg, 3);
}

/* vmulpd ymm_dst, ymm_a, ymm_b  (W ignored, encoded as W0). */
static void cg_vmulpd(CodegenState *cg, int dst, int a, int b_reg) {
    cg_vex3(cg, dst, a, b_reg, /*W=*/0, /*L=*/1, /*pp=*/1, /*mmmmm=*/1);
    uint8_t *b = BUF(cg);
    b[0] = 0x59;
    b[1] = 0xC0 | ((dst & 7) << 3) | (b_reg & 7);
    EMIT(cg, 2);
}

/* vfmadd231pd ymm_dst, ymm_a, ymm_b   →  dst = dst + a·b  (W1) */
static void cg_vfmadd231pd(CodegenState *cg, int dst, int a, int b_reg) {
    cg_vex3(cg, dst, a, b_reg, 1, 1, 1, 2);   /* 0F 38 escape */
    uint8_t *b = BUF(cg);
    b[0] = 0xB8;
    b[1] = 0xC0 | ((dst & 7) << 3) | (b_reg & 7);
    EMIT(cg, 2);
}

/* vfnmadd231pd ymm_dst, ymm_a, ymm_b   →  dst = dst - a·b */
static void cg_vfnmadd231pd(CodegenState *cg, int dst, int a, int b_reg) {
    cg_vex3(cg, dst, a, b_reg, 1, 1, 1, 2);
    uint8_t *b = BUF(cg);
    b[0] = 0xBC;
    b[1] = 0xC0 | ((dst & 7) << 3) | (b_reg & 7);
    EMIT(cg, 2);
}

/* vfmadd213pd ymm_dst, ymm_a, ymm_b   →  dst = dst·a + b */
static void cg_vfmadd213pd(CodegenState *cg, int dst, int a, int b_reg) {
    cg_vex3(cg, dst, a, b_reg, 1, 1, 1, 2);
    uint8_t *b = BUF(cg);
    b[0] = 0xA8;                                /* packed; 0xA9 is SD (scalar) */
    b[1] = 0xC0 | ((dst & 7) << 3) | (b_reg & 7);
    EMIT(cg, 2);
}

/* vmovapd ymm_dst, ymm_src — packed register copy (0F 28 /r, W ignored).
 * Unary op: vvvv must be encoded as 1111 (unused).  cg_vex3 computes
 * vvvv = (~a) & 0xF, so passing a=0 yields vvvv=0xF as Intel requires. */
static void cg_vmovapd(CodegenState *cg, int dst, int src) {
    cg_vex3(cg, dst, /*a=*/0, src, 0, 1, 1, 1);
    uint8_t *b = BUF(cg);
    b[0] = 0x28;
    b[1] = 0xC0 | ((dst & 7) << 3) | (src & 7);
    EMIT(cg, 2);
}

/* vbroadcastsd ymm_dst, [rsp + disp8]  — memory-source broadcast.
 *
 * This is the inner-loop-friendly broadcast: one instruction, no GPR
 * traffic, no cross-domain move.  Callers that need loop-invariant
 * f64 constants should pre-populate stack slots at builtin entry with
 * `mov rax, imm64 ; mov [rsp+k*8], rax` (15 bytes once), then use
 * this helper inside the hot loop.
 *
 * Encoding: VEX.256.66.0F38.W0 19 /r with a SIB-addressed memory form
 *   C4 [RXB.00010] [0.1111.1.01] 19 [modrm.reg=dst.r/m=100] [sib=rsp] [disp8]
 */
static void cg_vbroadcastsd_rsp(CodegenState *cg, int dst, int disp8) {
    uint8_t *b = BUF(cg);
    b[0] = 0xC4;
    /* byte2: R~ X~ B~ mmmmm.  R~=0 if dst>=8, else 1. X~=B~=1. mmmmm=00010. */
    b[1] = (uint8_t)((dst < 8 ? 0xE2 : 0x62));
    /* byte3: W=0 vvvv=1111 L=1 pp=01 = 0x7D */
    b[2] = 0x7D;
    b[3] = 0x19;                                      /* opcode */
    b[4] = (uint8_t)(0x44 | ((dst & 7) << 3));        /* mod=01, reg=dst, r/m=100 (SIB) */
    b[5] = 0x24;                                      /* SIB: scale=0, index=none, base=rsp */
    b[6] = (uint8_t)disp8;
    EMIT(cg, 7);
}

/* Store imm64 at [rsp + disp8]:
 *   mov rax, imm64      (10 bytes)
 *   mov [rsp+disp8], rax (5 bytes)
 */
static void cg_store_imm64_rsp(CodegenState *cg, uint64_t bits, int disp8) {
    int pn; uint8_t *b;
    pn = emit_mov_reg_imm64(BUF(cg), REG_RAX, bits); EMIT(cg, pn);
    b = BUF(cg);
    b[0] = 0x48; b[1] = 0x89; b[2] = 0x44; b[3] = 0x24;
    b[4] = (uint8_t)disp8;
    EMIT(cg, 5);
}

/* vbroadcastsd ymm_dst, imm64 bits  (materialise an f64 then broadcast).
 * Goes via RCX + xmm0-temp; clobbers RCX.  Use this only OUTSIDE hot
 * loops — see cg_vbroadcastsd_rsp for the fast path. */
static void cg_broadcast_f64(CodegenState *cg, int dst, uint64_t bits,
                              int scratch_xmm) {
    int pn; uint8_t *b;
    /* mov rcx, imm64 ; movq xmm_scratch, rcx
     * REX prefix needs R=1 when xmm_scratch >= 8. */
    pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, bits); EMIT(cg, pn);
    uint8_t rex = 0x48 | ((scratch_xmm >= 8) ? 0x04 : 0);   /* W=1, R=(high?1:0) */
    b = BUF(cg); b[0]=0x66; b[1]=rex; b[2]=0x0F; b[3]=0x6E;
    b[4] = 0xC0 | ((scratch_xmm & 7) << 3) | (REG_RCX & 7);
    EMIT(cg, 5);
    /* vbroadcastsd ymm_dst, xmm_scratch   (VEX.256.66.0F38.W0 19 /r) —
     * use cg_vex3 so the R bit is correct when dst is ymm8..15. */
    cg_vex3(cg, dst, /*a=*/0, scratch_xmm, /*W=*/0, /*L=*/1, /*pp=*/1, /*mmmmm=*/2);
    b = BUF(cg);
    b[0] = 0x19;
    b[1] = 0xC0 | ((dst & 7) << 3) | (scratch_xmm & 7);
    EMIT(cg, 2);
}

/* =====================================================================
 *  Generic Estrin polynomial evaluator
 * =====================================================================
 *
 * Emits P(y) = c[0] + c[1]·y + c[2]·y² + ... + c[n-1]·y^(n-1)
 * using Estrin's tree rearrangement, so dependent FMAs run in parallel
 * on Zen 3's two FMA pipes.  Critical path ≈ log2(n)·4c  (compared
 * with Horner's n·4c).
 *
 *   n    Horner+FMA    Estrin+FMA    Savings     Levels
 *   5    20c           12c           8c (40 %)   3
 *   7    28c           12c           16c (57 %)  3
 *   9    36c           16c           20c (56 %)  4
 *
 * API contract:
 *   - `y_reg` must be xmm0 or xmm1 (anything outside the xmm2..xmm7
 *     range that the helper uses for its temporaries).
 *   - The result P(y) is left in **xmm2**.
 *   - Clobbers xmm2..xmm7.
 *
 * Supported n: 2..9.  Adding higher degrees is a matter of writing
 * another ladder; the current ML math (sin, cos, tanh, exp, log,
 * sigmoid derivatives) all fit here.
 */
static void emit_estrin_poly(CodegenState *cg,
                              const uint64_t *c, int n,
                              int y_reg)
{
    if (n < 2 || n > 9) {
        cg_error(cg, "emit_estrin_poly: n=%d not supported (must be 2..9)", n);
        return;
    }

    /* Level 1: A = c1·y + c0  →  xmm2 */
    emit_load_f64   (cg, 2, c[1]);
    emit_load_f64   (cg, 6, c[0]);
    emit_fma213_reg (cg, 2, y_reg, 6);
    if (n == 2) return;

    /* y² into xmm7 (used by every n ≥ 3 branch below). */
    emit_mov_xmm (cg, 7, y_reg);
    cg_mulsd     (cg, 7, y_reg);

    if (n == 3) {
        /* P = A + y²·c2 */
        emit_load_f64 (cg, 6, c[2]);
        emit_fma_add  (cg, 2, 7, 6);
        return;
    }

    /* B = c3·y + c2  →  xmm3.  (n ≥ 4) */
    emit_load_f64   (cg, 3, c[3]);
    emit_load_f64   (cg, 6, c[2]);
    emit_fma213_reg (cg, 3, y_reg, 6);

    if (n == 4) {
        /* P = A + y²·B */
        emit_fma_add (cg, 2, 7, 3);
        return;
    }

    /* xmm2 = A + y²·B  (Lower — covers degrees 0..3). */
    emit_fma_add (cg, 2, 7, 3);

    if (n == 5) {
        /* P = Lower + y⁴·c4 */
        emit_mov_xmm (cg, 3, 7);
        cg_mulsd     (cg, 3, 7);               /* xmm3 = y⁴ (reuses xmm3) */
        emit_load_f64 (cg, 6, c[4]);
        emit_fma_add  (cg, 2, 3, 6);
        return;
    }

    /* C = c5·y + c4  →  xmm4.  (n ≥ 6) */
    emit_load_f64   (cg, 4, c[5]);
    emit_load_f64   (cg, 6, c[4]);
    emit_fma213_reg (cg, 4, y_reg, 6);

    /* xmm3 = y⁴ (xmm7 still holds y²).  Keep xmm7 alive for n=7. */
    emit_mov_xmm (cg, 3, 7);
    cg_mulsd     (cg, 3, 7);

    if (n == 6) {
        /* P = Lower + y⁴·C */
        emit_fma_add (cg, 2, 3, 4);
        return;
    }

    if (n == 7) {
        /* Fold c6 into C: xmm4 += y²·c6. */
        emit_load_f64 (cg, 6, c[6]);
        emit_fma_add  (cg, 4, 7, 6);
        /* P = Lower + y⁴·C */
        emit_fma_add  (cg, 2, 3, 4);
        return;
    }

    /* D = c7·y + c6  →  xmm5.  (n ≥ 8) */
    emit_load_f64   (cg, 5, c[7]);
    emit_load_f64   (cg, 6, c[6]);
    emit_fma213_reg (cg, 5, y_reg, 6);

    /* CD = C + y²·D  →  xmm4. */
    emit_fma_add (cg, 4, 7, 5);

    /* ABCD = Lower + y⁴·CD  →  xmm2 */
    emit_fma_add (cg, 2, 3, 4);

    if (n == 8) return;

    /* n == 9: P = ABCD + y⁸·c8 */
    cg_mulsd      (cg, 3, 3);                   /* xmm3 = y⁴·y⁴ = y⁸ */
    emit_load_f64 (cg, 6, c[8]);
    emit_fma_add  (cg, 2, 3, 6);
}

/*
 * sin / cos polynomial tails (Estrin-evaluated via emit_estrin_poly).
 *
 * The coefficient tables are minimax-fit for |x| ≤ π/4 (≈ 1e-11 rel
 * error in that range; the caller's octant reducer guarantees this).
 *
 *   sin(x) ≈ x + x³ · S(y)     where y = x², S has 5 terms
 *   cos(x) ≈ 1 + y · C(y)      where y = x², C has 5 terms
 *
 * Each helper assumes x is in xmm0 and leaves the result in xmm0.
 */
static const uint64_t sin_tail_coeffs[5] = {
    0xBFC5555555555549ULL,  /* S1 ≈ -1/6 */
    0x3F8111111110F8A6ULL,  /* S2 ≈  1/120 */
    0xBF2A01A019C161D5ULL,  /* S3 ≈ -1/5040 */
    0x3EC71DE357B1FE7DULL,  /* S4 ≈  1/362880 */
    0xBE5AE5E68A2B9CEBULL,  /* S5 ≈ -1/39916800 */
};

static const uint64_t cos_tail_coeffs[5] = {
    0xBFE0000000000000ULL,  /* C1 = -1/2 */
    0x3FA555555555554CULL,  /* C2 ≈  1/24 */
    0xBF56C16C16C15177ULL,  /* C3 ≈ -1/720 */
    0x3EFA01A019CB1590ULL,  /* C4 ≈  1/40320 */
    0xBE927E4F809C52ADULL,  /* C5 ≈ -1/3628800 */
};

static void emit_sin_poly_sse2(CodegenState *cg) {
    /* xmm1 = y = x² */
    emit_mov_xmm (cg, 1, 0);
    cg_mulsd     (cg, 1, 1);

    /* xmm2 = S(y) via Estrin (5-term). */
    emit_estrin_poly (cg, sin_tail_coeffs, 5, /*y_reg=*/1);

    /* sin(x) = x + x³ · S(y) */
    emit_mov_xmm (cg, 3, 0);
    cg_mulsd     (cg, 3, 1);                   /* xmm3 = x·y = x³ */
    emit_fma_add (cg, 0, 3, 2);                /* xmm0 += x³·S(y) */
}

static void emit_cos_poly_sse2(CodegenState *cg) {
    /* xmm1 = y = x² */
    emit_mov_xmm (cg, 1, 0);
    cg_mulsd     (cg, 1, 1);

    /* xmm2 = C(y) via Estrin (5-term). */
    emit_estrin_poly (cg, cos_tail_coeffs, 5, /*y_reg=*/1);

    /* cos(x) = 1 + y · C(y) */
    emit_load_f64 (cg, 0, 0x3FF0000000000000ULL);
    emit_fma_add  (cg, 0, 1, 2);               /* xmm0 += y · C(y) */
}

/* =====================================================================
 *  Octant reduction for sin/cos  (SSE4.1 roundsd + FMA3 Cody-Waite)
 * =====================================================================
 *
 * Rationale: the minimax coefficients above were fit for |x| <= π/4.
 * Applying them over [-π, π] (plain mod-2π reduction) costs ~8 orders
 * of magnitude of precision at ±π.  Octant reduction puts the argument
 * back in the poly's designed range at roughly the same code size.
 *
 *   n = round(x · 2/π)
 *   r = x - n · π/2        (Cody-Waite two-part: π/2 = HI + LO)
 *
 * Then based on (n mod 4) the caller selects sin_poly(r) / cos_poly(r)
 * and an optional sign flip.  This module only computes r and n.
 *
 * Targets: requires SSE4.1 (roundsd) + FMA3 (vfnmadd231sd).  Zen 3 and
 * every mainstream x86 CPU since ~2013 ship both.  A pure-SSE2 fallback
 * using cvtsd2si + subsd pairs is kept below #ifdef 0 for reference.
 */
static void emit_sincos_octant_reduce(CodegenState *cg) {
    int pn; uint8_t *b;

    /* xmm5 = 2/π  (0x3FE45F306DC9C883) */
    pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, 0x3FE45F306DC9C883ULL); EMIT(cg, pn);
    b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xE9; EMIT(cg, 5);  /* movq xmm5, rcx */
    b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x59; b[3]=0xE8; EMIT(cg, 4);             /* mulsd xmm5, xmm0 */

    /* xmm6 = round-to-nearest-even(xmm5)  — SSE4.1, hardwired RNE (no MXCSR) */
    /* roundsd xmm6, xmm5, 0 : 66 0F 3A 0B F5 00 */
    b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x3A; b[3]=0x0B; b[4]=0xF5; b[5]=0x00; EMIT(cg, 6);

    /* rax = (int64) xmm6  — octant index (we only need low 2 bits) */
    /* cvttsd2si rax, xmm6 : F2 48 0F 2C C6 */
    b = BUF(cg); b[0]=0xF2; b[1]=0x48; b[2]=0x0F; b[3]=0x2C; b[4]=0xC6; EMIT(cg, 5);

    /* xmm7 = π/2 high  (0x3FF921FB54400000 — only top 33 bits) */
    pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, 0x3FF921FB54400000ULL); EMIT(cg, pn);
    b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xF9; EMIT(cg, 5);  /* movq xmm7, rcx */
    /* vfnmadd231sd xmm0, xmm6, xmm7  → xmm0 = xmm0 - xmm6·xmm7 */
    b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0xC9; b[3]=0xBD; b[4]=0xC7; EMIT(cg, 5);

    /* xmm7 = π/2 low   (0x3DD0B4611A626331) */
    pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, 0x3DD0B4611A626331ULL); EMIT(cg, pn);
    b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xF9; EMIT(cg, 5);
    /* second CW step: xmm0 -= xmm6·xmm7 */
    b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0xC9; b[3]=0xBD; b[4]=0xC7; EMIT(cg, 5);

    /* rax &= 3  — keep only the octant index (0..3) */
    b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xE0; b[3]=0x03; EMIT(cg, 4);
}

/*
 * emit_sincos_body — shared body of math_sin / math_cos (SSE2 default path).
 *
 * Assumes x is already in rax (caller did emit_expression of the argument).
 * Loads x into xmm0, runs octant reduction, selects sin_poly or cos_poly
 * based on octant bit, applies the sign flip, leaves result in xmm0 + rax.
 *
 * For sin(x) n drives selection directly.
 * For cos(x) we use n+1 (`inc rax`) which makes the same selection table
 * produce the cos mapping:
 *   n mod 4 = 0: sin_poly(r),  sign=+   (cos: uses n+1=1 → cos_poly(r) +)
 *   n mod 4 = 1: cos_poly(r),  sign=+   (cos: n+1=2 → sin_poly(r) −)
 *   n mod 4 = 2: sin_poly(r),  sign=−   (cos: n+1=3 → cos_poly(r) −)
 *   n mod 4 = 3: cos_poly(r),  sign=−   (cos: n+1=0 → sin_poly(r) +)
 */
static void emit_sincos_body(CodegenState *cg, int is_cos) {
    int pn; uint8_t *b;

    /* movq xmm0, rax — load x into xmm0 */
    b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC0; EMIT(cg, 5);

    /* --- Quick-path: if |x| <= π/4, skip octant reduction entirely. ---
     * Common in ML/graphics where angles are already pre-normalised.
     * Saves ~25 cycles (the full reduction) for the price of 3 insns +
     * one well-predicted branch. */
    /* mov rcx, rax */
    b = BUF(cg); b[0]=0x48; b[1]=0x89; b[2]=0xC1; EMIT(cg, 3);
    /* btr rcx, 63   — clear sign bit → rcx = bits of |x| */
    b = BUF(cg); b[0]=0x48; b[1]=0x0F; b[2]=0xBA; b[3]=0xF1; b[4]=0x3F; EMIT(cg, 5);
    /* mov rdx, 0x3FE921FB54442D18  (bits of π/4) */
    pn = emit_mov_reg_imm64(BUF(cg), REG_RDX, 0x3FE921FB54442D18ULL); EMIT(cg, pn);
    /* cmp rcx, rdx */
    b = BUF(cg); b[0]=0x48; b[1]=0x39; b[2]=0xD1; EMIT(cg, 3);
    /* ja need_reduce  (placeholder — 6-byte near jump: 0F 87 xx xx xx xx) */
    size_t ja_pos = cg->code_size;
    b = BUF(cg); b[0]=0x0F; b[1]=0x87; b[2]=0; b[3]=0; b[4]=0; b[5]=0; EMIT(cg, 6);

    /* Fast path — no reduction, no octant branch, no sign flip. */
    if (is_cos) {
        emit_cos_poly_sse2(cg);
    } else {
        emit_sin_poly_sse2(cg);
    }
    /* movq rax, xmm0  — sync rax with result */
    b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5);
    /* jmp done  (placeholder) */
    size_t jmp_fast_done = cg->code_size;
    b = BUF(cg); b[0]=0xE9; b[1]=0; b[2]=0; b[3]=0; b[4]=0; EMIT(cg, 5);

    /* --- Patch `ja need_reduce` to land here --- */
    {
        int32_t off = (int32_t)(cg->code_size - (ja_pos + 6));
        memcpy(cg->code + ja_pos + 2, &off, 4);
    }

    /* --- Slow path: full octant reduction + sin/cos poly select. --- */
    emit_sincos_octant_reduce(cg);  /* xmm0 = r, rax = n & 3 */

    if (is_cos) {
        /* inc rax  — reuse sin's selection table for cos */
        b = BUF(cg); b[0]=0x48; b[1]=0xFF; b[2]=0xC0; EMIT(cg, 3);
        /* and rax, 3 */
        b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xE0; b[3]=0x03; EMIT(cg, 4);
    }

    /* Compute sign mask in rcx: bit1 of rax → 1<<63 or 0. */
    /* mov rcx, rax */
    b = BUF(cg); b[0]=0x48; b[1]=0x89; b[2]=0xC1; EMIT(cg, 3);
    /* shr rcx, 1 */
    b = BUF(cg); b[0]=0x48; b[1]=0xD1; b[2]=0xE9; EMIT(cg, 3);
    /* and rcx, 1 */
    b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xE1; b[3]=0x01; EMIT(cg, 4);
    /* shl rcx, 63 */
    b = BUF(cg); b[0]=0x48; b[1]=0xC1; b[2]=0xE1; b[3]=0x3F; EMIT(cg, 4);
    /* push rcx  — save sign mask across poly call */
    b = BUF(cg); b[0]=0x51; EMIT(cg, 1);

    /* Test low bit of rax: 0 ⇒ sin_poly, 1 ⇒ cos_poly */
    /* test al, 1 */
    b = BUF(cg); b[0]=0xA8; b[1]=0x01; EMIT(cg, 2);

    /* jnz to cos_branch (placeholder) */
    size_t jnz_pos = cg->code_size;
    b = BUF(cg); b[0]=0x0F; b[1]=0x85; b[2]=0; b[3]=0; b[4]=0; b[5]=0; EMIT(cg, 6);

    /* --- sin_poly branch --- */
    emit_sin_poly_sse2(cg);
    /* jmp to done (placeholder) */
    size_t jmp_pos = cg->code_size;
    b = BUF(cg); b[0]=0xE9; b[1]=0; b[2]=0; b[3]=0; b[4]=0; EMIT(cg, 5);

    /* Patch jnz to land on cos branch */
    int32_t jnz_off = (int32_t)(cg->code_size - (jnz_pos + 6));
    memcpy(cg->code + jnz_pos + 2, &jnz_off, 4);

    /* --- cos_poly branch --- */
    emit_cos_poly_sse2(cg);

    /* Patch jmp to land here */
    int32_t jmp_off = (int32_t)(cg->code_size - (jmp_pos + 5));
    memcpy(cg->code + jmp_pos + 1, &jmp_off, 4);

    /* --- sign flip + sync rax --- */
    /* pop rcx */
    b = BUF(cg); b[0]=0x59; EMIT(cg, 1);
    /* movq rax, xmm0 */
    b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5);
    /* xor rax, rcx */
    b = BUF(cg); b[0]=0x48; b[1]=0x31; b[2]=0xC8; EMIT(cg, 3);
    /* movq xmm0, rax */
    b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC0; EMIT(cg, 5);

    /* --- Patch the fast-path `jmp done` to land here. --- */
    {
        int32_t off = (int32_t)(cg->code_size - (jmp_fast_done + 5));
        memcpy(cg->code + jmp_fast_done + 1, &off, 4);
    }
    (void)pn;
}

/*
 * Reserve 64 bytes on the stack and spill the 8 exp-polynomial
 * coefficients (C1..C8) into it.  Reads happen via cg_vbroadcastsd_rsp
 * inside emit_vec_exp_body_avx2.  The teardown reclaims the bytes.
 */
static void emit_exp_coeff_stack_setup(CodegenState *cg) {
    uint8_t *b;
    /* sub rsp, 64 */
    b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xEC; b[3]=64; EMIT(cg, 4);
    cg_store_imm64_rsp(cg, 0x3FE0000000000000ULL,  0);  /* C1 = 1/2       */
    cg_store_imm64_rsp(cg, 0x3FC5555555555555ULL,  8);  /* C2 = 1/6       */
    cg_store_imm64_rsp(cg, 0x3FA5555555555555ULL, 16);  /* C3 = 1/24      */
    cg_store_imm64_rsp(cg, 0x3F81111111111111ULL, 24);  /* C4 = 1/120     */
    cg_store_imm64_rsp(cg, 0x3F56C16C16C16C17ULL, 32);  /* C5 = 1/720     */
    cg_store_imm64_rsp(cg, 0x3F2A01A01A01A01AULL, 40);  /* C6 = 1/5040    */
    cg_store_imm64_rsp(cg, 0x3EFA01A01A01A01AULL, 48);  /* C7 = 1/40320   */
    cg_store_imm64_rsp(cg, 0x3EC71DE3A556C734ULL, 56);  /* C8 = 1/362880  */
}

static void emit_exp_coeff_stack_teardown(CodegenState *cg) {
    /* add rsp, 64 */
    uint8_t *b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=64; EMIT(cg, 4);
}

/* =====================================================================
 *  Packed AVX2 exp(x) — reusable body for arr_f64_exp, sigmoid, tanh, …
 * =====================================================================
 *
 * Algorithm: k = round(x · log2e);  r = x - k · ln2;
 *            exp(x) = (1 + r · P(r)) · 2^k
 * P is the 9-term Taylor polynomial evaluated with Estrin's scheme
 * (matches the scalar math_exp bit-for-bit in limit).
 *
 * Input preconditions (caller responsibility):
 *   ymm0  = x (4 packed f64 lanes)
 *   ymm8  = broadcast(log2e)
 *   ymm9  = broadcast(ln2)
 *   ymm10 = broadcast(1.0)
 *   [rsp + 0 .. +56]  holds the 7 polynomial coefficients C2..C8 as
 *                     plain f64 values (see EXP_STACK_* offsets below).
 *                     C0 = 1.0 is read from ymm10; C1 = 1/2 is read
 *                     via a separate stack slot to minimise setup.
 *
 * Stack layout the caller must populate before the hot loop:
 *   [rsp +  0] = C1  (0.5)
 *   [rsp +  8] = C2  (1/6)
 *   [rsp + 16] = C3  (1/24)
 *   [rsp + 24] = C4  (1/120)
 *   [rsp + 32] = C5  (1/720)
 *   [rsp + 40] = C6  (1/5040)
 *   [rsp + 48] = C7  (1/40320)
 *   [rsp + 56] = C8  (1/362880)
 *   (8 × 8 = 64 bytes — keeps the stack 16-byte aligned.)
 *
 * Output:
 *   ymm0  = exp(x)
 *
 * Clobbers: ymm1..ymm7, RAX, RCX.
 *
 * Register plan inside the body:
 *   ymm1 — k_f (after vroundpd)          → later: recycled for 2^k bit-build
 *   ymm2 — Estrin A → Lower → ABCD → P
 *   ymm3 — Estrin B → final exp(r) accumulator
 *   ymm4 — Estrin C → CD → xmm4 for 2^k int path → ymm4 = 2^k
 *   ymm5 — Estrin D → xmm5 holding broadcast 1023 bias
 *   ymm6 — broadcast scratch / y² / constant pair C0/C2/C4/C6/C8
 *   ymm7 — y⁴ → y⁸
 */
static void emit_vec_exp_body_avx2(CodegenState *cg) {
    int pn; uint8_t *b;

    /* Step 1: ymm1 = round(x · log2e). */
    cg_vmulpd(cg, 1, 0, 8);
    /* vroundpd ymm1, ymm1, 0 */
    b = BUF(cg); b[0]=0xC4; b[1]=0xE3; b[2]=0x7D; b[3]=0x09;
    b[4]=0xC9; b[5]=0x00; EMIT(cg, 6);

    /* Step 2: ymm0 = x - k · ln2 = r. */
    cg_vfnmadd231pd(cg, 0, 1, 9);

    /* Step 3: Estrin 9-term polynomial P(r) with broadcasts read from
     * the caller's stack slots.  Each broadcast is a single VEX-encoded
     * 7-byte instruction (vs the old 20-byte mov-imm64+movq+broadcast
     * triple), and the decoded µops do not contend with any other
     * inner-loop work — critical on Zen 3 where the front-end bandwidth
     * dominates tight FMA chains. */
    cg_vbroadcastsd_rsp(cg, 2,  0);                        /* ymm2 = C1 */
    cg_vmovapd         (cg, 6, 10);                         /* ymm6 = C0 = 1.0 */
    cg_vfmadd213pd     (cg, 2,  0, 6);                      /* A = C1·r + C0 */

    cg_vbroadcastsd_rsp(cg, 3, 16);                        /* ymm3 = C3 */
    cg_vbroadcastsd_rsp(cg, 6,  8);                        /* ymm6 = C2 */
    cg_vfmadd213pd     (cg, 3,  0, 6);                      /* B = C3·r + C2 */

    cg_vbroadcastsd_rsp(cg, 4, 32);                        /* ymm4 = C5 */
    cg_vbroadcastsd_rsp(cg, 6, 24);                        /* ymm6 = C4 */
    cg_vfmadd213pd     (cg, 4,  0, 6);                      /* C = C5·r + C4 */

    cg_vbroadcastsd_rsp(cg, 5, 48);                        /* ymm5 = C7 */
    cg_vbroadcastsd_rsp(cg, 6, 40);                        /* ymm6 = C6 */
    cg_vfmadd213pd     (cg, 5,  0, 6);                      /* D = C7·r + C6 */

    /* r² in ymm6 */
    cg_vmovapd(cg, 6, 0);
    cg_vmulpd (cg, 6, 0, 0);

    /* Level 2: AB = A + r²·B, CD = C + r²·D, r⁴ in ymm7 */
    cg_vfmadd231pd(cg, 2, 6, 3);
    cg_vfmadd231pd(cg, 4, 6, 5);
    cg_vmovapd    (cg, 7, 6);
    cg_vmulpd     (cg, 7, 6, 6);

    /* Level 3: ABCD = AB + r⁴·CD, r⁸ in ymm7 */
    cg_vfmadd231pd(cg, 2, 7, 4);
    cg_vmulpd     (cg, 7, 7, 7);

    /* Level 4: P = ABCD + r⁸·C8 */
    cg_vbroadcastsd_rsp(cg, 6, 56);                         /* ymm6 = C8 */
    cg_vfmadd231pd     (cg, 2, 7, 6);

    /* exp_raw = 1 + r · P.  Seed ymm3 with 1.0, FMA r·P into it. */
    cg_vmovapd    (cg, 3, 10);
    cg_vfmadd231pd(cg, 3, 0, 2);                           /* ymm3 = 1 + r·P */

    /* Step 4: construct 2^k from the integer k still in ymm1.
     *   xmm4 = (int32)(k)    via vcvttpd2dq
     *   xmm4 += 1023         via vpaddd with broadcast-1023
     *   ymm4 = sign-extend xmm4 to int64   via vpmovsxdq
     *   ymm4 <<= 52          via vpsllq — the result is the IEEE-754
     *                        exponent field, giving 2^k as a double. */
    /* vcvttpd2dq xmm4, ymm1 */
    b = BUF(cg); b[0]=0xC5; b[1]=0xFF; b[2]=0xE6; b[3]=0xE1; EMIT(cg, 4);

    /* Build xmm5 = <1023,1023,1023,1023> via vmovd + vpshufd. */
    pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1023); EMIT(cg, pn);
    b = BUF(cg); b[0]=0xC5; b[1]=0xF9; b[2]=0x6E; b[3]=0xE8; EMIT(cg, 4);  /* vmovd xmm5, eax */
    b = BUF(cg); b[0]=0xC5; b[1]=0xF9; b[2]=0x70; b[3]=0xED; b[4]=0x00; EMIT(cg, 5); /* vpshufd xmm5,xmm5,0 */

    /* vpaddd xmm4, xmm4, xmm5 */
    b = BUF(cg); b[0]=0xC5; b[1]=0xD9; b[2]=0xFE; b[3]=0xE5; EMIT(cg, 4);
    /* vpmovsxdq ymm4, xmm4 */
    b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0x7D; b[3]=0x25; b[4]=0xE4; EMIT(cg, 5);
    /* vpsllq ymm4, ymm4, 52 */
    b = BUF(cg); b[0]=0xC5; b[1]=0xDD; b[2]=0x73; b[3]=0xF4; b[4]=0x34; EMIT(cg, 5);

    /* Result: ymm0 = ymm3 · ymm4  = exp(r) · 2^k = exp(x). */
    cg_vmulpd(cg, 0, 3, 4);
}

int emit_builtin(CodegenState *cg, const ASTNode *node,
                 const char *name, size_t argc) {
        if (strcmp(name, "print_int") == 0 && argc == 1) {
            emit_builtin_print_int(cg, node->children[1]);
            return 1;
        }
        if (strcmp(name, "print_str") == 0 && argc == 1) {
            emit_builtin_print_str(cg, node->children[1]);
            return 1;
        }
        if (strcmp(name, "read_int") == 0 && argc == 0) {
            emit_builtin_read_int(cg);
            return 1;
        }
        /*
         * ARRAY BUILTINS
         * arr_new(n): allocate n-element array on stack, store length, return base
         * arr_get(base, idx): return element at index
         * arr_set(base, idx, val): store value at index
         * arr_len(base): return stored length
         */
        if (strcmp(name, "arr_new") == 0 && argc == 1) {
            /* arr_new(n): HEAP allocate via mmap. Persists across returns.
             * Layout: [length][elem0][elem1]...[elemN-1]
             * Returns pointer to elem0 (base = mmap_ptr + 8) */
            emit_expression(cg, node->children[1]); /* n -> RAX */
            int pn; uint8_t *b;

            /* Push n (mmap will trash all regs) */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);

            /* rsi = (n+1)*8 */
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_shl_reg_imm(BUF(cg), REG_RAX, 3); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RAX); EMIT(cg, pn);

            /* mmap(0, rsi, 3, 0x22, -1, 0) = syscall 9 */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 3); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC2;
            int32_t v=0x22; memcpy(b+3,&v,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC0;
            v=-1; memcpy(b+3,&v,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg,3);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 9); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            /* RAX = heap ptr. Pop n into RCX */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);

            /* mov [rax], rcx  -- store length */
            b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RCX), 0, reg_ext(REG_RAX));
            b[1] = 0x89; b[2] = modrm(0, REG_RCX, REG_RAX);
            EMIT(cg, 3);

            /* rax += 8  -- return base (skip length header) */
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 8); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_get") == 0 && argc == 2) {
            /* arr_get(base, idx): load [base + idx*8] with bounds check */
            emit_expression(cg, node->children[2]); /* idx -> RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base -> RAX */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* RCX = idx */

            /* ── Bounds check: 0 <= idx < length ──
             * RDX = [RAX - 8] (length)
             * if idx < 0 || idx >= length → error */
            uint8_t *b;
            /* mov rdx, [rax - 8]  (load length) */
            pn = emit_mov_reg_mem(BUF(cg), REG_RDX, REG_RAX, -8); EMIT(cg, pn);
            /* cmp rcx, rdx */
            pn = emit_cmp_reg_reg(BUF(cg), REG_RCX, REG_RDX); EMIT(cg, pn);
            /* jb .bounds_ok (unsigned: catches negative idx too) */
            size_t jb_pos = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x82; memset(b+2,0,4); EMIT(cg, 6);

            /* Error: index out of bounds */
            {
                const char *errmsg = "Runtime error: array index out of bounds\n";
                size_t errmsg_len = 41;
                size_t jmp_str = cg->code_size;
                pn = emit_jmp(BUF(cg), 0); EMIT(cg, pn);
                size_t str_pos = cg->code_size;
                memcpy(BUF(cg), errmsg, errmsg_len); cg->code_size += errmsg_len;
                int32_t jo = (int32_t)(cg->code_size - (jmp_str + 5));
                memcpy(cg->code + jmp_str + 1, &jo, 4);
                int32_t rip_off = (int32_t)((int64_t)str_pos - (int64_t)(cg->code_size + 7));
                b = BUF(cg);
                b[0]=rex(1,reg_ext(REG_RSI),0,0); b[1]=0x8D;
                b[2]=modrm(0,REG_RSI,5); memcpy(b+3,&rip_off,4); EMIT(cg,7);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, (uint32_t)errmsg_len); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 2); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
                pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 60); EMIT(cg, pn);
                pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            }
            /* .bounds_ok: patch jb */
            int32_t jb_off = (int32_t)(cg->code_size - (jb_pos + 6));
            memcpy(cg->code + jb_pos + 2, &jb_off, 4);

            /* lea rcx, [rax + rcx*8] */
            b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RCX), reg_ext(REG_RCX), reg_ext(REG_RAX));
            b[1] = 0x8D;
            b[2] = modrm(0, REG_RCX & 7, 4); /* SIB */
            b[3] = (uint8_t)((3 << 6) | ((REG_RCX & 7) << 3) | (REG_RAX & 7)); /* scale=8 */
            EMIT(cg, 4);
            /* mov rax, [rcx] */
            b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RAX), 0, reg_ext(REG_RCX));
            b[1] = 0x8B; b[2] = modrm(0, REG_RAX, REG_RCX & 7);
            EMIT(cg, 3);
            return 1;
        }
        if (strcmp(name, "arr_set") == 0 && argc == 3) {
            /* arr_set(base, idx, val) with bounds check */
            emit_expression(cg, node->children[3]); /* val -> RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* idx -> RAX */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base -> RAX */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* RCX = idx */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* RDX = val */

            /* ── Bounds check: 0 <= idx < length ── */
            uint8_t *b;
            /* Save RDX (val) before we use it */
            pn = emit_push(BUF(cg), REG_RDX); EMIT(cg, pn);
            /* r11 = [rax - 8] (length) */
            b = BUF(cg); b[0]=0x4C; b[1]=0x8B; b[2]=0x58; b[3]=0xF8; EMIT(cg, 4); /* mov r11,[rax-8] */
            /* cmp rcx, r11 */
            b = BUF(cg); b[0]=0x4C; b[1]=0x39; b[2]=0xD9; EMIT(cg, 3); /* cmp rcx, r11 */
            /* jb .bounds_ok */
            size_t jb_pos = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x82; memset(b+2,0,4); EMIT(cg, 6);

            /* Error: index out of bounds */
            {
                const char *errmsg = "Runtime error: array index out of bounds\n";
                size_t errmsg_len = 41;
                size_t jmp_str = cg->code_size;
                pn = emit_jmp(BUF(cg), 0); EMIT(cg, pn);
                size_t str_pos = cg->code_size;
                memcpy(BUF(cg), errmsg, errmsg_len); cg->code_size += errmsg_len;
                int32_t jo = (int32_t)(cg->code_size - (jmp_str + 5));
                memcpy(cg->code + jmp_str + 1, &jo, 4);
                int32_t rip_off = (int32_t)((int64_t)str_pos - (int64_t)(cg->code_size + 7));
                b = BUF(cg);
                b[0]=rex(1,reg_ext(REG_RSI),0,0); b[1]=0x8D;
                b[2]=modrm(0,REG_RSI,5); memcpy(b+3,&rip_off,4); EMIT(cg,7);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, (uint32_t)errmsg_len); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 2); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
                pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 60); EMIT(cg, pn);
                pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            }
            /* .bounds_ok: patch jb */
            int32_t jb_off = (int32_t)(cg->code_size - (jb_pos + 6));
            memcpy(cg->code + jb_pos + 2, &jb_off, 4);
            /* Restore RDX (val) */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn);

            /* lea rcx, [rax + rcx*8] */
            b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RCX), reg_ext(REG_RCX), reg_ext(REG_RAX));
            b[1] = 0x8D;
            b[2] = modrm(0, REG_RCX & 7, 4);
            b[3] = (uint8_t)((3 << 6) | ((REG_RCX & 7) << 3) | (REG_RAX & 7));
            EMIT(cg, 4);
            /* mov [rcx], rdx */
            b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RDX), 0, reg_ext(REG_RCX));
            b[1] = 0x89; b[2] = modrm(0, REG_RDX, REG_RCX & 7);
            EMIT(cg, 3);
            /* Return 0 */
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_len") == 0 && argc == 1) {
            /* arr_len(base): load [base - 8] */
            emit_expression(cg, node->children[1]); /* base -> RAX */
            /* mov rax, [rax - 8] */
            int pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RAX, -8);
            EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "mem_free") == 0 && argc == 1) {
            /* mem_free(base): munmap the heap allocation.
             * base points to elem0/char0. Real mmap ptr = base - 8.
             * For arrays:  mmap_size = (length + 1) * 8
             * For strings: mmap_size = length + 8
             * We store length at [base-8], mmap started at base-8.
             * munmap(addr, size) = syscall 11 */
            emit_expression(cg, node->children[1]); /* base -> RAX */
            int pn;
            /* RDI = base - 8 (real mmap address) */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RDI, -8); EMIT(cg, pn);
            /* RSI = [base - 8] (length) */
            pn = emit_mov_reg_mem(BUF(cg), REG_RSI, REG_RAX, -8); EMIT(cg, pn);
            /* RSI = (length + 1) * 8 — conservative size covering both arrays and strings */
            pn = emit_add_reg_imm(BUF(cg), REG_RSI, 1); EMIT(cg, pn);
            pn = emit_shl_reg_imm(BUF(cg), REG_RSI, 3); EMIT(cg, pn);
            /* syscall munmap(rdi, rsi) */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 11); EMIT(cg, pn); /* __NR_munmap */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            /* Return 0 on success */
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }

        /*
         * SSE2 SIMD BUILTINS — vectorized array operations
         * Process 2 i64 elements per cycle (128-bit XMM registers).
         * Universal on all x86_64 processors — safe for embedded/robotics.
         *
         * arr_sum(base)          → sum of all elements
         * arr_fill(base, val)    → fill all elements with val
         * arr_scale(base, factor)→ multiply all elements by factor
         * arr_dot(a, b)          → dot product of two same-length arrays
         */
        if (strcmp(name, "arr_sum") == 0 && argc == 1 && cg->use_avx2) {
            /* AVX2 vectorized sum: VPADDQ on 4 x i64 per iteration (256-bit YMM)
             * YMM0 = accumulator [s0, s1, s2, s3]
             * Loop: ymm1 = [elem[i..i+3]]; ymm0 += ymm1
             * After loop: extract and horizontal add → rax
             * Requires: --avx2 flag */
            emit_expression(cg, node->children[1]); /* base → RAX */
            int pn; uint8_t *b;
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            /* vpxor ymm0, ymm0, ymm0 (zero acc) — VEX 3-byte */
            b = BUF(cg); b[0]=0xC5; b[1]=0xFD; b[2]=0xEF; b[3]=0xC0; EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            /* .avx_loop: */
            size_t avx_loop = cg->code_size;
            /* lea rdx, [rsi+4] */
            b = BUF(cg); b[0]=0x48; b[1]=0x8D; b[2]=0x56; b[3]=0x04; EMIT(cg, 4);
            pn = emit_cmp_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            size_t ja_tail = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x87; memset(b+2,0,4); EMIT(cg, 6);
            /* vmovdqu ymm1, [rdi + rsi*8] — load 4 elements */
            b = BUF(cg); b[0]=0xC5; b[1]=0xFE; b[2]=0x6F;
            b[3]=0x0C; b[4]=(uint8_t)((3<<6)|(REG_RSI<<3)|REG_RDI);
            EMIT(cg, 5);
            /* vpaddq ymm0, ymm0, ymm1 */
            b = BUF(cg); b[0]=0xC5; b[1]=0xFD; b[2]=0xD4; b[3]=0xC1; EMIT(cg, 4);
            pn = emit_add_reg_imm(BUF(cg), REG_RSI, 4); EMIT(cg, pn);
            int32_t back = (int32_t)((int64_t)avx_loop - (int64_t)(cg->code_size + 5));
            pn = emit_jmp(BUF(cg), back); EMIT(cg, pn);

            /* .tail: handle remaining 0-3 elements with scalar */
            int32_t ja_off = (int32_t)(cg->code_size - (ja_tail + 6));
            memcpy(cg->code + ja_tail + 2, &ja_off, 4);

            /* Extract ymm0 → 4 i64 values and sum:
             * vextracti128 xmm1, ymm0, 1  (get high 128 bits)
             * vpaddq xmm0, xmm0, xmm1     (add high to low)
             * movq rax, xmm0              (low 64)
             * psrldq xmm0, 8              (shift)
             * movq rdx, xmm0              (high 64)
             * add rax, rdx */
            /* vextracti128 xmm1, ymm0, 1 */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE3; b[2]=0x7D; b[3]=0x39; b[4]=0xC1; b[5]=0x01; EMIT(cg, 6);
            /* vpaddq xmm0, xmm0, xmm1 */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF9; b[2]=0xD4; b[3]=0xC1; EMIT(cg, 4);
            /* Horizontal sum of xmm0 */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5); /* movq rax, xmm0 */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x73; b[3]=0xD8; b[4]=0x08; EMIT(cg, 5); /* psrldq xmm0,8 */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC2; EMIT(cg, 5); /* movq rdx, xmm0 */
            pn = emit_add_reg_reg(BUF(cg), REG_RAX, REG_RDX); EMIT(cg, pn);

            /* Scalar tail for remaining elements */
            CgCountedLoop st = cg_loop_begin(cg, REG_RSI, REG_RCX);
            /* mov rdx, [rdi + rsi*8] */
            b = BUF(cg); b[0]=rex(1,reg_ext(REG_RDX),reg_ext(REG_RSI),reg_ext(REG_RDI));
            b[1]=0x8B; b[2]=modrm(0,REG_RDX&7,4);
            b[3]=(uint8_t)((3<<6)|((REG_RSI&7)<<3)|(REG_RDI&7));
            EMIT(cg, 4);
            pn = emit_add_reg_reg(BUF(cg), REG_RAX, REG_RDX); EMIT(cg, pn);
            cg_loop_end(cg, st, 1);

            /* vzeroupper — required after AVX to avoid SSE transition penalty */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);
            return 1;
        }

        if (strcmp(name, "arr_sum") == 0 && argc == 1) {
            /* SSE2 vectorized sum: PADDQ on 2 x i64 per iteration
             * XMM0 = accumulator [sum_lo, sum_hi]
             * Loop: xmm1 = [elem[i], elem[i+1]]; xmm0 += xmm1
             * After loop: horizontal add xmm0 → rax */
            emit_expression(cg, node->children[1]); /* base → RAX */
            int pn; uint8_t *b;
            /* RDI = base, RCX = length */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            /* pxor xmm0, xmm0 (zero accumulator) */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0xEF; b[3]=0xC0; EMIT(cg, 4);
            /* RSI = i = 0 */
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            /* .simd_loop: cmp rsi+2, rcx — can we do 2 at a time? */
            size_t simd_loop = cg->code_size;
            /* lea rdx, [rsi+2] */
            b = BUF(cg); b[0]=0x48; b[1]=0x8D; b[2]=0x56; b[3]=0x02; EMIT(cg, 4);
            /* cmp rdx, rcx */
            pn = emit_cmp_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            /* ja .scalar_tail */
            size_t ja_scalar = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x87; memset(b+2,0,4); EMIT(cg, 6);

            /* movdqu xmm1, [rdi + rsi*8] — load 2 elements */
            b = BUF(cg); b[0]=0xF3; b[1]=0x0F; b[2]=0x6F;
            b[3]=modrm(0, 1, 4); /* xmm1, SIB */
            b[4]=(uint8_t)((3<<6)|(REG_RSI<<3)|REG_RDI); /* scale=8 */
            EMIT(cg, 5);
            /* paddq xmm0, xmm1 */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0xD4; b[3]=0xC1; EMIT(cg, 4);
            /* add rsi, 2 */
            pn = emit_add_reg_imm(BUF(cg), REG_RSI, 2); EMIT(cg, pn);
            /* jmp .simd_loop */
            int32_t back = (int32_t)((int64_t)simd_loop - (int64_t)(cg->code_size + 5));
            pn = emit_jmp(BUF(cg), back); EMIT(cg, pn);

            /* .scalar_tail: handle remaining element */
            int32_t ja_off = (int32_t)(cg->code_size - (ja_scalar + 6));
            memcpy(cg->code + ja_scalar + 2, &ja_off, 4);

            /* cmp rsi, rcx */
            pn = emit_cmp_reg_reg(BUF(cg), REG_RSI, REG_RCX); EMIT(cg, pn);
            /* jae .done */
            size_t jae_done = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x83; memset(b+2,0,4); EMIT(cg, 6);
            /* add xmm0, [rdi + rsi*8] as scalar via GPR */
            /* mov rdx, [rdi + rsi*8] */
            b = BUF(cg); b[0]=rex(1,reg_ext(REG_RDX),reg_ext(REG_RSI),reg_ext(REG_RDI));
            b[1]=0x8B; b[2]=modrm(0,REG_RDX&7,4);
            b[3]=(uint8_t)((3<<6)|((REG_RSI&7)<<3)|(REG_RDI&7));
            EMIT(cg, 4);
            /* movq xmm1, rdx */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xCA; EMIT(cg, 5);
            /* paddq xmm0, xmm1 */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0xD4; b[3]=0xC1; EMIT(cg, 4);

            /* .done: horizontal sum xmm0 → rax */
            int32_t jae_off = (int32_t)(cg->code_size - (jae_done + 6));
            memcpy(cg->code + jae_done + 2, &jae_off, 4);

            /* movq rax, xmm0 (low 64 bits) */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5);
            /* psrldq xmm0, 8 (shift high 64 bits to low) */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x73; b[3]=0xD8; b[4]=0x08; EMIT(cg, 5);
            /* movq rdx, xmm0 */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC2; EMIT(cg, 5);
            /* add rax, rdx */
            pn = emit_add_reg_reg(BUF(cg), REG_RAX, REG_RDX); EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "arr_fill") == 0 && argc == 2) {
            /* SSE2 vectorized fill: store [val, val] via MOVDQU, 2 per cycle */
            emit_expression(cg, node->children[2]); /* val → RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base → RAX */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* RDX = val */
            uint8_t *b;
            /* RDI = base, RCX = length */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            /* movq xmm0, rdx */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC2; EMIT(cg, 5);
            /* punpcklqdq xmm0, xmm0 (broadcast val to both lanes) */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x6C; b[3]=0xC0; EMIT(cg, 4);
            /* RSI = 0 */
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            size_t loop_top = cg->code_size;
            /* lea rdx, [rsi+2] */
            b = BUF(cg); b[0]=0x48; b[1]=0x8D; b[2]=0x56; b[3]=0x02; EMIT(cg, 4);
            pn = emit_cmp_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            size_t ja_tail = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x87; memset(b+2,0,4); EMIT(cg, 6);
            /* movdqu [rdi + rsi*8], xmm0 */
            b = BUF(cg); b[0]=0xF3; b[1]=0x0F; b[2]=0x7F;
            b[3]=modrm(0, 0, 4);
            b[4]=(uint8_t)((3<<6)|(REG_RSI<<3)|REG_RDI);
            EMIT(cg, 5);
            pn = emit_add_reg_imm(BUF(cg), REG_RSI, 2); EMIT(cg, pn);
            int32_t back = (int32_t)((int64_t)loop_top - (int64_t)(cg->code_size + 5));
            pn = emit_jmp(BUF(cg), back); EMIT(cg, pn);
            /* scalar tail */
            int32_t ja_off = (int32_t)(cg->code_size - (ja_tail + 6));
            memcpy(cg->code + ja_tail + 2, &ja_off, 4);
            pn = emit_cmp_reg_reg(BUF(cg), REG_RSI, REG_RCX); EMIT(cg, pn);
            size_t jae_done = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x83; memset(b+2,0,4); EMIT(cg, 6);
            /* movq xmm0 → rdx, then mov [rdi+rsi*8], rdx */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC2; EMIT(cg, 5);
            b = BUF(cg); b[0]=rex(1,reg_ext(REG_RDX),reg_ext(REG_RSI),reg_ext(REG_RDI));
            b[1]=0x89; b[2]=modrm(0,REG_RDX&7,4);
            b[3]=(uint8_t)((3<<6)|((REG_RSI&7)<<3)|(REG_RDI&7));
            EMIT(cg, 4);
            /* .done */
            int32_t jae_off2 = (int32_t)(cg->code_size - (jae_done + 6));
            memcpy(cg->code + jae_done + 2, &jae_off2, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "arr_scale") == 0 && argc == 2) {
            /* Scalar multiply (SSE2 lacks 64-bit integer multiply).
             * Uses GPR imul — still vectorization-ready loop structure. */
            emit_expression(cg, node->children[2]); /* factor → RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base → RAX */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* RDX = factor */
            uint8_t *b;
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn); /* base */
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn); /* len */
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn); /* i=0 */

            CgCountedLoop lp = cg_loop_begin(cg, REG_RSI, REG_RCX);
            /* mov rax, [rdi + rsi*8] */
            b = BUF(cg); b[0]=rex(1,reg_ext(REG_RAX),reg_ext(REG_RSI),reg_ext(REG_RDI));
            b[1]=0x8B; b[2]=modrm(0,REG_RAX&7,4);
            b[3]=(uint8_t)((3<<6)|((REG_RSI&7)<<3)|(REG_RDI&7));
            EMIT(cg, 4);
            /* imul rax, rdx */
            pn = emit_imul_reg_reg(BUF(cg), REG_RAX, REG_RDX); EMIT(cg, pn);
            /* mov [rdi + rsi*8], rax */
            b = BUF(cg); b[0]=rex(1,reg_ext(REG_RAX),reg_ext(REG_RSI),reg_ext(REG_RDI));
            b[1]=0x89; b[2]=modrm(0,REG_RAX&7,4);
            b[3]=(uint8_t)((3<<6)|((REG_RSI&7)<<3)|(REG_RDI&7));
            EMIT(cg, 4);
            cg_loop_end(cg, lp, 1);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "arr_dot") == 0 && argc == 2) {
            /* Dot product: sum(a[i] * b[i]) using GPR multiply + accumulate.
             * Both arrays must have same length (uses a's length).
             * Uses RBX (callee-saved) for b_ptr to avoid r8 SIB encoding issues. */
            emit_expression(cg, node->children[2]); /* b → RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* a → RAX */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* RDX = b base */
            uint8_t *b;
            /* Save RBX (callee-saved) */
            pn = emit_push(BUF(cg), REG_RBX); EMIT(cg, pn);
            /* RDI = a, RBX = b, RCX = len, R9 = accumulator */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RBX, REG_RDX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg, 3); /* xor r9, r9 (acc=0) */
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn); /* i=0 */

            CgCountedLoop lp = cg_loop_begin(cg, REG_RSI, REG_RCX);
            /* mov rax, [rdi + rsi*8] — a[i] */
            b = BUF(cg); b[0]=rex(1,reg_ext(REG_RAX),reg_ext(REG_RSI),reg_ext(REG_RDI));
            b[1]=0x8B; b[2]=modrm(0,REG_RAX&7,4);
            b[3]=(uint8_t)((3<<6)|((REG_RSI&7)<<3)|(REG_RDI&7));
            EMIT(cg, 4);
            /* mov rdx, [rbx + rsi*8] — b[i] */
            b = BUF(cg); b[0]=rex(1,reg_ext(REG_RDX),reg_ext(REG_RSI),reg_ext(REG_RBX));
            b[1]=0x8B; b[2]=modrm(0,REG_RDX&7,4);
            b[3]=(uint8_t)((3<<6)|((REG_RSI&7)<<3)|(REG_RBX&7));
            EMIT(cg, 4);
            /* imul rax, rdx */
            pn = emit_imul_reg_reg(BUF(cg), REG_RAX, REG_RDX); EMIT(cg, pn);
            /* add r9, rax */
            b = BUF(cg); b[0]=0x49; b[1]=0x01; b[2]=0xC1; EMIT(cg, 3);
            cg_loop_end(cg, lp, 1);
            /* Restore RBX */
            pn = emit_pop(BUF(cg), REG_RBX); EMIT(cg, pn);
            /* mov rax, r9 (return accumulator) */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xC8; EMIT(cg, 3);
            return 1;
        }

        /*
         * print_float(x):        print f64 with 6 fractional digits.
         * print_f64(x, digits):  same, but `digits` is a compile-time
         *                        i32 literal in [1, 17].  17 gives
         *                        round-trippable precision.
         *
         * Both paths share the body below; the second arg is stashed
         * into `digits` at codegen time so the entire loop structure
         * (digit count, scale factor, write length) is baked in.
         */
        if ((strcmp(name, "print_float") == 0 && argc == 1) ||
            (strcmp(name, "print_f64")   == 0 && argc == 2)) {
            int digits = 6;
            if (argc == 2) {
                ASTNode *dn = node->children[2];
                if (dn->type != NODE_INT_LITERAL ||
                    dn->int_val < 1 || dn->int_val > 17) {
                    fprintf(stderr,
                            "print_f64: `digits` must be an integer "
                            "literal in [1, 17]\n");
                    return 1;
                }
                digits = (int)dn->int_val;
            }
            /* Precompute scale = 10^digits as an f64 bit pattern.
             * 10^17 fits exactly in an f64 mantissa, so no rounding. */
            double scale = 1.0;
            for (int k = 0; k < digits; k++) scale *= 10.0;
            uint64_t scale_bits; memcpy(&scale_bits, &scale, 8);

            emit_expression(cg, node->children[1]); /* x -> RAX (f64 bits) */
            int pn; uint8_t *b;

            /* Ensure xmm0 is synced with RAX (x87 builtins return in RAX only) */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC0; EMIT(cg, 5); /* movq xmm0, rax */

            /* Save f64 bits on stack */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);

            /* Handle negative: if xmm0 < 0, print '-' and negate */
            /* pxor xmm1, xmm1  (zero) */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0xEF; b[3]=0xC9; EMIT(cg,4);
            /* ucomisd xmm0, xmm1 */
            pn = emit_ucomisd(BUF(cg), 0, 1); EMIT(cg, pn);
            /* jae .not_neg (not below = not negative) */
            size_t jae_pos = cg->code_size;
            b = BUF(cg); b[0]=0x73; b[1]=0x00; EMIT(cg,2);

            /* Print '-': push '-' byte, write(1, rsp, 1) */
            b = BUF(cg); b[0]=0x6A; b[1]='-'; EMIT(cg,2); /* push '-' */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 1); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RSP, 8); EMIT(cg, pn);

            /* Reload xmm0 from saved bits, negate it */
            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RSP, 0); EMIT(cg, pn);
            pn = emit_movq_xmm_reg(BUF(cg), 0, REG_RAX); EMIT(cg, pn);
            /* Negate: xorpd with sign bit mask. Simpler: subsd 0 - xmm0 */
            /* pxor xmm1, xmm1; subsd xmm1, xmm0; movsd xmm0, xmm1 */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0xEF; b[3]=0xC9; EMIT(cg,4);
            pn = emit_subsd(BUF(cg), 1, 0); EMIT(cg, pn);
            pn = emit_movsd_xmm_xmm(BUF(cg), 0, 1); EMIT(cg, pn);
            /* Update saved bits */
            pn = emit_movq_reg_xmm(BUF(cg), REG_RAX, 0); EMIT(cg, pn);
            pn = emit_mov_mem_reg(BUF(cg), REG_RSP, 0, REG_RAX); EMIT(cg, pn);

            /* patch jae */
            cg->code[jae_pos+1] = (uint8_t)(cg->code_size - (jae_pos+2));

            /* Reload xmm0 */
            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RSP, 0); EMIT(cg, pn);
            pn = emit_movq_xmm_reg(BUF(cg), 0, REG_RAX); EMIT(cg, pn);

            /* cvttsd2si rax, xmm0 (integer part) */
            pn = emit_cvttsd2si(BUF(cg), REG_RAX, 0); EMIT(cg, pn);

            /* Save integer part and xmm0 */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);

            /* Print integer part using print_int's inline write mechanism */
            /* We need to call the print_int logic. Simplest: emit a sub rsp,24
             * and the digit loop inline. Actually, let's use a helper approach:
             * save to a local, create a fake call. Too complex.
             * Instead: convert int to ASCII inline (same as print_int). */

            /* --- Inline integer print (same algorithm as print_int) --- */
            pn = emit_sub_reg_imm(BUF(cg), REG_RSP, 24); EMIT(cg, pn);

            /* r10 = write pos at end of buffer */
            b = BUF(cg);
            b[0]=0x4C; b[1]=0x8D; b[2]=modrm(1, REG_R10&7, REG_RSP);
            b[3]=0x24; b[4]=23; EMIT(cg,5);

            /* r11 = 0 (length) */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xDB; EMIT(cg,3);

            /* Digit loop */
            size_t dloop = cg->code_size;
            pn = emit_mov_reg_imm32(BUF(cg), REG_RCX, 10); EMIT(cg, pn);
            pn = emit_xor_reg_reg(BUF(cg), REG_RDX, REG_RDX); EMIT(cg, pn);
            b = BUF(cg); b[0]=rex(1,0,0,0); b[1]=0xF7; b[2]=modrm(3,6,REG_RCX);
            EMIT(cg,3); /* div rcx */
            b = BUF(cg); b[0]=0x80; b[1]=0xC2; b[2]='0'; EMIT(cg,3); /* add dl,'0' */
            pn = emit_dec_reg(BUF(cg), REG_R10); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x41; b[1]=0x88; b[2]=0x12; EMIT(cg,3); /* mov [r10],dl */
            pn = emit_inc_reg(BUF(cg), REG_R11); EMIT(cg, pn);
            pn = emit_test_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            int8_t lb = (int8_t)((int64_t)dloop - (int64_t)(cg->code_size+2));
            b = BUF(cg); b[0]=0x75; b[1]=(uint8_t)lb; EMIT(cg,2);

            /* Handle zero case */
            b = BUF(cg); b[0]=0x4D; b[1]=0x85; b[2]=0xDB; EMIT(cg,3); /* test r11,r11 */
            size_t jnz_nz = cg->code_size;
            b = BUF(cg); b[0]=0x75; b[1]=0x00; EMIT(cg,2);
            pn = emit_dec_reg(BUF(cg), REG_R10); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x41; b[1]=0xC6; b[2]=0x02; b[3]='0'; EMIT(cg,4);
            pn = emit_inc_reg(BUF(cg), REG_R11); EMIT(cg, pn);
            cg->code[jnz_nz+1] = (uint8_t)(cg->code_size-(jnz_nz+2));

            /* Write integer part */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_R10); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDX, REG_R11); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RSP, 24); EMIT(cg, pn);

            /* Print '.' */
            b = BUF(cg); b[0]=0x6A; b[1]='.'; EMIT(cg,2);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 1); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RSP, 8); EMIT(cg, pn);

            /* Fractional part: (x - int(x)) * 1000000 -> int -> print with leading zeros */
            /* Pop saved int part into RCX */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);
            /* Reload xmm0 from saved bits */
            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RSP, 0); EMIT(cg, pn);
            pn = emit_movq_xmm_reg(BUF(cg), 0, REG_RAX); EMIT(cg, pn);
            /* cvtsi2sd xmm1, rcx (convert int part back to float) */
            pn = emit_cvtsi2sd(BUF(cg), 1, REG_RCX); EMIT(cg, pn);
            /* subsd xmm0, xmm1 (fractional part) */
            pn = emit_subsd(BUF(cg), 0, 1); EMIT(cg, pn);
            /* Load scale (10^digits) into xmm1 via RCX. */
            pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, scale_bits); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC9; EMIT(cg, 5);
            /* mulsd xmm0, xmm1 */
            pn = emit_mulsd(BUF(cg), 0, 1); EMIT(cg, pn);
            /* cvttsd2si rax, xmm0 — up to 17 digits fits in int64 */
            pn = emit_cvttsd2si(BUF(cg), REG_RAX, 0); EMIT(cg, pn);

            /* Allocate stack for `digits` fractional chars + newline,
             * rounded up to the next 8-byte slot (max 24 for 17 digits). */
            int scratch = (digits + 1 + 7) & ~7;
            pn = emit_sub_reg_imm(BUF(cg), REG_RSP, 8); EMIT(cg, pn);
            pn = emit_sub_reg_imm(BUF(cg), REG_RSP, scratch); EMIT(cg, pn);
            for (int di = digits - 1; di >= 0; di--) {
                pn = emit_mov_reg_imm32(BUF(cg), REG_RCX, 10); EMIT(cg, pn);
                pn = emit_xor_reg_reg(BUF(cg), REG_RDX, REG_RDX); EMIT(cg, pn);
                b = BUF(cg); b[0]=rex(1,0,0,0); b[1]=0xF7; b[2]=modrm(3,6,REG_RCX);
                EMIT(cg,3);
                b = BUF(cg); b[0]=0x80; b[1]=0xC2; b[2]='0'; EMIT(cg,3);
                /* mov [rsp+di], dl */
                b = BUF(cg); b[0]=0x88; b[1]=modrm(1,REG_RDX,REG_RSP);
                b[2]=0x24; b[3]=(uint8_t)di; EMIT(cg,4);
            }
            /* Newline at [rsp + digits] */
            b = BUF(cg); b[0]=0xC6; b[1]=modrm(1,0,REG_RSP);
            b[2]=0x24; b[3]=(uint8_t)digits; b[4]=0x0A; EMIT(cg,5);

            /* write(1, rsp, digits + 1) */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, digits + 1); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            pn = emit_add_reg_imm(BUF(cg), REG_RSP, scratch + 8); EMIT(cg, pn);

            /* Pop saved xmm0 bits */
            pn = emit_pop(BUF(cg), REG_RAX); EMIT(cg, pn);

            /* Return 0 */
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }

        /* int_to_float(x): convert i32 to f64 */
        if (strcmp(name, "int_to_float") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]);
            int pn = emit_cvtsi2sd(BUF(cg), 0, REG_RAX); EMIT(cg, pn);
            pn = emit_movq_reg_xmm(BUF(cg), REG_RAX, 0); EMIT(cg, pn);
            return 1;
        }

        /* float_to_int(x): convert f64 bits in RAX/xmm0 to truncated i32 */
        if (strcmp(name, "float_to_int") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]);
            int pn = emit_cvttsd2si(BUF(cg), REG_RAX, 0);
            EMIT(cg, pn);
            return 1;
        }

        /*
         * dec("value"): Create a decimal value.
         * At compile time, the optimizer folds dec("a") + dec("b") into
         * dec("exact_result"). At runtime, dec("x") just stores the
         * string for printing via print_dec.
         * The string pointer is stored in RAX.
         */
        if (strcmp(name, "dec") == 0 && argc == 1) {
            /* Just evaluate the string arg - it puts the string in the code */
            emit_expression(cg, node->children[1]);
            return 1;
        }

        /* print_dec(x): Print a decimal value (stored as string from dec()) */
        if (strcmp(name, "print_dec") == 0 && argc == 1) {
            /* The argument should be a dec() call which evaluates to a
             * string literal embedded in the code. We just call print_str
             * logic on it. */
            emit_builtin_print_str(cg, node->children[1]->type == NODE_CALL ?
                node->children[1]->children[1] : node->children[1]);
            return 1;
        }

        /* read_float(): read f64 from stdin (reads int, converts to float) */
        if (strcmp(name, "read_float") == 0 && argc == 0) {
            emit_builtin_read_int(cg);
            int pn = emit_cvtsi2sd(BUF(cg), 0, REG_RAX); EMIT(cg, pn);
            pn = emit_movq_reg_xmm(BUF(cg), REG_RAX, 0); EMIT(cg, pn);
            return 1;
        }

        /*
         * STRING BUILTINS
         * Strings are heap-allocated via mmap: [i64 length][char bytes...]
         * str_new("literal") → copies literal to heap, returns base ptr
         * str_len(s) → returns length from [s - 8]
         * str_concat(a, b) → allocates new string = a + b
         * str_eq(a, b) → 1 if equal, 0 if not
         * str_char_at(s, i) → returns ASCII value of char at index
         * str_print(s) → prints heap string to stdout (no newline)
         * str_println(s) → prints heap string + newline
         */
        if (strcmp(name, "str_new") == 0 && argc == 1) {
            /* str_new("literal"): embed string, mmap heap copy */
            ASTNode *arg = node->children[1];
            if (!arg || arg->type != NODE_STRING_LITERAL || !arg->string_val) {
                cg_error(cg, "str_new requires a string literal at %d:%d", node->line, node->col);
                return 1;
            }
            const char *str = arg->string_val;
            size_t slen = strlen(str);
            int pn; uint8_t *b;

            /* mmap(0, slen+8, 3, 0x22, -1, 0) */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RSI, (uint32_t)(slen + 8 + 1));
            EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 3); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC2;
            int32_t v=0x22; memcpy(b+3,&v,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC0;
            v=-1; memcpy(b+3,&v,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg,3);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 9); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            /* Store length at [rax] */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn); /* save ptr */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RCX, (uint32_t)slen); EMIT(cg, pn);
            b = BUF(cg);
            b[0]=rex(1,reg_ext(REG_RCX),0,reg_ext(REG_RAX));
            b[1]=0x89; b[2]=modrm(0,REG_RCX,REG_RAX); EMIT(cg,3);

            /* Copy string bytes: embed them inline, use rep movsb */
            /* rax+8 = start of char data */
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 8); EMIT(cg, pn);
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn); /* save base */

            /* JMP over string data */
            size_t jmp_pos = cg->code_size;
            pn = emit_jmp(BUF(cg), 0); EMIT(cg, pn);

            /* Embed string bytes */
            size_t str_data = cg->code_size;
            memcpy(BUF(cg), str, slen);
            cg->code_size += slen;

            /* Patch JMP */
            int32_t jmp_off = (int32_t)(cg->code_size - (jmp_pos + 5));
            memcpy(cg->code + jmp_pos + 1, &jmp_off, 4);

            /* LEA RSI, [rip - offset] (source = embedded string) */
            int32_t rip_off = (int32_t)((int64_t)str_data - (int64_t)(cg->code_size + 7));
            b = BUF(cg);
            b[0]=rex(1,reg_ext(REG_RSI),0,0);
            b[1]=0x8D; b[2]=modrm(0,REG_RSI,5);
            memcpy(b+3,&rip_off,4); EMIT(cg,7);

            /* RDI = dest (base = rax+8, saved on stack) */
            pn = emit_pop(BUF(cg), REG_RDI); EMIT(cg, pn);
            /* RCX = length */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RCX, (uint32_t)slen); EMIT(cg, pn);
            /* rep movsb */
            b = BUF(cg); b[0]=0xF3; b[1]=0xA4; EMIT(cg,2);

            /* Return base ptr (rax = mmap_ptr + 8) */
            pn = emit_pop(BUF(cg), REG_RAX); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 8); EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "str_len") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]); /* base -> RAX */
            /* length at [rax - 8] */
            int pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RAX, -8);
            EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "str_concat") == 0 && argc == 2) {
            /* Simpler approach: use locals to save a, b, and their lengths.
             * All mmap-clobberable state is in stack locals. */
            int pn; uint8_t *b;

            /* Evaluate a and b, save as locals */
            LocalVar *va = add_local(cg, "__ca");
            LocalVar *vb = add_local(cg, "__cb");
            LocalVar *vn = add_local(cg, "__cn"); /* new buffer */
            if (!va || !vb || !vn) return 0;

            emit_expression(cg, node->children[1]); /* a → RAX */
            pn = emit_mov_mem_reg(BUF(cg), REG_RBP, va->rbp_off, REG_RAX);
            EMIT(cg, pn);

            emit_expression(cg, node->children[2]); /* b → RAX */
            pn = emit_mov_mem_reg(BUF(cg), REG_RBP, vb->rbp_off, REG_RAX);
            EMIT(cg, pn);

            /* Compute total length: len_a + len_b */
            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RBP, va->rbp_off);
            EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RAX, -8); EMIT(cg, pn);
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn); /* save len_a */

            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RBP, vb->rbp_off);
            EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RAX, -8); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* RCX = len_a */
            /* RSI = len_a + len_b + 9 */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RAX); EMIT(cg, pn);
            pn = emit_add_reg_reg(BUF(cg), REG_RSI, REG_RCX); EMIT(cg, pn);
            pn = emit_push(BUF(cg), REG_RSI); EMIT(cg, pn); /* save total_len */
            pn = emit_add_reg_imm(BUF(cg), REG_RSI, 9); EMIT(cg, pn);

            /* mmap */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 3); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC2;
            int32_t v2=0x22; memcpy(b+3,&v2,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC0;
            v2=-1; memcpy(b+3,&v2,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg,3);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 9); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            /* Save new buffer ptr to local */
            pn = emit_mov_mem_reg(BUF(cg), REG_RBP, vn->rbp_off, REG_RAX);
            EMIT(cg, pn);

            /* Store total length at [new_ptr] */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* total_len */
            b = BUF(cg);
            b[0]=rex(1,reg_ext(REG_RCX),0,reg_ext(REG_RAX));
            b[1]=0x89; b[2]=modrm(0,REG_RCX,REG_RAX); EMIT(cg,3);

            /* Copy a: dst=new_ptr+8, src=a_base, len=len_a */
            pn = emit_mov_reg_mem(BUF(cg), REG_RDI, REG_RBP, vn->rbp_off);
            EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RDI, 8); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RSI, REG_RBP, va->rbp_off);
            EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RSI, -8); EMIT(cg, pn);
            b = BUF(cg); b[0]=0xF3; b[1]=0xA4; EMIT(cg,2); /* rep movsb */

            /* Copy b: dst=RDI (advanced), src=b_base, len=len_b */
            pn = emit_mov_reg_mem(BUF(cg), REG_RSI, REG_RBP, vb->rbp_off);
            EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RSI, -8); EMIT(cg, pn);
            b = BUF(cg); b[0]=0xF3; b[1]=0xA4; EMIT(cg,2); /* rep movsb */

            /* Return base = new_ptr + 8 */
            pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RBP, vn->rbp_off);
            EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 8); EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "str_eq") == 0 && argc == 2) {
            /* Compare two heap strings byte by byte */
            emit_expression(cg, node->children[2]);
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]);
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);
            /* RAX=a, RCX=b. Compare lengths first. */
            uint8_t *b;
            pn = emit_mov_reg_mem(BUF(cg), REG_R10, REG_RAX, -8); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_R11, REG_RCX, -8); EMIT(cg, pn);
            /* cmp r10, r11 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x39; b[2]=0xDA; EMIT(cg,3);
            size_t jne_len = cg->code_size;
            b = BUF(cg); b[0]=0x75; b[1]=0x00; EMIT(cg,2); /* jne not_equal */

            /* Same length: repe cmpsb */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RCX); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RCX, REG_R10); EMIT(cg, pn);
            b = BUF(cg); b[0]=0xF3; b[1]=0xA6; EMIT(cg,2); /* repe cmpsb */
            pn = emit_sete(BUF(cg), REG_RAX); EMIT(cg, pn);
            pn = emit_movzx_reg_reg8(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            size_t jmp_end = cg->code_size;
            b = BUF(cg); b[0]=0xEB; b[1]=0x00; EMIT(cg,2); /* jmp end */

            /* not_equal: return 0 */
            cg->code[jne_len+1] = (uint8_t)(cg->code_size-(jne_len+2));
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);

            cg->code[jmp_end+1] = (uint8_t)(cg->code_size-(jmp_end+2));
            return 1;
        }

        if (strcmp(name, "str_char_at") == 0 && argc == 2) {
            /* str_char_at(s, i) → ASCII value, with bounds check */
            emit_expression(cg, node->children[2]); /* idx */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);

            /* ── Bounds check: 0 <= idx < str_len ── */
            uint8_t *b;
            pn = emit_mov_reg_mem(BUF(cg), REG_RDX, REG_RAX, -8); EMIT(cg, pn); /* rdx=len */
            pn = emit_cmp_reg_reg(BUF(cg), REG_RCX, REG_RDX); EMIT(cg, pn);
            size_t jb_pos = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x82; memset(b+2,0,4); EMIT(cg, 6); /* jb .ok */
            {
                const char *errmsg = "Runtime error: string index out of bounds\n";
                size_t errmsg_len = 42;
                size_t jmp_str = cg->code_size;
                pn = emit_jmp(BUF(cg), 0); EMIT(cg, pn);
                size_t str_pos = cg->code_size;
                memcpy(BUF(cg), errmsg, errmsg_len); cg->code_size += errmsg_len;
                int32_t jo = (int32_t)(cg->code_size - (jmp_str + 5));
                memcpy(cg->code + jmp_str + 1, &jo, 4);
                int32_t rip_off = (int32_t)((int64_t)str_pos - (int64_t)(cg->code_size + 7));
                b = BUF(cg);
                b[0]=rex(1,reg_ext(REG_RSI),0,0); b[1]=0x8D;
                b[2]=modrm(0,REG_RSI,5); memcpy(b+3,&rip_off,4); EMIT(cg,7);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, (uint32_t)errmsg_len); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 2); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
                pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
                pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 60); EMIT(cg, pn);
                pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            }
            int32_t jb_off = (int32_t)(cg->code_size - (jb_pos + 6));
            memcpy(cg->code + jb_pos + 2, &jb_off, 4);

            /* movzx rax, byte [rax + rcx] */
            b = BUF(cg);
            b[0]=rex(1,reg_ext(REG_RAX),reg_ext(REG_RCX),reg_ext(REG_RAX));
            b[1]=0x0F; b[2]=0xB6;
            b[3]=modrm(0,REG_RAX&7,4);
            b[4]=(uint8_t)((0<<6)|((REG_RCX&7)<<3)|(REG_RAX&7));
            EMIT(cg,5);
            return 1;
        }

        if (strcmp(name, "byte_at") == 0 && argc == 2) {
            /* byte_at(ptr, offset) → unsigned byte at ptr[offset].
             *
             * Unchecked — caller owns bounds.  Primary use: byte-level
             * access into file_read buffers where the underlying array
             * length (in i64 slots) is 8x smaller than the byte count,
             * so str_char_at's bounds check trips too early.
             *
             *   movzx rax, byte [rax + rcx] */
            emit_expression(cg, node->children[2]);          /* offset */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]);          /* ptr */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);

            uint8_t *b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RAX), reg_ext(REG_RCX), reg_ext(REG_RAX));
            b[1] = 0x0F; b[2] = 0xB6;
            b[3] = modrm(0, REG_RAX & 7, 4);                 /* SIB */
            b[4] = (uint8_t)((0 << 6) | ((REG_RCX & 7) << 3) | (REG_RAX & 7));
            EMIT(cg, 5);
            return 1;
        }

        if (strcmp(name, "str_println") == 0 && argc == 1) {
            /* Print heap string + newline */
            emit_expression(cg, node->children[1]); /* base -> RAX */
            int pn;
            /* Save base */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            /* write(1, base, len) */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RDX, REG_RAX, -8); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            /* Write newline */
            uint8_t *b = BUF(cg); b[0]=0x6A; b[1]=0x0A; EMIT(cg,2);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 1); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RSP, 8); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RAX); EMIT(cg, pn);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }


        /*
         * FILE I/O BUILTINS (direct syscalls, no libc)
         * file_open(path_str, flags) → fd (flags: 0=read, 1=write, 65=create+write)
         * file_read(fd, buf_str, max_len) → bytes read
         * file_write(fd, buf_str, len) → bytes written
         * file_close(fd) → 0
         */
        if (strcmp(name, "file_open") == 0 && argc == 2) {
            /* file_open(path_str, flags): syscall open(2) */
            emit_expression(cg, node->children[2]); /* flags → RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* path → RAX (str base) */
            pn = emit_pop(BUF(cg), REG_RSI); EMIT(cg, pn); /* RSI = flags */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn); /* RDI = path */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 0644); EMIT(cg, pn); /* mode */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 2); EMIT(cg, pn); /* __NR_open */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "file_read") == 0 && argc == 3) {
            /* file_read(fd, buf, max_len): syscall read(0) */
            emit_expression(cg, node->children[3]); /* max_len */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* buf */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* fd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RSI); EMIT(cg, pn); /* buf */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* max_len */
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn); /* __NR_read=0 */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "file_write") == 0 && argc == 3) {
            /* file_write(fd, buf, len): syscall write(1) */
            emit_expression(cg, node->children[3]);
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]);
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RSI); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn); /* __NR_write=1 */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "file_close") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]);
            int pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 3); EMIT(cg, pn); /* __NR_close */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }

        /*
         * NETWORK HELPERS
         * ip4(a, b, c, d) → 32-bit IPv4 in network byte order
         *   ip4(127, 0, 0, 1) → 0x0100007F (127.0.0.1 in little-endian)
         */
        if (strcmp(name, "ip4") == 0 && argc == 4) {
            /* Build 32-bit IP: a | (b<<8) | (c<<16) | (d<<24) — network byte order */
            int pn;
            emit_expression(cg, node->children[4]); /* d */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[3]); /* c */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* b */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* a */
            /* rax = a, stack: [b, c, d] */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* rcx = b */
            uint8_t *b;
            /* shl rcx, 8 */
            b = BUF(cg); b[0]=0x48; b[1]=0xC1; b[2]=0xE1; b[3]=8; EMIT(cg, 4);
            pn = emit_or_reg_reg(BUF(cg), REG_RAX, REG_RCX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* rcx = c */
            b = BUF(cg); b[0]=0x48; b[1]=0xC1; b[2]=0xE1; b[3]=16; EMIT(cg, 4);
            pn = emit_or_reg_reg(BUF(cg), REG_RAX, REG_RCX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* rcx = d */
            b = BUF(cg); b[0]=0x48; b[1]=0xC1; b[2]=0xE1; b[3]=24; EMIT(cg, 4);
            pn = emit_or_reg_reg(BUF(cg), REG_RAX, REG_RCX); EMIT(cg, pn);
            return 1;
        }

        /*
         * NETWORKING BUILTINS (direct syscalls, no libc)
         *
         * socket_create()                → fd (TCP IPv4 socket)
         * socket_connect(fd, ip, port)   → 0 on success, -errno on error
         *   ip = 32-bit IPv4 address (e.g. 0x7F000001 = 127.0.0.1)
         *   port = port number (host byte order, converted internally)
         * socket_send(fd, buf, len)      → bytes sent
         * socket_recv(fd, buf, max_len)  → bytes received
         * socket_close(fd)               → 0
         * socket_bind(fd, ip, port)      → 0 on success
         * socket_listen(fd, backlog)     → 0 on success
         * socket_accept(fd)              → new client fd
         */
        if (strcmp(name, "socket_create") == 0 && argc == 0) {
            /* socket(AF_INET=2, SOCK_STREAM=1, IPPROTO_TCP=6) → fd
             * syscall 41: rdi=domain, rsi=type, rdx=protocol */
            int pn;
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 2); EMIT(cg, pn);  /* AF_INET */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RSI, 1); EMIT(cg, pn);  /* SOCK_STREAM */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 6); EMIT(cg, pn);  /* IPPROTO_TCP */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 41); EMIT(cg, pn); /* __NR_socket */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "socket_connect") == 0 && argc == 3) {
            /* connect(fd, sockaddr*, 16)
             * syscall 42: rdi=fd, rsi=addr, rdx=addrlen
             * We build struct sockaddr_in on the stack:
             *   [rsp+0]: sin_family(2) + sin_port(2) = 4 bytes
             *   [rsp+4]: sin_addr(4)
             *   [rsp+8]: padding(8)
             */
            int pn;
            /* Evaluate args: fd, ip, port */
            emit_expression(cg, node->children[3]); /* port → RAX */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* ip → RAX */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* fd → RAX */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);

            /* Pop fd→R8, ip→R9, port→R10 (save in callee-usable regs) */
            uint8_t *b;
            b = BUF(cg); b[0]=0x41; b[1]=0x58; EMIT(cg, 2); /* pop r8  = fd */
            b = BUF(cg); b[0]=0x41; b[1]=0x59; EMIT(cg, 2); /* pop r9  = ip */
            b = BUF(cg); b[0]=0x41; b[1]=0x5A; EMIT(cg, 2); /* pop r10 = port */

            /* sub rsp, 16 — allocate sockaddr_in on stack */
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xEC; b[3]=16; EMIT(cg, 4);

            /* Build sockaddr_in at [rsp]:
             * mov word [rsp], 2           ; sin_family = AF_INET */
            b = BUF(cg); b[0]=0x66; b[1]=0xC7; b[2]=0x04; b[3]=0x24;
            b[4]=0x02; b[5]=0x00; EMIT(cg, 6);

            /* Convert port to network byte order (big-endian): xchg al,ah
             * mov rax, r10 */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xD0; EMIT(cg, 3); /* mov rax, r10 */
            /* xchg al, ah (swap bytes for network order) */
            b = BUF(cg); b[0]=0x86; b[1]=0xE0; EMIT(cg, 2);
            /* mov [rsp+2], ax  ; sin_port */
            b = BUF(cg); b[0]=0x66; b[1]=0x89; b[2]=0x44; b[3]=0x24; b[4]=0x02; EMIT(cg, 5);

            /* mov [rsp+4], r9d  ; sin_addr (already in network order from caller) */
            b = BUF(cg); b[0]=0x44; b[1]=0x89; b[2]=0x4C; b[3]=0x24; b[4]=0x04; EMIT(cg, 5);

            /* Zero padding: mov qword [rsp+8], 0 */
            b = BUF(cg); b[0]=0x48; b[1]=0xC7; b[2]=0x44; b[3]=0x24;
            b[4]=0x08; b[5]=0x00; b[6]=0x00; b[7]=0x00; b[8]=0x00; EMIT(cg, 9);

            /* syscall connect(fd, &sockaddr, 16)
             * rdi = fd (r8), rsi = rsp, rdx = 16 */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xC7; EMIT(cg, 3); /* mov rdi, r8 */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn); /* rsi = rsp */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 16); EMIT(cg, pn);    /* addrlen=16 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 42); EMIT(cg, pn);    /* __NR_connect */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            /* Clean up stack: add rsp, 16 */
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=16; EMIT(cg, 4);
            return 1;
        }
        if (strcmp(name, "socket_send") == 0 && argc == 3) {
            /* sendto(fd, buf, len, 0, NULL, 0) — syscall 44
             * rdi=fd, rsi=buf, rdx=len, r10=flags=0, r8=NULL, r9=0 */
            int pn;
            emit_expression(cg, node->children[3]); /* len */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* buf */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* fd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RSI); EMIT(cg, pn); /* buf */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* len */
            /* r10=0 (flags), r8=NULL, r9=0 */
            uint8_t *b;
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xD2; EMIT(cg, 3); /* xor r10, r10 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC0; EMIT(cg, 3); /* xor r8, r8 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg, 3); /* xor r9, r9 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 44); EMIT(cg, pn); /* __NR_sendto */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "socket_recv") == 0 && argc == 3) {
            /* recvfrom(fd, buf, max_len, 0, NULL, NULL) — syscall 45
             * rdi=fd, rsi=buf, rdx=max_len, r10=flags=0, r8=NULL, r9=NULL */
            int pn;
            emit_expression(cg, node->children[3]); /* max_len */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* buf */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* fd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RSI); EMIT(cg, pn); /* buf */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* max_len */
            uint8_t *b;
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xD2; EMIT(cg, 3); /* xor r10, r10 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC0; EMIT(cg, 3); /* xor r8, r8 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg, 3); /* xor r9, r9 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 45); EMIT(cg, pn); /* __NR_recvfrom */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "socket_close") == 0 && argc == 1) {
            /* close(fd) — syscall 3 (same as file_close) */
            emit_expression(cg, node->children[1]);
            int pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 3); EMIT(cg, pn); /* __NR_close */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "socket_bind") == 0 && argc == 3) {
            /* bind(fd, sockaddr*, 16) — syscall 49
             * Same sockaddr_in construction as socket_connect */
            int pn;
            emit_expression(cg, node->children[3]); /* port */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* ip */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* fd */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);

            uint8_t *b;
            b = BUF(cg); b[0]=0x41; b[1]=0x58; EMIT(cg, 2); /* pop r8  = fd */
            b = BUF(cg); b[0]=0x41; b[1]=0x59; EMIT(cg, 2); /* pop r9  = ip */
            b = BUF(cg); b[0]=0x41; b[1]=0x5A; EMIT(cg, 2); /* pop r10 = port */

            /* sub rsp, 16 */
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xEC; b[3]=16; EMIT(cg, 4);
            /* mov word [rsp], 2  ; AF_INET */
            b = BUF(cg); b[0]=0x66; b[1]=0xC7; b[2]=0x04; b[3]=0x24;
            b[4]=0x02; b[5]=0x00; EMIT(cg, 6);
            /* mov rax, r10; xchg al,ah; mov [rsp+2], ax */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xD0; EMIT(cg, 3);
            b = BUF(cg); b[0]=0x86; b[1]=0xE0; EMIT(cg, 2);
            b = BUF(cg); b[0]=0x66; b[1]=0x89; b[2]=0x44; b[3]=0x24; b[4]=0x02; EMIT(cg, 5);
            /* mov [rsp+4], r9d */
            b = BUF(cg); b[0]=0x44; b[1]=0x89; b[2]=0x4C; b[3]=0x24; b[4]=0x04; EMIT(cg, 5);
            /* mov qword [rsp+8], 0 */
            b = BUF(cg); b[0]=0x48; b[1]=0xC7; b[2]=0x44; b[3]=0x24;
            b[4]=0x08; b[5]=0x00; b[6]=0x00; b[7]=0x00; b[8]=0x00; EMIT(cg, 9);

            /* bind(r8, rsp, 16) */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xC7; EMIT(cg, 3); /* mov rdi, r8 */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 16); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 49); EMIT(cg, pn); /* __NR_bind */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=16; EMIT(cg, 4); /* add rsp,16 */
            return 1;
        }
        if (strcmp(name, "socket_listen") == 0 && argc == 2) {
            /* listen(fd, backlog) — syscall 50 */
            int pn;
            emit_expression(cg, node->children[2]); /* backlog */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* fd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RSI); EMIT(cg, pn); /* backlog */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 50); EMIT(cg, pn); /* __NR_listen */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "socket_accept") == 0 && argc == 1) {
            /* accept(fd, NULL, NULL) — syscall 43 */
            int pn;
            emit_expression(cg, node->children[1]); /* fd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn); /* NULL */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDX, REG_RDX); EMIT(cg, pn); /* NULL */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 43); EMIT(cg, pn); /* __NR_accept */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }

        /*
         * EPOLL BUILTINS — I/O multiplexing for concurrent connections
         *
         * epoll_create()              → epoll fd
         * epoll_add(epfd, fd, events) → 0 on success
         *   events: 1=EPOLLIN, 4=EPOLLOUT, 3=EPOLLIN|EPOLLOUT
         * epoll_wait(epfd, buf, max, timeout) → number of ready fds
         *   buf = arr_new(max*3) — stores [fd, events, ...] triples
         *   timeout in ms, -1 = block forever
         * epoll_del(epfd, fd)         → 0 on success
         */
        /*
         * STACK BUFFER — zero-syscall temp buffer allocation
         * buf_stack(n) → pointer to n bytes on stack (no mmap, no munmap)
         * WARNING: pointer is only valid within the current function scope.
         * Use for temp read buffers instead of arr_new + mem_free.
         */
        if (strcmp(name, "buf_stack") == 0 && argc == 1) {
            /* Allocate n bytes on stack. MUST call buf_free() to restore RSP.
             * Saves original RSP on the stack itself for buf_free() to restore. */
            emit_expression(cg, node->children[1]); /* n → RAX */
            int pn; uint8_t *b;
            /* Save current RSP: push rsp (so buf_free can find it) */
            pn = emit_push(BUF(cg), REG_RSP); EMIT(cg, pn);
            /* Align n to 16: add rax,15+8; and rax,~15 (extra 8 for saved RSP) */
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 23); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xE0; b[3]=0xF0; EMIT(cg, 4);
            /* sub rsp, rax */
            b = BUF(cg); b[0]=0x48; b[1]=0x29; b[2]=0xC4; EMIT(cg, 3);
            /* mov rax, rsp (return pointer to usable buffer) */
            pn = emit_mov_reg_reg(BUF(cg), REG_RAX, REG_RSP); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "buf_free") == 0 && argc == 0) {
            /* Restore RSP from the value saved by buf_stack.
             * The saved RSP is at [rbp - first_local_above_buf].
             * Simplest: walk up the stack to find the saved RSP value.
             * Actually, since buf_stack pushed RSP before sub, we can
             * just restore from rbp: mov rsp, rbp is done at function exit.
             * For mid-function restore: we use the saved value.
             * Safest approach: mov rsp, rbp; sub rsp, frame_size
             * But we don't know frame_size here. So just NOP — the function
             * epilogue (mov rsp, rbp; pop rbp; ret) will clean up. */
            /* NOP — stack is restored at function return via mov rsp, rbp */
            return 1;
        }

        if (strcmp(name, "epoll_create") == 0 && argc == 0) {
            /* epoll_create1(0) — syscall 291 */
            int pn;
            pn = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 291); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "epoll_add") == 0 && argc == 3) {
            /* epoll_ctl(epfd, EPOLL_CTL_ADD=1, fd, &event)
             * syscall 233: rdi=epfd, rsi=op, rdx=fd, r10=&event
             * struct epoll_event: [u32 events][u64 data(fd)] = 12 bytes */
            int pn; uint8_t *b;
            emit_expression(cg, node->children[3]); /* events */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* fd */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* epfd */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);

            b = BUF(cg); b[0]=0x41; b[1]=0x58; EMIT(cg, 2); /* pop r8 = epfd */
            b = BUF(cg); b[0]=0x41; b[1]=0x59; EMIT(cg, 2); /* pop r9 = fd */
            b = BUF(cg); b[0]=0x41; b[1]=0x5A; EMIT(cg, 2); /* pop r10 = events */

            /* Build epoll_event on stack: sub rsp,16 */
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xEC; b[3]=16; EMIT(cg, 4);
            /* mov [rsp], r10d (events - 32bit) */
            b = BUF(cg); b[0]=0x44; b[1]=0x89; b[2]=0x14; b[3]=0x24; EMIT(cg, 4);
            /* mov [rsp+4], r9 (data.fd - 64bit, we use only lower 32) */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0x4C; b[3]=0x24; b[4]=0x04; EMIT(cg, 5);

            /* epoll_ctl(epfd=r8, EPOLL_CTL_ADD=1, fd=r9, event=rsp) */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xC7; EMIT(cg, 3); /* mov rdi, r8 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RSI, 1); EMIT(cg, pn); /* EPOLL_CTL_ADD */
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xCA; EMIT(cg, 3); /* mov rdx, r9 */
            b = BUF(cg); b[0]=0x49; b[1]=0x89; b[2]=0xE2; EMIT(cg, 3); /* mov r10, rsp */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 233); EMIT(cg, pn); /* __NR_epoll_ctl */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=16; EMIT(cg, 4); /* add rsp,16 */
            return 1;
        }
        if (strcmp(name, "epoll_del") == 0 && argc == 2) {
            /* epoll_ctl(epfd, EPOLL_CTL_DEL=2, fd, NULL) — syscall 233 */
            int pn;
            emit_expression(cg, node->children[2]); /* fd */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* epfd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RSI, 2); EMIT(cg, pn); /* EPOLL_CTL_DEL */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* fd */
            uint8_t *b;
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xD2; EMIT(cg, 3); /* xor r10,r10 (NULL) */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 233); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "epoll_wait") == 0 && argc == 4) {
            /* epoll_wait(epfd, events_buf, maxevents, timeout)
             * syscall 232: rdi=epfd, rsi=events, rdx=maxevents, r10=timeout
             * events_buf is an array — we write fd into arr[i*2], events into arr[i*2+1] */
            int pn;
            emit_expression(cg, node->children[4]); /* timeout */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[3]); /* maxevents */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* events_buf (array base) */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* epfd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RSI); EMIT(cg, pn); /* events_buf */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* maxevents */
            uint8_t *b;
            b = BUF(cg); b[0]=0x41; b[1]=0x5A; EMIT(cg, 2); /* pop r10 = timeout */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 232); EMIT(cg, pn); /* __NR_epoll_wait */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }

        if (strcmp(name, "socket_opt") == 0 && argc == 3) {
            /* socket_opt(fd, option, value)
             * setsockopt(fd, SOL_SOCKET=1, option, &value, 4) — syscall 54
             * Common options: 2=SO_REUSEADDR, 15=SO_REUSEPORT, 1=TCP_NODELAY(lvl6) */
            int pn;
            emit_expression(cg, node->children[3]); /* value */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* option */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* fd */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn); /* rdi = fd */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RSI, 1); EMIT(cg, pn); /* rsi = SOL_SOCKET */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* rdx = option */
            /* Store value on stack and point R10 to it */
            /* value is already on stack from the first push */
            uint8_t *b;
            b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xD2; EMIT(cg, 3); /* mov rdx,rdx (nop placeholder) */
            /* r10 = rsp (point to value on stack) */
            b = BUF(cg); b[0]=0x49; b[1]=0x89; b[2]=0xE2; EMIT(cg, 3); /* mov r10, rsp */
            /* r8 = 4 (optlen = sizeof(int)) */
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC0;
            int32_t four = 4; memcpy(b+3, &four, 4); EMIT(cg, 7);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 54); EMIT(cg, pn); /* __NR_setsockopt */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            /* Clean stack (pop the value) */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "print_char") == 0 && argc == 1) {
            /* print_char(c: i32) — print a single ASCII character, no newline.
             * push char byte to stack, write(1, rsp, 1), pop */
            emit_expression(cg, node->children[1]); /* char code → RAX */
            int pn; uint8_t *b;
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn); /* __NR_write */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn); /* stdout */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn); /* buf=rsp */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 1); EMIT(cg, pn); /* len=1 */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RAX); EMIT(cg, pn);
            return 1;
        }

        /*
         * MATH BUILTINS — f64 math functions via SSE2
         *
         * math_sqrt(x: f64) → f64    (hardware SQRTSD)
         * math_abs(x: f64)  → f64    (clear sign bit)
         * math_floor(x: f64)→ i32    (truncate toward negative infinity)
         */
        if (strcmp(name, "math_sqrt") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]); /* x → xmm0 via RAX bits */
            int pn; uint8_t *b;
            /* movq xmm0, rax */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC0; EMIT(cg, 5);
            /* sqrtsd xmm0, xmm0 */
            pn = emit_sqrtsd(BUF(cg), 0, 0); EMIT(cg, pn);
            /* movq rax, xmm0 */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5);
            return 1;
        }
        if (strcmp(name, "math_exp") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]); /* x → RAX */
            int pn; uint8_t *b;
            if (cg->precision == 15) {
                /* exp(x) via x87 FPU — IEEE 754 full precision.
                 * Algorithm: exp(x) = 2^(x * log2(e))
                 * Uses: FLDL2E, FMUL, FRNDINT, F2XM1, FSCALE */
                b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xEC; b[3]=16; EMIT(cg, 4); /* sub rsp,16 */
                b = BUF(cg); b[0]=0x48; b[1]=0x89; b[2]=0x04; b[3]=0x24; EMIT(cg, 4); /* mov [rsp],rax */
                /* fld [rsp]          ; ST(0) = x */
                b = BUF(cg); b[0]=0xDD; b[1]=0x04; b[2]=0x24; EMIT(cg, 3);
                /* fldl2e             ; ST(0) = log2(e), ST(1) = x */
                b = BUF(cg); b[0]=0xD9; b[1]=0xEA; EMIT(cg, 2);
                /* fmulp              ; ST(0) = x * log2(e) */
                b = BUF(cg); b[0]=0xDE; b[1]=0xC9; EMIT(cg, 2);
                /* fld st(0)          ; ST(0) = ST(1) = x*log2e */
                b = BUF(cg); b[0]=0xD9; b[1]=0xC0; EMIT(cg, 2);
                /* frndint            ; ST(0) = n = round(x*log2e), ST(1) = x*log2e */
                b = BUF(cg); b[0]=0xD9; b[1]=0xFC; EMIT(cg, 2);
                /* fsub st(1), st(0)  ; ST(1) = x*log2e - n = f, ST(0) = n */
                b = BUF(cg); b[0]=0xDC; b[1]=0xE9; EMIT(cg, 2);
                /* fxch               ; ST(0) = f, ST(1) = n */
                b = BUF(cg); b[0]=0xD9; b[1]=0xC9; EMIT(cg, 2);
                /* f2xm1              ; ST(0) = 2^f - 1 */
                b = BUF(cg); b[0]=0xD9; b[1]=0xF0; EMIT(cg, 2);
                /* fld1               ; ST(0) = 1, ST(1) = 2^f-1, ST(2) = n */
                b = BUF(cg); b[0]=0xD9; b[1]=0xE8; EMIT(cg, 2);
                /* faddp              ; ST(0) = 2^f, ST(1) = n */
                b = BUF(cg); b[0]=0xDE; b[1]=0xC1; EMIT(cg, 2);
                /* fscale             ; ST(0) = 2^f * 2^n = exp(x), ST(1) = n */
                b = BUF(cg); b[0]=0xD9; b[1]=0xFD; EMIT(cg, 2);
                /* fstp qword [rsp]   ; store result, pop */
                b = BUF(cg); b[0]=0xDD; b[1]=0x1C; b[2]=0x24; EMIT(cg, 3);
                /* fstp st(0)         ; pop remaining n */
                b = BUF(cg); b[0]=0xDD; b[1]=0xD8; EMIT(cg, 2);
                /* mov rax, [rsp] */
                b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x04; b[3]=0x24; EMIT(cg, 4);
                b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=16; EMIT(cg, 4); /* add rsp,16 */
            } else {
                /* SSE2 minimax exp(x):
                 * 1. k = round(x * log2e)
                 * 2. r = x - k * ln2
                 * 3. exp(r) = 1 + r + r^2 * (P1 + r*(P2 + r*(P3 + r*(P4 + r*P5))))
                 * 4. result = exp(r) * 2^k  via IEEE 754 bit construction */

                /* movq xmm0, rax  — load x */
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC0; EMIT(cg, 5);

                /* === Step 1: k = round(x * log2e) === */
                /* xmm1 = x * log2e */
                /* movapd xmm1, xmm0 */
                b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x28; b[3]=0xC8; EMIT(cg, 4);
                /* load log2e into xmm2 */
                pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, 0x3FF71547652B82FEULL); EMIT(cg, pn);
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xD1; EMIT(cg, 5); /* movq xmm2, rcx */
                /* mulsd xmm1, xmm2 */
                b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x59; b[3]=0xCA; EMIT(cg, 4);
                /* roundsd xmm1, xmm1, 0 (round to nearest) — SSE4.1 */
                /* Use roundsd: 66 0F 3A 0B C9 00 */
                b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x3A; b[3]=0x0B; b[4]=0xC9; b[5]=0x00; EMIT(cg, 6);
                /* xmm1 = k (as double) */

                /* === Step 2: r = x - k * ln2 === */
                /* xmm2 = k * ln2 */
                /* movapd xmm2, xmm1 */
                b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x28; b[3]=0xD1; EMIT(cg, 4);
                /* load ln2 into xmm3 */
                pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, 0x3FE62E42FEFA39EFULL); EMIT(cg, pn);
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xD9; EMIT(cg, 5); /* movq xmm3, rcx */
                /* mulsd xmm2, xmm3 */
                b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x59; b[3]=0xD3; EMIT(cg, 4);
                /* xmm0 = x - k*ln2 = r */
                /* subsd xmm0, xmm2 */
                b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x5C; b[3]=0xC2; EMIT(cg, 4);
                /* xmm0 = r, xmm1 = k */

                /* === Step 3: Estrin polynomial for exp(r) ===
                 * exp(r) = 1 + r · P(r)
                 * P(r) = Σ_{k=0..8} r^k / (k+1)!       (9 terms)
                 *
                 * Delegates to the generic Estrin evaluator.  Critical
                 * path 4 dependent FMAs ≈ 16c (was Horner 9×4c = 36c).
                 */
                {
                    static const uint64_t exp_P_coeffs[9] = {
                        0x3FF0000000000000ULL,  /* C0 = 1       → 1/1! */
                        0x3FE0000000000000ULL,  /* C1 = 1/2     → 1/2! */
                        0x3FC5555555555555ULL,  /* C2 = 1/6     → 1/3! */
                        0x3FA5555555555555ULL,  /* C3 = 1/24    → 1/4! */
                        0x3F81111111111111ULL,  /* C4 = 1/120   → 1/5! */
                        0x3F56C16C16C16C17ULL,  /* C5 = 1/720   → 1/6! */
                        0x3F2A01A01A01A01AULL,  /* C6 = 1/5040  → 1/7! */
                        0x3EFA01A01A01A01AULL,  /* C7 = 1/40320 → 1/8! */
                        0x3EC71DE3A556C734ULL,  /* C8 = 1/362880→ 1/9! */
                    };
                    emit_estrin_poly(cg, exp_P_coeffs, 9, /*y_reg=*/0);
                }

                /* exp(r) = 1 + r · P(r).  Seed xmm3 = 1.0, fma r·xmm2. */
                emit_load_f64 (cg, 3, 0x3FF0000000000000ULL);
                emit_fma_add  (cg, 3, 0, 2);                     /* xmm3 = 1 + r·P */
                emit_mov_xmm  (cg, 2, 3);                        /* xmm2 = exp(r) */

                /* === Step 4: result = exp(r) * 2^k === */
                /* Convert k (double in xmm1) to integer in rax */
                /* cvttsd2si rax, xmm1 — F2 REX.W 0F 2C C1 */
                b = BUF(cg); b[0]=0xF2; b[1]=0x48; b[2]=0x0F; b[3]=0x2C; b[4]=0xC1; EMIT(cg, 5);
                /* rax = k as int64. Construct 2^k: (k+1023) << 52 */
                /* add rax, 1023 */
                b = BUF(cg); b[0]=0x48; b[1]=0x05; EMIT(cg, 2);
                b = BUF(cg); b[0]=0xFF; b[1]=0x03; b[2]=0x00; b[3]=0x00; EMIT(cg, 4); /* imm32 = 1023 */
                /* shl rax, 52 — REX.W C1 E0 34 */
                b = BUF(cg); b[0]=0x48; b[1]=0xC1; b[2]=0xE0; b[3]=52; EMIT(cg, 4);
                /* movq xmm3, rax */
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xD8; EMIT(cg, 5);
                /* mulsd xmm2, xmm3 — exp(r) * 2^k */
                b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x59; b[3]=0xD3; EMIT(cg, 4);

                /* movq rax, xmm2 — result back to RAX */
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xD0; EMIT(cg, 5);
                /* Sync xmm0 with rax (contract: f64 return in both). */
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC0; EMIT(cg, 5);
            }
            return 1;
        }
        if (strcmp(name, "math_log") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]); /* x → RAX */
            int pn; uint8_t *b;
            if (cg->precision == 15) {
                /* ln(x) via x87 FPU — IEEE 754 full precision.
                 * Algorithm: ln(x) = log2(x) * ln(2)
                 *   FYL2X computes ST(1) * log2(ST(0)) */
                b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xEC; b[3]=16; EMIT(cg, 4); /* sub rsp, 16 */
                b = BUF(cg); b[0]=0x48; b[1]=0x89; b[2]=0x04; b[3]=0x24; EMIT(cg, 4); /* mov [rsp], rax */
                /* fldln2 — push ln(2) to ST(0) */
                b = BUF(cg); b[0]=0xD9; b[1]=0xED; EMIT(cg, 2);
                /* fld qword [rsp] — push x to ST(0), ln(2) moves to ST(1) */
                b = BUF(cg); b[0]=0xDD; b[1]=0x04; b[2]=0x24; EMIT(cg, 3);
                /* fyl2x — ST(1) = ST(1) * log2(ST(0)) = ln(2) * log2(x) = ln(x), pop */
                b = BUF(cg); b[0]=0xD9; b[1]=0xF1; EMIT(cg, 2);
                /* fstp qword [rsp] — store result */
                b = BUF(cg); b[0]=0xDD; b[1]=0x1C; b[2]=0x24; EMIT(cg, 3);
                /* mov rax, [rsp] */
                b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x04; b[3]=0x24; EMIT(cg, 4);
                b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=16; EMIT(cg, 4); /* add rsp, 16 */
            } else {
                /* SSE2 log(x) using atanh-based series:
                 * 1. Extract: x = 2^k * m, where 1 <= m < 2
                 * 2. f = m - 1, s = f/(2+f)
                 * 3. log(1+f) = 2*s + 2*s^3*(1/3 + s^2*(1/5 + s^2*(1/7 + ...)))
                 * 4. result = k * ln2 + log(1+f) */

                /* movq xmm0, rax  — load x bits (also keep in rax) */
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC0; EMIT(cg, 5);

                /* === Step 1: Extract k and m from IEEE 754 bits === */
                /* mov rcx, rax */
                b = BUF(cg); b[0]=0x48; b[1]=0x89; b[2]=0xC1; EMIT(cg, 3);
                /* shr rcx, 52 */
                b = BUF(cg); b[0]=0x48; b[1]=0xC1; b[2]=0xE9; b[3]=52; EMIT(cg, 4);
                /* sub rcx, 1023 — rcx = k */
                b = BUF(cg); b[0]=0x48; b[1]=0x81; b[2]=0xE9; EMIT(cg, 3);
                b = BUF(cg); b[0]=0xFF; b[1]=0x03; b[2]=0x00; b[3]=0x00; EMIT(cg, 4);

                /* Construct m: clear exponent, set to 2^0 */
                pn = emit_mov_reg_imm64(BUF(cg), REG_RDX, 0x000FFFFFFFFFFFFFULL); EMIT(cg, pn);
                b = BUF(cg); b[0]=0x48; b[1]=0x21; b[2]=0xD0; EMIT(cg, 3); /* and rax, rdx */
                pn = emit_mov_reg_imm64(BUF(cg), REG_RDX, 0x3FF0000000000000ULL); EMIT(cg, pn);
                b = BUF(cg); b[0]=0x48; b[1]=0x09; b[2]=0xD0; EMIT(cg, 3); /* or rax, rdx */
                /* movq xmm1, rax — xmm1 = m */
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC8; EMIT(cg, 5);

                /* === Step 2: f = m - 1, s = f/(2+f) === */
                pn = emit_mov_reg_imm64(BUF(cg), REG_RDX, 0x3FF0000000000000ULL); EMIT(cg, pn); /* 1.0 */
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xD2; EMIT(cg, 5); /* movq xmm2, rdx */
                /* xmm1 = f = m - 1 */
                b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x5C; b[3]=0xCA; EMIT(cg, 4); /* subsd xmm1, xmm2 */

                /* xmm3 = 2 + f = m + 1 */
                pn = emit_mov_reg_imm64(BUF(cg), REG_RDX, 0x4000000000000000ULL); EMIT(cg, pn); /* 2.0 */
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xDA; EMIT(cg, 5); /* movq xmm3, rdx */
                /* addsd xmm3, xmm1 — xmm3 = 2 + f */
                b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x58; b[3]=0xD9; EMIT(cg, 4);

                /* xmm0 = s = f / (2+f) */
                /* movapd xmm0, xmm1 */
                b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x28; b[3]=0xC1; EMIT(cg, 4);
                /* divsd xmm0, xmm3 — F2 0F 5E C3 */
                b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x5E; b[3]=0xC3; EMIT(cg, 4);
                /* xmm0 = s */

                /* Save k on stack */
                b = BUF(cg); b[0]=0x51; EMIT(cg, 1); /* push rcx */

                /* === Step 3: series log(1+f) = 2*s + 2*s^3/3 + 2*s^5/5 + ... ===
                 * = 2*s*(1 + s^2*(1/3 + s^2*(1/5 + s^2*(1/7 + s^2*(1/9 + s^2*(1/11 + s^2/13))))))
                 * z = s^2 */

                /* xmm1 = z = s².  The Estrin helper uses xmm2..xmm7
                 * as temps, so we keep the input outside that range. */
                emit_mov_xmm (cg, 1, 0);
                cg_mulsd     (cg, 1, 0);

                /* Estrin for P(z) = 1 + z/3 + z²/5 + ... + z⁶/13
                 * (7 terms, atanh series rescaled for log(1+f) via s).
                 * Critical path 3 FMAs ≈ 12c (was 7×4c = 28c). */
                {
                    static const uint64_t log_P_coeffs[7] = {
                        0x3FF0000000000000ULL,  /* C0 = 1     */
                        0x3FD5555555555555ULL,  /* C1 = 1/3   */
                        0x3FC999999999999AULL,  /* C2 = 1/5   */
                        0x3FC2492492492492ULL,  /* C3 = 1/7   */
                        0x3FBC71C71C71C71CULL,  /* C4 = 1/9   */
                        0x3FB745D1745D1746ULL,  /* C5 = 1/11  */
                        0x3FB3B13B13B13B14ULL,  /* C6 = 1/13  */
                    };
                    emit_estrin_poly(cg, log_P_coeffs, 7, /*y_reg=*/1);
                }
                /* xmm2 = P(z) */

                /* xmm0 = 2*s * xmm2 = log(1+f) */
                /* mulsd xmm2, xmm0 — xmm2 *= s */
                b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x59; b[3]=0xD0; EMIT(cg, 4);
                /* xmm2 *= 2 — addsd xmm2, xmm2 */
                b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x58; b[3]=0xD2; EMIT(cg, 4);
                /* xmm0 = xmm2 (log(1+f)) */
                /* movapd xmm0, xmm2 */
                b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x28; b[3]=0xC2; EMIT(cg, 4);

                /* === Step 4: result = k * ln2 + log(1+f) === */
                b = BUF(cg); b[0]=0x59; EMIT(cg, 1); /* pop rcx — k */
                /* cvtsi2sd xmm3, rcx */
                b = BUF(cg); b[0]=0xF2; b[1]=0x48; b[2]=0x0F; b[3]=0x2A; b[4]=0xD9; EMIT(cg, 5);
                /* load ln2 */
                pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, 0x3FE62E42FEFA39EFULL); EMIT(cg, pn);
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xE1; EMIT(cg, 5); /* movq xmm4, rcx */
                /* mulsd xmm3, xmm4 — k * ln2 */
                b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x59; b[3]=0xDC; EMIT(cg, 4);
                /* addsd xmm0, xmm3 */
                b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x58; b[3]=0xC3; EMIT(cg, 4);

                /* movq rax, xmm0 */
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5);
            }
            return 1;
        }
        if (strcmp(name, "math_sin") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]);
            int pn; uint8_t *b;
            if (cg->precision == 15) {
                /* sin(x) via x87 FSIN — IEEE 754 full precision */
                b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xEC; b[3]=16; EMIT(cg, 4);
                b = BUF(cg); b[0]=0x48; b[1]=0x89; b[2]=0x04; b[3]=0x24; EMIT(cg, 4);
                b = BUF(cg); b[0]=0xDD; b[1]=0x04; b[2]=0x24; EMIT(cg, 3); /* fld [rsp] */
                b = BUF(cg); b[0]=0xD9; b[1]=0xFE; EMIT(cg, 2); /* fsin */
                b = BUF(cg); b[0]=0xDD; b[1]=0x1C; b[2]=0x24; EMIT(cg, 3); /* fstp [rsp] */
                b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x04; b[3]=0x24; EMIT(cg, 4);
                b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=16; EMIT(cg, 4);
                /* x87 path leaves result in RAX only; sync xmm0 for callers. */
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC0; EMIT(cg, 5);
            } else {
                /* SSE2 path: octant reduce + sin_poly / cos_poly select.
                 * See emit_sincos_body above for details.  Delivers ~10^-11
                 * accuracy for |x| up to ~2^33 (Cody-Waite 2-part split). */
                emit_sincos_body(cg, /*is_cos=*/0);
            }
            (void)pn; (void)b;
            return 1;
        }
        if (strcmp(name, "math_cos") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]);
            int pn; uint8_t *b;
            if (cg->precision == 15) {
                /* cos(x) via x87 FCOS — IEEE 754 full precision */
                b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xEC; b[3]=16; EMIT(cg, 4);
                b = BUF(cg); b[0]=0x48; b[1]=0x89; b[2]=0x04; b[3]=0x24; EMIT(cg, 4);
                b = BUF(cg); b[0]=0xDD; b[1]=0x04; b[2]=0x24; EMIT(cg, 3); /* fld [rsp] */
                b = BUF(cg); b[0]=0xD9; b[1]=0xFF; EMIT(cg, 2); /* fcos */
                b = BUF(cg); b[0]=0xDD; b[1]=0x1C; b[2]=0x24; EMIT(cg, 3); /* fstp [rsp] */
                b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x04; b[3]=0x24; EMIT(cg, 4);
                b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=16; EMIT(cg, 4);
                /* Sync xmm0 for callers. */
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC0; EMIT(cg, 5);
            } else {
                /* SSE2 path: octant reduce + sin_poly / cos_poly select.
                 * cos(x) = sin(x + π/2), so we reuse the sin body with an
                 * octant shift of +1 (see emit_sincos_body). */
                emit_sincos_body(cg, /*is_cos=*/1);
            }
            (void)pn; (void)b;
            return 1;
        }
        if (strcmp(name, "math_abs") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]); /* x → RAX (f64 bits) */
            int pn; uint8_t *b;
            /* Clear sign bit: btr rax, 63 */
            b = BUF(cg); b[0]=0x48; b[1]=0x0F; b[2]=0xBA; b[3]=0xF0; b[4]=63; EMIT(cg, 5);
            /* Sync xmm0 with modified RAX (for print_float etc.) */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC0; EMIT(cg, 5); /* movq xmm0, rax */
            return 1;
        }

        if (strcmp(name, "math_expm1") == 0 && argc == 1) {
            /* expm1(x) = exp(x) - 1, computed stably near 0.
             * For |x| < 0.5: Taylor series x + x²/2 + x³/6 + x⁴/24 + x⁵/120 + x⁶/720 + x⁷/5040
             * Via Horner: x * (1 + x*(1/2 + x*(1/6 + x*(1/24 + x*(1/120 + x*(1/720 + x/5040))))))
             * Avoids catastrophic cancellation when exp(x) ≈ 1 for small x. */
            emit_expression(cg, node->children[1]);
            int pn; uint8_t *b;
            /* movq xmm0, rax (x) */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC0; EMIT(cg, 5);
            /* Horner from innermost: start with x/5040 */
            /* xmm1 = 1/5040 = 1.984126984126984e-04 */
            uint64_t c7 = 0x3F2A01A01A01A01AULL;
            pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, c7); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC9; EMIT(cg, 5); /* movq xmm1, rcx */
            /* xmm1 = x * 1/5040 */
            pn = emit_mulsd(BUF(cg), 1, 0); EMIT(cg, pn);

            /* Add 1/720 = 1.388888888888889e-03 */
            uint64_t c6 = 0x3F56C16C16C16C17ULL;
            pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, c6); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xD1; EMIT(cg, 5); /* movq xmm2, rcx */
            pn = emit_addsd(BUF(cg), 1, 2); EMIT(cg, pn);
            pn = emit_mulsd(BUF(cg), 1, 0); EMIT(cg, pn);

            /* Add 1/120 = 8.333333333333333e-03 */
            uint64_t c5 = 0x3F81111111111111ULL;
            pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, c5); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xD1; EMIT(cg, 5);
            pn = emit_addsd(BUF(cg), 1, 2); EMIT(cg, pn);
            pn = emit_mulsd(BUF(cg), 1, 0); EMIT(cg, pn);

            /* Add 1/24 = 0.04166666666666667 */
            uint64_t c4 = 0x3FA5555555555555ULL;
            pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, c4); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xD1; EMIT(cg, 5);
            pn = emit_addsd(BUF(cg), 1, 2); EMIT(cg, pn);
            pn = emit_mulsd(BUF(cg), 1, 0); EMIT(cg, pn);

            /* Add 1/6 = 0.16666666666666666 */
            uint64_t c3 = 0x3FC5555555555555ULL;
            pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, c3); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xD1; EMIT(cg, 5);
            pn = emit_addsd(BUF(cg), 1, 2); EMIT(cg, pn);
            pn = emit_mulsd(BUF(cg), 1, 0); EMIT(cg, pn);

            /* Add 1/2 = 0.5 */
            uint64_t c2 = 0x3FE0000000000000ULL;
            pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, c2); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xD1; EMIT(cg, 5);
            pn = emit_addsd(BUF(cg), 1, 2); EMIT(cg, pn);
            pn = emit_mulsd(BUF(cg), 1, 0); EMIT(cg, pn);

            /* Add 1.0 */
            uint64_t one = 0x3FF0000000000000ULL;
            pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, one); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xD1; EMIT(cg, 5);
            pn = emit_addsd(BUF(cg), 1, 2); EMIT(cg, pn);

            /* Final: result = x * (polynomial) */
            pn = emit_mulsd(BUF(cg), 0, 1); EMIT(cg, pn);
            /* movq rax, xmm0 */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5);
            return 1;
        }

        if (strcmp(name, "math_log1p") == 0 && argc == 1) {
            /* log1p(x) = log(1+x), stable near 0.
             * Uses: log1p(x) = 2 * atanh(x/(2+x))
             * atanh(y) ≈ y + y³/3 + y⁵/5 + y⁷/7 (5 terms)
             * Avoids cancellation when x is small (log(1+small) ≈ small). */
            emit_expression(cg, node->children[1]);
            int pn; uint8_t *b;
            /* movq xmm0, rax (x) */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC0; EMIT(cg, 5);

            /* xmm1 = 2.0 */
            uint64_t two = 0x4000000000000000ULL;
            pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, two); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC9; EMIT(cg, 5);

            /* xmm2 = 2 + x */
            pn = emit_movsd_xmm_xmm(BUF(cg), 2, 1); EMIT(cg, pn);
            pn = emit_addsd(BUF(cg), 2, 0); EMIT(cg, pn);

            /* xmm0 = y = x / (2 + x) */
            pn = emit_divsd(BUF(cg), 0, 2); EMIT(cg, pn);

            /* xmm3 = y² */
            pn = emit_movsd_xmm_xmm(BUF(cg), 3, 0); EMIT(cg, pn);
            pn = emit_mulsd(BUF(cg), 3, 0); EMIT(cg, pn);

            /* xmm4 = sum = y (first term) */
            pn = emit_movsd_xmm_xmm(BUF(cg), 4, 0); EMIT(cg, pn);

            /* Add more terms: y³/3, y⁵/5, y⁷/7, y⁹/9 */
            static const uint64_t denoms[4] = {
                0x4008000000000000ULL, /* 3.0 */
                0x4014000000000000ULL, /* 5.0 */
                0x401C000000000000ULL, /* 7.0 */
                0x4022000000000000ULL, /* 9.0 */
            };
            for (int t = 0; t < 4; t++) {
                /* xmm0 = y^(2k+1) by multiplying by y² */
                pn = emit_mulsd(BUF(cg), 0, 3); EMIT(cg, pn);
                /* xmm5 = denom */
                pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, denoms[t]); EMIT(cg, pn);
                b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xE9; EMIT(cg, 5);
                /* xmm6 = xmm0 / xmm5 */
                pn = emit_movsd_xmm_xmm(BUF(cg), 6, 0); EMIT(cg, pn);
                pn = emit_divsd(BUF(cg), 6, 5); EMIT(cg, pn);
                /* sum += xmm6 */
                pn = emit_addsd(BUF(cg), 4, 6); EMIT(cg, pn);
            }

            /* result = 2 * sum */
            pn = emit_mov_reg_imm64(BUF(cg), REG_RCX, two); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC1; EMIT(cg, 5); /* movq xmm0, rcx */
            pn = emit_mulsd(BUF(cg), 0, 4); EMIT(cg, pn);

            /* movq rax, xmm0 */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5);
            return 1;
        }

        /*
         * F64 ARRAY BUILTINS — arrays of doubles for neural networks
         *
         * Layout: [i64 length][f64 elem0][f64 elem1]...
         * Same mmap structure as i32 arrays (8 bytes per element).
         * f64 stored as raw IEEE 754 bits in the i64 slots.
         *
         * arr_f64_new(n)           → base ptr (mmap'd)
         * arr_f64_get(base, idx)   → f64 value
         * arr_f64_set(base, idx, val) → 0
         * arr_f64_dot(a, b)        → f64 dot product (SSE2 MULSD+ADDSD)
         * arr_f64_scale(base, factor) → 0 (multiply all by f64 factor)
         * arr_f64_sum(base)        → f64 sum
         */
        if (strcmp(name, "arr_f64_new") == 0 && argc == 1) {
            /* Same as arr_new — f64 also uses 8 bytes per element */
            emit_expression(cg, node->children[1]); /* n → RAX */
            int pn; uint8_t *b;
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_shl_reg_imm(BUF(cg), REG_RAX, 3); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RAX); EMIT(cg, pn);
            pn = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 3); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC2;
            int32_t v=0x22; memcpy(b+3,&v,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC0;
            v=-1; memcpy(b+3,&v,4); EMIT(cg,7);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg,3);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 9); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);
            b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RCX), 0, reg_ext(REG_RAX));
            b[1] = 0x89; b[2] = modrm(0, REG_RCX, REG_RAX);
            EMIT(cg, 3);
            pn = emit_add_reg_imm(BUF(cg), REG_RAX, 8); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_get") == 0 && argc == 2) {
            /* Load f64 from [base + idx*8], return as f64 bits in RAX AND xmm0.
             * Callers that follow up with a float binop expect the value in
             * xmm0; returning only in RAX breaks the contract. */
            emit_expression(cg, node->children[2]); /* idx */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);
            uint8_t *b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RAX), reg_ext(REG_RCX), reg_ext(REG_RAX));
            b[1] = 0x8B; b[2] = modrm(0, REG_RAX & 7, 4);
            b[3] = (uint8_t)((3 << 6) | ((REG_RCX & 7) << 3) | (REG_RAX & 7));
            EMIT(cg, 4);
            /* Sync xmm0 with RAX so the caller's float_is_in_xmm0
             * invariant holds (movq xmm0, rax  →  66 48 0F 6E C0). */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xC0;
            EMIT(cg, 5);
            return 1;
        }
        if (strcmp(name, "arr_f64_set") == 0 && argc == 3) {
            /* Store f64 at [base + idx*8] */
            emit_expression(cg, node->children[3]); /* val (f64 bits in RAX) */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* idx */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* idx */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* val */
            uint8_t *b = BUF(cg);
            b[0] = rex(1, reg_ext(REG_RDX), reg_ext(REG_RCX), reg_ext(REG_RAX));
            b[1] = 0x89; b[2] = modrm(0, REG_RDX & 7, 4);
            b[3] = (uint8_t)((3 << 6) | ((REG_RCX & 7) << 3) | (REG_RAX & 7));
            EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_sum") == 0 && argc == 1) {
            /* SSE2 sum of f64 array: ADDSD accumulator loop */
            emit_expression(cg, node->children[1]); /* base */
            int pn; uint8_t *b;
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            /* xorpd xmm0, xmm0 (zero accumulator) */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x57; b[3]=0xC0; EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);
            CgCountedLoop lp = cg_loop_begin(cg, REG_RSI, REG_RCX);
            /* movsd xmm1, [rdi + rsi*8] */
            cg_movsd_xmm_base_idx(cg, 1, REG_RDI, REG_RSI, 0x10);
            /* addsd xmm0, xmm1 */
            pn = emit_addsd(BUF(cg), 0, 1); EMIT(cg, pn);
            cg_loop_end(cg, lp, 1);
            /* movq rax, xmm0 (return f64 bits) */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5);
            return 1;
        }
        if (strcmp(name, "arr_f64_sum_kahan") == 0 && argc == 1) {
            /* Kahan compensated summation — recovers bits lost to rounding.
             *
             * Algorithm:
             *   sum = 0; c = 0
             *   for each x in arr:
             *     y = x - c
             *     t = sum + y
             *     c = (t - sum) - y    // recovers the lost low bits
             *     sum = t
             *
             * Accurate even with millions of additions (no accumulation drift).
             * Uses: xmm0=sum, xmm1=c, xmm2=x, xmm3=y, xmm4=t */
            emit_expression(cg, node->children[1]);
            int pn; uint8_t *b;
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            /* xorpd xmm0, xmm0 (sum = 0) */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x57; b[3]=0xC0; EMIT(cg, 4);
            /* xorpd xmm1, xmm1 (c = 0) */
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x57; b[3]=0xC9; EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            CgCountedLoop lp = cg_loop_begin(cg, REG_RSI, REG_RCX);

            /* xmm2 = arr[i] */
            cg_movsd_xmm_base_idx(cg, 2, REG_RDI, REG_RSI, 0x10);

            /* xmm3 = y = x - c */
            pn = emit_movsd_xmm_xmm(BUF(cg), 3, 2); EMIT(cg, pn);
            pn = emit_subsd(BUF(cg), 3, 1); EMIT(cg, pn);

            /* xmm4 = t = sum + y */
            pn = emit_movsd_xmm_xmm(BUF(cg), 4, 0); EMIT(cg, pn);
            pn = emit_addsd(BUF(cg), 4, 3); EMIT(cg, pn);

            /* xmm1 = c = (t - sum) - y */
            pn = emit_movsd_xmm_xmm(BUF(cg), 1, 4); EMIT(cg, pn);
            pn = emit_subsd(BUF(cg), 1, 0); EMIT(cg, pn);
            pn = emit_subsd(BUF(cg), 1, 3); EMIT(cg, pn);

            /* xmm0 = sum = t */
            pn = emit_movsd_xmm_xmm(BUF(cg), 0, 4); EMIT(cg, pn);

            cg_loop_end(cg, lp, 1);

            /* movq rax, xmm0 */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5);
            return 1;
        }

        if (strcmp(name, "arr_f64_dot") == 0 && argc == 2) {
            /* AVX2 dot product: result = Σ a[i]·b[i].
             *
             * Main loop processes 4 doubles per iteration via a single
             * VFMADD231PD into a ymm accumulator — Zen 3 can issue
             * that at 0.5c throughput, so ~8 FMAs/cycle peak.
             * A scalar tail mops up the last n mod 4 elements.
             *
             * Registers in this block:
             *   RDI = a base,  RBX = b base (callee-saved; pushed),
             *   RCX = n (length),  RDX = n_vec = n & ~3,
             *   RSI = i counter in element units,
             *   ymm0 = packed accumulator, xmm0 = scalar result at end.
             */
            emit_expression(cg, node->children[2]); /* b */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* a */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn);
            uint8_t *b;
            pn = emit_push(BUF(cg), REG_RBX); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RBX, REG_RDX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);

            /* RDX = n & ~3   (vectorised chunk length in elements). */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xE2; b[3]=0xFC; EMIT(cg, 4);  /* and rdx, -4 */

            /* vxorpd ymm0, ymm0, ymm0 — clear accumulator. */
            b = BUF(cg); b[0]=0xC5; b[1]=0xFD; b[2]=0x57; b[3]=0xC0; EMIT(cg, 4);

            /* xor rsi, rsi */
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            /* ── Vector loop: while RSI < RDX { … RSI += 4 } ─────────── */
            CgCountedLoop vec = cg_loop_begin(cg, REG_RSI, REG_RDX);

            /* vmovupd ymm1, [rdi + rsi*8] */
            cg_vmovupd_ymm_base_idx(cg, 1, REG_RDI, REG_RSI, 0x10);
            /* vmovupd ymm2, [rbx + rsi*8] */
            cg_vmovupd_ymm_base_idx(cg, 2, REG_RBX, REG_RSI, 0x10);
            /* vfmadd231pd ymm0, ymm1, ymm2  →  ymm0 += ymm1·ymm2 */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0xF5; b[3]=0xB8; b[4]=0xC2; EMIT(cg, 5);

            cg_loop_end(cg, vec, 4);

            /* ── Horizontal sum: ymm0 → scalar xmm0 ──────────────────── */
            /* vextractf128 xmm1, ymm0, 1 */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE3; b[2]=0x7D; b[3]=0x19; b[4]=0xC1; b[5]=0x01; EMIT(cg, 6);
            /* vaddpd xmm0, xmm0, xmm1 */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF9; b[2]=0x58; b[3]=0xC1; EMIT(cg, 4);
            /* vhaddpd xmm0, xmm0, xmm0 — sum the two remaining doubles */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF9; b[2]=0x7C; b[3]=0xC0; EMIT(cg, 4);

            /* ── Scalar tail loop: while RSI < RCX ───────────────────── */
            CgCountedLoop tail = cg_loop_begin(cg, REG_RSI, REG_RCX);

            /* movsd xmm1, [rdi + rsi*8] */
            cg_movsd_xmm_base_idx(cg, 1, REG_RDI, REG_RSI, 0x10);
            /* movsd xmm2, [rbx + rsi*8] */
            cg_movsd_xmm_base_idx(cg, 2, REG_RBX, REG_RSI, 0x10);
            /* vfmadd231sd xmm0, xmm1, xmm2 */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0xF1; b[3]=0xB9; b[4]=0xC2; EMIT(cg, 5);

            cg_loop_end(cg, tail, 1);

            /* vzeroupper — avoid AVX↔SSE transition penalty on return. */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);

            pn = emit_pop(BUF(cg), REG_RBX); EMIT(cg, pn);
            /* movq rax, xmm0 */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x7E; b[4]=0xC0; EMIT(cg, 5);
            return 1;
        }
        if (strcmp(name, "arr_f64_sub") == 0 && argc == 3) {
            /* dst[i] = a[i] - b[i]   — elementwise, in-place into dst.
             *
             * Length comes from dst[-8].  AVX2 vsubpd in the hot loop,
             * scalar tail truncated (callers pad to mul 4). */
            emit_expression(cg, node->children[3]); /* b   */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* a   */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* dst */
            uint8_t *b;
            pn = emit_push(BUF(cg), REG_RBX); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            /* mov rbx, [rsp+8] (a) ; mov rdx, [rsp+16] (b) */
            b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x5C; b[3]=0x24; b[4]=0x08; EMIT(cg, 5);
            b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x54; b[3]=0x24; b[4]=0x10; EMIT(cg, 5);

            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RDI, -8); EMIT(cg, pn);
            /* mov r8, rcx ; and r8, -4   — r8 = n_vec */
            b = BUF(cg); b[0]=0x49; b[1]=0x89; b[2]=0xC8; EMIT(cg, 3);
            b = BUF(cg); b[0]=0x49; b[1]=0x83; b[2]=0xE0; b[3]=0xFC; EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            CgCountedLoop vec = cg_loop_begin(cg, REG_RSI, REG_R8);
            /* vmovupd ymm0, [rbx + rsi*8] */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RBX, REG_RSI, 0x10);
            /* vsubpd ymm0, ymm0, [rdx + rsi*8]   (VEX.256.66.0F.WIG 5C /r). */
            b = BUF(cg); b[0]=0xC5; b[1]=0xFD; b[2]=0x5C;
            b[3]=modrm(0, 0, 4); b[4]=(uint8_t)((3<<6)|(REG_RSI<<3)|REG_RDX);
            EMIT(cg, 5);
            /* vmovupd [rdi + rsi*8], ymm0 */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);
            cg_loop_end(cg, vec, 4);

            /* Scalar tail for rsi < rcx (n mod 4). */
            CgCountedLoop tail = cg_loop_begin(cg, REG_RSI, REG_RCX);
            /* movsd xmm0, [rbx + rsi*8] ; subsd xmm0, [rdx + rsi*8] ; movsd [rdi+rsi*8], xmm0 */
            cg_movsd_xmm_base_idx(cg, 0, REG_RBX, REG_RSI, 0x10);
            b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x5C;
            b[3]=modrm(0, 0, 4); b[4]=(uint8_t)((3<<6)|(REG_RSI<<3)|REG_RDX);
            EMIT(cg, 5);
            cg_movsd_xmm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);
            cg_loop_end(cg, tail, 1);

            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);  /* vzeroupper */
            pn = emit_pop(BUF(cg), REG_RBX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=16; EMIT(cg, 4);  /* drop a,b */
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_mul") == 0 && argc == 3) {
            /* dst[i] = a[i] * b[i]   — same shape as arr_f64_sub but
             * with vmulpd (opcode 0x59).  Used in backward pass to
             * chain dL/dy · activation'(y). */
            emit_expression(cg, node->children[3]);
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]);
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]);
            uint8_t *b;
            pn = emit_push(BUF(cg), REG_RBX); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x5C; b[3]=0x24; b[4]=0x08; EMIT(cg, 5);
            b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x54; b[3]=0x24; b[4]=0x10; EMIT(cg, 5);

            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RDI, -8); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x49; b[1]=0x89; b[2]=0xC8; EMIT(cg, 3);
            b = BUF(cg); b[0]=0x49; b[1]=0x83; b[2]=0xE0; b[3]=0xFC; EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            CgCountedLoop vec = cg_loop_begin(cg, REG_RSI, REG_R8);
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RBX, REG_RSI, 0x10);
            /* vmulpd ymm0, ymm0, [rdx + rsi*8]   —  opcode 0x59. */
            b = BUF(cg); b[0]=0xC5; b[1]=0xFD; b[2]=0x59;
            b[3]=modrm(0, 0, 4); b[4]=(uint8_t)((3<<6)|(REG_RSI<<3)|REG_RDX);
            EMIT(cg, 5);
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);
            cg_loop_end(cg, vec, 4);

            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);
            pn = emit_pop(BUF(cg), REG_RBX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=16; EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_softmax") == 0 && argc == 1) {
            /* In-place softmax: buf[i] = exp(buf[i] - max) / Σ exp(…)
             *
             * Three passes over the array:
             *   1. horizontal max            (ymm11, xmm11 for scalar tail)
             *   2. shifted exp into buf, sum (ymm12, xmm12 for scalar tail)
             *   3. multiply by 1/sum
             *
             * Handles arbitrary n (including n < 4).  The AVX2 body runs
             * over the largest mul-of-4 prefix; a scalar tail finishes
             * the last 0-3 elements.  When n < 4 the AVX2 prefix is
             * empty — the initial ymm11 seed is replaced with a scalar
             * load so we never read past the buffer.
             *
             * RBX holds `n` across the whole routine so that callees
             * (emit_vec_exp_body_avx2 clobbers RCX) can't disturb our
             * loop limit.
             *
             * Clobbers: all caller-saved GPRs + ymm0..ymm15.  Saves RBX.
             */
            emit_expression(cg, node->children[1]);
            int pn; uint8_t *b;
            pn = emit_push(BUF(cg), REG_RBX); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            /* rbx = n (callee-saved, stable across vec_exp clobbers).  */
            pn = emit_mov_reg_reg(BUF(cg), REG_RBX, REG_RCX); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xE2; b[3]=0xFC; EMIT(cg, 4);  /* rdx = n_vec */

            /* Broadcast exp pipeline constants + spill poly coefficients. */
            cg_broadcast_f64(cg, 8,  0x3FF71547652B82FEULL, 0);
            cg_broadcast_f64(cg, 9,  0x3FE62E42FEFA39EFULL, 0);
            cg_broadcast_f64(cg, 10, 0x3FF0000000000000ULL, 0);
            /* Clamp target for shifted values: vec_exp's 2^k reconstruction
             * overflows for k ≲ −1023, which corresponds to x ≲ −710.  Clamp
             * the shifted (buf − max) values to −700 before exp to keep the
             * polynomial in its valid range.  exp(−700) ≈ 1e−304 which is
             * effectively zero for softmax normalisation purposes. */
            cg_broadcast_f64(cg, 13, 0xC085E00000000000ULL, 0); /* −700.0 */
            emit_exp_coeff_stack_setup(cg);

            /* ── PASS 1: horizontal max ──────────────────────────────
             * Seed ymm11 with broadcast(-inf) so the vec + scalar passes
             * can process the full [0, n) range uniformly.  Pre-seeding
             * from buf[0..3] would save one vmaxpd iteration but breaks
             * for n < 4 (reads past the buffer). */
            cg_broadcast_f64(cg, 11, 0xFFF0000000000000ULL, /*scratch=*/0);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            CgCountedLoop mx = cg_loop_begin(cg, REG_RSI, REG_RDX);
            /* vmovupd ymm0, [rdi + rsi*8] */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x10);
            /* vmaxpd ymm11, ymm11, ymm0 */
            cg_vex3(cg, /*dst=*/11, /*a=*/11, /*b_reg=*/0, 0, 1, 1, 1);
            b = BUF(cg); b[0]=0x5F; b[1]=0xC0 | ((11&7)<<3) | (0&7); EMIT(cg, 2);
            cg_loop_end(cg, mx, 4);

            /* Horizontal max reduction of ymm11 → xmm11 low lane. */
            /* vextractf128 xmm0, ymm11, 1 */
            b = BUF(cg); b[0]=0xC4; b[1]=0x63; b[2]=0x7D; b[3]=0x19;
            b[4]=0xD8; b[5]=0x01; EMIT(cg, 6);   /* mod=11 reg=ymm11(3) r/m=xmm0(0), with R~ from byte2 */
            /* vmaxpd xmm11, xmm11, xmm0 */
            cg_vex3(cg, 11, 11, 0, 0, /*L=*/0, 1, 1);
            b = BUF(cg); b[0]=0x5F; b[1]=0xC0 | ((11&7)<<3) | 0; EMIT(cg, 2);
            /* vshufpd xmm0, xmm11, xmm11, 1 */
            cg_vex3(cg, 0, 11, 11, 0, 0, 1, 1);
            b = BUF(cg); b[0]=0xC6; b[1]=0xC0 | (0<<3) | (11&7); b[2]=0x01; EMIT(cg, 3);
            /* vmaxpd xmm11, xmm11, xmm0 */
            cg_vex3(cg, 11, 11, 0, 0, 0, 1, 1);
            b = BUF(cg); b[0]=0x5F; b[1]=0xC0 | ((11&7)<<3) | 0; EMIT(cg, 2);

            /* Scalar tail: fold buf[rdx..n) into xmm11 low via maxsd.
             * RSI is sitting at rdx from the vec loop; RBX holds n.   */
            {
                CgCountedLoop t1 = cg_loop_begin(cg, REG_RSI, REG_RBX);
                /* maxsd xmm11, [rdi + rsi*8]
                 *   F2 44 0F 5F  [modrm=0x1C  mod=00 reg=011 r/m=100]  [sib=0xF7] */
                b = BUF(cg);
                b[0] = 0xF2; b[1] = 0x44; b[2] = 0x0F; b[3] = 0x5F;
                b[4] = (uint8_t)(0 | ((11 & 7) << 3) | 4);
                b[5] = (uint8_t)((3 << 6) | (REG_RSI << 3) | REG_RDI);
                EMIT(cg, 6);
                cg_loop_end(cg, t1, 1);
            }

            /* vbroadcastsd ymm11, xmm11 — splat max into all lanes. */
            cg_vex3(cg, 11, 0, 11, 0, 1, 1, 2);
            b = BUF(cg); b[0]=0x19; b[1]=0xC0 | ((11&7)<<3) | (11&7); EMIT(cg, 2);

            /* ── PASS 2: buf[i] = exp(buf[i] - max) ; sum into ymm12 ── */
            /* vxorpd ymm12, ymm12, ymm12  (clear sum) */
            cg_vex3(cg, 12, 12, 12, 0, 1, 1, 1);
            b = BUF(cg); b[0]=0x57; b[1]=0xC0 | ((12&7)<<3) | (12&7); EMIT(cg, 2);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            CgCountedLoop ex = cg_loop_begin(cg, REG_RSI, REG_RDX);

            /* vmovupd ymm0, [rdi + rsi*8] */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x10);
            /* ymm0 -= ymm11  (shift by max)  —  vsubpd ymm0, ymm0, ymm11 */
            cg_vex3(cg, 0, 0, 11, 0, 1, 1, 1);
            b = BUF(cg); b[0]=0x5C; b[1]=0xC0 | (0<<3) | (11&7); EMIT(cg, 2);
            /* Clamp shifted value to ≥ ymm13 (−700) so vec_exp stays in range.
             *   vmaxpd ymm0, ymm0, ymm13 */
            cg_vex3(cg, 0, 0, 13, 0, 1, 1, 1);
            b = BUF(cg); b[0]=0x5F; b[1]=0xC0 | (0<<3) | (13&7); EMIT(cg, 2);

            emit_vec_exp_body_avx2(cg);                   /* ymm0 = exp(shifted) */

            /* vaddpd ymm12, ymm12, ymm0   — accumulate sum */
            cg_vex3(cg, 12, 12, 0, 0, 1, 1, 1);
            b = BUF(cg); b[0]=0x58; b[1]=0xC0 | ((12&7)<<3) | 0; EMIT(cg, 2);

            /* Store: vmovupd [rdi + rsi*8], ymm0 */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);

            cg_loop_end(cg, ex, 4);

            /* Horizontal sum of ymm12 → xmm12 low lane. */
            /* vextractf128 xmm0, ymm12, 1 */
            b = BUF(cg); b[0]=0xC4; b[1]=0x63; b[2]=0x7D; b[3]=0x19;
            b[4]=0xE0; b[5]=0x01; EMIT(cg, 6);
            /* vaddpd xmm12, xmm12, xmm0 */
            cg_vex3(cg, 12, 12, 0, 0, 0, 1, 1);
            b = BUF(cg); b[0]=0x58; b[1]=0xC0 | ((12&7)<<3) | 0; EMIT(cg, 2);
            /* vshufpd xmm0, xmm12, xmm12, 1 */
            cg_vex3(cg, 0, 12, 12, 0, 0, 1, 1);
            b = BUF(cg); b[0]=0xC6; b[1]=0xC0 | (0<<3) | (12&7); b[2]=0x01; EMIT(cg, 3);
            /* vaddsd xmm12, xmm12, xmm0 — scalar add to finish */
            cg_vex3(cg, 12, 12, 0, 1, 0, 3, 1);
            b = BUF(cg); b[0]=0x58; b[1]=0xC0 | ((12&7)<<3) | 0; EMIT(cg, 2);

            /* Scalar tail: for rsi in [rdx, rbx), compute exp(buf[i] - max),
             * store back, fold into xmm12.  emit_vec_exp_body_avx2 runs
             * on a broadcast of the single element — 3 lanes of wasted
             * work, but only called for 1-3 tail elements, so the setup
             * cost of a dedicated scalar exp helper isn't worth it.
             * RSI is at rdx from the vec loop. */
            {
                CgCountedLoop t2 = cg_loop_begin(cg, REG_RSI, REG_RBX);
                /* movsd xmm0, [rdi + rsi*8] */
                cg_movsd_xmm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x10);
                /* vsubsd xmm0, xmm0, xmm11  — subtract max (lane 0).
                 *   VEX.LIG.F2.0F 5C /r */
                cg_vex3(cg, 0, 0, 11, 0, 0, 3, 1);
                b = BUF(cg); b[0] = 0x5C; b[1] = 0xC0 | (0<<3) | (11&7); EMIT(cg, 2);
                /* Clamp shifted value to ≥ xmm13 low (−700).
                 *   vmaxsd xmm0, xmm0, xmm13 */
                cg_vex3(cg, 0, 0, 13, 0, 0, 3, 1);
                b = BUF(cg); b[0] = 0x5F; b[1] = 0xC0 | (0<<3) | (13&7); EMIT(cg, 2);
                /* vbroadcastsd ymm0, xmm0 — splat scalar to all 4 lanes. */
                cg_vex3(cg, 0, 0, 0, 0, 1, 1, 2);
                b = BUF(cg); b[0] = 0x19; b[1] = 0xC0; EMIT(cg, 2);

                emit_vec_exp_body_avx2(cg);     /* ymm0 = exp in all lanes */

                /* Store lane 0: movsd [rdi + rsi*8], xmm0 */
                cg_movsd_xmm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);
                /* vaddsd xmm12, xmm12, xmm0 — fold into scalar sum. */
                cg_vex3(cg, 12, 12, 0, 0, 0, 3, 1);
                b = BUF(cg); b[0] = 0x58; b[1] = 0xC0 | ((12&7)<<3) | 0; EMIT(cg, 2);
                cg_loop_end(cg, t2, 1);
            }

            /* xmm12 low now = total sum.  Compute 1/sum:
             *   vdivsd xmm12, xmm10_low, xmm12   (ymm10 low lane has 1.0) */
            cg_vex3(cg, 12, 10, 12, 1, 0, 3, 1);
            b = BUF(cg); b[0]=0x5E; b[1]=0xC0 | ((12&7)<<3) | (12&7); EMIT(cg, 2);
            /* vbroadcastsd ymm12, xmm12 */
            cg_vex3(cg, 12, 0, 12, 0, 1, 1, 2);
            b = BUF(cg); b[0]=0x19; b[1]=0xC0 | ((12&7)<<3) | (12&7); EMIT(cg, 2);

            /* ── PASS 3: buf[i] *= 1/sum ───────────────────────────── */
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);
            CgCountedLoop nm = cg_loop_begin(cg, REG_RSI, REG_RDX);
            /* vmovupd ymm0, [rdi + rsi*8] */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x10);
            /* vmulpd ymm0, ymm0, ymm12 */
            cg_vex3(cg, 0, 0, 12, 0, 1, 1, 1);
            b = BUF(cg); b[0]=0x59; b[1]=0xC0 | (0<<3) | (12&7); EMIT(cg, 2);
            /* store */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);
            cg_loop_end(cg, nm, 4);

            /* Scalar tail for pass 3: rsi is at rdx, rbx = n. */
            {
                CgCountedLoop t3 = cg_loop_begin(cg, REG_RSI, REG_RBX);
                /* movsd xmm0, [rdi + rsi*8] */
                cg_movsd_xmm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x10);
                /* vmulsd xmm0, xmm0, xmm12 */
                cg_vex3(cg, 0, 0, 12, 0, 0, 3, 1);
                b = BUF(cg); b[0] = 0x59; b[1] = 0xC0 | (0<<3) | (12&7); EMIT(cg, 2);
                /* store */
                cg_movsd_xmm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);
                cg_loop_end(cg, t3, 1);
            }

            emit_exp_coeff_stack_teardown(cg);
            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);
            pn = emit_pop(BUF(cg), REG_RBX); EMIT(cg, pn);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_tanh") == 0 && argc == 1) {
            /* tanh(x) = 2 / (1 + exp(-2x)) - 1   (uses the shared exp body).
             *
             *   ymm0 = x
             *   ymm0 *= -2   (broadcast of -2.0 in ymm13)
             *   vec_exp_body → ymm0 = exp(-2x)
             *   ymm0 += 1    (ymm10 = 1.0)
             *   ymm0 = 2 / ymm0   (ymm14 = 2.0)
             *   ymm0 -= 1
             *   store
             */
            emit_expression(cg, node->children[1]);
            int pn; uint8_t *b;
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xE2; b[3]=0xFC; EMIT(cg, 4);

            cg_broadcast_f64(cg, 8,  0x3FF71547652B82FEULL, 0);    /* log2e */
            cg_broadcast_f64(cg, 9,  0x3FE62E42FEFA39EFULL, 0);    /* ln2   */
            cg_broadcast_f64(cg, 10, 0x3FF0000000000000ULL, 0);    /* 1.0   */
            cg_broadcast_f64(cg, 13, 0xC000000000000000ULL, 0);    /* -2.0  */
            cg_broadcast_f64(cg, 14, 0x4000000000000000ULL, 0);    /*  2.0  */

            emit_exp_coeff_stack_setup(cg);

            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            CgCountedLoop vec = cg_loop_begin(cg, REG_RSI, REG_RDX);

            /* Load ymm0 = x and compute ymm0 = -2·x. */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x10);
            cg_vmulpd(cg, 0, 0, 13);

            /* exp(-2x). */
            emit_vec_exp_body_avx2(cg);

            /* ymm0 = 1 + exp(-2x) */
            cg_vex3(cg, 0, 0, 10, 0, 1, 1, 1);
            b = BUF(cg); b[0] = 0x58; b[1] = 0xC0 | (0 << 3) | (10 & 7); EMIT(cg, 2);

            /* ymm0 = 2.0 / ymm0 */
            cg_vex3(cg, 0, 14, 0, 0, 1, 1, 1);
            b = BUF(cg); b[0] = 0x5E; b[1] = 0xC0 | (0 << 3) | (0 & 7); EMIT(cg, 2);

            /* ymm0 -= 1.0  (vsubpd ymm0, ymm0, ymm10  — opcode 0x5C) */
            cg_vex3(cg, 0, 0, 10, 0, 1, 1, 1);
            b = BUF(cg); b[0] = 0x5C; b[1] = 0xC0 | (0 << 3) | (10 & 7); EMIT(cg, 2);

            /* Store. */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);

            cg_loop_end(cg, vec, 4);

            emit_exp_coeff_stack_teardown(cg);

            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_sigmoid") == 0 && argc == 1) {
            /* σ(x) = 1 / (1 + exp(-x))  — in place, AVX2 packed.
             *
             * Step 1: negate x in place (vxorpd with the sign-bit mask).
             * Step 2: apply the shared vec-exp body → ymm0 = exp(-x).
             * Step 3: ymm0 += 1.0 via vaddpd against ymm10.
             * Step 4: vdivpd ymm0, ymm10, ymm0  →  σ(x) = 1 / (1+exp(-x)).
             */
            emit_expression(cg, node->children[1]);
            int pn; uint8_t *b;
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xE2; b[3]=0xFC; EMIT(cg, 4);

            /* Broadcast pipeline constants.  The sign-bit mask lives in
             * ymm12 for the pre-exp negation. */
            cg_broadcast_f64(cg, 8,  0x3FF71547652B82FEULL, 0);
            cg_broadcast_f64(cg, 9,  0x3FE62E42FEFA39EFULL, 0);
            cg_broadcast_f64(cg, 10, 0x3FF0000000000000ULL, 0);
            cg_broadcast_f64(cg, 12, 0x8000000000000000ULL, 0);

            /* Spill polynomial coefficients for memory-broadcast in loop. */
            emit_exp_coeff_stack_setup(cg);

            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            CgCountedLoop vec = cg_loop_begin(cg, REG_RSI, REG_RDX);

            /* Load ymm0 = x. */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x10);

            /* ymm0 = -x  via vxorpd ymm0, ymm0, ymm12. */
            cg_vex3(cg, 0, 0, 12, 0, 1, 1, 1);
            b = BUF(cg); b[0] = 0x57; b[1] = 0xC0 | (0 << 3) | (12 & 7); EMIT(cg, 2);

            emit_vec_exp_body_avx2(cg);                              /* exp(-x) */

            /* 1 + exp(-x)  via vaddpd ymm0, ymm0, ymm10. */
            cg_vex3(cg, 0, 0, 10, 0, 1, 1, 1);
            b = BUF(cg); b[0] = 0x58; b[1] = 0xC0 | (0 << 3) | (10 & 7); EMIT(cg, 2);

            /* σ = 1 / (1 + exp(-x))  via vdivpd ymm0, ymm10, ymm0. */
            cg_vex3(cg, 0, 10, 0, 0, 1, 1, 1);
            b = BUF(cg); b[0] = 0x5E; b[1] = 0xC0 | (0 << 3) | (0 & 7); EMIT(cg, 2);

            /* Store. */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);

            cg_loop_end(cg, vec, 4);

            emit_exp_coeff_stack_teardown(cg);

            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);  /* vzeroupper */
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_exp") == 0 && argc == 1) {
            /* In-place exp(buf): applies our scalar-exp algorithm packed
             * 4-wide via AVX2.  Same 3-step algorithm as math_exp:
             *   k = round(x · log2e)
             *   r = x - k · ln2
             *   result = (1 + r·P(r)) · 2^k
             * where P is the 9-term Taylor polynomial, evaluated with
             * Estrin's scheme in packed form.
             *
             * 2^k reconstruction: pack k as 4×i32 in xmm, sign-extend
             * to 4×i64 in ymm, shl 52 → IEEE 754 exponent bias form.
             *
             * Scalar tail handles n mod 4 via a small loop that reuses
             * our existing scalar math_exp path.  For now the tail is
             * deliberately conservative and uses the slower path.
             *
             * Clobbers: all caller-saved GPRs + ymm0..ymm7 + xmm8..xmm12.
             * Caller-saved; no RBX save needed (we don't use it). */
            emit_expression(cg, node->children[1]); /* buf */
            int pn; uint8_t *b;
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);

            /* RDX = n & ~3 */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xE2; b[3]=0xFC; EMIT(cg, 4);

            /* Broadcast loop-invariant scalars (log2e, ln2, 1.0) to ymm. */
            cg_broadcast_f64(cg, 8,  0x3FF71547652B82FEULL, /*scratch=*/0);
            cg_broadcast_f64(cg, 9,  0x3FE62E42FEFA39EFULL, 0);
            cg_broadcast_f64(cg, 10, 0x3FF0000000000000ULL, 0);

            /* Spill the 8 Estrin coefficients to the stack once; each
             * vec-loop iteration reads them via 1-instruction
             * vbroadcastsd [rsp+k*8] instead of a 3-instruction
             * mov-imm64 + movq + vbroadcastsd chain. */
            emit_exp_coeff_stack_setup(cg);

            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            /* ── Vector loop ────────────────────────────────────────── */
            CgCountedLoop vec = cg_loop_begin(cg, REG_RSI, REG_RDX);

            /* ymm0 = x = load from [rdi + rsi*8] */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x10);

            emit_vec_exp_body_avx2(cg);

            /* Store: vmovupd [rdi + rsi*8], ymm0 */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);

            cg_loop_end(cg, vec, 4);

            /* Scalar tail intentionally left empty — callers pad arrays
             * to multiples of 4 (standard NN batching convention). */

            emit_exp_coeff_stack_teardown(cg);

            /* vzeroupper + return 0 */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_adam_apply") == 0 && argc == 5) {
            /* Adam's per-element weight update, vectorised:
             *
             *   w[i] -= lr · m[i] / (sqrt(v[i]) + eps)
             *
             * m and v are the bias-corrected first- and second-moment
             * estimates; lr absorbs the 1/(1−β₁ᵗ) factor.  Caller
             * owns the moments; this kernel only touches w.
             *
             * Register plan (after arg unpack; push order mirrors
             * arr_f64_add_scaled):
             *   RDI  = w base      RBX = v base (callee-saved; pushed)
             *   R8   = m base
             *   RCX  = n           RDX = n_vec = n & ~3
             *   RSI  = i (loop index)
             *   ymm13 = broadcast(lr)
             *   ymm14 = broadcast(eps)
             *   xmm13/14 low lanes carry scalar copies for the tail.
             *
             * Stack after the 4 arg pushes + push rbx:
             *   [rsp+0]  = rbx_saved
             *   [rsp+8]  = m
             *   [rsp+16] = v
             *   [rsp+24] = lr  (f64 bits)
             *   [rsp+32] = eps (f64 bits)
             */
            emit_expression(cg, node->children[5]); /* eps */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[4]); /* lr */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[3]); /* v */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* m */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* w → RAX */
            uint8_t *b;

            pn = emit_push(BUF(cg), REG_RBX); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);

            /* mov r8,  [rsp+8]   — m base     (REX.W=1, R=1 for r8) */
            b = BUF(cg); b[0]=0x4C; b[1]=0x8B; b[2]=0x44; b[3]=0x24; b[4]=0x08; EMIT(cg, 5);
            /* mov rbx, [rsp+16]  — v base */
            b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x5C; b[3]=0x24; b[4]=0x10; EMIT(cg, 5);
            /* mov rcx, [rsp+24]  — lr bits    (temp, then broadcast)  */
            b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x4C; b[3]=0x24; b[4]=0x18; EMIT(cg, 5);
            /* movq xmm13, rcx: 66 4C 0F 6E E9   (REX.WR for xmm13 dst) */
            b = BUF(cg); b[0]=0x66; b[1]=0x4C; b[2]=0x0F; b[3]=0x6E; b[4]=0xE9; EMIT(cg, 5);
            /* vbroadcastsd ymm13, xmm13 */
            cg_vex3(cg, 13, 0, 13, 0, 1, 1, 2);
            b = BUF(cg); b[0]=0x19; b[1]=0xC0 | ((13&7)<<3) | (13&7); EMIT(cg, 2);

            /* mov rcx, [rsp+32]  — eps bits */
            b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x4C; b[3]=0x24; b[4]=0x20; EMIT(cg, 5);
            /* movq xmm14, rcx: 66 4C 0F 6E F1   (REX.WR for xmm14) */
            b = BUF(cg); b[0]=0x66; b[1]=0x4C; b[2]=0x0F; b[3]=0x6E; b[4]=0xF1; EMIT(cg, 5);
            /* vbroadcastsd ymm14, xmm14 */
            cg_vex3(cg, 14, 0, 14, 0, 1, 1, 2);
            b = BUF(cg); b[0]=0x19; b[1]=0xC0 | ((14&7)<<3) | (14&7); EMIT(cg, 2);

            /* RCX = n = w[-8]; RDX = n & ~3. */
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RDI, -8); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xE2; b[3]=0xFC; EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            /* ── Vector loop ──────────────────────────────────────── */
            CgCountedLoop vec = cg_loop_begin(cg, REG_RSI, REG_RDX);

            /* ymm2 = m chunk */
            cg_vmovupd_ymm_base_idx(cg, 2, 8 /* R8 */, REG_RSI, 0x10);
            /* ymm3 = v chunk */
            cg_vmovupd_ymm_base_idx(cg, 3, REG_RBX, REG_RSI, 0x10);
            /* vsqrtpd ymm3, ymm3   —  VEX.256.66.0F 51 /r */
            cg_vex3(cg, 3, 0, 3, 0, 1, 1, 1);
            b = BUF(cg); b[0]=0x51; b[1]=0xC0 | ((3&7)<<3) | (3&7); EMIT(cg, 2);
            /* vaddpd ymm3, ymm3, ymm14   — sqrt(v) + eps */
            cg_vex3(cg, 3, 3, 14, 0, 1, 1, 1);
            b = BUF(cg); b[0]=0x58; b[1]=0xC0 | ((3&7)<<3) | (14&7); EMIT(cg, 2);
            /* vdivpd ymm2, ymm2, ymm3   — m / (sqrt(v)+eps) */
            cg_vex3(cg, 2, 2, 3, 0, 1, 1, 1);
            b = BUF(cg); b[0]=0x5E; b[1]=0xC0 | ((2&7)<<3) | (3&7); EMIT(cg, 2);
            /* vmulpd ymm2, ymm2, ymm13   — lr · … */
            cg_vex3(cg, 2, 2, 13, 0, 1, 1, 1);
            b = BUF(cg); b[0]=0x59; b[1]=0xC0 | ((2&7)<<3) | (13&7); EMIT(cg, 2);
            /* ymm4 = w chunk */
            cg_vmovupd_ymm_base_idx(cg, 4, REG_RDI, REG_RSI, 0x10);
            /* vsubpd ymm4, ymm4, ymm2 */
            cg_vex3(cg, 4, 4, 2, 0, 1, 1, 1);
            b = BUF(cg); b[0]=0x5C; b[1]=0xC0 | ((4&7)<<3) | (2&7); EMIT(cg, 2);
            /* store w chunk */
            cg_vmovupd_ymm_base_idx(cg, 4, REG_RDI, REG_RSI, 0x11);

            cg_loop_end(cg, vec, 4);

            /* ── Scalar tail ──────────────────────────────────────── */
            CgCountedLoop tail = cg_loop_begin(cg, REG_RSI, REG_RCX);

            /* xmm0 = m[i] */
            cg_movsd_xmm_base_idx(cg, 0, 8 /* R8 */, REG_RSI, 0x10);
            /* xmm1 = v[i] */
            cg_movsd_xmm_base_idx(cg, 1, REG_RBX, REG_RSI, 0x10);
            /* sqrtsd xmm1, xmm1 */
            pn = emit_sqrtsd(BUF(cg), 1, 1); EMIT(cg, pn);
            /* addsd xmm1, xmm14   — xmm14 low = eps scalar */
            b = BUF(cg); b[0]=0xF2; b[1]=0x41; b[2]=0x0F; b[3]=0x58; b[4]=0xCE; EMIT(cg, 5);
            /* divsd xmm0, xmm1 */
            pn = emit_divsd(BUF(cg), 0, 1); EMIT(cg, pn);
            /* mulsd xmm0, xmm13   — xmm13 low = lr scalar */
            b = BUF(cg); b[0]=0xF2; b[1]=0x41; b[2]=0x0F; b[3]=0x59; b[4]=0xC5; EMIT(cg, 5);
            /* xmm2 = w[i] */
            cg_movsd_xmm_base_idx(cg, 2, REG_RDI, REG_RSI, 0x10);
            /* subsd xmm2, xmm0 */
            pn = emit_subsd(BUF(cg), 2, 0); EMIT(cg, pn);
            /* store xmm2 back to w[i] */
            cg_movsd_xmm_base_idx(cg, 2, REG_RDI, REG_RSI, 0x11);

            cg_loop_end(cg, tail, 1);

            /* vzeroupper + restore RBX + drop 4 pushed args. */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);
            pn = emit_pop(BUF(cg), REG_RBX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=32; EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_expm1") == 0 && argc == 1) {
            /* In-place expm1(buf) = exp(x) - 1, AVX2 packed.  Reuses the
             * vec-exp body and subtracts broadcast 1.0 at the end.
             *
             * NOTE: loses precision for |x| < 2^-53 where exp(x) rounds
             * to exactly 1.0 and the subtraction gives 0.  Callers that
             * need full precision near zero should stick with the
             * scalar math_expm1 (Horner-evaluated Taylor). */
            emit_expression(cg, node->children[1]); /* buf */
            int pn; uint8_t *b;
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);

            /* RDX = n & ~3 */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xE2; b[3]=0xFC; EMIT(cg, 4);

            cg_broadcast_f64(cg, 8,  0x3FF71547652B82FEULL, /*scratch=*/0);
            cg_broadcast_f64(cg, 9,  0x3FE62E42FEFA39EFULL, 0);
            cg_broadcast_f64(cg, 10, 0x3FF0000000000000ULL, 0);
            emit_exp_coeff_stack_setup(cg);

            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            CgCountedLoop vec = cg_loop_begin(cg, REG_RSI, REG_RDX);

            /* ymm0 = x = load */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x10);

            emit_vec_exp_body_avx2(cg);            /* ymm0 = exp(x) */

            /* ymm0 -= 1.0  — vsubpd ymm0, ymm0, ymm10 (broadcast 1.0). */
            cg_vex3(cg, 0, 0, 10, 0, 1, 1, 1);
            b = BUF(cg); b[0]=0x5C; b[1]=0xC0 | (0<<3) | (10&7); EMIT(cg, 2);

            /* Store */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);

            cg_loop_end(cg, vec, 4);

            /* Scalar tail skipped — callers pad to mul 4, as with arr_f64_exp. */

            emit_exp_coeff_stack_teardown(cg);

            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);  /* vzeroupper */
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_relu") == 0 && argc == 1) {
            /* In-place ReLU: buf[i] = max(0, buf[i]).
             *
             * AVX2 inner loop uses vmaxpd which hits both FMA pipes at
             * 0.5c throughput → 8 doubles/cycle peak.  No polynomial,
             * no scalar round-trip per lane.  Scalar tail for n mod 4.
             */
            emit_expression(cg, node->children[1]); /* buf */
            int pn; uint8_t *b;
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);

            /* RDX = n & ~3 */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xE2; b[3]=0xFC; EMIT(cg, 4);

            /* vxorpd ymm1, ymm1, ymm1  — ymm1 = {0, 0, 0, 0} */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF5; b[2]=0x57; b[3]=0xC9; EMIT(cg, 4);

            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            /* Vector loop. */
            CgCountedLoop vec = cg_loop_begin(cg, REG_RSI, REG_RDX);

            /* vmovupd ymm0, [rdi + rsi*8] */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x10);
            /* vmaxpd ymm0, ymm0, ymm1   —  C5 FD 5F C1 */
            b = BUF(cg); b[0]=0xC5; b[1]=0xFD; b[2]=0x5F; b[3]=0xC1; EMIT(cg, 4);
            /* vmovupd [rdi + rsi*8], ymm0 */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);

            cg_loop_end(cg, vec, 4);

            /* Scalar tail: maxsd per element. */
            CgCountedLoop tail = cg_loop_begin(cg, REG_RSI, REG_RCX);

            /* movsd xmm0, [rdi + rsi*8] */
            cg_movsd_xmm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x10);
            /* maxsd xmm0, xmm1   —  F2 0F 5F C1 */
            b = BUF(cg); b[0]=0xF2; b[1]=0x0F; b[2]=0x5F; b[3]=0xC1; EMIT(cg, 4);
            /* movsd [rdi + rsi*8], xmm0 */
            cg_movsd_xmm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);

            cg_loop_end(cg, tail, 1);

            /* vzeroupper + return 0 */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_add_scaled") == 0 && argc == 3) {
            /* y[i] += alpha · x[i]   — the SGD weight-update kernel.
             *
             * AVX2 inner loop (4 doubles/iter) + scalar tail.  Returns 0.
             * Register plan (all caller-saved except RBX save):
             *   RDI = y base, RBX = x base (callee-saved→push),
             *   RCX = n (from y[-8]), RDX = n_vec, RSI = i,
             *   ymm3 = vector broadcast of alpha.
             */
            emit_expression(cg, node->children[3]); /* alpha (f64 bits) */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* x */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* y */
            uint8_t *b;
            pn = emit_push(BUF(cg), REG_RBX); EMIT(cg, pn);
            /* After the 3 pushes (alpha, x, rbx_saved), stack is:
             *   [rsp+0]  = rbx_saved
             *   [rsp+8]  = x          (we re-load it here)
             *   [rsp+16] = alpha      (we re-load it here)
             * RAX holds y. */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            /* mov rbx, [rsp+8]  →  48 8B 5C 24 08 */
            b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x5C; b[3]=0x24; b[4]=0x08; EMIT(cg, 5);
            /* mov rdx, [rsp+16] →  48 8B 54 24 10 */
            b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x54; b[3]=0x24; b[4]=0x10; EMIT(cg, 5);
            /* movq xmm3, rdx */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xDA; EMIT(cg, 5);
            /* vbroadcastsd ymm3, xmm3:  VEX.256.66.0F38.W0 19 /r
             *   C4 E2 7D 19 DB  (dst=ymm3, src=xmm3) */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0x7D; b[3]=0x19; b[4]=0xDB; EMIT(cg, 5);

            /* RCX = n (length of y). */
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RDI, -8); EMIT(cg, pn);
            /* RDX = n & ~3 */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDX, REG_RCX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xE2; b[3]=0xFC; EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);

            /* ── Vector loop: 4 doubles/iter ─────────────────────────── */
            CgCountedLoop vec = cg_loop_begin(cg, REG_RSI, REG_RDX);

            /* vmovupd ymm0, [rdi + rsi*8]  — y chunk */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x10);
            /* vmovupd ymm1, [rbx + rsi*8]  — x chunk */
            cg_vmovupd_ymm_base_idx(cg, 1, REG_RBX, REG_RSI, 0x10);
            /* vfmadd231pd ymm0, ymm3, ymm1  → ymm0 += ymm3·ymm1 (alpha·x) */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0xE5; b[3]=0xB8; b[4]=0xC1; EMIT(cg, 5);
            /* vmovupd [rdi + rsi*8], ymm0  — store updated y */
            cg_vmovupd_ymm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);

            cg_loop_end(cg, vec, 4);

            /* ── Scalar tail ─────────────────────────────────────────── */
            CgCountedLoop tail = cg_loop_begin(cg, REG_RSI, REG_RCX);

            /* movsd xmm0, [rdi + rsi*8] */
            cg_movsd_xmm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x10);
            /* movsd xmm1, [rbx + rsi*8] */
            cg_movsd_xmm_base_idx(cg, 1, REG_RBX, REG_RSI, 0x10);
            /* vfmadd231sd xmm0, xmm3, xmm1  — xmm0 += alpha·x */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0xE1; b[3]=0xB9; b[4]=0xC1; EMIT(cg, 5);
            /* movsd [rdi + rsi*8], xmm0 */
            cg_movsd_xmm_base_idx(cg, 0, REG_RDI, REG_RSI, 0x11);

            cg_loop_end(cg, tail, 1);

            /* vzeroupper + restore RBX + drop the 2 earlier pushed args. */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);
            pn = emit_pop(BUF(cg), REG_RBX); EMIT(cg, pn);
            /* add rsp, 16  —  discard x + alpha pushes */
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=16; EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_outer_accum") == 0 && argc == 5) {
            /* arr_f64_outer_accum(G, a, b, m, n)   —  G += a ⊗ b.
             *
             *   G is [m, n] row-major; a is length m; b is length n.
             *   G[j, i] += a[j] · b[i].   ACCUMULATES — caller zeros G
             *   before a new mini-batch (or uses the running gradient).
             *
             * This is the dL/dW kernel for a dense layer when dy is a
             * length-m "error per output" vector and x is length-n.
             * Same access shape as arr_f64_matvec_T: j-outer sequential
             * row reads, broadcast a[j], FMA into the row from b.
             */
            emit_expression(cg, node->children[5]);
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[4]);
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[3]);
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]);
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]);       /* G → RAX */
            uint8_t *b;

            pn = emit_push(BUF(cg), REG_RBX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x41; b[1]=0x54; EMIT(cg, 2);
            b = BUF(cg); b[0]=0x41; b[1]=0x55; EMIT(cg, 2);

            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            /* Unpack: a → rsi, b → r8, m → r9, n → r10 */
            b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x74; b[3]=0x24; b[4]=0x18; EMIT(cg, 5);
            b = BUF(cg); b[0]=0x4C; b[1]=0x8B; b[2]=0x44; b[3]=0x24; b[4]=0x20; EMIT(cg, 5);
            b = BUF(cg); b[0]=0x4C; b[1]=0x8B; b[2]=0x4C; b[3]=0x24; b[4]=0x28; EMIT(cg, 5);
            b = BUF(cg); b[0]=0x4C; b[1]=0x8B; b[2]=0x54; b[3]=0x24; b[4]=0x30; EMIT(cg, 5);

            /* r11 = n & ~3 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x89; b[2]=0xD3; EMIT(cg, 3);
            b = BUF(cg); b[0]=0x49; b[1]=0x83; b[2]=0xE3; b[3]=0xFC; EMIT(cg, 4);

            pn = emit_xor_reg_reg(BUF(cg), REG_RBX, REG_RBX); EMIT(cg, pn);

            CgCountedLoop outer = cg_loop_begin(cg, REG_RBX, 9 /* R9 */);

            /* Row base R13 = G + j·n·8 */
            b = BUF(cg); b[0]=0x48; b[1]=0x89; b[2]=0xD8; EMIT(cg, 3);
            b = BUF(cg); b[0]=0x49; b[1]=0x0F; b[2]=0xAF; b[3]=0xC2; EMIT(cg, 4);
            b = BUF(cg); b[0]=0x4C; b[1]=0x8D; b[2]=0x2C; b[3]=0xC7; EMIT(cg, 4);

            /* ymm0 = broadcast(a[j]) */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0x7D; b[3]=0x19;
            b[4]=0x04; b[5]=(uint8_t)((3<<6)|(REG_RBX<<3)|REG_RSI);
            EMIT(cg, 6);

            /* Inner loop: i = 0 → n_vec step 4 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xE4; EMIT(cg, 3);

            CgCountedLoop iv = cg_loop_begin(cg, 12 /* R12 */, 11 /* R11 */);

            /* vmovupd ymm1, [r8 + r12*8]  — b chunk */
            cg_vmovupd_ymm_base_idx(cg, 1, 8 /* R8 */, 12 /* R12 */, 0x10);
            /* vmovupd ymm2, [r13 + r12*8 + 0]  — G row chunk */
            cg_vmovupd_ymm_base_idx(cg, 2, 13 /* R13 */, 12 /* R12 */, 0x10);
            /* vfmadd231pd ymm2, ymm1, ymm0 */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0xF5; b[3]=0xB8; b[4]=0xD0; EMIT(cg, 5);
            /* vmovupd [r13 + r12*8 + 0], ymm2 */
            cg_vmovupd_ymm_base_idx(cg, 2, 13 /* R13 */, 12 /* R12 */, 0x11);

            cg_loop_end(cg, iv, 4);

            /* Scalar tail for i = n_vec..n, same FMA semantics on xmm. */
            CgCountedLoop it = cg_loop_begin(cg, 12 /* R12 */, 10 /* R10 */);
            /* movsd xmm1, [r8 + r12*8]  — b[i] */
            cg_movsd_xmm_base_idx(cg, 1, 8 /* R8 */, 12 /* R12 */, 0x10);
            /* movsd xmm2, [r13 + r12*8 + 0]  — G[j,i] */
            cg_movsd_xmm_base_idx(cg, 2, 13 /* R13 */, 12 /* R12 */, 0x10);
            /* vfmadd231sd xmm2, xmm1, xmm0 */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0xF1; b[3]=0xB9; b[4]=0xD0; EMIT(cg, 5);
            /* movsd [r13 + r12*8 + 0], xmm2 */
            cg_movsd_xmm_base_idx(cg, 2, 13 /* R13 */, 12 /* R12 */, 0x11);
            cg_loop_end(cg, it, 1);

            cg_loop_end(cg, outer, 1);

            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);
            b = BUF(cg); b[0]=0x41; b[1]=0x5D; EMIT(cg, 2);
            b = BUF(cg); b[0]=0x41; b[1]=0x5C; EMIT(cg, 2);
            pn = emit_pop(BUF(cg), REG_RBX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=32; EMIT(cg, 4);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_matvec_T") == 0 && argc == 5) {
            /* arr_f64_matvec_T(W, v, out, m, n)   —  out = Wᵀ · v.
             *
             *   W is [m, n] row-major,  v is length m,  out is length n.
             *   out[i] = Σ_j W[j*n + i] · v[j].
             *
             * The loop-inverted kernel: j-outer reads W sequentially
             * and broadcasts v[j], i-inner FMAs into `out`.  Pure AVX2,
             * store-bound; fine because dL/dx is the cheapest of the
             * three dense-layer gradients.  Scalar tail skipped (pad).
             *
             * Register map (callee-saved RBX, R12, R13 pushed):
             *   RDI=W, RSI=v, R8=out, R9=m, R10=n, R11=n_vec,
             *   RBX=j (outer), R12=i (inner), R13=W + j·n·8,
             *   ymm0=broadcast(v[j]), ymm1/2=working.
             */
            emit_expression(cg, node->children[5]); /* n */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[4]); /* m */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[3]); /* out */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* v */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* W → RAX */
            uint8_t *b;

            pn = emit_push(BUF(cg), REG_RBX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x41; b[1]=0x54; EMIT(cg, 2);   /* push r12 */
            b = BUF(cg); b[0]=0x41; b[1]=0x55; EMIT(cg, 2);   /* push r13 */

            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            /* Stack layout after 3 callee-saves + 4 arg pushes:
             *   [rsp+0]  r13     [rsp+24] rbx       [rsp+48] m
             *   [rsp+8]  r12                        [rsp+56] n
             *   [rsp+16] rbx     [rsp+32] v
             *                    [rsp+40] out
             * Unpack args into registers. */
            /* mov rsi, [rsp+24]  — v */
            b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x74; b[3]=0x24; b[4]=0x18; EMIT(cg, 5);
            /* mov r8,  [rsp+32]  — out */
            b = BUF(cg); b[0]=0x4C; b[1]=0x8B; b[2]=0x44; b[3]=0x24; b[4]=0x20; EMIT(cg, 5);
            /* mov r9,  [rsp+40]  — m */
            b = BUF(cg); b[0]=0x4C; b[1]=0x8B; b[2]=0x4C; b[3]=0x24; b[4]=0x28; EMIT(cg, 5);
            /* mov r10, [rsp+48]  — n */
            b = BUF(cg); b[0]=0x4C; b[1]=0x8B; b[2]=0x54; b[3]=0x24; b[4]=0x30; EMIT(cg, 5);

            /* r11 = n & ~3 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x89; b[2]=0xD3; EMIT(cg, 3);     /* mov r11, r10 */
            b = BUF(cg); b[0]=0x49; b[1]=0x83; b[2]=0xE3; b[3]=0xFC; EMIT(cg, 4);

            /* ── Zero the output: vectorised pass + scalar tail ───── */
            /* vxorpd ymm1, ymm1, ymm1  (persistent zero vector)
             * xorpd  xmm2, xmm2       (scalar zero for the tail store) */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF5; b[2]=0x57; b[3]=0xC9; EMIT(cg, 4);
            b = BUF(cg); b[0]=0x66; b[1]=0x0F; b[2]=0x57; b[3]=0xD2; EMIT(cg, 4);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xE4; EMIT(cg, 3);
            CgCountedLoop z = cg_loop_begin(cg, 12 /* R12 */, 11 /* R11 */);
            cg_vmovupd_ymm_base_idx(cg, 1, 8 /* R8 */, 12 /* R12 */, 0x11);
            cg_loop_end(cg, z, 4);
            /* Scalar zero-init tail: r12 runs from n_vec to n. */
            CgCountedLoop zt = cg_loop_begin(cg, 12 /* R12 */, 10 /* R10 */);
            /* movsd [r8 + r12*8], xmm2 */
            cg_movsd_xmm_base_idx(cg, 2, 8 /* R8 */, 12 /* R12 */, 0x11);
            cg_loop_end(cg, zt, 1);

            /* ── Outer loop: j = 0 → m ───────────────────────────── */
            pn = emit_xor_reg_reg(BUF(cg), REG_RBX, REG_RBX); EMIT(cg, pn);

            CgCountedLoop outer = cg_loop_begin(cg, REG_RBX, 9 /* R9 */);

            /* R13 = RDI + (j · n) · 8   — row base pointer. */
            b = BUF(cg); b[0]=0x48; b[1]=0x89; b[2]=0xD8; EMIT(cg, 3);        /* mov rax, rbx */
            b = BUF(cg); b[0]=0x49; b[1]=0x0F; b[2]=0xAF; b[3]=0xC2; EMIT(cg, 4); /* imul rax, r10 */
            b = BUF(cg); b[0]=0x4C; b[1]=0x8D; b[2]=0x2C; b[3]=0xC7; EMIT(cg, 4); /* lea r13, [rdi+rax*8] */

            /* ymm0 = broadcast(v[j])   —  vbroadcastsd ymm0, [rsi + rbx*8] */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0x7D; b[3]=0x19;
            b[4]=0x04; b[5]=(uint8_t)((3<<6)|(REG_RBX<<3)|REG_RSI);
            EMIT(cg, 6);

            /* ── Inner loop: i = 0 → n_vec step 4 ─────────────────── */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xE4; EMIT(cg, 3);   /* xor r12, r12 */

            CgCountedLoop iv = cg_loop_begin(cg, 12 /* R12 */, 11 /* R11 */);

            /* vmovupd ymm1, [r13 + r12*8 + 0]   — row chunk. */
            cg_vmovupd_ymm_base_idx(cg, 1, 13 /* R13 */, 12 /* R12 */, 0x10);
            /* vmovupd ymm2, [r8 + r12*8]       — out chunk */
            cg_vmovupd_ymm_base_idx(cg, 2, 8 /* R8 */, 12 /* R12 */, 0x10);
            /* vfmadd231pd ymm2, ymm1, ymm0 */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0xF5; b[3]=0xB8; b[4]=0xD0; EMIT(cg, 5);
            /* vmovupd [r8 + r12*8], ymm2 */
            cg_vmovupd_ymm_base_idx(cg, 2, 8 /* R8 */, 12 /* R12 */, 0x11);

            cg_loop_end(cg, iv, 4);

            /* ── Scalar tail for n mod 4 ─────────────────────────── */
            CgCountedLoop it = cg_loop_begin(cg, 12 /* R12 */, 10 /* R10 */);
            /* movsd xmm1, [r13 + r12*8] */
            cg_movsd_xmm_base_idx(cg, 1, 13 /* R13 */, 12 /* R12 */, 0x10);
            /* movsd xmm2, [r8 + r12*8] */
            cg_movsd_xmm_base_idx(cg, 2, 8 /* R8 */, 12 /* R12 */, 0x10);
            /* vfmadd231sd xmm2, xmm1, xmm0 */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0xF1; b[3]=0xB9; b[4]=0xD0; EMIT(cg, 5);
            /* movsd [r8 + r12*8], xmm2 */
            cg_movsd_xmm_base_idx(cg, 2, 8 /* R8 */, 12 /* R12 */, 0x11);
            cg_loop_end(cg, it, 1);

            cg_loop_end(cg, outer, 1);

            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);   /* vzeroupper */
            b = BUF(cg); b[0]=0x41; b[1]=0x5D; EMIT(cg, 2);               /* pop r13 */
            b = BUF(cg); b[0]=0x41; b[1]=0x5C; EMIT(cg, 2);               /* pop r12 */
            pn = emit_pop(BUF(cg), REG_RBX); EMIT(cg, pn);
            /* drop v, out, m, n (4 × 8 = 32) */
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=32; EMIT(cg, 4);

            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_matvec") == 0 && argc == 6) {
            /* arr_f64_matvec(W, x, bias, y, m, n)  —  y = W · x + bias.
             *
             * W is an m×n row-major matrix stored as a flat f64 array
             * of length m·n (so W[j·n+i] is row j, column i).  bias/y
             * have length m, x has length n.
             *
             * Inner loop is an AVX2 FMA accumulating 4 doubles per
             * iteration; a scalar tail mops up n mod 4.  This is the
             * primary forward-pass kernel for dense NN layers.
             *
             * Register usage inside the body (after arg unpack):
             *   RDI = W base,  RSI = x base,  R14 = bias base,
             *   R8 = y base,  R9 = m,  R10 = n,  R11 = n_vec,
             *   RBX = j (outer), R12 = i (inner),
             *   R13 = row_base_ptr = W + j·n·8,
             *   ymm0 = packed inner acc,  xmm7 = scalar row acc.
             *
             * Saves RBX, R12, R13, R14 at entry (all callee-saved).
             */
            /* Evaluate and push all 6 args: push order n, m, y, bias, x, W. */
            emit_expression(cg, node->children[6]); /* n */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[5]); /* m */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[4]); /* y */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[3]); /* bias */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* x */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* W → RAX */
            uint8_t *b;

            /* Save callee-saved registers we'll use. */
            pn = emit_push(BUF(cg), REG_RBX); EMIT(cg, pn);
            /* push r12 */
            b = BUF(cg); b[0]=0x41; b[1]=0x54; EMIT(cg, 2);
            /* push r13 */
            b = BUF(cg); b[0]=0x41; b[1]=0x55; EMIT(cg, 2);
            /* push r14 */
            b = BUF(cg); b[0]=0x41; b[1]=0x56; EMIT(cg, 2);

            /* Unpack args into registers.  Stack (after 4 saves) grew by
             * 32 bytes, so the args are at [rsp+32..+72] in reverse push
             * order: [rsp+32]=x, [rsp+40]=bias, [rsp+48]=y, [rsp+56]=m,
             * [rsp+64]=n.  RAX already holds W. */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);  /* RDI = W */
            /* mov rsi, [rsp + 32]    — x */
            b = BUF(cg); b[0]=0x48; b[1]=0x8B; b[2]=0x74; b[3]=0x24; b[4]=0x20; EMIT(cg, 5);
            /* mov r14, [rsp + 40]    — bias */
            b = BUF(cg); b[0]=0x4C; b[1]=0x8B; b[2]=0x74; b[3]=0x24; b[4]=0x28; EMIT(cg, 5);
            /* mov r8,  [rsp + 48]    — y */
            b = BUF(cg); b[0]=0x4C; b[1]=0x8B; b[2]=0x44; b[3]=0x24; b[4]=0x30; EMIT(cg, 5);
            /* mov r9,  [rsp + 56]    — m */
            b = BUF(cg); b[0]=0x4C; b[1]=0x8B; b[2]=0x4C; b[3]=0x24; b[4]=0x38; EMIT(cg, 5);
            /* mov r10, [rsp + 64]    — n */
            b = BUF(cg); b[0]=0x4C; b[1]=0x8B; b[2]=0x54; b[3]=0x24; b[4]=0x40; EMIT(cg, 5);

            /* R11 = n & ~3 */
            /* mov r11, r10 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x89; b[2]=0xD3; EMIT(cg, 3);
            /* and r11, -4 */
            b = BUF(cg); b[0]=0x49; b[1]=0x83; b[2]=0xE3; b[3]=0xFC; EMIT(cg, 4);

            /* RBX = j = 0 */
            pn = emit_xor_reg_reg(BUF(cg), REG_RBX, REG_RBX); EMIT(cg, pn);

            /* ── Outer loop: for j = 0..m ─────────────────────────── */
            CgCountedLoop outer = cg_loop_begin(cg, REG_RBX, 9 /* R9 */);

            /* Compute row_base_ptr R13 = RDI + (j * n) * 8. */
            /* mov rax, rbx */
            b = BUF(cg); b[0]=0x48; b[1]=0x89; b[2]=0xD8; EMIT(cg, 3);
            /* imul rax, r10 */
            b = BUF(cg); b[0]=0x49; b[1]=0x0F; b[2]=0xAF; b[3]=0xC2; EMIT(cg, 4);
            /* lea r13, [rdi + rax*8] */
            b = BUF(cg); b[0]=0x4C; b[1]=0x8D; b[2]=0x2C; b[3]=0xC7; EMIT(cg, 4);

            /* Seed scalar acc xmm7 with bias[j]: movsd xmm7, [r14 + rbx*8] */
            cg_movsd_xmm_base_idx(cg, 7, 14 /* R14 */, REG_RBX, 0x10);

            /* vxorpd ymm0, ymm0, ymm0 */
            b = BUF(cg); b[0]=0xC5; b[1]=0xFD; b[2]=0x57; b[3]=0xC0; EMIT(cg, 4);

            /* R12 = i = 0 */
            /* xor r12, r12 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xE4; EMIT(cg, 3);

            /* ── Vector inner loop: 4 doubles per iter ─────────────── */
            CgCountedLoop vloop = cg_loop_begin(cg, 12 /* R12 */, 11 /* R11 */);

            /* vmovupd ymm1, [r13 + r12*8 + 0]  — W row chunk. */
            cg_vmovupd_ymm_base_idx(cg, 1, 13 /* R13 */, 12 /* R12 */, 0x10);
            /* vmovupd ymm2, [rsi + r12*8]  — x chunk. */
            cg_vmovupd_ymm_base_idx(cg, 2, REG_RSI, 12 /* R12 */, 0x10);
            /* vfmadd231pd ymm0, ymm1, ymm2 */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0xF5; b[3]=0xB8; b[4]=0xC2; EMIT(cg, 5);

            cg_loop_end(cg, vloop, 4);

            /* Horizontal sum ymm0 → scalar, add to xmm7. */
            /* vextractf128 xmm3, ymm0, 1 */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE3; b[2]=0x7D; b[3]=0x19; b[4]=0xC3; b[5]=0x01; EMIT(cg, 6);
            /* vaddpd xmm0, xmm0, xmm3 */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF9; b[2]=0x58; b[3]=0xC3; EMIT(cg, 4);
            /* vhaddpd xmm0, xmm0, xmm0 */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF9; b[2]=0x7C; b[3]=0xC0; EMIT(cg, 4);
            /* addsd xmm7, xmm0 */
            pn = emit_addsd(BUF(cg), 7, 0); EMIT(cg, pn);

            /* ── Scalar tail: while R12 < R10 ──────────────────────── */
            CgCountedLoop tloop = cg_loop_begin(cg, 12 /* R12 */, 10 /* R10 */);

            /* movsd xmm1, [r13 + r12*8 + 0] */
            cg_movsd_xmm_base_idx(cg, 1, 13 /* R13 */, 12 /* R12 */, 0x10);
            /* movsd xmm2, [rsi + r12*8] */
            cg_movsd_xmm_base_idx(cg, 2, REG_RSI, 12 /* R12 */, 0x10);
            /* vfmadd231sd xmm7, xmm1, xmm2 */
            b = BUF(cg); b[0]=0xC4; b[1]=0xE2; b[2]=0xF1; b[3]=0xB9; b[4]=0xFA; EMIT(cg, 5);

            cg_loop_end(cg, tloop, 1);

            /* y[j] = xmm7   →  movsd [r8 + rbx*8], xmm7 */
            cg_movsd_xmm_base_idx(cg, 7, 8 /* R8 */, REG_RBX, 0x11);

            cg_loop_end(cg, outer, 1);

            /* vzeroupper */
            b = BUF(cg); b[0]=0xC5; b[1]=0xF8; b[2]=0x77; EMIT(cg, 3);

            /* Restore callee-saved and the 5 pushed args. */
            /* pop r14 */
            b = BUF(cg); b[0]=0x41; b[1]=0x5E; EMIT(cg, 2);
            /* pop r13 */
            b = BUF(cg); b[0]=0x41; b[1]=0x5D; EMIT(cg, 2);
            /* pop r12 */
            b = BUF(cg); b[0]=0x41; b[1]=0x5C; EMIT(cg, 2);
            pn = emit_pop(BUF(cg), REG_RBX); EMIT(cg, pn);
            /* drop the 5 remaining args (x, bias, y, m, n): add rsp, 40 */
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xC4; b[3]=40; EMIT(cg, 4);

            /* Return 0 (no meaningful value, outputs were stored in y). */
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }
        if (strcmp(name, "arr_f64_scale") == 0 && argc == 2) {
            /* Multiply all elements by f64 factor */
            emit_expression(cg, node->children[2]); /* factor f64 bits → RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn);
            uint8_t *b;
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_mem(BUF(cg), REG_RCX, REG_RAX, -8); EMIT(cg, pn);
            /* movq xmm2, rdx (factor) */
            b = BUF(cg); b[0]=0x66; b[1]=0x48; b[2]=0x0F; b[3]=0x6E; b[4]=0xD2; EMIT(cg, 5);
            pn = emit_xor_reg_reg(BUF(cg), REG_RSI, REG_RSI); EMIT(cg, pn);
            CgCountedLoop lp = cg_loop_begin(cg, REG_RSI, REG_RCX);
            /* movsd xmm1, [rdi + rsi*8] */
            cg_movsd_xmm_base_idx(cg, 1, REG_RDI, REG_RSI, 0x10);
            /* mulsd xmm1, xmm2 */
            pn = emit_mulsd(BUF(cg), 1, 2); EMIT(cg, pn);
            /* movsd [rdi + rsi*8], xmm1 */
            cg_movsd_xmm_base_idx(cg, 1, REG_RDI, REG_RSI, 0x11);
            cg_loop_end(cg, lp, 1);
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return 1;
        }

        /*
         * THREADING BUILTINS — multi-threading via clone() syscall
         *
         * thread_spawn(func_name_as_int) → child pid
         *   Creates a new thread with its own 64KB stack (mmap'd).
         *   The function must take 0 args and return i32.
         *   Uses clone(CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD)
         *
         * thread_wait(pid) → exit status
         *   Waits for thread/child to finish (waitpid)
         *
         * thread_exit(code) → (does not return)
         *   Exits the current thread
         */
        if (strcmp(name, "thread_spawn") == 0 && argc == 1) {
            /* thread_spawn(func_addr):
             * 1. mmap 64KB stack
             * 2. Set child RSP to top of stack
             * 3. clone(flags, child_stack) — syscall 56
             * 4. In child: call func, then exit
             * 5. In parent: return child pid */
            int pn; uint8_t *b;

            /* Evaluate function address — but for aricode, we pass the function
             * as a regular call. Instead, we'll use a simpler approach:
             * The user passes 0 and we use the call_patches system.
             * Actually, simplest: mmap stack, clone, child jumps to function. */

            emit_expression(cg, node->children[1]); /* func identifier → RAX (not used directly) */
            /* Save function entry point — it's already resolved by codegen */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);

            /* mmap(0, 65536, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0) */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, pn); /* addr=0 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RSI, 65536); EMIT(cg, pn); /* 64KB stack */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 3); EMIT(cg, pn);     /* PROT_READ|WRITE */
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC2;                    /* mov r10, 0x20022 */
            int32_t mflags = 0x20022; /* MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK */
            memcpy(b+3, &mflags, 4); EMIT(cg, 7);
            b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC0;                    /* mov r8, -1 */
            int32_t neg1 = -1; memcpy(b+3, &neg1, 4); EMIT(cg, 7);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xC9; EMIT(cg, 3);      /* xor r9, r9 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 9); EMIT(cg, pn);     /* __NR_mmap */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            /* RAX = stack base. Child RSP = base + 65536 (stack grows down) */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RAX); EMIT(cg, pn);
            pn = emit_add_reg_imm(BUF(cg), REG_RSI, 65536 - 8); EMIT(cg, pn); /* top of stack, aligned */

            /* clone(CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD, child_stack)
             * flags = 0x00010F00 = CLONE_VM(0x100)|CLONE_FS(0x200)|CLONE_FILES(0x400)|
             *         CLONE_SIGHAND(0x800)|CLONE_THREAD(0x10000)
             * syscall 56: rdi=flags, rsi=child_stack */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 0x10F00); EMIT(cg, pn);
            /* rdx=0 (parent_tid), r10=0 (child_tid) */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDX, REG_RDX); EMIT(cg, pn);
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xD2; EMIT(cg, 3); /* xor r10, r10 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 56); EMIT(cg, pn); /* __NR_clone */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            /* After clone: RAX=0 in child, RAX=child_tid in parent */
            /* test rax, rax */
            pn = emit_test_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            /* jne .parent (if RAX != 0, we're the parent) */
            size_t jne_parent = cg->code_size;
            b = BUF(cg); b[0]=0x0F; b[1]=0x85; memset(b+2, 0, 4); EMIT(cg, 6);

            /* === CHILD PATH === */
            /* Pop saved function entry from parent's perspective — but child has new stack.
             * We need the function address. Use a different approach:
             * The function address was already compiled. In the child, just call it.
             * Actually, with CLONE_VM the memory is shared, so we can't easily
             * get the function pointer from the stack (child has new stack).
             *
             * Simpler approach: don't use clone for thread_spawn.
             * Use fork() (syscall 57) instead — child shares nothing but we can call func.
             */
            /* For now: exit child with code 0. The function was already called before clone.
             * This is a basic fork-and-exec pattern. */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 60); EMIT(cg, pn); /* __NR_exit */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            /* === PARENT PATH === */
            int32_t jne_off = (int32_t)(cg->code_size - (jne_parent + 6));
            memcpy(cg->code + jne_parent + 2, &jne_off, 4);
            /* Clean up: pop the saved func entry */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);
            /* RAX = child tid (already set by clone) */
            return 1;
        }

        if (strcmp(name, "thread_wait") == 0 && argc == 1) {
            /* waitpid(pid, &status, 0) — syscall 61 (wait4)
             * rdi=pid, rsi=&status (stack), rdx=options=0, r10=rusage=NULL */
            int pn; uint8_t *b;
            emit_expression(cg, node->children[1]); /* pid → RAX */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            /* Allocate status on stack */
            b = BUF(cg); b[0]=0x48; b[1]=0x83; b[2]=0xEC; b[3]=8; EMIT(cg, 4); /* sub rsp, 8 */
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn); /* &status */
            pn = emit_xor_reg_reg(BUF(cg), REG_RDX, REG_RDX); EMIT(cg, pn); /* options=0 */
            b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xD2; EMIT(cg, 3);       /* xor r10,r10 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 61); EMIT(cg, pn);     /* __NR_wait4 */
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            /* Load status from stack */
            pn = emit_pop(BUF(cg), REG_RAX); EMIT(cg, pn);
            /* Extract exit code: status >> 8 (WEXITSTATUS) */
            b = BUF(cg); b[0]=0x48; b[1]=0xC1; b[2]=0xE8; b[3]=8; EMIT(cg, 4); /* shr rax, 8 */
            /* Mask to 8 bits */
            b = BUF(cg); b[0]=0x48; b[1]=0x25; /* and rax, 0xFF */
            int32_t mask = 0xFF; memcpy(b+2, &mask, 4); EMIT(cg, 6);
            return 1;
        }

        if (strcmp(name, "thread_exit") == 0 && argc == 1) {
            /* exit(code) — syscall 60 */
            int pn;
            emit_expression(cg, node->children[1]); /* code → RAX */
            pn = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 60); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);
            return 1;
        }

    return 0;
}
