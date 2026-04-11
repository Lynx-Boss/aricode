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

static void emit_expression(CodegenState *cg, const ASTNode *node);
static void emit_statement(CodegenState *cg, const ASTNode *node);
static void emit_block(CodegenState *cg, const ASTNode *node);

/* ------------------------------------------------------------------ */
/*  Symbol lookup                                                     */
/* ------------------------------------------------------------------ */

static LocalVar *find_local(CodegenState *cg, const char *name) {
    for (size_t i = 0; i < cg->local_count; i++) {
        if (strcmp(cg->locals[i].name, name) == 0)
            return &cg->locals[i];
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

static void emit_identifier(CodegenState *cg, const ASTNode *node) {
    LocalVar *v = find_local(cg, node->string_val);
    if (!v) {
        cg_error(cg, "undefined variable '%s' at %d:%d",
                 node->string_val, node->line, node->col);
        return;
    }
    /* Load from stack: mov rax, [rbp + offset] */
    int n = emit_mov_reg_mem(BUF(cg), REG_RAX, REG_RBP, v->rbp_off);
    EMIT(cg, n);
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

static void emit_expression(CodegenState *cg, const ASTNode *node) {
    if (cg->had_error || !node) return;

    switch (node->type) {
    case NODE_INT_LITERAL:
        emit_int_literal(cg, node);
        break;
    case NODE_BOOL_LITERAL:
        emit_int_literal(cg, node); /* bool_val stored in int_val=0/1 */
        break;
    case NODE_IDENTIFIER:
        emit_identifier(cg, node);
        break;
    case NODE_BINARY_OP:
        emit_binary_op(cg, node);
        break;
    case NODE_UNARY_OP:
        emit_unary_op(cg, node);
        break;
    case NODE_CALL:
        emit_call_expr(cg, node);
        break;
    default:
        cg_error(cg, "unsupported expression node type %s at %d:%d",
                 node_type_name(node->type), node->line, node->col);
        break;
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

    if (node->child_count >= 2) {
        /* Evaluate initializer -> RAX */
        emit_expression(cg, node->children[1]);
        /* Store to stack */
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
    case NODE_EXPR_STMT:
        if (node->child_count > 0)
            emit_expression(cg, node->children[0]);
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
