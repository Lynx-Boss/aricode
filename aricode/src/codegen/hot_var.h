/*
 * aricode - Code Generator: Hot-var register allocation catalog
 * ===============================================================
 *
 * "Hot-var mode" is aricode's function-wide register-allocation
 * policy for xmm-safe functions: f64 locals get pinned to
 * xmm8..xmm15 and i32 locals to r12..r15 for the full body, so
 * every read is a 1-insn reg-reg move instead of a stack round-trip.
 *
 * A function is xmm-safe iff its body NEVER leaves the cache
 * registers (xmm8..xmm15, r12..r15) clobbered at the point of a
 * downstream read.  The two concrete things that break this are:
 *
 *   (a) a call to anything outside `call_is_xmm_safe` — SysV treats
 *       xmm8..xmm15 as caller-saved, so a regular function call is
 *       free to overwrite them; and
 *
 *   (b) try/catch / error.raise — the error-unwind machinery
 *       commandeers r12..r15 as its register quartet.
 *
 * Adding a builtin to the safe list REQUIRES an empirical audit:
 * compile a probe that calls just that builtin, disassemble the
 * emit, verify no xmm8..xmm15 / ymm8..ymm15 / r12..r15 appears as
 * a live (i.e. unsave-restore-paired) operand.  A regex on the
 * codegen_builtins.c source is NOT sufficient — raw-byte emission
 * dodges text searches (the canonical miss: softmax uses ymm11/12/13
 * via direct byte literals, so grep on its source shows 0 refs).
 * See project_instruction_scheduling.md (memory note) for the bug
 * that motivated this warning.
 *
 * ONE VIOLATION silently corrupts every hot-var local in the caller.
 * MNIST's training accuracy was the historical canary (regressed
 * from 90 % to 8 % after one epoch when the f64 return-contract
 * broke).  Re-audit whenever a builtin's register plan changes.
 */

#ifndef ARICODE_HOT_VAR_H
#define ARICODE_HOT_VAR_H

#include "codegen.h"

/*
 * Return 1 iff calling `fn` is safe to emit from inside an xmm-safe
 * function body.  A "safe" builtin touches at most xmm0..xmm7,
 * ymm0..ymm7, rax..r11, and push/pop-balances any rbx / r12..r15
 * it uses internally.
 *
 * The current safe set is enumerated in hot_var.c; NULL is handled
 * by returning 0.
 */
int call_is_xmm_safe(const char *fn);

/*
 * Walk an AST subtree and return 1 iff it contains at least one
 * NODE_CALL to a non-safe callee, a NODE_TRY_CATCH, or a
 * NODE_ERROR_RAISE.  Used by emit_function to decide whether the
 * current function body qualifies for hot-var mode.
 */
int subtree_has_xmm_clobbering_call(const ASTNode *n);

/*
 * If `node` is an identifier whose LocalVar has been assigned to a
 * hot-GP register (r12..r15), return that register index.  Else -1.
 *
 * Used by the integer-binop fast path: when the right operand of a
 * compare / add / etc. is a hot-GP identifier, we skip the stack
 * stash and read the home register directly as the RCX source.
 */
int node_hot_gp_reg(CodegenState *cg, const ASTNode *node);

/*
 * f64 counterpart: returns the hot-XMM index (xmm8..xmm15) or -1.
 *
 * Used by the float-binop fast path: addsd/subsd/mulsd/divsd all
 * accept an extended xmm register as source via REX.B, so we can
 * emit `addsd xmm0, xmm_right_home` directly.
 */
int node_hot_xmm_reg(CodegenState *cg, const ASTNode *node);

/*
 * Does calling `fn` clobber ymm8..ymm15 as live state (not push/pop
 * paired)?  Such builtins CAN still be called from xmm-safe functions
 * IF the caller wraps the call with a save/restore of ymm8..ymm15
 * (see emit_ymm8_15_save / _restore below).
 *
 * `call_is_xmm_safe` returns 1 for these too; the wrap is what keeps
 * the caller's hot-var cache intact across the call.  A builtin on
 * neither list is truly unsafe — any function containing it falls
 * out of hot-var mode entirely.
 *
 * Today's "needs save" set: softmax, sigmoid, tanh, adam_apply,
 * conv2d_3x3_p1, conv2d_3x3_p1_multi, and the AVX2 exp/log1p/expm1
 * variants.  Each was verified empirically (disassembly probe) to
 * write ymm8..ymm13 as live accumulators.
 */
int call_needs_ymm_save(const char *fn);

/*
 * Emit the "save ymm8..ymm15 on the stack" prologue / epilogue around
 * a call to a builtin in `call_needs_ymm_save`.  Allocates 256 bytes
 * of stack (8 ymm × 32 B), writes via vmovupd, reverses on restore.
 *
 * Only safe to emit inside an xmm-safe function body that's genuinely
 * using ymm8..ymm15 as a hot-var cache — there's no point paying the
 * 16×vmovupd overhead otherwise.  Caller guards with
 * `cg->in_xmm_safe_fn`.
 */
void emit_ymm8_15_save(CodegenState *cg);
void emit_ymm8_15_restore(CodegenState *cg);

#endif /* ARICODE_HOT_VAR_H */
