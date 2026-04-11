/*
 * aricode - Ari Code Language
 * Code Generator: AST -> x86_64 Machine Code
 *
 * Walks the AST and emits raw x86_64 instructions into a flat buffer.
 * After all functions are emitted, a _start stub is appended that calls
 * main() and exits via syscall(60, retval).
 *
 * Register allocation strategy:
 *   - Expressions evaluate into RAX (accumulator).
 *   - Binary ops: left -> RAX -> push; right -> RAX; pop left into RCX.
 *   - Function args: System V ABI (RDI, RSI, RDX, RCX, R8, R9).
 *   - Locals live on the stack, addressed as [RBP - offset].
 *
 * OPTIMIZATION NOTES (Zen 3 tuned):
 *   - xor reg, reg for zeroing (breaks false deps, 1 uop)
 *   - imm8 forms for small constants (saves 3 bytes per instruction)
 *   - 32-bit ops where safe (avoids REX prefix)
 *   - LEA for add-with-constant when beneficial
 */

#include "codegen.h"
#include "optimizer.h"
#include "x86_64.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

#define EMIT(cg, count) do { (cg)->code_size += (count); } while (0)
#define BUF(cg) ((cg)->code + (cg)->code_size)

static void cg_error(CodegenState *cg, const char *fmt, ...) {
    if (cg->had_error) return;
    cg->had_error = 1;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cg->error_msg, sizeof(cg->error_msg), fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------ */
/*  Forward declarations                                              */
/* ------------------------------------------------------------------ */

/* Returns 0=int, 1=float to indicate result type */
static int  emit_expression(CodegenState *cg, const ASTNode *node);
static void emit_statement(CodegenState *cg, const ASTNode *node);
static void emit_block(CodegenState *cg, const ASTNode *node);

/* ------------------------------------------------------------------ */
/*  Symbol lookup                                                     */
/* ------------------------------------------------------------------ */

static LocalVar *find_local(CodegenState *cg, const char *name) {
    /* Search from the END to find the most recently declared variable.
     * This is critical for variables declared inside while loops --
     * each iteration creates a new stack slot, and we must always
     * reference the latest one. */
    for (size_t i = cg->local_count; i > 0; i--) {
        if (strcmp(cg->locals[i - 1].name, name) == 0)
            return &cg->locals[i - 1];
    }
    return NULL;
}

static FuncEntry *find_func(CodegenState *cg, const char *name) {
    for (size_t i = 0; i < cg->func_count; i++) {
        if (strcmp(cg->funcs[i].name, name) == 0)
            return &cg->funcs[i];
    }
    return NULL;
}

static LocalVar *add_local(CodegenState *cg, const char *name) {
    if (cg->local_count >= CODEGEN_MAX_VARS) {
        cg_error(cg, "too many local variables");
        return NULL;
    }
    cg->stack_offset -= 8; /* each local takes 8 bytes */
    LocalVar *v = &cg->locals[cg->local_count++];
    v->name    = name;
    v->rbp_off = cg->stack_offset;
    return v;
}

/* ------------------------------------------------------------------ */
/*  Expression codegen                                                */
/* ------------------------------------------------------------------ */

/*
 * After emit_expression(), the result is in RAX.
 */

/*
 * Emit a f64 literal by embedding its 8-byte IEEE 754 representation
 * in the code, jumping over it, then loading with MOVSD via RIP-relative.
 * The f64 value lives in xmm0 but we also store the bits in RAX for
 * compatibility with the stack-based variable system.
 */
static void emit_float_literal(CodegenState *cg, const ASTNode *node) {
    double val = node->float_val;
    uint64_t bits;
    memcpy(&bits, &val, 8);
    int n;

    /* mov rax, imm64 (the IEEE 754 bits) - for stack storage */
    n = emit_mov_reg_imm64(BUF(cg), REG_RAX, bits);
    EMIT(cg, n);

    /* Load into xmm0: movq xmm0, rax */
    n = emit_movq_xmm_reg(BUF(cg), 0, REG_RAX);
    EMIT(cg, n);
}

static void emit_int_literal(CodegenState *cg, const ASTNode *node) {
    int64_t val = node->int_val;
    if (val == 0) {
        /* OPTIMIZATION: xor eax, eax */
        int n = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX);
        EMIT(cg, n);
    } else if (val > 0 && val <= UINT32_MAX) {
        /* 32-bit mov (zero-extends to 64-bit, no REX needed) */
        int n = emit_mov_reg_imm32(BUF(cg), REG_RAX, (uint32_t)val);
        EMIT(cg, n);
    } else {
        /* Full 64-bit immediate */
        int n = emit_mov_reg_imm64(BUF(cg), REG_RAX, (uint64_t)val);
        EMIT(cg, n);
    }
}

static int emit_identifier(CodegenState *cg, const ASTNode *node) {
    LocalVar *v = find_local(cg, node->string_val);
    if (!v) {
        cg_error(cg, "undefined variable '%s' at %d:%d",
                 node->string_val, node->line, node->col);
        return 0;
    }
    /* Load from stack: mov rax, [rbp + offset] */
    int n = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RBP, v->rbp_off);
    EMIT(cg, n);

    /* If float, also load into xmm0 */
    if (v->is_float) {
        n = emit_movq_xmm_reg(BUF(cg), 0, REG_RAX);
        EMIT(cg, n);
    }
    return v->is_float;
}

/*
 * PEEPHOLE: Check if binary op right operand is an immediate constant.
 * If so, we can avoid the push/pop dance and use reg,imm instructions.
 */
static int right_is_imm(const ASTNode *node) {
    if (node->child_count < 2) return 0;
    return node->children[1]->type == NODE_INT_LITERAL ||
           node->children[1]->type == NODE_BOOL_LITERAL;
}

/*
 * PEEPHOLE: Check if left operand is an immediate constant and right is not.
 * For commutative ops (+ *) we can swap to use the reg,imm form.
 */
static int is_commutative(const char *op) {
    return strcmp(op, "+") == 0 || strcmp(op, "*") == 0 ||
           strcmp(op, "==") == 0 || strcmp(op, "!=") == 0;
}

