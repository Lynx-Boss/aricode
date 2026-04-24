/*
 * aricode - Code Generator: Hot-var register allocation catalog
 * ===============================================================
 * See hot_var.h for the safety contract.
 */

#include "hot_var.h"
#include "codegen.h"

#include <stddef.h>
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
