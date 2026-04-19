/*
 * aricode - Ari Code Language
 * x86_64 Machine Code Encoding Helpers
 *
 * Target: AMD Ryzen 7 5800X (Zen 3), Linux x86_64, System V ABI.
 *
 * Every emit_* function appends raw bytes to a code buffer and returns
 * the number of bytes written.  The caller must ensure sufficient space.
 */

#ifndef ARICODE_X86_64_H
#define ARICODE_X86_64_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Register encoding (low 3 bits used in ModR/M, REX.B for R8-R15)  */
/* ------------------------------------------------------------------ */

#define REG_RAX  0
#define REG_RCX  1
#define REG_RDX  2
#define REG_RBX  3
#define REG_RSP  4
#define REG_RBP  5
#define REG_RSI  6
#define REG_RDI  7
#define REG_R8   8
#define REG_R9   9
#define REG_R10  10
#define REG_R11  11
#define REG_R12  12
#define REG_R13  13
#define REG_R14  14
#define REG_R15  15

/* System V ABI: integer argument registers in order */
static const int SYS_V_ARG_REGS[] = {
    REG_RDI, REG_RSI, REG_RDX, REG_RCX, REG_R8, REG_R9
};
#define SYS_V_ARG_COUNT 6

/* ------------------------------------------------------------------ */
/*  Encoding helpers                                                  */
/* ------------------------------------------------------------------ */

/*
 * ModR/M byte: mod(2 bits) | reg(3 bits) | rm(3 bits)
 */