static void emit_binary_op(CodegenState *cg, const ASTNode *node) {
    const char *op = node->op;
    if (!op || node->child_count < 2) {
        cg_error(cg, "malformed binary op at %d:%d", node->line, node->col);
        return;
    }

    ASTNode *left  = node->children[0];
    ASTNode *right = node->children[1];
    int n;

    /*
     * FLOAT PATH: If either operand is float, use SSE instructions.
     * Convention: left in xmm0, right in xmm1, result in xmm0.
     * Bits also stored in RAX for stack variable compatibility.
     */
    int left_is_float = (left->type == NODE_FLOAT_LITERAL) ||
        (left->type == NODE_IDENTIFIER && find_local(cg, left->string_val) &&
         find_local(cg, left->string_val)->is_float);
    int right_is_float = (right->type == NODE_FLOAT_LITERAL) ||
        (right->type == NODE_IDENTIFIER && find_local(cg, right->string_val) &&
         find_local(cg, right->string_val)->is_float);

    if (left_is_float || right_is_float) {
        /* Evaluate left -> xmm0 */
        emit_expression(cg, left);
        /* Save xmm0 via RAX -> stack */
        n = emit_movq_reg_xmm(BUF(cg), REG_RAX, 0); EMIT(cg, n);
        n = emit_push(BUF(cg), REG_RAX); EMIT(cg, n);

        /* Evaluate right -> xmm0 */
        emit_expression(cg, right);
        /* xmm1 = right (xmm0) */
        n = emit_movsd_xmm_xmm(BUF(cg), 1, 0); EMIT(cg, n);

        /* Pop left into xmm0 via stack */
        n = emit_movsd_xmm_mem(BUF(cg), 0, REG_RSP, 0); EMIT(cg, n);
        n = emit_add_reg_imm(BUF(cg), REG_RSP, 8); EMIT(cg, n);

        /* xmm0 = left, xmm1 = right */
        if (strcmp(op, "+") == 0) {
            n = emit_addsd(BUF(cg), 0, 1); EMIT(cg, n);
        } else if (strcmp(op, "-") == 0) {
            n = emit_subsd(BUF(cg), 0, 1); EMIT(cg, n);
        } else if (strcmp(op, "*") == 0) {
            n = emit_mulsd(BUF(cg), 0, 1); EMIT(cg, n);
        } else if (strcmp(op, "/") == 0) {
            n = emit_divsd(BUF(cg), 0, 1); EMIT(cg, n);
        } else if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
                   strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
                   strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
            /* ucomisd sets CF and ZF */
            n = emit_ucomisd(BUF(cg), 0, 1); EMIT(cg, n);
            if (strcmp(op, "<") == 0) {
                /* below: CF=1 */
                uint8_t *b = BUF(cg);
                b[0] = 0x0F; b[1] = 0x92; b[2] = modrm(3, 0, REG_RAX); EMIT(cg, 3);
            } else if (strcmp(op, ">") == 0) {
                /* above: CF=0 and ZF=0 */
                uint8_t *b = BUF(cg);
                b[0] = 0x0F; b[1] = 0x97; b[2] = modrm(3, 0, REG_RAX); EMIT(cg, 3);
            } else if (strcmp(op, "==") == 0) {
                n = emit_sete(BUF(cg), REG_RAX); EMIT(cg, n);
            } else if (strcmp(op, "!=") == 0) {
                n = emit_setne(BUF(cg), REG_RAX); EMIT(cg, n);
            } else if (strcmp(op, "<=") == 0) {
                uint8_t *b = BUF(cg);
                b[0] = 0x0F; b[1] = 0x96; b[2] = modrm(3, 0, REG_RAX); EMIT(cg, 3);
            } else { /* >= */
                uint8_t *b = BUF(cg);
                b[0] = 0x0F; b[1] = 0x93; b[2] = modrm(3, 0, REG_RAX); EMIT(cg, 3);
            }
            n = emit_movzx_reg_reg8(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, n);
            return; /* comparison returns int, not float */
        } else {
            cg_error(cg, "unsupported float operator '%s'", op);
            return;
        }

        /* Move result bits from xmm0 to RAX for stack storage */
        n = emit_movq_reg_xmm(BUF(cg), REG_RAX, 0); EMIT(cg, n);
        return;
    }

    /*
     * INTEGER PATH below
     * PEEPHOLE OPTIMIZATION: Immediate operand path
     * If the right side is a constant, skip push/pop and use add rax, imm
     * For commutative ops, also handle left-constant case by swapping.
     */
    int use_imm = 0;
    int32_t imm_val = 0;

    if (right_is_imm(node)) {
        /* Right is constant: evaluate left -> RAX, then op RAX, imm */
        use_imm = 1;
        imm_val = (int32_t)right->int_val;
        emit_expression(cg, left);
    } else if (left->type == NODE_INT_LITERAL && is_commutative(op)) {
        /* Left is constant, op is commutative: evaluate right -> RAX, op RAX, imm */
        use_imm = 1;
        imm_val = (int32_t)left->int_val;
        emit_expression(cg, right);
    }

    if (use_imm) {
        /* ADD rax, imm / SUB rax, imm / CMP rax, imm */
        if (strcmp(op, "+") == 0) {
            n = emit_add_reg_imm(BUF(cg), REG_RAX, imm_val);
            EMIT(cg, n);
            return;
        } else if (strcmp(op, "-") == 0) {
            n = emit_sub_reg_imm(BUF(cg), REG_RAX, imm_val);
            EMIT(cg, n);
            return;
        } else if (strcmp(op, "*") == 0 && imm_val > 0) {
            /* IMUL rax, rax, imm32 -- 3-operand form */
            /* Opcode: REX.W 69 /r id */
            uint8_t *b = BUF(cg);
            int off = 0;
            b[off++] = rex(1, reg_ext(REG_RAX), 0, reg_ext(REG_RAX));
            b[off++] = 0x69;
            b[off++] = modrm(3, REG_RAX, REG_RAX);
            memcpy(b + off, &imm_val, 4);
            off += 4;
            EMIT(cg, off);
            return;
        }
        /* Bitwise with immediate */
        else if (strcmp(op, "&") == 0) {
            n = emit_and_reg_imm(BUF(cg), REG_RAX, imm_val);
            EMIT(cg, n);
            return;
        } else if (strcmp(op, "|") == 0) {
            n = emit_or_reg_imm(BUF(cg), REG_RAX, imm_val);
            EMIT(cg, n);
            return;
        } else if (strcmp(op, ">>") == 0) {
            n = emit_sar_reg_imm(BUF(cg), REG_RAX, (uint8_t)imm_val);
            EMIT(cg, n);
            return;
        } else if (strcmp(op, "<<") == 0) {
            n = emit_shl_reg_imm(BUF(cg), REG_RAX, (uint8_t)imm_val);
            EMIT(cg, n);
            return;
        }
        /* For comparisons with immediate, use cmp rax, imm */
        else if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
                 strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
                 strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
            n = emit_cmp_reg_imm(BUF(cg), REG_RAX, imm_val);
            EMIT(cg, n);
            goto emit_setcc;
        }
        /* Fall through to general path for other ops */
        /* We already evaluated one side into RAX, need to re-do general path */
        /* Actually for div/mod with imm, we need the register path */
        /* Load imm into RCX manually */
        if (strcmp(op, "/") == 0 || strcmp(op, "%") == 0) {
            n = emit_mov_reg_imm32(BUF(cg), REG_RCX, (uint32_t)imm_val);
            EMIT(cg, n);
            goto do_div_mod;
        }
    }

    /* ---- GENERAL PATH: both sides non-constant ---- */
    if (!use_imm) {
        /* Evaluate left operand -> RAX */
        emit_expression(cg, left);

        /* Push left result */
        n = emit_push(BUF(cg), REG_RAX);
        EMIT(cg, n);

        /* Evaluate right operand -> RAX */
        emit_expression(cg, right);

        /* Move right to RCX, pop left into RAX */
        n = emit_mov_reg_reg(BUF(cg), REG_RCX, REG_RAX);
        EMIT(cg, n);

        n = emit_pop(BUF(cg), REG_RAX);
        EMIT(cg, n);
    }

    /* Now: RAX = left, RCX = right */

    if (strcmp(op, "+") == 0) {
        n = emit_add_reg_reg(BUF(cg), REG_RAX, REG_RCX);
        EMIT(cg, n);
    } else if (strcmp(op, "-") == 0) {
        n = emit_sub_reg_reg(BUF(cg), REG_RAX, REG_RCX);
        EMIT(cg, n);
    } else if (strcmp(op, "*") == 0) {
        n = emit_imul_reg_reg(BUF(cg), REG_RAX, REG_RCX);
        EMIT(cg, n);
    } else if (strcmp(op, "/") == 0 || strcmp(op, "%") == 0) {
do_div_mod:
        n = emit_cqo(BUF(cg));
        EMIT(cg, n);
        n = emit_idiv_reg(BUF(cg), REG_RCX);
        EMIT(cg, n);
        if (strcmp(op, "%") == 0) {
            n = emit_mov_reg_reg(BUF(cg), REG_RAX, REG_RDX);
            EMIT(cg, n);
        }
    } else if (strcmp(op, "&") == 0) {
        n = emit_and_reg_reg(BUF(cg), REG_RAX, REG_RCX);
        EMIT(cg, n);
    } else if (strcmp(op, "|") == 0) {
        n = emit_or_reg_reg(BUF(cg), REG_RAX, REG_RCX);
        EMIT(cg, n);
    } else if (strcmp(op, "^") == 0) {
        n = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RCX);
        EMIT(cg, n);
    } else if (strcmp(op, "<<") == 0) {
        /* SHL RAX, CL (shift count in CL register) */
        uint8_t *b = BUF(cg);
        b[0] = rex(1, 0, 0, 0); b[1] = 0xD3; b[2] = modrm(3, 4, REG_RAX);
        EMIT(cg, 3);
    } else if (strcmp(op, ">>") == 0) {
        /* SAR RAX, CL (arithmetic shift right, preserves sign) */
        uint8_t *b = BUF(cg);
        b[0] = rex(1, 0, 0, 0); b[1] = 0xD3; b[2] = modrm(3, 7, REG_RAX);
        EMIT(cg, 3);
    } else if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
               strcmp(op, "<") == 0  || strcmp(op, ">") == 0  ||
               strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) {
        n = emit_cmp_reg_reg(BUF(cg), REG_RAX, REG_RCX);
        EMIT(cg, n);

emit_setcc:
        if (strcmp(op, "==") == 0) {
            n = emit_sete(BUF(cg), REG_RAX);
        } else if (strcmp(op, "!=") == 0) {
            n = emit_setne(BUF(cg), REG_RAX);
        } else if (strcmp(op, "<") == 0) {
            n = emit_setl(BUF(cg), REG_RAX);
        } else if (strcmp(op, ">") == 0) {
            n = emit_setg(BUF(cg), REG_RAX);
        } else if (strcmp(op, "<=") == 0) {
            n = emit_setle(BUF(cg), REG_RAX);
        } else { /* >= */
            n = emit_setge(BUF(cg), REG_RAX);
        }
        EMIT(cg, n);

        /* Zero-extend AL to RAX */
        n = emit_movzx_reg_reg8(BUF(cg), REG_RAX, REG_RAX);
        EMIT(cg, n);
    } else {
        cg_error(cg, "unknown binary operator '%s' at %d:%d",
                 op, node->line, node->col);
    }
}

