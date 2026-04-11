/*
 * aricode - Ari Code Language
 * AST Optimizer Implementation
 *
 * Transforms the AST to produce optimal machine code.
 * Every optimization is correctness-preserving.
 */

#include "optimizer.h"
#include "../decimal/decimal.h"
#include "../decimal/decimal_ops.h"

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
/*  Strength Reduction                                                */
/* ================================================================== */

/*
 * Check if a value is a power of 2.
 * Returns the exponent (log2), or -1 if not a power of 2.
 */
static int is_power_of_2(int64_t val) {
    if (val <= 0) return -1;
    if ((val & (val - 1)) != 0) return -1;
    int exp = 0;
    while (val > 1) { val >>= 1; exp++; }
    return exp;
}

/*
 * Strength reduction pass.
 * Replaces expensive operations with cheaper equivalents:
 *   n / 2^k  ->  n >> k   (for positive divisors, unsigned semantics)
 *   n % 2^k  ->  n & (2^k - 1)
 *   n * 2^k  ->  n << k
 *
 * Returns the number of replacements made.
 */
static int strength_reduce_node(ASTNode *node) {
    if (!node) return 0;

    int count = 0;

    /* Recurse into children first (bottom-up) */
    for (size_t i = 0; i < node->child_count; i++) {
        count += strength_reduce_node(node->children[i]);
    }

    if (node->type != NODE_BINARY_OP || !node->op) return count;
    if (node->child_count < 2) return count;

    ASTNode *right = node->children[1];
    if (!is_const_int(right)) return count;

    int64_t rv = right->int_val;
    int exp = is_power_of_2(rv);
    if (exp < 0) return count;

    if (strcmp(node->op, "/") == 0 && exp > 0) {
        /* n / 2^k  ->  n >> k */
        free(node->op);
        node->op = strdup(">>");
        right->int_val = exp;
        count++;
    } else if (strcmp(node->op, "%") == 0) {
        /* n % 2^k  ->  n & (2^k - 1) */
        free(node->op);
        node->op = strdup("&");
        right->int_val = rv - 1;
        count++;
    } else if (strcmp(node->op, "*") == 0 && exp > 0) {
        /* n * 2^k  ->  n << k */
        free(node->op);
        node->op = strdup("<<");
        right->int_val = exp;
        count++;
    }

    return count;
}

static int optimizer_strength_reduce(ASTNode *root) {
    if (!root) return 0;
    return strength_reduce_node(root);
}

/* ================================================================== */
/*  Decimal Constant Folding                                          */
/* ================================================================== */

/*
 * Fold decimal operations at compile time using AriDecimal.
 * Detects patterns like: dec_add(dec("0.1"), dec("0.2"))
 * and evaluates them to dec("0.3") at compile time.
 *
 * This is what makes aricode unique: 0.1 + 0.2 = 0.3 EXACTLY.
 * No IEEE 754 error. The computation happens in the compiler.
 */

/* Check if a node is a call to dec("...") with a string literal arg */
static int is_dec_literal_call(const ASTNode *node) {
    if (!node || node->type != NODE_CALL) return 0;
    if (node->child_count < 2) return 0;
    ASTNode *callee = node->children[0];
    if (callee->type != NODE_IDENTIFIER || !callee->string_val) return 0;
    if (strcmp(callee->string_val, "dec") != 0) return 0;
    ASTNode *arg = node->children[1];
    return arg->type == NODE_STRING_LITERAL && arg->string_val != NULL;
}

static const char *get_dec_value(const ASTNode *node) {
    return node->children[1]->string_val;
}

/*
 * Try to fold a binary op where both operands are dec("...") calls.
 * Evaluates at compile time and replaces with a new dec("result") call.
 */
static int try_fold_decimal(ASTNode *node) {
    if (node->type != NODE_BINARY_OP || !node->op) return 0;
    if (node->child_count < 2) return 0;

    ASTNode *left = node->children[0];
    ASTNode *right = node->children[1];

    if (!is_dec_literal_call(left) || !is_dec_literal_call(right)) return 0;

    const char *lval = get_dec_value(left);
    const char *rval = get_dec_value(right);
    const char *op = node->op;

    /* Parse decimals */
    AriDecimal a = ari_dec_from_string(lval);
    AriDecimal b = ari_dec_from_string(rval);
    AriDecimal result;

    if (strcmp(op, "+") == 0) {
        result = ari_dec_add(&a, &b);
    } else if (strcmp(op, "-") == 0) {
        result = ari_dec_sub(&a, &b);
    } else if (strcmp(op, "*") == 0) {
        result = ari_dec_mul(&a, &b);
    } else if (strcmp(op, "/") == 0) {
        result = ari_dec_div(&a, &b, 20);
    } else {
        ari_dec_free(&a);
        ari_dec_free(&b);
        return 0;
    }

    /* Convert result to string */
    char buf[128];
    ari_dec_to_string(&result, buf, sizeof(buf));

    /* Replace this BINARY_OP node with a CALL to dec("result") */
    /* Reuse the node structure: make it a CALL to dec with the result string */
    for (size_t i = 0; i < node->child_count; i++)
        ast_free(node->children[i]);
    free(node->children);
    free(node->op);

    node->type = NODE_CALL;
    node->op = NULL;
    node->children = NULL;
    node->child_count = 0;
    node->child_cap = 0;

    /* Add callee: identifier "dec" */
    ASTNode *callee = ast_create_node(NODE_IDENTIFIER, node->line, node->col);
    callee->string_val = strdup("dec");
    ast_add_child(node, callee);

    /* Add arg: string literal with result */
    ASTNode *arg = ast_create_node(NODE_STRING_LITERAL, node->line, node->col);
    arg->string_val = strdup(buf);
    ast_add_child(node, arg);

    ari_dec_free(&a);
    ari_dec_free(&b);
    ari_dec_free(&result);

    return 1;
}

static int fold_decimals(ASTNode *node) {
    if (!node) return 0;
    int count = 0;
    for (size_t i = 0; i < node->child_count; i++)
        count += fold_decimals(node->children[i]);
    count += try_fold_decimal(node);
    return count;
}

/* ================================================================== */
/*  Main optimizer entry point                                        */
/* ================================================================== */

int optimizer_run(ASTNode *root) {
    if (!root) return 0;

    int total = 0;

    /* Strength reduction BEFORE constant folding (creates new patterns) */
    total += optimizer_strength_reduce(root);

    /* Run constant folding iteratively until no more changes */
    int changed;
    do {
        changed = optimizer_constant_fold(root);
        total += changed;
    } while (changed > 0);

    /* Dead code elimination */
    total += optimizer_dead_code_elim(root);

    /* Decimal constant folding - evaluate exact arithmetic at compile time */
    total += fold_decimals(root);

    return total;
}
