/*
 * aricode - Code Generator: Hot-var register allocation catalog
 * ===============================================================
 * See hot_var.h for the safety contract.
 */

#include "hot_var.h"
#include "codegen.h"
#include "x86_64.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Declared in codegen.c; we reach into the local table by name. */
extern LocalVar *find_local(CodegenState *cg, const char *name);

int call_is_xmm_safe(const char *fn) {
    if (!fn) return 0;
    /* Type-conversion / magnitude — the oldest three. */
    if (strcmp(fn, "float_to_int") == 0) return 1;
    if (strcmp(fn, "int_to_float") == 0) return 1;
    if (strcmp(fn, "math_abs")     == 0) return 1;
    /* Integer-slot array primitives — pure bounds-check + load/store. */
    if (strcmp(fn, "arr_get")      == 0) return 1;
    if (strcmp(fn, "arr_set")      == 0) return 1;
    if (strcmp(fn, "arr_len")      == 0) return 1;
    if (strcmp(fn, "arr_new")      == 0) return 1;
    if (strcmp(fn, "byte_at")      == 0) return 1;
    if (strcmp(fn, "mem_free")     == 0) return 1;
    /* Scalar transcendentals — SSE2 polynomial approximations,
     * xmm0..xmm7 only. */
    if (strcmp(fn, "math_sqrt")    == 0) return 1;
    if (strcmp(fn, "math_exp")     == 0) return 1;
    if (strcmp(fn, "math_log")     == 0) return 1;
    if (strcmp(fn, "math_sin")     == 0) return 1;
    if (strcmp(fn, "math_cos")     == 0) return 1;
    if (strcmp(fn, "math_expm1")   == 0) return 1;
    if (strcmp(fn, "math_log1p")   == 0) return 1;
    /* Scalar f64-slot array primitives — same shape as arr_get/set. */
    if (strcmp(fn, "arr_f64_get")  == 0) return 1;
    if (strcmp(fn, "arr_f64_set")  == 0) return 1;
    if (strcmp(fn, "arr_f64_new")  == 0) return 1;
    /* AVX2 reductions — ymm0 accumulator, xmm0 tail, rbx push/pop. */
    if (strcmp(fn, "arr_f64_sum")         == 0) return 1;
    if (strcmp(fn, "arr_f64_sum_range")   == 0) return 1;
    if (strcmp(fn, "arr_f64_sum_kahan")   == 0) return 1;
    if (strcmp(fn, "arr_f64_dot")         == 0) return 1;
    if (strcmp(fn, "arr_f64_dot_range")   == 0) return 1;
    /* AVX2 element-wise / bulk memory — same register discipline. */
    if (strcmp(fn, "arr_f64_scale")       == 0) return 1;
    if (strcmp(fn, "arr_f64_fill")        == 0) return 1;
    if (strcmp(fn, "arr_f64_copy_at")     == 0) return 1;
    if (strcmp(fn, "arr_f64_copy_slice")  == 0) return 1;
    if (strcmp(fn, "arr_f64_add_scaled")  == 0) return 1;
    if (strcmp(fn, "arr_f64_sub")         == 0) return 1;
    if (strcmp(fn, "arr_f64_mul")         == 0) return 1;
    if (strcmp(fn, "arr_f64_relu")        == 0) return 1;
    if (strcmp(fn, "arr_f64_log")         == 0) return 1;
    /* Dense-layer kernels — push r12..r14 (ABI save/restore) but
     * empirically touch only ymm0..ymm3, xmm7 inside the body. */
    if (strcmp(fn, "arr_f64_matvec")      == 0) return 1;
    if (strcmp(fn, "arr_f64_matvec_T")    == 0) return 1;
    if (strcmp(fn, "arr_f64_outer_accum") == 0) return 1;
    /* f32 primitives — empirically use only ymm0..ymm3 (verified by
     * disassembly probe).  Same shape as the f64 reductions but with
     * 8-lane vfmadd231ps / vaddps / vmaxps instead of the 4-lane pd
     * forms. */
    if (strcmp(fn, "arr_f32_new")        == 0) return 1;
    if (strcmp(fn, "arr_f32_get")        == 0) return 1;
    if (strcmp(fn, "arr_f32_set")        == 0) return 1;
    if (strcmp(fn, "arr_f32_dot")        == 0) return 1;
    if (strcmp(fn, "arr_f32_sum")        == 0) return 1;
    if (strcmp(fn, "arr_f32_relu")       == 0) return 1;
    if (strcmp(fn, "arr_f32_scale")      == 0) return 1;
    if (strcmp(fn, "arr_f32_fill")       == 0) return 1;
    if (strcmp(fn, "arr_f32_add_scaled") == 0) return 1;
    if (strcmp(fn, "arr_f32_matvec")     == 0) return 1;
    if (strcmp(fn, "arr_f32_matvec_T")   == 0) return 1;
    if (strcmp(fn, "arr_f32_outer_accum")== 0) return 1;
    if (strcmp(fn, "arr_f32_copy_at")    == 0) return 1;
    if (strcmp(fn, "arr_f32_copy_slice") == 0) return 1;
    if (strcmp(fn, "arr_f32_adam_apply") == 0) return 1;
    /* Builtins that clobber ymm8..ymm15 are also callable from
     * xmm-safe bodies — the caller (emit_call_expr) wraps them with
     * a vmovupd save/restore of the cache registers when it notices
     * they're on `call_needs_ymm_save`'s list.  The classification
     * must accept them so the enclosing function stays hot. */
    if (call_needs_ymm_save(fn))          return 1;
    return 0;
}