static void emit_unary_op(CodegenState *cg, const ASTNode *node) {
    if (!node->op || node->child_count < 1) {
        cg_error(cg, "malformed unary op at %d:%d", node->line, node->col);
        return;
    }

    emit_expression(cg, node->children[0]);

    if (strcmp(node->op, "-") == 0) {
        int n = emit_neg_reg(BUF(cg), REG_RAX);
        EMIT(cg, n);
    } else if (strcmp(node->op, "!") == 0) {
        /* Logical not: compare with 0, sete */
        int n = emit_cmp_reg_imm(BUF(cg), REG_RAX, 0);
        EMIT(cg, n);
        n = emit_sete(BUF(cg), REG_RAX);
        EMIT(cg, n);
        n = emit_movzx_reg_reg8(BUF(cg), REG_RAX, REG_RAX);
        EMIT(cg, n);
    } else {
        cg_error(cg, "unknown unary operator '%s'", node->op);
    }
}

/*
 * BUILTIN: print_str("text")
 * Prints a string literal to stdout followed by a newline.
 * Embeds the string directly in the code segment:
 *   jmp over_string    ; skip the data
 *   .data: "text\n"    ; string bytes
 *   over_string:
 *   lea rsi, [rip - offset]  ; point back to the string
 *   mov rdx, len
 *   mov rdi, 1         ; stdout
 *   mov rax, 1         ; __NR_write
 *   syscall
 */
static void emit_builtin_print_str(CodegenState *cg, const ASTNode *arg) {
    if (!arg || arg->type != NODE_STRING_LITERAL || !arg->string_val) {
        cg_error(cg, "print_str requires a string literal argument");
        return;
    }

    const char *str = arg->string_val;
    size_t slen = strlen(str);
    size_t total_len = slen + 1;  /* string + newline */
    int n;

    /* JMP over the string data */
    size_t jmp_pos = cg->code_size;
    n = emit_jmp(BUF(cg), 0);  /* placeholder */
    EMIT(cg, n);

    /* Embed string bytes + newline */
    size_t str_pos = cg->code_size;
    memcpy(BUF(cg), str, slen);
    cg->code_size += slen;
    BUF(cg)[0] = '\n';
    cg->code_size += 1;

    /* Patch JMP to land here */
    int32_t jmp_off = (int32_t)(cg->code_size - (jmp_pos + 5));
    memcpy(cg->code + jmp_pos + 1, &jmp_off, 4);

    /* lea rsi, [rip - offset]  -- point back to str_pos */
    /* RIP-relative: offset = current_pos + 7 (size of lea) - str_pos, negated */
    int32_t rip_off = (int32_t)((int64_t)str_pos - (int64_t)(cg->code_size + 7));
    uint8_t *b = BUF(cg);
    b[0] = rex(1, reg_ext(REG_RSI), 0, 0);
    b[1] = 0x8D;
    b[2] = modrm(0, REG_RSI, 5);  /* mod=00, rm=101 = RIP-relative */
    memcpy(b + 3, &rip_off, 4);
    EMIT(cg, 7);

    /* mov rdx, total_len */
    n = emit_mov_reg_imm32(BUF(cg), REG_RDX, (uint32_t)total_len);
    EMIT(cg, n);

    /* mov rdi, 1 (stdout) */
    n = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1);
    EMIT(cg, n);

    /* mov rax, 1 (__NR_write) */
    n = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1);
    EMIT(cg, n);

    /* syscall */
    n = emit_syscall(BUF(cg));
    EMIT(cg, n);

    /* Return 0 */
    n = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX);
    EMIT(cg, n);
}

/*
 * BUILTIN: print_int(n)
 * Prints an integer to stdout followed by a newline.
 * Uses a 24-byte stack buffer, fills digits right-to-left,
 * then calls sys_write(1, buf, len).
 *
 * This makes aricode a REAL language - it can produce output.
 */