static inline uint8_t modrm(uint8_t mod, uint8_t reg, uint8_t rm) {
    return (uint8_t)((mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

/*
 * REX prefix for 64-bit operand size and/or extended registers.
 *   W = 1 for 64-bit operand size
 *   R = extends ModR/M reg field  (bit 3 of reg)
 *   X = extends SIB index field   (bit 3 of index)
 *   B = extends ModR/M rm field   (bit 3 of rm)
 */
static inline uint8_t rex(int w, int r, int x, int b) {
    return (uint8_t)(0x40 | (w << 3) | (r << 2) | (x << 1) | b);
}

/*
 * Does this register require REX.B / REX.R extension?
 */
static inline int reg_ext(int reg) {
    return (reg >> 3) & 1;
}

/*
 * Do we need a REX prefix for a 64-bit reg-reg operation?
 */
static inline int needs_rex64(int reg1, int reg2) {
    (void)reg1; (void)reg2;
    return 1; /* Always need REX.W for 64-bit operations */
}

/* ------------------------------------------------------------------ */
/*  Instruction emitters -- each returns bytes written                */
/* ------------------------------------------------------------------ */

/*
 * MOV reg, imm32  (32-bit immediate, zero-extended to 64 bits)
 * Opcode: [REX] B8+rd id
 * OPTIMIZATION: For zero, we use xor reg, reg instead (2-3 bytes vs 5-6).
 */
static inline int emit_mov_reg_imm32(uint8_t *buf, int reg, uint32_t value) {
    int off = 0;
    if (value == 0) {
        /* xor reg, reg -- breaks dependency, smaller encoding */
        if (reg >= 8) {
            buf[off++] = rex(0, reg_ext(reg), 0, reg_ext(reg));
        }
        buf[off++] = 0x31;
        buf[off++] = modrm(3, reg, reg);
        return off;
    }
    if (reg >= 8) {
        buf[off++] = rex(0, 0, 0, reg_ext(reg));
    }
    buf[off++] = (uint8_t)(0xB8 + (reg & 7));
    memcpy(buf + off, &value, 4);
    off += 4;
    return off;
}

/*
 * MOV reg, imm64  (64-bit immediate, needed for large constants)
 * Opcode: REX.W B8+rd io
 */
static inline int emit_mov_reg_imm64(uint8_t *buf, int reg, uint64_t value) {
    int off = 0;
    /* If it fits in 32 bits, use the shorter form */
    if (value <= UINT32_MAX) {
        return emit_mov_reg_imm32(buf, reg, (uint32_t)value);
    }
    buf[off++] = rex(1, 0, 0, reg_ext(reg));
    buf[off++] = (uint8_t)(0xB8 + (reg & 7));
    memcpy(buf + off, &value, 8);
    off += 8;
    return off;
}

/*
 * MOV reg, reg  (64-bit)
 * Opcode: REX.W 89 /r  (mov r/m64, r64)
 */
static inline int emit_mov_reg_reg(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = rex(1, reg_ext(src), 0, reg_ext(dst));
    buf[off++] = 0x89;
    buf[off++] = modrm(3, src, dst);
    return off;
}

/*
 * MOV [base + disp32], reg  (store 64-bit register to memory)
 */
static inline int emit_mov_mem_reg(uint8_t *buf, int base, int32_t disp, int src) {
    int off = 0;
    buf[off++] = rex(1, reg_ext(src), 0, reg_ext(base));
    buf[off++] = 0x89;
    if (base == REG_RSP || base == REG_R12) {
        /* RSP/R12 as base needs SIB byte */
        buf[off++] = modrm(2, src, 4); /* mod=10, rm=100 (SIB) */
        buf[off++] = 0x24;             /* SIB: scale=0, index=RSP(none), base=RSP */
    } else {
        buf[off++] = modrm(2, src, base); /* mod=10 (disp32) */
    }
    memcpy(buf + off, &disp, 4);
    off += 4;
    return off;
}

/*
 * MOV reg, [base + disp32]  (load 64-bit register from memory)
 */
static inline int emit_mov_reg_mem(uint8_t *buf, int dst, int base, int32_t disp) {
    int off = 0;
    buf[off++] = rex(1, reg_ext(dst), 0, reg_ext(base));
    buf[off++] = 0x8B;
    if (base == REG_RSP || base == REG_R12) {
        buf[off++] = modrm(2, dst, 4);
        buf[off++] = 0x24;
    } else {
        buf[off++] = modrm(2, dst, base);
    }
    memcpy(buf + off, &disp, 4);
    off += 4;
    return off;
}

/*
 * ADD dst, src  (64-bit)
 * Opcode: REX.W 01 /r
 */
static inline int emit_add_reg_reg(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = rex(1, reg_ext(src), 0, reg_ext(dst));
    buf[off++] = 0x01;
    buf[off++] = modrm(3, src, dst);
    return off;
}

/*
 * ADD dst, imm32  (64-bit)
 * OPTIMIZATION: Use short form 83 /0 ib for imm8, or 05 id for RAX.
 */
static inline int emit_add_reg_imm(uint8_t *buf, int dst, int32_t value) {
    int off = 0;
    if (value >= -128 && value <= 127) {
        /* Short form: REX.W 83 /0 ib */
        buf[off++] = rex(1, 0, 0, reg_ext(dst));
        buf[off++] = 0x83;
        buf[off++] = modrm(3, 0, dst);
        buf[off++] = (uint8_t)(int8_t)value;
        return off;
    }
    if (dst == REG_RAX) {
        /* RAX short form: REX.W 05 id */
        buf[off++] = rex(1, 0, 0, 0);
        buf[off++] = 0x05;
        memcpy(buf + off, &value, 4);
        off += 4;
        return off;
    }
    /* General form: REX.W 81 /0 id */
    buf[off++] = rex(1, 0, 0, reg_ext(dst));
    buf[off++] = 0x81;
    buf[off++] = modrm(3, 0, dst);
    memcpy(buf + off, &value, 4);
    off += 4;
    return off;
}

/*
 * SUB dst, src  (64-bit)
 * Opcode: REX.W 29 /r
 */
static inline int emit_sub_reg_reg(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = rex(1, reg_ext(src), 0, reg_ext(dst));
    buf[off++] = 0x29;
    buf[off++] = modrm(3, src, dst);
    return off;
}

/*
 * SUB dst, imm32  (64-bit)
 * OPTIMIZATION: Use short form 83 /5 ib for imm8.
 */
static inline int emit_sub_reg_imm(uint8_t *buf, int dst, int32_t value) {
    int off = 0;
    if (value >= -128 && value <= 127) {
        buf[off++] = rex(1, 0, 0, reg_ext(dst));
        buf[off++] = 0x83;
        buf[off++] = modrm(3, 5, dst);
        buf[off++] = (uint8_t)(int8_t)value;
        return off;
    }
    if (dst == REG_RAX) {
        buf[off++] = rex(1, 0, 0, 0);
        buf[off++] = 0x2D;
        memcpy(buf + off, &value, 4);
        off += 4;
        return off;
    }
    buf[off++] = rex(1, 0, 0, reg_ext(dst));
    buf[off++] = 0x81;
    buf[off++] = modrm(3, 5, dst);
    memcpy(buf + off, &value, 4);
    off += 4;
    return off;
}

/*
 * IMUL dst, src  (64-bit signed multiply)
 * Opcode: REX.W 0F AF /r
 */
static inline int emit_imul_reg_reg(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = rex(1, reg_ext(dst), 0, reg_ext(src));
    buf[off++] = 0x0F;
    buf[off++] = 0xAF;
    buf[off++] = modrm(3, dst, src);
    return off;
}

/*
 * PUSH reg
 * Opcode: [REX.B] 50+rd  (no REX.W needed, push is always 64-bit in long mode)
 */
static inline int emit_push(uint8_t *buf, int reg) {
    int off = 0;
    if (reg >= 8)
        buf[off++] = rex(0, 0, 0, 1);
    buf[off++] = (uint8_t)(0x50 + (reg & 7));
    return off;
}

/*
 * POP reg
 * Opcode: [REX.B] 58+rd
 */
static inline int emit_pop(uint8_t *buf, int reg) {
    int off = 0;
    if (reg >= 8)
        buf[off++] = rex(0, 0, 0, 1);
    buf[off++] = (uint8_t)(0x58 + (reg & 7));
    return off;
}

/*
 * RET
 * Opcode: C3
 */
static inline int emit_ret(uint8_t *buf) {
    buf[0] = 0xC3;
    return 1;
}

/*
 * SYSCALL
 * Opcode: 0F 05
 */
static inline int emit_syscall(uint8_t *buf) {
    buf[0] = 0x0F;
    buf[1] = 0x05;
    return 2;
}

/*
 * CMP reg, imm32  (64-bit)
 * OPTIMIZATION: Use short form 83 /7 ib for imm8.
 */
static inline int emit_cmp_reg_imm(uint8_t *buf, int reg, int32_t value) {
    int off = 0;
    if (value >= -128 && value <= 127) {
        buf[off++] = rex(1, 0, 0, reg_ext(reg));
        buf[off++] = 0x83;
        buf[off++] = modrm(3, 7, reg);
        buf[off++] = (uint8_t)(int8_t)value;
        return off;
    }
    if (reg == REG_RAX) {
        buf[off++] = rex(1, 0, 0, 0);
        buf[off++] = 0x3D;
        memcpy(buf + off, &value, 4);
        off += 4;
        return off;
    }
    buf[off++] = rex(1, 0, 0, reg_ext(reg));
    buf[off++] = 0x81;
    buf[off++] = modrm(3, 7, reg);
    memcpy(buf + off, &value, 4);
    off += 4;
    return off;
}

/*
 * CMP r1, r2  (64-bit)
 * Opcode: REX.W 39 /r
 */
static inline int emit_cmp_reg_reg(uint8_t *buf, int r1, int r2) {
    int off = 0;
    buf[off++] = rex(1, reg_ext(r2), 0, reg_ext(r1));
    buf[off++] = 0x39;
    buf[off++] = modrm(3, r2, r1);
    return off;
}

/*
 * JMP rel32
 * Opcode: E9 cd
 * `offset` is relative to the END of this instruction.
 */
static inline int emit_jmp(uint8_t *buf, int32_t offset) {
    buf[0] = 0xE9;
    memcpy(buf + 1, &offset, 4);
    return 5;
}

/*
 * JE rel32  (jump if equal / zero flag set)
 * Opcode: 0F 84 cd
 */
static inline int emit_je(uint8_t *buf, int32_t offset) {
    buf[0] = 0x0F;
    buf[1] = 0x84;
    memcpy(buf + 2, &offset, 4);
    return 6;
}

/*
 * JNE rel32
 * Opcode: 0F 85 cd
 */
static inline int emit_jne(uint8_t *buf, int32_t offset) {
    buf[0] = 0x0F;
    buf[1] = 0x85;
    memcpy(buf + 2, &offset, 4);
    return 6;
}

/*
 * JL rel32  (jump if less, signed)
 * Opcode: 0F 8C cd
 */
static inline int emit_jl(uint8_t *buf, int32_t offset) {
    buf[0] = 0x0F;
    buf[1] = 0x8C;
    memcpy(buf + 2, &offset, 4);
    return 6;
}

/*
 * JGE rel32  (jump if greater or equal, signed)
 * Opcode: 0F 8D cd
 */
static inline int emit_jge(uint8_t *buf, int32_t offset) {
    buf[0] = 0x0F;
    buf[1] = 0x8D;
    memcpy(buf + 2, &offset, 4);
    return 6;
}

/*
 * JG rel32  (jump if greater, signed)
 * Opcode: 0F 8F cd
 */
static inline int emit_jg(uint8_t *buf, int32_t offset) {
    buf[0] = 0x0F;
    buf[1] = 0x8F;
    memcpy(buf + 2, &offset, 4);
    return 6;
}

/*
 * JLE rel32  (jump if less or equal, signed)
 * Opcode: 0F 8E cd
 */
static inline int emit_jle(uint8_t *buf, int32_t offset) {
    buf[0] = 0x0F;
    buf[1] = 0x8E;
    memcpy(buf + 2, &offset, 4);
    return 6;
}

/*
 * CALL rel32
 * Opcode: E8 cd
 */
static inline int emit_call(uint8_t *buf, int32_t offset) {
    buf[0] = 0xE8;
    memcpy(buf + 1, &offset, 4);
    return 5;
}

/*
 * XOR dst, src  (64-bit)
 * Opcode: REX.W 31 /r
 * OPTIMIZATION: xor reg, reg is the canonical way to zero a register
 * (2-3 bytes, breaks false dependency, sets flags).
 */
static inline int emit_xor_reg_reg(uint8_t *buf, int dst, int src) {
    int off = 0;
    /* If zeroing a register, skip REX.W -- 32-bit xor zero-extends */
    if (dst == src && dst < 8) {
        buf[off++] = 0x31;
        buf[off++] = modrm(3, dst, dst);
        return off;
    }
    if (dst == src && dst >= 8) {
        buf[off++] = rex(0, reg_ext(dst), 0, reg_ext(dst));
        buf[off++] = 0x31;
        buf[off++] = modrm(3, dst, dst);
        return off;
    }
    buf[off++] = rex(1, reg_ext(src), 0, reg_ext(dst));
    buf[off++] = 0x31;
    buf[off++] = modrm(3, src, dst);
    return off;
}

/*
 * LEA dst, [base + disp32]  (64-bit)
 * Useful for add-with-constant optimization.
 */
static inline int emit_lea(uint8_t *buf, int dst, int base, int index,
                            int scale, int32_t disp) {
    int off = 0;
    (void)scale; /* simplified: no SIB unless needed */

    if (index < 0) {
        /* Simple: LEA dst, [base + disp32] */
        buf[off++] = rex(1, reg_ext(dst), 0, reg_ext(base));
        buf[off++] = 0x8D;
        if (base == REG_RSP || base == REG_R12) {
            buf[off++] = modrm(2, dst, 4);
            buf[off++] = 0x24;
        } else {
            buf[off++] = modrm(2, dst, base);
        }
        memcpy(buf + off, &disp, 4);
        off += 4;
    } else {
        /* SIB form: LEA dst, [base + index*scale + disp32] */
        int sc = 0;
        if (scale == 2) sc = 1;
        else if (scale == 4) sc = 2;
        else if (scale == 8) sc = 3;

        buf[off++] = rex(1, reg_ext(dst), reg_ext(index), reg_ext(base));
        buf[off++] = 0x8D;
        buf[off++] = modrm(2, dst, 4); /* mod=10, rm=100 -> SIB follows */
        buf[off++] = (uint8_t)((sc << 6) | ((index & 7) << 3) | (base & 7));
        memcpy(buf + off, &disp, 4);
        off += 4;
    }
    return off;
}

/*
 * CQO -- sign-extend RAX into RDX:RAX (needed before IDIV)
 * Opcode: REX.W 99
 */
static inline int emit_cqo(uint8_t *buf) {
    buf[0] = rex(1, 0, 0, 0);
    buf[1] = 0x99;
    return 2;
}

/*
 * IDIV r/m64 -- signed divide RDX:RAX by reg
 * Opcode: REX.W F7 /7
 * Result: quotient in RAX, remainder in RDX
 */
static inline int emit_idiv_reg(uint8_t *buf, int reg) {
    int off = 0;
    buf[off++] = rex(1, 0, 0, reg_ext(reg));
    buf[off++] = 0xF7;
    buf[off++] = modrm(3, 7, reg);
    return off;
}

/*
 * NEG reg  (64-bit two's complement negate)
 * Opcode: REX.W F7 /3
 */
static inline int emit_neg_reg(uint8_t *buf, int reg) {
    int off = 0;
    buf[off++] = rex(1, 0, 0, reg_ext(reg));
    buf[off++] = 0xF7;
    buf[off++] = modrm(3, 3, reg);
    return off;
}

/*
 * SETCC instructions -- set byte based on condition flags
 * Used after CMP to materialize boolean results.
 */
static inline int emit_sete(uint8_t *buf, int reg) {
    int off = 0;
    if (reg >= 8 || reg >= 4) /* need REX for SPL/BPL/SIL/DIL or R8+ */
        buf[off++] = rex(0, 0, 0, reg_ext(reg));
    buf[off++] = 0x0F;
    buf[off++] = 0x94;
    buf[off++] = modrm(3, 0, reg);
    return off;
}

static inline int emit_setne(uint8_t *buf, int reg) {
    int off = 0;
    if (reg >= 8 || reg >= 4)
        buf[off++] = rex(0, 0, 0, reg_ext(reg));
    buf[off++] = 0x0F;
    buf[off++] = 0x95;
    buf[off++] = modrm(3, 0, reg);
    return off;
}

static inline int emit_setl(uint8_t *buf, int reg) {
    int off = 0;
    if (reg >= 8 || reg >= 4)
        buf[off++] = rex(0, 0, 0, reg_ext(reg));
    buf[off++] = 0x0F;
    buf[off++] = 0x9C;
    buf[off++] = modrm(3, 0, reg);
    return off;
}

static inline int emit_setg(uint8_t *buf, int reg) {
    int off = 0;
    if (reg >= 8 || reg >= 4)
        buf[off++] = rex(0, 0, 0, reg_ext(reg));
    buf[off++] = 0x0F;
    buf[off++] = 0x9F;
    buf[off++] = modrm(3, 0, reg);
    return off;
}

static inline int emit_setle(uint8_t *buf, int reg) {
    int off = 0;
    if (reg >= 8 || reg >= 4)
        buf[off++] = rex(0, 0, 0, reg_ext(reg));
    buf[off++] = 0x0F;
    buf[off++] = 0x9E;
    buf[off++] = modrm(3, 0, reg);
    return off;
}

static inline int emit_setge(uint8_t *buf, int reg) {
    int off = 0;
    if (reg >= 8 || reg >= 4)
        buf[off++] = rex(0, 0, 0, reg_ext(reg));
    buf[off++] = 0x0F;
    buf[off++] = 0x9D;
    buf[off++] = modrm(3, 0, reg);
    return off;
}

/*
 * MOVZX r64, r/m8 -- zero-extend byte to 64-bit
 * Opcode: REX.W 0F B6 /r
 */
static inline int emit_movzx_reg_reg8(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = rex(1, reg_ext(dst), 0, reg_ext(src));
    buf[off++] = 0x0F;
    buf[off++] = 0xB6;
    buf[off++] = modrm(3, dst, src);
    return off;
}

/*
 * NOP
 * Opcode: 90
 */
static inline int emit_nop(uint8_t *buf) {
    buf[0] = 0x90;
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Bitwise and shift instructions                                    */
/* ------------------------------------------------------------------ */

/*
 * AND dst, src  (64-bit)
 * Opcode: REX.W 21 /r
 */
static inline int emit_and_reg_reg(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = rex(1, reg_ext(src), 0, reg_ext(dst));
    buf[off++] = 0x21;
    buf[off++] = modrm(3, src, dst);
    return off;
}

/*
 * AND dst, imm32  (64-bit)
 * Opcode: REX.W 81 /4 id  or  REX.W 83 /4 ib  (short form)
 */
static inline int emit_and_reg_imm(uint8_t *buf, int dst, int32_t value) {
    int off = 0;
    if (value >= -128 && value <= 127) {
        buf[off++] = rex(1, 0, 0, reg_ext(dst));
        buf[off++] = 0x83;
        buf[off++] = modrm(3, 4, dst);
        buf[off++] = (uint8_t)(int8_t)value;
        return off;
    }
    buf[off++] = rex(1, 0, 0, reg_ext(dst));
    buf[off++] = 0x81;
    buf[off++] = modrm(3, 4, dst);
    memcpy(buf + off, &value, 4);
    off += 4;
    return off;
}

/*
 * OR dst, src  (64-bit)
 * Opcode: REX.W 09 /r
 */
static inline int emit_or_reg_reg(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = rex(1, reg_ext(src), 0, reg_ext(dst));
    buf[off++] = 0x09;
    buf[off++] = modrm(3, src, dst);
    return off;
}

/*
 * OR dst, imm32  (64-bit)
 */
static inline int emit_or_reg_imm(uint8_t *buf, int dst, int32_t value) {
    int off = 0;
    if (value >= -128 && value <= 127) {
        buf[off++] = rex(1, 0, 0, reg_ext(dst));
        buf[off++] = 0x83;
        buf[off++] = modrm(3, 1, dst);
        buf[off++] = (uint8_t)(int8_t)value;
        return off;
    }
    buf[off++] = rex(1, 0, 0, reg_ext(dst));
    buf[off++] = 0x81;
    buf[off++] = modrm(3, 1, dst);
    memcpy(buf + off, &value, 4);
    off += 4;
    return off;
}

/*
 * TEST dst, imm32  (64-bit)
 * Opcode: REX.W F7 /0 id  (or  REX.W A9 id for RAX)
 * Sets flags based on AND without storing result.
 */
static inline int emit_test_reg_imm(uint8_t *buf, int reg, int32_t value) {
    int off = 0;
    if (reg == REG_RAX) {
        buf[off++] = rex(1, 0, 0, 0);
        buf[off++] = 0xA9;
        memcpy(buf + off, &value, 4);
        off += 4;
        return off;
    }
    buf[off++] = rex(1, 0, 0, reg_ext(reg));
    buf[off++] = 0xF7;
    buf[off++] = modrm(3, 0, reg);
    memcpy(buf + off, &value, 4);
    off += 4;
    return off;
}

/*
 * TEST dst, src  (64-bit)
 * Opcode: REX.W 85 /r
 */
static inline int emit_test_reg_reg(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = rex(1, reg_ext(src), 0, reg_ext(dst));
    buf[off++] = 0x85;
    buf[off++] = modrm(3, src, dst);
    return off;
}

/*
 * SHL dst, imm8  (64-bit shift left)
 * Opcode: REX.W C1 /4 ib
 */
static inline int emit_shl_reg_imm(uint8_t *buf, int dst, uint8_t count) {
    int off = 0;
    if (count == 1) {
        /* Short form: REX.W D1 /4 */
        buf[off++] = rex(1, 0, 0, reg_ext(dst));
        buf[off++] = 0xD1;
        buf[off++] = modrm(3, 4, dst);
        return off;
    }
    buf[off++] = rex(1, 0, 0, reg_ext(dst));
    buf[off++] = 0xC1;
    buf[off++] = modrm(3, 4, dst);
    buf[off++] = count;
    return off;
}

/*
 * SHR dst, imm8  (64-bit logical shift right)
 * Opcode: REX.W C1 /5 ib
 */
static inline int emit_shr_reg_imm(uint8_t *buf, int dst, uint8_t count) {
    int off = 0;
    if (count == 1) {
        buf[off++] = rex(1, 0, 0, reg_ext(dst));
        buf[off++] = 0xD1;
        buf[off++] = modrm(3, 5, dst);
        return off;
    }
    buf[off++] = rex(1, 0, 0, reg_ext(dst));
    buf[off++] = 0xC1;
    buf[off++] = modrm(3, 5, dst);
    buf[off++] = count;
    return off;
}

/*
 * SAR dst, imm8  (64-bit arithmetic shift right, preserves sign)
 * Opcode: REX.W C1 /7 ib
 */
static inline int emit_sar_reg_imm(uint8_t *buf, int dst, uint8_t count) {
    int off = 0;
    if (count == 1) {
        buf[off++] = rex(1, 0, 0, reg_ext(dst));
        buf[off++] = 0xD1;
        buf[off++] = modrm(3, 7, dst);
        return off;
    }
    buf[off++] = rex(1, 0, 0, reg_ext(dst));
    buf[off++] = 0xC1;
    buf[off++] = modrm(3, 7, dst);
    buf[off++] = count;
    return off;
}

/*
 * INC reg  (64-bit)
 * Opcode: REX.W FF /0
 */
static inline int emit_inc_reg(uint8_t *buf, int reg) {
    int off = 0;
    buf[off++] = rex(1, 0, 0, reg_ext(reg));
    buf[off++] = 0xFF;
    buf[off++] = modrm(3, 0, reg);
    return off;
}

/*
 * DEC reg  (64-bit)
 * Opcode: REX.W FF /1
 */
static inline int emit_dec_reg(uint8_t *buf, int reg) {
    int off = 0;
    buf[off++] = rex(1, 0, 0, reg_ext(reg));
    buf[off++] = 0xFF;
    buf[off++] = modrm(3, 1, reg);
    return off;
}

/* ------------------------------------------------------------------ */
/*  SSE2 floating-point instructions (f64 / double precision)         */
/* ------------------------------------------------------------------ */

/*
 * XMM register encoding: xmm0-xmm15
 * SSE instructions use the same register numbering as GPRs.
 * Prefix 0x66 selects double-precision (f64) variants.
 */

/*
 * MOVSD xmm, xmm  (move scalar double)
 * Opcode: F2 0F 10 /r
 */
static inline int emit_movsd_xmm_xmm(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = 0xF2;
    if (reg_ext(dst) || reg_ext(src))
        buf[off++] = rex(0, reg_ext(dst), 0, reg_ext(src));
    buf[off++] = 0x0F;
    buf[off++] = 0x10;
    buf[off++] = modrm(3, dst & 7, src & 7);
    return off;
}

/*
 * MOVAPD xmm, xmm  (move aligned packed double — register-to-register).
 * Opcode: 66 0F 28 /r
 *
 * Prefer this over MOVSD for register-to-register copies on Zen 3:
 * MOVSD preserves the upper 64 bits of the destination, creating a
 * false merge dependency that defeats the zero-latency renamer.
 * MOVAPD is treated as a pure rename (zero latency, no port use).
 */
static inline int emit_movapd_xmm_xmm(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = 0x66;
    if (reg_ext(dst) || reg_ext(src))
        buf[off++] = rex(0, reg_ext(dst), 0, reg_ext(src));
    buf[off++] = 0x0F;
    buf[off++] = 0x28;
    buf[off++] = modrm(3, dst & 7, src & 7);
    return off;
}

/*
 * MOVSD xmm, [base + disp32]  (load f64 from memory)
 * Opcode: F2 [REX] 0F 10 /r
 */
static inline int emit_movsd_xmm_mem(uint8_t *buf, int dst, int base, int32_t disp) {
    int off = 0;
    buf[off++] = 0xF2;
    if (reg_ext(dst) || reg_ext(base))
        buf[off++] = rex(0, reg_ext(dst), 0, reg_ext(base));
    buf[off++] = 0x0F;
    buf[off++] = 0x10;
    if (base == REG_RSP || base == REG_R12) {
        buf[off++] = modrm(2, dst & 7, 4);
        buf[off++] = 0x24;
    } else {
        buf[off++] = modrm(2, dst & 7, base & 7);
    }
    memcpy(buf + off, &disp, 4);
    off += 4;
    return off;
}

/*
 * MOVSD [base + disp32], xmm  (store f64 to memory)
 * Opcode: F2 [REX] 0F 11 /r
 */
static inline int emit_movsd_mem_xmm(uint8_t *buf, int base, int32_t disp, int src) {
    int off = 0;
    buf[off++] = 0xF2;
    if (reg_ext(src) || reg_ext(base))
        buf[off++] = rex(0, reg_ext(src), 0, reg_ext(base));
    buf[off++] = 0x0F;
    buf[off++] = 0x11;
    if (base == REG_RSP || base == REG_R12) {
        buf[off++] = modrm(2, src & 7, 4);
        buf[off++] = 0x24;
    } else {
        buf[off++] = modrm(2, src & 7, base & 7);
    }
    memcpy(buf + off, &disp, 4);
    off += 4;
    return off;
}

/*
 * ADDSD xmm, xmm  (add scalar double)
 * Opcode: F2 0F 58 /r
 */
static inline int emit_addsd(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = 0xF2;
    if (reg_ext(dst) || reg_ext(src))
        buf[off++] = rex(0, reg_ext(dst), 0, reg_ext(src));
    buf[off++] = 0x0F;
    buf[off++] = 0x58;
    buf[off++] = modrm(3, dst & 7, src & 7);
    return off;
}

/*
 * SUBSD xmm, xmm  (subtract scalar double)
 * Opcode: F2 0F 5C /r
 */
static inline int emit_subsd(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = 0xF2;
    if (reg_ext(dst) || reg_ext(src))
        buf[off++] = rex(0, reg_ext(dst), 0, reg_ext(src));
    buf[off++] = 0x0F;
    buf[off++] = 0x5C;
    buf[off++] = modrm(3, dst & 7, src & 7);
    return off;
}

/*
 * MULSD xmm, xmm  (multiply scalar double)
 * Opcode: F2 0F 59 /r
 */
static inline int emit_mulsd(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = 0xF2;
    if (reg_ext(dst) || reg_ext(src))
        buf[off++] = rex(0, reg_ext(dst), 0, reg_ext(src));
    buf[off++] = 0x0F;
    buf[off++] = 0x59;
    buf[off++] = modrm(3, dst & 7, src & 7);
    return off;
}

/*
 * DIVSD xmm, xmm  (divide scalar double)
 * Opcode: F2 0F 5E /r
 */
static inline int emit_divsd(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = 0xF2;
    if (reg_ext(dst) || reg_ext(src))
        buf[off++] = rex(0, reg_ext(dst), 0, reg_ext(src));
    buf[off++] = 0x0F;
    buf[off++] = 0x5E;
    buf[off++] = modrm(3, dst & 7, src & 7);
    return off;
}

/*
 * UCOMISD xmm, xmm  (unordered compare scalar double, sets EFLAGS)
 * Opcode: 66 0F 2E /r
 */
static inline int emit_ucomisd(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = 0x66;
    if (reg_ext(dst) || reg_ext(src))
        buf[off++] = rex(0, reg_ext(dst), 0, reg_ext(src));
    buf[off++] = 0x0F;
    buf[off++] = 0x2E;
    buf[off++] = modrm(3, dst & 7, src & 7);
    return off;
}

/*
 * CVTSI2SD xmm, reg  (convert signed i64 to f64)
 * Opcode: F2 REX.W 0F 2A /r
 */
static inline int emit_cvtsi2sd(uint8_t *buf, int xmm_dst, int gpr_src) {
    int off = 0;
    buf[off++] = 0xF2;
    buf[off++] = rex(1, reg_ext(xmm_dst), 0, reg_ext(gpr_src));
    buf[off++] = 0x0F;
    buf[off++] = 0x2A;
    buf[off++] = modrm(3, xmm_dst & 7, gpr_src & 7);
    return off;
}

/*
 * CVTTSD2SI reg, xmm  (convert f64 to signed i64, truncate)
 * Opcode: F2 REX.W 0F 2C /r
 */
static inline int emit_cvttsd2si(uint8_t *buf, int gpr_dst, int xmm_src) {
    int off = 0;
    buf[off++] = 0xF2;
    buf[off++] = rex(1, reg_ext(gpr_dst), 0, reg_ext(xmm_src));
    buf[off++] = 0x0F;
    buf[off++] = 0x2C;
    buf[off++] = modrm(3, gpr_dst & 7, xmm_src & 7);
    return off;
}

/*
 * MOVQ reg, xmm  (move quadword from XMM to GPR)
 * Opcode: 66 REX.W 0F 7E /r
 */
static inline int emit_movq_reg_xmm(uint8_t *buf, int gpr, int xmm) {
    int off = 0;
    buf[off++] = 0x66;
    buf[off++] = rex(1, reg_ext(xmm), 0, reg_ext(gpr));
    buf[off++] = 0x0F;
    buf[off++] = 0x7E;
    buf[off++] = modrm(3, xmm & 7, gpr & 7);
    return off;
}

/*
 * MOVQ xmm, reg  (move quadword from GPR to XMM)
 * Opcode: 66 REX.W 0F 6E /r
 */
static inline int emit_movq_xmm_reg(uint8_t *buf, int xmm, int gpr) {
    int off = 0;
    buf[off++] = 0x66;
    buf[off++] = rex(1, reg_ext(xmm), 0, reg_ext(gpr));
    buf[off++] = 0x0F;
    buf[off++] = 0x6E;
    buf[off++] = modrm(3, xmm & 7, gpr & 7);
    return off;
}

/*
 * SQRTSD xmm, xmm  (square root of scalar double)
 * Opcode: F2 0F 51 /r
 */
static inline int emit_sqrtsd(uint8_t *buf, int dst, int src) {
    int off = 0;
    buf[off++] = 0xF2;
    if (reg_ext(dst) || reg_ext(src))
        buf[off++] = rex(0, reg_ext(dst), 0, reg_ext(src));
    buf[off++] = 0x0F;
    buf[off++] = 0x51;
    buf[off++] = modrm(3, dst & 7, src & 7);
    return off;
}

#endif /* ARICODE_X86_64_H */