int subtree_has_xmm_clobbering_call(const ASTNode *n) {
    if (!n) return 0;
    if (n->type == NODE_CALL) {
        const ASTNode *callee = n->child_count > 0 ? n->children[0] : NULL;
        const char *name = callee ? callee->string_val : NULL;
        if (!call_is_xmm_safe(name)) return 1;
        /* Safe call itself, but check its args for nested calls. */
        for (size_t i = 1; i < n->child_count; i++)
            if (subtree_has_xmm_clobbering_call(n->children[i])) return 1;
        return 0;
    }
    /* try/catch uses r12..r15 as the error-unwind register quartet,
     * which collides with our i32 cache.  Treat as clobbering so the
     * enclosing function opts out of hot-var mode. */
    if (n->type == NODE_TRY_CATCH || n->type == NODE_ERROR_RAISE) return 1;
    for (size_t i = 0; i < n->child_count; i++)
        if (subtree_has_xmm_clobbering_call(n->children[i])) return 1;
    return 0;
}

int node_hot_gp_reg(CodegenState *cg, const ASTNode *node) {
    if (!node || node->type != NODE_IDENTIFIER || !node->string_val) return -1;
    LocalVar *v = find_local(cg, node->string_val);
    if (!v || v->hot_gp < 0) return -1;
    return v->hot_gp;
}

int node_hot_xmm_reg(CodegenState *cg, const ASTNode *node) {
    if (!node || node->type != NODE_IDENTIFIER || !node->string_val) return -1;
    LocalVar *v = find_local(cg, node->string_val);
    if (!v || v->hot_xmm < 0) return -1;
    return v->hot_xmm;
}

int call_needs_ymm_save(const char *fn) {
    if (!fn) return 0;
    /* Builtins that write ymm8..ymm13 as live vector accumulators,
     * verified by disassembly probe.  Each one makes the calling
     * function drop out of hot-var mode unless wrapped in a
     * save/restore of the ymm8..ymm15 cache registers. */
    if (strcmp(fn, "arr_f64_softmax")            == 0) return 1;
    if (strcmp(fn, "arr_f64_sigmoid")            == 0) return 1;
    if (strcmp(fn, "arr_f64_tanh")               == 0) return 1;
    if (strcmp(fn, "arr_f64_adam_apply")         == 0) return 1;
    if (strcmp(fn, "arr_f64_conv2d_3x3_p1")      == 0) return 1;
    if (strcmp(fn, "arr_f64_conv2d_3x3_p1_multi")== 0) return 1;
    if (strcmp(fn, "arr_f32_conv2d_3x3_p1")      == 0) return 1;
    if (strcmp(fn, "arr_f32_mul")        == 0) return 1;
    if (strcmp(fn, "arr_f64_log1p")              == 0) return 1;
    if (strcmp(fn, "arr_f64_exp")                == 0) return 1;
    if (strcmp(fn, "arr_f64_expm1")              == 0) return 1;
    return 0;
}