static void emit_builtin_print_int(CodegenState *cg, const ASTNode *arg) {
    int n;
    uint8_t *b;

    /* Evaluate argument -> RAX */
    emit_expression(cg, arg);

    /* sub rsp, 24  -- allocate stack buffer */
    n = emit_sub_reg_imm(BUF(cg), REG_RSP, 24);
    EMIT(cg, n);

    /* r10 = write position (starts at end: rsp+22 for newline) */
    /* lea r10, [rsp+23] */
    b = BUF(cg);
    b[0] = rex(1, 1, 0, 0);  /* REX.WR */
    b[1] = 0x8D;
    b[2] = modrm(1, REG_R10 & 7, REG_RSP);  /* mod=01, disp8 */
    b[3] = 0x24;  /* SIB: base=RSP */
    b[4] = 23;    /* disp8 */
    EMIT(cg, 5);

    /* mov byte [r10], 0x0A  -- newline at end */
    b = BUF(cg);
    b[0] = 0x41; b[1] = 0xC6; b[2] = 0x02; b[3] = 0x0A;
    EMIT(cg, 4);

    /* r11 = 1 (length starts at 1 for newline) */
    b = BUF(cg);
    b[0] = rex(1, 0, 0, 1); /* REX.WB */
    b[1] = 0xC7;
    b[2] = modrm(3, 0, REG_R11 & 7);
    int32_t one = 1;
    memcpy(b + 3, &one, 4);
    EMIT(cg, 7);

    /* Check if negative: test rax, rax */
    n = emit_test_reg_reg(BUF(cg), REG_RAX, REG_RAX);
    EMIT(cg, n);

    /* Save sign flag in r9 (0 = positive, 1 = negative) */
    /* setns would be complex, just use jns to skip neg */
    /* mov r9d, 0 */
    b = BUF(cg);
    b[0] = 0x41; b[1] = 0xB9; memset(b+2, 0, 4);
    EMIT(cg, 6);

    /* jns .positive */
    size_t jns_pos = cg->code_size;
    b = BUF(cg); b[0] = 0x79; b[1] = 0x00;
    EMIT(cg, 2);

    /* neg rax */
    n = emit_neg_reg(BUF(cg), REG_RAX);
    EMIT(cg, n);

    /* mov r9d, 1 (flag: was negative) */
    b = BUF(cg);
    b[0] = 0x41; b[1] = 0xB9;
    int32_t ione = 1; memcpy(b+2, &ione, 4);
    EMIT(cg, 6);

    /* patch jns */
    cg->code[jns_pos + 1] = (uint8_t)(cg->code_size - (jns_pos + 2));

    /* Handle zero specially */
    n = emit_test_reg_reg(BUF(cg), REG_RAX, REG_RAX);
    EMIT(cg, n);
    size_t jnz_pos = cg->code_size;
    b = BUF(cg); b[0] = 0x75; b[1] = 0x00;
    EMIT(cg, 2);

    /* Zero case: put '0' before newline */
    /* dec r10 */
    n = emit_dec_reg(BUF(cg), REG_R10);
    EMIT(cg, n);
    /* mov byte [r10], '0' */
    b = BUF(cg);
    b[0] = 0x41; b[1] = 0xC6; b[2] = 0x02; b[3] = '0';
    EMIT(cg, 4);
    /* inc r11 */
    n = emit_inc_reg(BUF(cg), REG_R11);
    EMIT(cg, n);
    /* jmp to write */
    size_t jmp_write_pos = cg->code_size;
    b = BUF(cg); b[0] = 0xEB; b[1] = 0x00;
    EMIT(cg, 2);

    /* patch jnz (skip zero case) */
    cg->code[jnz_pos + 1] = (uint8_t)(cg->code_size - (jnz_pos + 2));

    /* Digit extraction loop: rax / 10, remainder + '0' -> buffer */
    size_t digit_loop = cg->code_size;

    /* mov rcx, 10 */
    n = emit_mov_reg_imm32(BUF(cg), REG_RCX, 10);
    EMIT(cg, n);
    /* xor edx, edx */
    n = emit_xor_reg_reg(BUF(cg), REG_RDX, REG_RDX);
    EMIT(cg, n);
    /* div rcx (unsigned: rax = quotient, rdx = remainder) */
    b = BUF(cg);
    b[0] = rex(1, 0, 0, 0); b[1] = 0xF7; b[2] = modrm(3, 6, REG_RCX);
    EMIT(cg, 3);
    /* add dl, '0' */
    b = BUF(cg);
    b[0] = 0x80; b[1] = 0xC2; b[2] = '0';
    EMIT(cg, 3);
    /* dec r10 */
    n = emit_dec_reg(BUF(cg), REG_R10);
    EMIT(cg, n);
    /* mov [r10], dl */
    b = BUF(cg);
    b[0] = 0x41; b[1] = 0x88; b[2] = 0x12;
    EMIT(cg, 3);
    /* inc r11 */
    n = emit_inc_reg(BUF(cg), REG_R11);
    EMIT(cg, n);
    /* test rax, rax */
    n = emit_test_reg_reg(BUF(cg), REG_RAX, REG_RAX);
    EMIT(cg, n);
    /* jnz digit_loop */
    int8_t loop_back = (int8_t)((int64_t)digit_loop - (int64_t)(cg->code_size + 2));
    b = BUF(cg); b[0] = 0x75; b[1] = (uint8_t)loop_back;
    EMIT(cg, 2);

    /* If negative: prepend '-' */
    /* test r9d, r9d */
    b = BUF(cg);
    b[0] = 0x45; b[1] = 0x85; b[2] = 0xC9;
    EMIT(cg, 3);
    /* jz .write */
    size_t jz_write = cg->code_size;
    b = BUF(cg); b[0] = 0x74; b[1] = 0x00;
    EMIT(cg, 2);
    /* dec r10 */
    n = emit_dec_reg(BUF(cg), REG_R10);
    EMIT(cg, n);
    /* mov byte [r10], '-' */
    b = BUF(cg);
    b[0] = 0x41; b[1] = 0xC6; b[2] = 0x02; b[3] = '-';
    EMIT(cg, 4);
    /* inc r11 */
    n = emit_inc_reg(BUF(cg), REG_R11);
    EMIT(cg, n);

    /* patch jz and jmp_write */
    cg->code[jz_write + 1] = (uint8_t)(cg->code_size - (jz_write + 2));
    cg->code[jmp_write_pos + 1] = (uint8_t)(cg->code_size - (jmp_write_pos + 2));

    /* sys_write(1, r10, r11) */
    n = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, n);  /* __NR_write */
    n = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, n);  /* fd = stdout */
    n = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_R10); EMIT(cg, n); /* buf */
    n = emit_mov_reg_reg(BUF(cg), REG_RDX, REG_R11); EMIT(cg, n); /* len */
    n = emit_syscall(BUF(cg)); EMIT(cg, n);

    /* Restore stack: add rsp, 24 */
    n = emit_add_reg_imm(BUF(cg), REG_RSP, 24);
    EMIT(cg, n);

    /* Return 0 in RAX (print_int returns nothing meaningful) */
    n = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX);
    EMIT(cg, n);
}

/*
 * BUILTIN: read_int()
 * Reads an integer from stdin. Parses ASCII digits, handles optional
 * leading '-' for negative numbers. Uses sys_read(0, stack_buf, 20).
 * Result in RAX.
 */
