/*
 * aricode - Ari Code Language
 * AST Optimizer
 *
 * Performs compile-time optimizations on the AST before codegen:
 *   - Constant folding: evaluate constant expressions at compile time
 *   - Dead code elimination: remove code after return statements
 *   - Strength reduction: replace expensive ops with cheaper ones
 *
 * These optimizations allow aricode to generate fewer instructions
 * than hand-written assembly while maintaining correctness.
 */

#ifndef ARICODE_OPTIMIZER_H
#define ARICODE_OPTIMIZER_H

#include "../parser/ast.h"

/*
 * Run all optimization passes on the AST.
 * Modifies the tree in-place.  Returns the number of optimizations applied.
 */
int optimizer_run(ASTNode *root);

/*
 * Constant folding pass.
 * Evaluates compile-time constant expressions and replaces subtrees
 * with literal nodes.
 *
 * Examples:
 *   37 + 5        -> 42
 *   10 * 2 + 3    -> 23
 *   100 / 5 - 8   -> 12
 *   -(42)          -> -42
 *   1 < 2          -> true
 */
int optimizer_constant_fold(ASTNode *root);

/*
 * Dead code elimination pass.
 * Removes unreachable statements after return in blocks.
 */
int optimizer_dead_code_elim(ASTNode *root);

#endif /* ARICODE_OPTIMIZER_H */
