/*
 * aricode - Ari Code Language
 * AST Optimizer Implementation
 *
 * Transforms the AST to produce optimal machine code.
 * Every optimization is correctness-preserving.
 */

#include "optimizer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================== */
/*  Constant Folding                                                  */
/* ================================================================== */

/*
 * Check if a node is a compile-time constant integer.
 */
static int is_const_int(const ASTNode *node) {
    return node && node->type == NODE_INT_LITERAL;
}

/*
 * Check if a node is a compile-time constant boolean.
 */
static int is_const_bool(const ASTNode *node) {
    return node && node->type == NODE_BOOL_LITERAL;
}

/*
 * Try to fold a binary operation on two integer constants.
 * Returns 1 if folded (node is replaced), 0 if not foldable.
 */
static int try_fold_binary_int(ASTNode *node) {
    if (node->child_count < 2) return 0;

    ASTNode *left  = node->children[0];
    ASTNode *right = node->children[1];

    if (!is_const_int(left) || !is_const_int(right)) return 0;
    if (!node->op) return 0;

    int64_t lv = left->int_val;
    int64_t rv = right->int_val;
    const char *op = node->op;

    /* Arithmetic operations -> produce INT_LITERAL */
    if (strcmp(op, "+") == 0) {
        node->type    = NODE_INT_LITERAL;
        node->int_val = lv + rv;
    } else if (strcmp(op, "-") == 0) {
        node->type    = NODE_INT_LITERAL;
        node->int_val = lv - rv;
    } else if (strcmp(op, "*") == 0) {
        node->type    = NODE_INT_LITERAL;
        node->int_val = lv * rv;
    } else if (strcmp(op, "/") == 0) {
        if (rv == 0) return 0; /* don't fold div by zero */
        node->type    = NODE_INT_LITERAL;
        node->int_val = lv / rv;
    } else if (strcmp(op, "%") == 0) {
        if (rv == 0) return 0;
        node->type    = NODE_INT_LITERAL;
        node->int_val = lv % rv;
    }
    /* Comparison operations -> produce BOOL_LITERAL */
    else if (strcmp(op, "==") == 0) {
        node->type     = NODE_BOOL_LITERAL;
        node->bool_val = (lv == rv) ? 1 : 0;
        node->int_val  = node->bool_val;
    } else if (strcmp(op, "!=") == 0) {
        node->type     = NODE_BOOL_LITERAL;
        node->bool_val = (lv != rv) ? 1 : 0;
        node->int_val  = node->bool_val;
    } else if (strcmp(op, "<") == 0) {
        node->type     = NODE_BOOL_LITERAL;
        node->bool_val = (lv < rv) ? 1 : 0;
        node->int_val  = node->bool_val;
    } else if (strcmp(op, ">") == 0) {
        node->type     = NODE_BOOL_LITERAL;
        node->bool_val = (lv > rv) ? 1 : 0;
        node->int_val  = node->bool_val;
    } else if (strcmp(op, "<=") == 0) {
        node->type     = NODE_BOOL_LITERAL;
        node->bool_val = (lv <= rv) ? 1 : 0;
        node->int_val  = node->bool_val;
    } else if (strcmp(op, ">=") == 0) {
        node->type     = NODE_BOOL_LITERAL;
        node->bool_val = (lv >= rv) ? 1 : 0;
        node->int_val  = node->bool_val;
    } else {
        return 0;
    }

    /* Free the children and operator - this node is now a literal */
    for (size_t i = 0; i < node->child_count; i++)
        ast_free(node->children[i]);
    free(node->children);
    node->children   = NULL;
    node->child_count = 0;
    node->child_cap   = 0;
    free(node->op);
    node->op = NULL;

    return 1;
}

/*
 * Try to fold a unary operation on a constant.
 */