static void emit_builtin_read_int(CodegenState *cg) {
    int n;
    uint8_t *b;

    /* sub rsp, 24 -- stack buffer */
    n = emit_sub_reg_imm(BUF(cg), REG_RSP, 24);
    EMIT(cg, n);

    /* r10 = byte count (index into buffer) */
    b = BUF(cg);
    b[0] = 0x4D; b[1] = 0x31; b[2] = 0xD2; EMIT(cg, 3); /* xor r10, r10 */

    /* Read loop: read 1 byte at a time until '\n' or EOF */
    size_t read_loop = cg->code_size;

    /* sys_read(0, rsp+r10, 1) */
    n = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, n); /* __NR_read */
    n = emit_xor_reg_reg(BUF(cg), REG_RDI, REG_RDI); EMIT(cg, n); /* stdin */
    /* lea rsi, [rsp + r10] */
    b = BUF(cg);
    b[0] = 0x4A; b[1] = 0x8D;
    b[2] = modrm(0, REG_RSI, 4);
    b[3] = (uint8_t)((0 << 6) | ((REG_R10 & 7) << 3) | (REG_RSP & 7));
    EMIT(cg, 4);
    n = emit_mov_reg_imm32(BUF(cg), REG_RDX, 1); EMIT(cg, n); /* 1 byte */
    n = emit_syscall(BUF(cg)); EMIT(cg, n);

    /* if rax <= 0, done (EOF) */
    n = emit_test_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, n);
    size_t jle_pos = cg->code_size;
    b = BUF(cg); b[0] = 0x7E; b[1] = 0x00; EMIT(cg, 2); /* jle done */

    /* Check if byte is '\n' */
    /* movzx ecx, byte [rsp + r10] */
    b = BUF(cg);
    b[0] = 0x42; b[1] = 0x0F; b[2] = 0xB6;
    b[3] = modrm(0, REG_RCX, 4);
    b[4] = (uint8_t)((0 << 6) | ((REG_R10 & 7) << 3) | (REG_RSP & 7));
    EMIT(cg, 5);

    /* cmp cl, '\n' */
    b = BUF(cg);
    b[0] = 0x80; b[1] = 0xF9; b[2] = 0x0A; EMIT(cg, 3);

    /* je done_read */
    size_t je_done = cg->code_size;
    b = BUF(cg); b[0] = 0x74; b[1] = 0x00; EMIT(cg, 2);

    /* inc r10 */
    n = emit_inc_reg(BUF(cg), REG_R10); EMIT(cg, n);

    /* jmp read_loop */
    int8_t rl_back = (int8_t)((int64_t)read_loop - (int64_t)(cg->code_size + 2));
    b = BUF(cg); b[0] = 0xEB; b[1] = (uint8_t)rl_back; EMIT(cg, 2);

    /* done_read: patch jumps */
    cg->code[jle_pos + 1] = (uint8_t)(cg->code_size - (jle_pos + 2));
    cg->code[je_done + 1] = (uint8_t)(cg->code_size - (je_done + 2));

    /* Now parse: r10 = number of digit bytes in buffer at rsp */
    /* r11 = result = 0, r9 = sign, rcx = parse index */
    b = BUF(cg);
    b[0] = 0x4D; b[1] = 0x31; b[2] = 0xDB; EMIT(cg, 3); /* xor r11, r11 */
    b = BUF(cg);
    b[0] = 0x4D; b[1] = 0x31; b[2] = 0xC9; EMIT(cg, 3); /* xor r9, r9 (sign) */

    /* Use rbx as parse index (save it) */
    n = emit_push(BUF(cg), REG_RBX); EMIT(cg, n);
    n = emit_xor_reg_reg(BUF(cg), REG_RBX, REG_RBX); EMIT(cg, n); /* rbx = 0 */

    /* Check '-' at buf[0] */
    b = BUF(cg);
    b[0] = 0x0F; b[1] = 0xB6; b[2] = modrm(1, REG_RCX, REG_RSP);
    b[3] = 0x24; b[4] = 8; /* disp8 = 8 (because we pushed rbx) */
    EMIT(cg, 5);
    b = BUF(cg);
    b[0] = 0x80; b[1] = 0xF9; b[2] = '-'; EMIT(cg, 3);
    size_t jne2 = cg->code_size;
    b = BUF(cg); b[0] = 0x75; b[1] = 0x00; EMIT(cg, 2);
    /* negative: r9=1, rbx=1 */
    b = BUF(cg);
    b[0] = 0x49; b[1] = 0xC7; b[2] = 0xC1;
    int32_t one = 1; memcpy(b+3, &one, 4); EMIT(cg, 7);
    n = emit_mov_reg_imm32(BUF(cg), REG_RBX, 1); EMIT(cg, n);
    cg->code[jne2 + 1] = (uint8_t)(cg->code_size - (jne2 + 2));

    /* Parse digit loop */
    size_t ploop = cg->code_size;

    /* cmp rbx, r10 (index >= length?) */
    b = BUF(cg);
    b[0] = 0x4C; b[1] = 0x39; b[2] = 0xD3; EMIT(cg, 3); /* cmp rbx, r10 */
    size_t jge_end = cg->code_size;
    b = BUF(cg); b[0] = 0x7D; b[1] = 0x00; EMIT(cg, 2);

    /* movzx ecx, byte [rsp + rbx + 8] (8 for pushed rbx) */
    b = BUF(cg);
    b[0] = 0x0F; b[1] = 0xB6;
    b[2] = modrm(1, REG_RCX, 4); /* SIB, disp8 */
    b[3] = (uint8_t)((0 << 6) | ((REG_RBX & 7) << 3) | (REG_RSP & 7));
    b[4] = 8;
    EMIT(cg, 5);

    /* sub cl, '0' */
    b = BUF(cg); b[0] = 0x80; b[1] = 0xE9; b[2] = '0'; EMIT(cg, 3);
    /* cmp cl, 9 */
    b = BUF(cg); b[0] = 0x80; b[1] = 0xF9; b[2] = 9; EMIT(cg, 3);
    size_t ja2 = cg->code_size;
    b = BUF(cg); b[0] = 0x77; b[1] = 0x00; EMIT(cg, 2); /* ja done */

    /* r11 = r11 * 10 + digit */
    b = BUF(cg);
    b[0] = 0x4D; b[1] = 0x6B; b[2] = 0xDB; b[3] = 10; EMIT(cg, 4); /* imul r11,r11,10 */
    b = BUF(cg);
    b[0] = 0x48; b[1] = 0x0F; b[2] = 0xB6; b[3] = 0xC9; EMIT(cg, 4); /* movzx rcx,cl */
    b = BUF(cg);
    b[0] = 0x49; b[1] = 0x01; b[2] = 0xCB; EMIT(cg, 3); /* add r11, rcx */

    /* inc rbx */
    n = emit_inc_reg(BUF(cg), REG_RBX); EMIT(cg, n);
    int8_t pb = (int8_t)((int64_t)ploop - (int64_t)(cg->code_size + 2));
    b = BUF(cg); b[0] = 0xEB; b[1] = (uint8_t)pb; EMIT(cg, 2);

    /* patch exits */
    cg->code[jge_end + 1] = (uint8_t)(cg->code_size - (jge_end + 2));
    cg->code[ja2 + 1] = (uint8_t)(cg->code_size - (ja2 + 2));

    /* Restore rbx */
    n = emit_pop(BUF(cg), REG_RBX); EMIT(cg, n);

    /* Negate if sign */
    b = BUF(cg);
    b[0] = 0x4D; b[1] = 0x85; b[2] = 0xC9; EMIT(cg, 3); /* test r9, r9 */
    size_t jz2 = cg->code_size;
    b = BUF(cg); b[0] = 0x74; b[1] = 0x00; EMIT(cg, 2);
    b = BUF(cg);
    b[0] = 0x49; b[1] = 0xF7; b[2] = 0xDB; EMIT(cg, 3); /* neg r11 */
    cg->code[jz2 + 1] = (uint8_t)(cg->code_size - (jz2 + 2));

    /* mov rax, r11 */
    b = BUF(cg);
    b[0] = 0x4C; b[1] = 0x89; b[2] = 0xD8; EMIT(cg, 3);

    /* add rsp, 24 */
    n = emit_add_reg_imm(BUF(cg), REG_RSP, 24);
    EMIT(cg, n);
}

