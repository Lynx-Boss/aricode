/*
 * aricode - Code Generator: Peephole catalog
 * ============================================
 *
 * A peephole in aricode is a tail-inspecting rewrite: it looks at the
 * last few bytes of the in-progress code buffer, optionally rewinds
 * the cursor, and (optionally) emits a replacement sequence.  Each
 * peephole is a tiny, local transformation — the catalog lives here
 * so new additions can see the contracts the existing ones obey.
 *
 * CONTRACT (applies to every function in this header):
 *
 *   - The peephole runs at an arbitrary point during codegen, NOT as
 *     a post-pass.  It sees `cg->code_size` bytes already emitted and
 *     may read `cg->code[0 .. code_size-1]`.
 *
 *   - If it rewinds the cursor (`cg->code_size -= N`), it MUST NOT
 *     assume any runtime register state that the rewound instructions
 *     would have produced.  Callers that rely on rax, xmm0, or flags
 *     being populated by the rewound sequence need to be audited
 *     individually — see the per-function comments below and the
 *     memory note in project_instruction_scheduling.md ("stale-rax
 *     reload bug").
 *
 *   - Peepholes do NOT examine the AST.  They are purely byte-level
 *     rewrites, which keeps their correctness decidable from the
 *     emit_* code alone.
 */

#ifndef ARICODE_PEEPHOLES_H
#define ARICODE_PEEPHOLES_H

#include "codegen.h"

/*
 * Strip a trailing `movq rax, xmm0` (66 48 0F 7E C0, 5 bytes) if it's
 * present at the end of the code buffer.
 *
 *   Rewinds:           5 bytes (or 0, if the pattern doesn't match)
 *   Register contract: assumes rax IS permitted to hold stale bits
 *                      after this call.  Callers that need rax in
 *                      sync with xmm0 MUST re-emit the sync, or
 *                      guard the peephole with a check.
 *   Returns:           1 if the peephole fired, 0 otherwise.
 *
 * Use-case: after a float binop / hot-XMM identifier read, the dual-
 * domain sync to rax is often unused by the next op (e.g. we're about
 * to `movapd xmm_dst, xmm0` into a hot slot and never touch rax).
 * Dropping the sync saves 5 bytes + 1 µop per firing.
 */
int peephole_drop_movq_rax_xmm0(CodegenState *cg);

/*
 * Emit the "skip if false" conditional jump of an `if` / `while` /
 * `for` header.  Returns the code-buffer offset of the 6-byte rel32
 * so the caller can patch the target later.
 *
 *   Rewinds:           7 bytes when the condition produced a boolean
 *                      via `setCC al ; movzx rax, al` (the common
 *                      case for `i < n` etc.).  Otherwise 0.
 *   Register contract: when rewinding, requires that the FLAGS
 *                      register from the preceding cmp/ucomisd is
 *                      still live.  The rewound `setCC + movzx` does
 *                      not touch flags, so this holds by construction.
 *   Returns:           byte offset of the rel32 field inside the
 *                      6-byte JCC near instruction.
 *
 * Effect: replaces the 11-byte "set ... movzx ... cmp rax, 0 ; je"
 * sequence with a single 6-byte direct JCC inverse.  Saves 5 bytes
 * and 3 pipeline slots per branch.
 */
size_t cg_emit_cond_jump_skip(CodegenState *cg);

#endif /* ARICODE_PEEPHOLES_H */