static int try_fold_unary(ASTNode *node) {
    if (node->child_count < 1) return 0;

    ASTNode *operand = node->children[0];
    if (!node->op) return 0;

    if (strcmp(node->op, "-") == 0 && is_const_int(operand)) {
        node->type    = NODE_INT_LITERAL;
        node->int_val = -operand->int_val;

        for (size_t i = 0; i < node->child_count; i++)
            ast_free(node->children[i]);
        free(node->children);
        node->children   = NULL;
        node->child_count = 0;
        node->child_cap   = 0;
        free(node->op);
        node->op = NULL;
        return 1;
    }

    if (strcmp(node->op, "!") == 0 && is_const_bool(operand)) {
        node->type     = NODE_BOOL_LITERAL;
        node->bool_val = operand->bool_val ? 0 : 1;
        node->int_val  = node->bool_val;

        for (size_t i = 0; i < node->child_count; i++)
            ast_free(node->children[i]);
        free(node->children);
        node->children   = NULL;
        node->child_count = 0;
        node->child_cap   = 0;
        free(node->op);
        node->op = NULL;
        return 1;
    }

    return 0;
}

/*
 * Recursively fold constants in a subtree (bottom-up).
 * Returns the number of folds performed.
 */
static int fold_node(ASTNode *node) {
    if (!node) return 0;

    int count = 0;

    /* First, recurse into children (bottom-up) */
    for (size_t i = 0; i < node->child_count; i++) {
        count += fold_node(node->children[i]);
    }

    /* Then try to fold this node */
    if (node->type == NODE_BINARY_OP) {
        count += try_fold_binary_int(node);
    } else if (node->type == NODE_UNARY_OP) {
        count += try_fold_unary(node);
    }

    return count;
}

int optimizer_constant_fold(ASTNode *root) {
    if (!root) return 0;
    return fold_node(root);
}

/* ================================================================== */
/*  Dead Code Elimination                                             */
/* ================================================================== */

/*
 * In a block, remove all statements after the first return.
 * Returns the number of statements removed.
 */
static int dce_block(ASTNode *block) {
    if (!block || block->type != NODE_BLOCK) return 0;

    int count = 0;
    int found_return = 0;

    for (size_t i = 0; i < block->child_count; i++) {
        ASTNode *stmt = block->children[i];

        /* Recurse into nested blocks and if-bodies */
        if (stmt->type == NODE_BLOCK) {
            count += dce_block(stmt);
        } else if (stmt->type == NODE_IF) {
            /* Recurse into then/else blocks */
            if (stmt->child_count >= 2)
                count += dce_block(stmt->children[1]);
            if (stmt->child_count >= 3)
                count += dce_block(stmt->children[2]);
        } else if (stmt->type == NODE_FN_DECL) {
            /* Recurse into function body */
            if (stmt->child_count > 0) {
                ASTNode *body = stmt->children[stmt->child_count - 1];
                count += dce_block(body);
            }
        }

        if (found_return) {
            /* Everything after a return is dead */
            ast_free(block->children[i]);
            block->children[i] = NULL;
            count++;
        }

        if (stmt && stmt->type == NODE_RETURN) {
            found_return = 1;
        }
    }

    /* Compact: remove NULL entries */
    if (count > 0) {
        size_t write = 0;
        for (size_t read = 0; read < block->child_count; read++) {
            if (block->children[read]) {
                block->children[write++] = block->children[read];
            }
        }
        block->child_count = write;
    }

    return count;
}

static int dce_walk(ASTNode *node) {
    if (!node) return 0;

    int count = 0;

    if (node->type == NODE_BLOCK) {
        count += dce_block(node);
    }

    /* Walk into function declarations to find their bodies */
    if (node->type == NODE_FN_DECL && node->child_count > 0) {
        ASTNode *body = node->children[node->child_count - 1];
        count += dce_block(body);
    }

    /* Walk into program children */
    if (node->type == NODE_PROGRAM) {
        for (size_t i = 0; i < node->child_count; i++) {
            count += dce_walk(node->children[i]);
        }
    }

    return count;
}

int optimizer_dead_code_elim(ASTNode *root) {
    if (!root) return 0;
    return dce_walk(root);
}

/* ================================================================== */
/*  Main optimizer entry point                                        */
/* ================================================================== */

int optimizer_run(ASTNode *root) {
    if (!root) return 0;

    int total = 0;

    /* Run constant folding iteratively until no more changes */
    int changed;
    do {
        changed = optimizer_constant_fold(root);
        total += changed;
    } while (changed > 0);

    /* Dead code elimination */
    total += optimizer_dead_code_elim(root);

    return total;
}