static void emit_call_expr(CodegenState *cg, const ASTNode *node) {
    /*
     * NODE_CALL layout:
     *   child[0] = callee (NODE_IDENTIFIER with function name)
     *   child[1..n] = arguments
     */
    if (node->child_count < 1) {
        cg_error(cg, "malformed call at %d:%d", node->line, node->col);
        return;
    }

    const ASTNode *callee = node->children[0];
    size_t argc = node->child_count - 1;

    /* Check for built-in functions */
    if (callee->type == NODE_IDENTIFIER && callee->string_val) {
        if (strcmp(callee->string_val, "print_int") == 0 && argc == 1) {
            emit_builtin_print_int(cg, node->children[1]);
            return;
        }
        if (strcmp(callee->string_val, "print_str") == 0 && argc == 1) {
            emit_builtin_print_str(cg, node->children[1]);
            return;
        }
        if (strcmp(callee->string_val, "read_int") == 0 && argc == 0) {
            emit_builtin_read_int(cg);
            return;
        }
        /* float_to_int(x): convert f64 bits in RAX/xmm0 to truncated i32 */
        if (strcmp(callee->string_val, "float_to_int") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]);
            /* xmm0 has the f64 value (loaded by emit_float_literal) */
            /* cvttsd2si rax, xmm0 */
            int pn = emit_cvttsd2si(BUF(cg), REG_RAX, 0);
            EMIT(cg, pn);
            return;
        }
    }

    if (argc > SYS_V_ARG_COUNT) {
        cg_error(cg, "too many arguments (max %d) at %d:%d",
                 SYS_V_ARG_COUNT, node->line, node->col);
        return;
    }

    /*
     * Evaluate arguments right-to-left, push each.
     * Then pop into the correct ABI registers.
     */
    for (size_t i = argc; i > 0; i--) {
        emit_expression(cg, node->children[i]);
        int n = emit_push(BUF(cg), REG_RAX);
        EMIT(cg, n);
    }

    for (size_t i = 0; i < argc; i++) {
        int n = emit_pop(BUF(cg), SYS_V_ARG_REGS[i]);
        EMIT(cg, n);
    }

    /*
     * Emit CALL rel32.  We don't know the target offset yet if it
     * hasn't been emitted, so record a patch entry.
     */
    if (callee->type != NODE_IDENTIFIER) {
        cg_error(cg, "indirect calls not supported at %d:%d",
                 node->line, node->col);
        return;
    }

    /* Emit call with placeholder offset */
    BUF(cg)[0] = 0xE8; /* CALL rel32 */
    cg->code_size += 1;

    /* Record patch location (points to the 4-byte rel32) */
    if (cg->patch_count < 1024) {
        cg->call_patches[cg->patch_count].code_pos = cg->code_size;
        cg->call_patches[cg->patch_count].target   = callee->string_val;
        cg->patch_count++;
    }

    /* Placeholder 4 bytes */
    memset(BUF(cg), 0, 4);
    cg->code_size += 4;

    /* Result is in RAX per System V ABI */
}

