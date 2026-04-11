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
static void emit_if(CodegenState *cg, const ASTNode *node);

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

/*
 * Recursively check if an expression tree evaluates to a float.
 * This is needed because nested expressions like (2.0 * g) produce
 * a BINARY_OP node that doesn't directly contain a FLOAT_LITERAL.
 */
static int expr_is_float(CodegenState *cg, const ASTNode *node) {
    if (!node) return 0;
    if (node->type == NODE_FLOAT_LITERAL) return 1;
    if (node->type == NODE_IDENTIFIER && node->string_val) {
        LocalVar *v = find_local(cg, node->string_val);
        return v ? v->is_float : 0;
    }
    if (node->type == NODE_BINARY_OP && node->child_count >= 2) {
        return expr_is_float(cg, node->children[0]) ||
               expr_is_float(cg, node->children[1]);
    }
    if (node->type == NODE_UNARY_OP && node->child_count >= 1) {
        return expr_is_float(cg, node->children[0]);
    }
    return 0;
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
     * Recursively check the entire expression tree for float types.
     */
    int left_is_float = expr_is_float(cg, left);
    int right_is_float = expr_is_float(cg, right);

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
        /*
         * ARRAY BUILTINS
         * arr_new(n): allocate n-element array on stack, store length, return base
         * arr_get(base, idx): return element at index
         * arr_set(base, idx, val): store value at index
         * arr_len(base): return stored length
         */
        if (strcmp(callee->string_val, "arr_new") == 0 && argc == 1) {
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
            return;
        }
        if (strcmp(callee->string_val, "arr_get") == 0 && argc == 2) {
            /* arr_get(base, idx): load [base + idx*8] */
            emit_expression(cg, node->children[2]); /* idx -> RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base -> RAX */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* RCX = idx */
            /* lea rcx, [rax + rcx*8] */
            uint8_t *b = BUF(cg);
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
            return;
        }
        if (strcmp(callee->string_val, "arr_set") == 0 && argc == 3) {
            /* arr_set(base, idx, val) */
            emit_expression(cg, node->children[3]); /* val -> RAX */
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[2]); /* idx -> RAX */
            pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]); /* base -> RAX */
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn); /* RCX = idx */
            pn = emit_pop(BUF(cg), REG_RDX); EMIT(cg, pn); /* RDX = val */
            /* lea rcx, [rax + rcx*8] */
            uint8_t *b = BUF(cg);
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
            return;
        }
        if (strcmp(callee->string_val, "arr_len") == 0 && argc == 1) {
            /* arr_len(base): load [base - 8] */
            emit_expression(cg, node->children[1]); /* base -> RAX */
            /* mov rax, [rax - 8] */
            int pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RAX, -8);
            EMIT(cg, pn);
            return;
        }

        /*
         * print_float(x): Print f64 with 6 decimal places.
         * Strategy: print integer part, ".", then fractional part.
         * Uses the integer print_int mechanism for each part.
         */
        if (strcmp(callee->string_val, "print_float") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]); /* x -> xmm0 + RAX */
            int pn; uint8_t *b;

            /* Save xmm0 bits on stack */
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
            /* Load 1000000.0 into xmm1 */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1000000); EMIT(cg, pn);
            pn = emit_cvtsi2sd(BUF(cg), 1, REG_RAX); EMIT(cg, pn);
            /* mulsd xmm0, xmm1 */
            pn = emit_mulsd(BUF(cg), 0, 1); EMIT(cg, pn);
            /* cvttsd2si rax, xmm0 */
            pn = emit_cvttsd2si(BUF(cg), REG_RAX, 0); EMIT(cg, pn);

            /* Print 6 digits with leading zeros */
            pn = emit_sub_reg_imm(BUF(cg), REG_RSP, 8); EMIT(cg, pn);

            /* Generate 6 digits right-to-left */
            /* We'll store them at rsp[0..5] then write all 6 + newline */
            pn = emit_sub_reg_imm(BUF(cg), REG_RSP, 8); EMIT(cg, pn);
            int di;
            for (di = 5; di >= 0; di--) {
                pn = emit_mov_reg_imm32(BUF(cg), REG_RCX, 10); EMIT(cg, pn);
                pn = emit_xor_reg_reg(BUF(cg), REG_RDX, REG_RDX); EMIT(cg, pn);
                b = BUF(cg); b[0]=rex(1,0,0,0); b[1]=0xF7; b[2]=modrm(3,6,REG_RCX);
                EMIT(cg,3);
                b = BUF(cg); b[0]=0x80; b[1]=0xC2; b[2]='0'; EMIT(cg,3);
                /* mov [rsp+di], dl */
                b = BUF(cg); b[0]=0x88; b[1]=modrm(1,REG_RDX,REG_RSP);
                b[2]=0x24; b[3]=(uint8_t)di; EMIT(cg,4);
            }
            /* Put newline at [rsp+6] */
            b = BUF(cg); b[0]=0xC6; b[1]=modrm(1,0,REG_RSP);
            b[2]=0x24; b[3]=6; b[4]=0x0A; EMIT(cg,5);

            /* write(1, rsp, 7) -- 6 digits + newline */
            pn = emit_mov_reg_imm32(BUF(cg), REG_RAX, 1); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDI, 1); EMIT(cg, pn);
            pn = emit_mov_reg_reg(BUF(cg), REG_RSI, REG_RSP); EMIT(cg, pn);
            pn = emit_mov_reg_imm32(BUF(cg), REG_RDX, 7); EMIT(cg, pn);
            pn = emit_syscall(BUF(cg)); EMIT(cg, pn);

            pn = emit_add_reg_imm(BUF(cg), REG_RSP, 16); EMIT(cg, pn);

            /* Pop saved xmm0 bits */
            pn = emit_pop(BUF(cg), REG_RAX); EMIT(cg, pn);

            /* Return 0 */
            pn = emit_xor_reg_reg(BUF(cg), REG_RAX, REG_RAX); EMIT(cg, pn);
            return;
        }

        /* int_to_float(x): convert i32 to f64 */
        if (strcmp(callee->string_val, "int_to_float") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]);
            int pn = emit_cvtsi2sd(BUF(cg), 0, REG_RAX); EMIT(cg, pn);
            pn = emit_movq_reg_xmm(BUF(cg), REG_RAX, 0); EMIT(cg, pn);
            return;
        }

        /* float_to_int(x): convert f64 bits in RAX/xmm0 to truncated i32 */
        if (strcmp(callee->string_val, "float_to_int") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]);
            int pn = emit_cvttsd2si(BUF(cg), REG_RAX, 0);
            EMIT(cg, pn);
            return;
        }

        /*
         * dec("value"): Create a decimal value.
         * At compile time, the optimizer folds dec("a") + dec("b") into
         * dec("exact_result"). At runtime, dec("x") just stores the
         * string for printing via print_dec.
         * The string pointer is stored in RAX.
         */
        if (strcmp(callee->string_val, "dec") == 0 && argc == 1) {
            /* Just evaluate the string arg - it puts the string in the code */
            emit_expression(cg, node->children[1]);
            return;
        }

        /* print_dec(x): Print a decimal value (stored as string from dec()) */
        if (strcmp(callee->string_val, "print_dec") == 0 && argc == 1) {
            /* The argument should be a dec() call which evaluates to a
             * string literal embedded in the code. We just call print_str
             * logic on it. */
            emit_builtin_print_str(cg, node->children[1]->type == NODE_CALL ?
                node->children[1]->children[1] : node->children[1]);
            return;
        }

        /* read_float(): read f64 from stdin (reads int, converts to float) */
        if (strcmp(callee->string_val, "read_float") == 0 && argc == 0) {
            emit_builtin_read_int(cg);
            int pn = emit_cvtsi2sd(BUF(cg), 0, REG_RAX); EMIT(cg, pn);
            pn = emit_movq_reg_xmm(BUF(cg), REG_RAX, 0); EMIT(cg, pn);
            return;
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
        if (strcmp(callee->string_val, "str_new") == 0 && argc == 1) {
            /* str_new("literal"): embed string, mmap heap copy */
            ASTNode *arg = node->children[1];
            if (!arg || arg->type != NODE_STRING_LITERAL || !arg->string_val) {
                cg_error(cg, "str_new requires a string literal");
                return;
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
            return;
        }

        if (strcmp(callee->string_val, "str_len") == 0 && argc == 1) {
            emit_expression(cg, node->children[1]); /* base -> RAX */
            /* length at [rax - 8] */
            int pn = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RAX, -8);
            EMIT(cg, pn);
            return;
        }

        if (strcmp(callee->string_val, "str_concat") == 0 && argc == 2) {
            /* Simpler approach: use locals to save a, b, and their lengths.
             * All mmap-clobberable state is in stack locals. */
            int pn; uint8_t *b;

            /* Evaluate a and b, save as locals */
            LocalVar *va = add_local(cg, "__ca");
            LocalVar *vb = add_local(cg, "__cb");
            LocalVar *vn = add_local(cg, "__cn"); /* new buffer */
            if (!va || !vb || !vn) return;

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
            return;
        }

        if (strcmp(callee->string_val, "str_eq") == 0 && argc == 2) {
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
            return;
        }

        if (strcmp(callee->string_val, "str_char_at") == 0 && argc == 2) {
            /* str_char_at(s, i) → ASCII value */
            emit_expression(cg, node->children[2]);
            int pn = emit_push(BUF(cg), REG_RAX); EMIT(cg, pn);
            emit_expression(cg, node->children[1]);
            pn = emit_pop(BUF(cg), REG_RCX); EMIT(cg, pn);
            /* movzx rax, byte [rax + rcx] */
            uint8_t *b = BUF(cg);
            b[0]=rex(1,reg_ext(REG_RAX),reg_ext(REG_RCX),reg_ext(REG_RAX));
            b[1]=0x0F; b[2]=0xB6;
            b[3]=modrm(0,REG_RAX&7,4);
            b[4]=(uint8_t)((0<<6)|((REG_RCX&7)<<3)|(REG_RAX&7));
            EMIT(cg,5);
            return;
        }

        if (strcmp(callee->string_val, "str_println") == 0 && argc == 1) {
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
    case NODE_IF:
        /* Ternary expression: if used as expression, result in RAX */
        emit_if(cg, node);
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

    /* Find the initializer expression:
     * If child[0] is TYPE_ANNOTATION, init is child[1]
     * If child[0] is NOT TYPE_ANNOTATION, init is child[0] (no type given) */
    ASTNode *init_expr = NULL;
    if (node->child_count >= 2 && node->children[0] &&
        node->children[0]->type == NODE_TYPE_ANNOTATION) {
        init_expr = node->children[1];
    } else if (node->child_count >= 1 && node->children[0] &&
               node->children[0]->type != NODE_TYPE_ANNOTATION) {
        init_expr = node->children[0];
    }

    if (init_expr) {
        int expr_type = emit_expression(cg, init_expr);
        if (expr_type == 1 && !v->is_float) v->is_float = 1;
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

    /* Push loop context for break/continue */
    int ld = cg->loop_depth;
    if (ld < 32) {
        cg->loop_end_count[ld] = 0;
        cg->loop_cont_count[ld] = 0;
        cg->loop_is_for[ld] = 0; /* while loop */
        cg->loop_depth++;
    }

    /* loop_start label */
    size_t loop_start = cg->code_size;
    if (ld < 32) cg->loop_start[ld] = loop_start;

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

    /* Patch all break JMPs to point here */
    if (ld < 32) {
        for (int bi = 0; bi < cg->loop_end_count[ld]; bi++) {
            size_t brk = cg->loop_end_patches[ld][bi];
            int32_t brk_off = (int32_t)(cg->code_size - (brk + 5));
            memcpy(cg->code + brk + 1, &brk_off, 4);
        }
        cg->loop_depth--;
    }
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

    /* Push loop context */
    int ld = cg->loop_depth;
    if (ld < 32) {
        cg->loop_end_count[ld] = 0;
        cg->loop_cont_count[ld] = 0;
        cg->loop_is_for[ld] = 1; /* for loop: continue patches needed */
        cg->loop_depth++;
    }

    /* Initializer */
    emit_statement(cg, node->children[0]);

    /* Condition start (for JMP back after update) */
    size_t cond_start = cg->code_size;

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

    /* Update position = continue target for `for` loops.
     * Patch all continue JMPs emitted during the body to land here. */
    if (ld < 32) {
        for (int ci = 0; ci < cg->loop_cont_count[ld]; ci++) {
            size_t cont_jmp = cg->loop_cont_patches[ld][ci];
            int32_t cont_off = (int32_t)(cg->code_size - (cont_jmp + 5));
            memcpy(cg->code + cont_jmp + 1, &cont_off, 4);
        }
    }

    /* Update (might be assignment expression) */
    if (node->children[2]->type == NODE_BINARY_OP &&
        node->children[2]->op && strcmp(node->children[2]->op, "=") == 0) {
        emit_assignment(cg, node->children[2]);
    } else {
        emit_expression(cg, node->children[2]);
    }

    /* JMP back to condition */
    int32_t back_rel = (int32_t)((int64_t)cond_start - (int64_t)(cg->code_size + 5));
    n = emit_jmp(BUF(cg), back_rel);
    EMIT(cg, n);

    /* Patch JE */
    int32_t je_off = (int32_t)(cg->code_size - (je_pos + 6));
    memcpy(cg->code + je_pos + 2, &je_off, 4);

    /* Patch break JMPs */
    if (ld < 32) {
        for (int bi = 0; bi < cg->loop_end_count[ld]; bi++) {
            size_t brk = cg->loop_end_patches[ld][bi];
            int32_t brk_off = (int32_t)(cg->code_size - (brk + 5));
            memcpy(cg->code + brk + 1, &brk_off, 4);
        }
        cg->loop_depth--;
    }
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
    case NODE_BREAK: {
        /* JMP to loop end (will be patched when loop finishes) */
        if (cg->loop_depth > 0) {
            int ld = cg->loop_depth - 1;
            if (cg->loop_end_count[ld] < 16) {
                cg->loop_end_patches[ld][cg->loop_end_count[ld]++] = cg->code_size;
            }
            int n = emit_jmp(BUF(cg), 0); EMIT(cg, n); /* placeholder */
        }
        break;
    }
    case NODE_CONTINUE: {
        if (cg->loop_depth > 0) {
            int ld = cg->loop_depth - 1;
            if (cg->loop_is_for[ld]) {
                /* For loop: continue → forward jump to update (patched later) */
                if (cg->loop_cont_count[ld] < 16) {
                    cg->loop_cont_patches[ld][cg->loop_cont_count[ld]++] = cg->code_size;
                }
                int n = emit_jmp(BUF(cg), 0); EMIT(cg, n); /* placeholder */
            } else {
                /* While loop: continue → jump back to condition */
                int32_t rel = (int32_t)((int64_t)cg->loop_start[ld] -
                                        (int64_t)(cg->code_size + 5));
                int n = emit_jmp(BUF(cg), rel); EMIT(cg, n);
            }
        }
        break;
    }
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

    case NODE_MATCH: {
        /*
         * match (expr) { pattern1 => body1, pattern2 => body2, ... }
         * Compiled as a chain of if/else: evaluate expr once,
         * compare against each pattern, execute matching body.
         */
        if (node->child_count < 2) break;
        int n;

        /* Evaluate match expression -> RAX */
        emit_expression(cg, node->children[0]);

        /* Save to a temp local so each arm can compare */
        LocalVar *match_val = add_local(cg, "__match_val");
        if (!match_val) break;
        n = emit_mov_mem_reg(BUF(cg), REG_RBP, match_val->rbp_off, REG_RAX);
        EMIT(cg, n);

        /* Collect JMP-to-end positions for each arm */
        size_t jmp_ends[64];
        int jmp_count = 0;

        for (size_t arm_i = 1; arm_i < node->child_count; arm_i++) {
            ASTNode *arm = node->children[arm_i];
            if (!arm || arm->type != NODE_MATCH_ARM || arm->child_count < 2)
                continue;

            /* Load match value */
            n = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RBP, match_val->rbp_off);
            EMIT(cg, n);
            /* Save to RCX for comparison */
            n = emit_mov_reg_reg(BUF(cg), REG_RCX, REG_RAX);
            EMIT(cg, n);

            /* Evaluate pattern -> RAX */
            emit_expression(cg, arm->children[0]);

            /* cmp rcx, rax (match_val == pattern?) */
            n = emit_cmp_reg_reg(BUF(cg), REG_RCX, REG_RAX);
            EMIT(cg, n);

            /* JNE to next arm */
            size_t jne_pos = cg->code_size;
            uint8_t *b = BUF(cg); b[0]=0x0F; b[1]=0x85;
            memset(b+2, 0, 4); EMIT(cg, 6);

            /* Execute arm body */
            emit_statement(cg, arm->children[1]);

            /* JMP to end of match */
            if (jmp_count < 64) {
                jmp_ends[jmp_count++] = cg->code_size;
            }
            n = emit_jmp(BUF(cg), 0); EMIT(cg, n);

            /* Patch JNE to here */
            int32_t jne_off = (int32_t)(cg->code_size - (jne_pos + 6));
            memcpy(cg->code + jne_pos + 2, &jne_off, 4);
        }

        /* Patch all JMP-to-end */
        for (int ji = 0; ji < jmp_count; ji++) {
            int32_t off = (int32_t)(cg->code_size - (jmp_ends[ji] + 5));
            memcpy(cg->code + jmp_ends[ji] + 1, &off, 4);
        }
        break;
    }

    case NODE_TRY_CATCH: {
        /*
         * try { body } catch (e: Error) { handler }
         *
         * Cross-function error handling via callee-saved registers:
         *   R12 = saved RSP (try entry)
         *   R13 = saved RBP (try entry)
         *   R14 = catch entry address (code offset + load base)
         *   R15 = catch active (1 = active, 0 = inactive)
         *
         * error.raise anywhere (even in called functions) restores
         * R12->RSP, R13->RBP, and jumps to R14. Error code in RAX.
         */
        int n; uint8_t *b;

        /* Save RSP -> R12 */
        b = BUF(cg); b[0]=0x49; b[1]=0x89; b[2]=0xE4; EMIT(cg,3); /* mov r12, rsp */
        /* Save RBP -> R13 */
        b = BUF(cg); b[0]=0x49; b[1]=0x89; b[2]=0xED; EMIT(cg,3); /* mov r13, rbp */

        /* Load catch entry address into R14 (LEA with RIP-relative) */
        /* We don't know the address yet, so emit a placeholder MOV R14, imm64
         * and patch it later. */
        size_t r14_patch = cg->code_size;
        b = BUF(cg);
        b[0] = 0x49; b[1] = 0xBE; /* mov r14, imm64 */
        memset(b+2, 0, 8); /* placeholder */
        EMIT(cg, 10);

        /* Set R15 = 1 (catch active) */
        b = BUF(cg); b[0]=0x49; b[1]=0xC7; b[2]=0xC7;
        int32_t one=1; memcpy(b+3,&one,4); EMIT(cg,7); /* mov r15, 1 */

        /* Emit try body */
        emit_block(cg, node->children[0]);

        /* Success: deactivate catch */
        b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xFF; EMIT(cg,3); /* xor r15, r15 */

        /* JMP over catch */
        size_t jmp_after = cg->code_size;
        n = emit_jmp(BUF(cg), 0); EMIT(cg, n);

        /* === CATCH ENTRY POINT === */
        size_t catch_addr = cg->code_size;

        /* Patch R14 with the catch address + ELF load base (0x400000 + header) */
        uint64_t catch_runtime_addr = 0x400078 + catch_addr; /* ELF base + header */
        memcpy(cg->code + r14_patch + 2, &catch_runtime_addr, 8);

        /* Restore RSP and RBP from R12/R13 */
        b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xE4; EMIT(cg,3); /* mov rsp, r12 */
        b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xED; EMIT(cg,3); /* mov rbp, r13 */

        /* Deactivate catch */
        b = BUF(cg); b[0]=0x4D; b[1]=0x31; b[2]=0xFF; EMIT(cg,3); /* xor r15, r15 */

        /* Define catch variable (error code in RAX) */
        if (node->string_val) {
            LocalVar *catch_var = add_local(cg, node->string_val);
            if (catch_var) {
                n = emit_mov_mem_reg(BUF(cg), REG_RBP, catch_var->rbp_off, REG_RAX);
                EMIT(cg, n);
            }
        }

        /* Emit catch body (children[2]) */
        if (node->child_count >= 3) {
            emit_block(cg, node->children[2]);
        }

        /* Patch JMP after try */
        int32_t jmp_off = (int32_t)(cg->code_size - (jmp_after + 5));
        memcpy(cg->code + jmp_after + 1, &jmp_off, 4);

        break;
    }

    case NODE_ERROR_RAISE: {
        /*
         * error.raise(code, "message")
         * If R15 != 0 (catch active): restore RSP=R12, RBP=R13, jmp R14
         * If R15 == 0: exit with error code (unhandled error)
         * Error code in RAX.
         */
        int n; uint8_t *b;

        /* Evaluate error code -> RAX */
        if (node->child_count >= 1) {
            emit_expression(cg, node->children[0]);
        }

        /* test r15, r15 (is catch active?) */
        b = BUF(cg); b[0]=0x4D; b[1]=0x85; b[2]=0xFF; EMIT(cg,3);

        /* jz .no_catch */
        size_t jz_pos = cg->code_size;
        b = BUF(cg); b[0]=0x74; b[1]=0x00; EMIT(cg,2);

        /* Catch is active: restore and jump */
        b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xE4; EMIT(cg,3); /* mov rsp, r12 */
        b = BUF(cg); b[0]=0x4C; b[1]=0x89; b[2]=0xED; EMIT(cg,3); /* mov rbp, r13 */
        /* jmp r14 */
        b = BUF(cg); b[0]=0x41; b[1]=0xFF; b[2]=0xE6; EMIT(cg,3);

        /* .no_catch: exit with error code */
        cg->code[jz_pos+1] = (uint8_t)(cg->code_size - (jz_pos+2));
        n = emit_mov_reg_reg(BUF(cg), REG_RDI, REG_RAX); EMIT(cg, n);
        n = emit_mov_reg_imm32(BUF(cg), REG_RAX, 60); EMIT(cg, n);
        n = emit_syscall(BUF(cg)); EMIT(cg, n);

        break;
    }

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

        /* Skip already-patched entries (e.g. error.raise jumps) */
        if (!target) continue;

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