/*
 * Emit `vmovupd [rsp+disp8], ymmN`  —  VEX-encoded AVX1 store of a
 * 256-bit ymm register to an rsp-relative slot.  Used only by the
 * save/restore wrappers below, so keeping the helper local.
 *
 * Encoding layout (3-byte VEX so we can address ymm0..ymm15 uniformly):
 *   C4  [R~ X~ B~ 0 0 0 0 1]    R~=0 for ymm8..15, B~=1 (rsp base)
 *   [0 1111 1 01]               W=0, vvvv=1111, L=1 (256-bit), pp=01
 *   11                          opcode (vmovupd m, r)
 *   mod=01 reg=ymm&7 r/m=100    SIB follows, disp8
 *   24                          SIB: scale=0 index=100(none) base=100(rsp)
 *   disp8
 */
static void emit_vmovupd_rsp_disp_ymm(CodegenState *cg, int32_t disp, int ymm, int is_load) {
    uint8_t *b = BUF(cg);
    int R_high = (ymm >= 8);
    b[0] = 0xC4;
    b[1] = (uint8_t)((R_high ? 0x00 : 0x80) | 0x40 | 0x20 | 0x01);
    b[2] = 0x7D;
    b[3] = (uint8_t)(is_load ? 0x10 : 0x11);
    if (disp >= -128 && disp <= 127) {
        b[4] = (uint8_t)(0x40 | ((ymm & 7) << 3) | 0x04);   /* mod=01 disp8 */
        b[5] = 0x24;                                         /* SIB: [rsp] */
        b[6] = (uint8_t)(int8_t)disp;
        EMIT(cg, 7);
    } else {
        b[4] = (uint8_t)(0x80 | ((ymm & 7) << 3) | 0x04);   /* mod=10 disp32 */
        b[5] = 0x24;                                         /* SIB: [rsp] */
        memcpy(b + 6, &disp, 4);
        EMIT(cg, 10);
    }
}

/*
 * Save only the ymm slots actually in use by the hot-var allocator
 * at this call site.  `next_hot_xmm` advances from 8 to 16 as f64
 * locals are declared; at the point we're emitting a call, the
 * allocated slots are xmm8..(next_hot_xmm-1).  Any f64 locals
 * declared AFTER this call haven't been assigned yet, so their
 * ymm state is not live here.
 *
 * Practical impact: a function with 2 f64 locals saves 2 × 32 B
 * instead of 8 × 32 B — 4 vmovupd around each unsafe call instead
 * of 16.  On small builtins like softmax (~N=10), the full 16-op
 * wrap was enough to make hot-var mode a net loss; the trimmed
 * form turns it back into a net win.
 *
 * If next_hot_xmm == 8 (no f64 hot locals), emit nothing at all.
 */
void emit_ymm8_15_save(CodegenState *cg) {
    int count = cg->next_hot_xmm - 8;
    if (count <= 0) return;
    if (count > 8) count = 8;
    int n = emit_sub_reg_imm(BUF(cg), REG_RSP, count * 32); EMIT(cg, n);
    for (int i = 0; i < count; i++) {
        emit_vmovupd_rsp_disp_ymm(cg, i * 32, 8 + i, 0 /*store*/);
    }
}

void emit_ymm8_15_restore(CodegenState *cg) {
    int count = cg->next_hot_xmm - 8;
    if (count <= 0) return;
    if (count > 8) count = 8;
    for (int i = 0; i < count; i++) {
        emit_vmovupd_rsp_disp_ymm(cg, i * 32, 8 + i, 1 /*load*/);
    }
    int n = emit_add_reg_imm(BUF(cg), REG_RSP, count * 32); EMIT(cg, n);
}