static int emit_expression(CodegenState *cg, const ASTNode *node) {
    if (cg->had_error || !node) return 0;

    switch (node->type) {
    case NODE_INT_LITERAL:
        emit_int_literal(cg, node);
        return 0;
    case NODE_FLOAT_LITERAL:
        emit_float_literal(cg, node);
        return 1;
    case NODE_BOOL_LITERAL:
        emit_int_literal(cg, node);
        return 0;
    case NODE_IDENTIFIER:
        return emit_identifier(cg, node);
    case NODE_BINARY_OP:
        emit_binary_op(cg, node);
        /* Check if this is a float operation by looking at children */
        if (node->child_count >= 2 &&
            (node->children[0]->type == NODE_FLOAT_LITERAL ||
             (node->children[0]->type == NODE_IDENTIFIER &&
              find_local(cg, node->children[0]->string_val) &&
              find_local(cg, node->children[0]->string_val)->is_float)))
            return 1;
        return 0;
    case NODE_UNARY_OP:
        emit_unary_op(cg, node);
        return 0;
    case NODE_CALL:
        emit_call_expr(cg, node);
        return 0;
    default:
        cg_error(cg, "unsupported expression node type %s at %d:%d",
                 node_type_name(node->type), node->line, node->col);
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/*  Statement codegen                                                 */
/* ------------------------------------------------------------------ */

/*
 * TAIL CALL OPTIMIZATION
 * If `return f(args)` calls the current function, replace CALL+RET
 * with: evaluate args -> load into ABI regs -> restore frame -> JMP.
 * This converts recursion into iteration at the machine code level,
 * eliminating stack frame overhead for recursive functions.
 *
 * Effect on Mersenne: 23,170 stack frames -> 0 stack growth.
 */
static int is_tail_call(const CodegenState *cg, const ASTNode *expr) {
    if (!expr || expr->type != NODE_CALL) return 0;
    if (expr->child_count < 1) return 0;
    if (!cg->current_fn_name) return 0;

    const ASTNode *callee = expr->children[0];
    if (callee->type != NODE_IDENTIFIER || !callee->string_val) return 0;

    return strcmp(callee->string_val, cg->current_fn_name) == 0;
}

static void emit_tail_call(CodegenState *cg, const ASTNode *call_node) {
    size_t argc = call_node->child_count - 1;
    int n;

    /* Evaluate arguments right-to-left, push each */
    for (size_t i = argc; i > 0; i--) {
        emit_expression(cg, call_node->children[i]);
        n = emit_push(BUF(cg), REG_RAX);
        EMIT(cg, n);
    }

    /* Pop into the correct ABI registers */
    for (size_t i = 0; i < argc; i++) {
        n = emit_pop(BUF(cg), SYS_V_ARG_REGS[i]);
        EMIT(cg, n);
    }

    /* Restore frame: mov rsp, rbp; pop rbp */
    n = emit_mov_reg_reg(BUF(cg), REG_RSP, REG_RBP);
    EMIT(cg, n);
    n = emit_pop(BUF(cg), REG_RBP);
    EMIT(cg, n);

    /* JMP to function entry (instead of CALL + RET) */
    int32_t rel = (int32_t)((int64_t)cg->current_fn_entry -
                            (int64_t)(cg->code_size + 5));
    n = emit_jmp(BUF(cg), rel);
    EMIT(cg, n);
}

static void emit_return(CodegenState *cg, const ASTNode *node) {
    /* Check for tail call optimization */
    if (node->child_count > 0 && is_tail_call(cg, node->children[0])) {
        emit_tail_call(cg, node->children[0]);
        return;
    }

    if (node->child_count > 0) {
        emit_expression(cg, node->children[0]);
    }
    /* Function epilogue: restore RSP, pop RBP, ret */
    int n = emit_mov_reg_reg(BUF(cg), REG_RSP, REG_RBP);
    EMIT(cg, n);
    n = emit_pop(BUF(cg), REG_RBP);
    EMIT(cg, n);
    n = emit_ret(BUF(cg));
    EMIT(cg, n);
}

static void emit_var_decl(CodegenState *cg, const ASTNode *node) {
    /*
     * NODE_VAR_DECL layout:
     *   string_val = variable name
     *   child[0] = type annotation
     *   child[1] = initializer expression
     */
    LocalVar *v = add_local(cg, node->string_val);
    if (!v) return;

    /* Check type annotation for float */
    if (node->child_count >= 1 && node->children[0] &&
        node->children[0]->type == NODE_TYPE_ANNOTATION &&
        node->children[0]->string_val) {
        const char *tname = node->children[0]->string_val;
        if (strcmp(tname, "f64") == 0 || strcmp(tname, "f32") == 0) {
            v->is_float = 1;
        }
    }

    if (node->child_count >= 2) {
        /* Evaluate initializer -> RAX (and xmm0 if float) */
        int expr_type = emit_expression(cg, node->children[1]);
        /* Infer float from initializer if no type annotation */
        if (expr_type == 1 && !v->is_float) v->is_float = 1;
        /* Store to stack (both int and float use 8-byte RAX) */
        int n = emit_mov_mem_reg(BUF(cg), REG_RBP, v->rbp_off, REG_RAX);
        EMIT(cg, n);
    }
}

static void emit_if(CodegenState *cg, const ASTNode *node) {
    /*
     * NODE_IF layout:
     *   child[0] = condition expression
     *   child[1] = then block
     *   child[2] = else block (optional)
     */
    if (node->child_count < 2) {
        cg_error(cg, "malformed if at %d:%d", node->line, node->col);
        return;
    }

    /* Evaluate condition -> RAX */
    emit_expression(cg, node->children[0]);

    /* Test RAX: cmp rax, 0 */
    int n = emit_cmp_reg_imm(BUF(cg), REG_RAX, 0);
    EMIT(cg, n);

    /* JE over then-block (placeholder offset) */
    size_t je_pos = cg->code_size;
    n = emit_je(BUF(cg), 0); /* placeholder */
    EMIT(cg, n);

    /* Then block */
    emit_block(cg, node->children[1]);

    if (node->child_count >= 3) {
        /* JMP over else-block */
        size_t jmp_pos = cg->code_size;
        n = emit_jmp(BUF(cg), 0); /* placeholder */
        EMIT(cg, n);

        /* Patch JE to point here (else block start) */
        int32_t je_off = (int32_t)(cg->code_size - (je_pos + 6));
        memcpy(cg->code + je_pos + 2, &je_off, 4);

        /* Else block */
        emit_block(cg, node->children[2]);

        /* Patch JMP to point here */
        int32_t jmp_off = (int32_t)(cg->code_size - (jmp_pos + 5));
        memcpy(cg->code + jmp_pos + 1, &jmp_off, 4);
    } else {
        /* No else: patch JE to point here */
        int32_t je_off = (int32_t)(cg->code_size - (je_pos + 6));
        memcpy(cg->code + je_pos + 2, &je_off, 4);
    }
}

/*
 * WHILE loop codegen.
 *   child[0] = condition
 *   child[1] = body block
 *
 * Emits:
 *   loop_start:
 *     evaluate condition -> RAX
 *     cmp rax, 0
 *     je loop_end
 *     <body>
 *     jmp loop_start
 *   loop_end:
 */
static void emit_while(CodegenState *cg, const ASTNode *node) {
    if (node->child_count < 2) {
        cg_error(cg, "malformed while at %d:%d", node->line, node->col);
        return;
    }

    /* loop_start label */
    size_t loop_start = cg->code_size;

    /* Evaluate condition -> RAX */
    emit_expression(cg, node->children[0]);

    /* Test: cmp rax, 0 */
    int n = emit_cmp_reg_imm(BUF(cg), REG_RAX, 0);
    EMIT(cg, n);

    /* JE to loop_end (placeholder) */
    size_t je_pos = cg->code_size;
    n = emit_je(BUF(cg), 0);
    EMIT(cg, n);

    /* Body */
    emit_block(cg, node->children[1]);

    /* JMP back to loop_start */
    int32_t back_rel = (int32_t)((int64_t)loop_start - (int64_t)(cg->code_size + 5));
    n = emit_jmp(BUF(cg), back_rel);
    EMIT(cg, n);

    /* Patch JE to point here (loop_end) */
    int32_t je_off = (int32_t)(cg->code_size - (je_pos + 6));
    memcpy(cg->code + je_pos + 2, &je_off, 4);
}

/*
 * Variable assignment (x = expr).
 * The parser stores this as NODE_BINARY_OP with op="=".
 *   child[0] = identifier (lvalue)
 *   child[1] = expression (rvalue)
 */
static void emit_assignment(CodegenState *cg, const ASTNode *node) {
    if (node->child_count < 2) {
        cg_error(cg, "malformed assignment at %d:%d", node->line, node->col);
        return;
    }

    ASTNode *lhs = node->children[0];
    if (lhs->type != NODE_IDENTIFIER || !lhs->string_val) {
        cg_error(cg, "left side of assignment must be a variable at %d:%d",
                 node->line, node->col);
        return;
    }

    LocalVar *v = find_local(cg, lhs->string_val);
    if (!v) {
        cg_error(cg, "undefined variable '%s' in assignment at %d:%d",
                 lhs->string_val, node->line, node->col);
        return;
    }

    /* Evaluate rvalue -> RAX */
    emit_expression(cg, node->children[1]);

    /* Store to stack: mov [rbp + offset], rax */
    int n = emit_mov_mem_reg(BUF(cg), REG_RBP, v->rbp_off, REG_RAX);
    EMIT(cg, n);
}

/*
 * FOR loop codegen.
 *   child[0] = initializer (var_decl or expr_stmt)
 *   child[1] = condition expression
 *   child[2] = update expression
 *   child[3] = body block
 *
 * Emits: init; loop_start: cond; je end; body; update; jmp start; end:
 */
static void emit_for(CodegenState *cg, const ASTNode *node) {
    if (node->child_count < 4) {
        cg_error(cg, "malformed for at %d:%d", node->line, node->col);
        return;
    }

    /* Initializer */
    emit_statement(cg, node->children[0]);

    /* loop_start label */
    size_t loop_start = cg->code_size;

    /* Condition */
    emit_expression(cg, node->children[1]);
    int n = emit_cmp_reg_imm(BUF(cg), REG_RAX, 0);
    EMIT(cg, n);

    /* JE to loop_end */
    size_t je_pos = cg->code_size;
    n = emit_je(BUF(cg), 0);
    EMIT(cg, n);

    /* Body */
    emit_block(cg, node->children[3]);

    /* Update (might be assignment expression) */
    if (node->children[2]->type == NODE_BINARY_OP &&
        node->children[2]->op && strcmp(node->children[2]->op, "=") == 0) {
        emit_assignment(cg, node->children[2]);
    } else {
        emit_expression(cg, node->children[2]);
    }

    /* JMP back to loop_start */
    int32_t back_rel = (int32_t)((int64_t)loop_start - (int64_t)(cg->code_size + 5));
    n = emit_jmp(BUF(cg), back_rel);
    EMIT(cg, n);

    /* Patch JE */
    int32_t je_off = (int32_t)(cg->code_size - (je_pos + 6));
    memcpy(cg->code + je_pos + 2, &je_off, 4);
}

static void emit_statement(CodegenState *cg, const ASTNode *node) {
    if (cg->had_error || !node) return;

    switch (node->type) {
    case NODE_RETURN:
        emit_return(cg, node);
        break;
    case NODE_VAR_DECL:
    case NODE_CONST_DECL:
        emit_var_decl(cg, node);
        break;
    case NODE_IF:
        emit_if(cg, node);
        break;
    case NODE_WHILE:
        emit_while(cg, node);
        break;
    case NODE_FOR:
        emit_for(cg, node);
        break;
    case NODE_EXPR_STMT:
        /* Check for assignment expression (NODE_BINARY_OP with op="=") */
        if (node->child_count > 0 && node->children[0]->type == NODE_BINARY_OP &&
            node->children[0]->op && strcmp(node->children[0]->op, "=") == 0) {
            emit_assignment(cg, node->children[0]);
        } else if (node->child_count > 0) {
            emit_expression(cg, node->children[0]);
        }
        break;
    case NODE_BLOCK:
        emit_block(cg, node);
        break;
    default:
        cg_error(cg, "unsupported statement node type %s at %d:%d",
                 node_type_name(node->type), node->line, node->col);
        break;
    }
}

static void emit_block(CodegenState *cg, const ASTNode *node) {
    if (!node) return;
    for (size_t i = 0; i < node->child_count; i++) {
        emit_statement(cg, node->children[i]);
        if (cg->had_error) return;
    }
}

/* ------------------------------------------------------------------ */
/*  Function codegen                                                  */
/* ------------------------------------------------------------------ */

static void emit_function(CodegenState *cg, const ASTNode *node) {
    /*
     * NODE_FN_DECL layout:
     *   string_val   = function name
     *   child[0]     = params (NODE_BLOCK of NODE_VAR_DECLs)
     *   child[1]     = return type OR body
     *   child[last]  = body (NODE_BLOCK)
     */
    if (!node->string_val) {
        cg_error(cg, "unnamed function at %d:%d", node->line, node->col);
        return;
    }

    /* Record function entry */
    if (cg->func_count >= CODEGEN_MAX_FUNCS) {
        cg_error(cg, "too many functions");
        return;
    }

    FuncEntry *fe = &cg->funcs[cg->func_count++];
    fe->name     = node->string_val;
    fe->code_off = cg->code_size;

    /* Set current function info for tail call optimization */
    cg->current_fn_name  = node->string_val;
    cg->current_fn_entry = cg->code_size;

    /* Reset locals for this function */
    cg->local_count  = 0;
    cg->stack_offset = 0;

    /* Parse parameters */
    const ASTNode *params = node->children[0]; /* child 0 = param block */
    fe->param_cnt = (int)params->child_count;

    /* Function prologue: push rbp; mov rbp, rsp */
    int n = emit_push(BUF(cg), REG_RBP);
    EMIT(cg, n);
    n = emit_mov_reg_reg(BUF(cg), REG_RBP, REG_RSP);
    EMIT(cg, n);

    /* Reserve stack space placeholder -- we'll patch it after body */
    size_t sub_rsp_pos = cg->code_size;
    /* Emit a sub rsp, imm8 placeholder (4 bytes: REX.W 83 EC imm8) */
    n = emit_sub_reg_imm(BUF(cg), REG_RSP, 0);
    EMIT(cg, n);

    /* Allocate locals for parameters and store from ABI registers */
    for (size_t i = 0; i < params->child_count && i < SYS_V_ARG_COUNT; i++) {
        const ASTNode *param = params->children[i];
        LocalVar *v = add_local(cg, param->string_val);
        if (!v) return;

        /* Store argument register to local slot */
        n = emit_mov_mem_reg(BUF(cg), REG_RBP, v->rbp_off,
                             SYS_V_ARG_REGS[i]);
        EMIT(cg, n);
    }

    /* Find the body block (last child) */
    const ASTNode *body = node->children[node->child_count - 1];

    /* Emit body statements */
    emit_block(cg, body);

    /* Patch stack reservation.
     * stack_offset is negative, aligned to 16 bytes. */
    int32_t frame_size = -cg->stack_offset;
    if (frame_size % 16 != 0)
        frame_size = (frame_size + 15) & ~15;
    if (frame_size == 0)
        frame_size = 0; /* no locals */

    /* Re-encode the sub rsp, imm at sub_rsp_pos.
     * OPTIMIZATION: If frame_size is 0 (no locals), NOP-out the sub rsp
     * instruction to avoid wasting 4 bytes. */
    if (frame_size > 0) {
        size_t saved_size = cg->code_size;
        cg->code_size = sub_rsp_pos;
        n = emit_sub_reg_imm(BUF(cg), REG_RSP, frame_size);
        cg->code_size = saved_size;
    } else {
        /* NOP-fill the 4 bytes of the placeholder sub rsp, 0 */
        memset(cg->code + sub_rsp_pos, 0x90, 4); /* 4x NOP */
    }

    /* OPTIMIZATION: Only emit safety epilogue if the block doesn't
     * end with an explicit return statement.  This saves 8-10 bytes
     * per function that already has a return. */
    int needs_safety_epilogue = 1;
    if (body && body->child_count > 0) {
        ASTNode *last = body->children[body->child_count - 1];
        if (last && last->type == NODE_RETURN)
            needs_safety_epilogue = 0;
    }

    if (needs_safety_epilogue) {
        /* mov rax, 0 (default return value) */
        n = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX);
        EMIT(cg, n);
        n = emit_mov_reg_reg(BUF(cg), REG_RSP, REG_RBP);
        EMIT(cg, n);
        n = emit_pop(BUF(cg), REG_RBP);
        EMIT(cg, n);
        n = emit_ret(BUF(cg));
        EMIT(cg, n);
    }
}

/* ------------------------------------------------------------------ */
/*  _start entry point                                                */
/* ------------------------------------------------------------------ */

static void emit_start(CodegenState *cg) {
    /*
     * _start:
     *   ; Align stack to 16 bytes (ABI requirement before CALL)
     *   and rsp, -16
     *   call main
     *   ; exit(rax)
     *   mov rdi, rax       ; exit code = main's return value
     *   mov rax, 60         ; __NR_exit
     *   syscall
     */
    cg->entry_offset = cg->code_size;

    /* and rsp, -16  -->  REX.W 83 E4 F0 */
    uint8_t *b = BUF(cg);
    b[0] = rex(1, 0, 0, 0); /* REX.W */
    b[1] = 0x83;
    b[2] = modrm(3, 4, REG_RSP); /* /4 = AND, rm = RSP */
    b[3] = 0xF0; /* -16 as signed imm8 */
    cg->code_size += 4;

    /* call main (placeholder, will be patched) */
    BUF(cg)[0] = 0xE8;
    cg->code_size += 1;
    if (cg->patch_count < 1024) {
        cg->call_patches[cg->patch_count].code_pos = cg->code_size;
        cg->call_patches[cg->patch_count].target   = "main";
        cg->patch_count++;
    }
    memset(BUF(cg), 0, 4);
    cg->code_size += 4;

    /* mov rdi, rax */
    int n = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX);
    EMIT(cg, n);

    /* mov rax, 60 (exit syscall) */
    n = emit_mov_reg_imm32(BUF(cg), REG_RAX, 60);
    EMIT(cg, n);

    /* syscall */
    n = emit_syscall(BUF(cg));
    EMIT(cg, n);
}

/* ------------------------------------------------------------------ */
/*  Patch CALL rel32 targets                                          */
/* ------------------------------------------------------------------ */

static int patch_calls(CodegenState *cg) {
    for (size_t i = 0; i < cg->patch_count; i++) {
        const char *target = cg->call_patches[i].target;
        size_t call_site   = cg->call_patches[i].code_pos;

        FuncEntry *fe = find_func(cg, target);
        if (!fe) {
            cg_error(cg, "undefined function '%s'", target);
            return -1;
        }

        /* rel32 = target_addr - (call_site + 4) */
        int32_t rel = (int32_t)((int64_t)fe->code_off -
                                (int64_t)(call_site + 4));
        memcpy(cg->code + call_site, &rel, 4);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

void codegen_init(CodegenState *cg) {
    memset(cg, 0, sizeof(CodegenState));
}

int codegen_generate(CodegenState *cg, const ASTNode *ast) {
    if (!ast || ast->type != NODE_PROGRAM) {
        cg_error(cg, "expected NODE_PROGRAM as root");
        return -1;
    }

    /* OPTIMIZATION PASS: Run AST optimizations before code generation.
     * This folds constants, eliminates dead code, etc. */
    optimizer_run((ASTNode *)ast);

    /* First pass: emit all functions */
    for (size_t i = 0; i < ast->child_count; i++) {
        const ASTNode *child = ast->children[i];
        if (child->type == NODE_FN_DECL) {
            emit_function(cg, child);
            if (cg->had_error) return -1;
        }
    }

    /* Emit _start entry point */
    emit_start(cg);

    /* Patch all CALL targets */
    if (patch_calls(cg) != 0)
        return -1;

    return cg->had_error ? -1 : 0;
}

int codegen_compile_to_file(const ASTNode *ast, const char *output_path) {
    CodegenState cg;
    codegen_init(&cg);

    if (codegen_generate(&cg, ast) != 0) {
        fprintf(stderr, "aricode/codegen: %s\n", cg.error_msg);
        return -1;
    }

    ElfBinary *bin = elf_create(cg.code, cg.code_size, cg.entry_offset);
    if (!bin) return -1;

    int rc = elf_write(output_path, bin);
    elf_free(bin);
    return rc;
}
