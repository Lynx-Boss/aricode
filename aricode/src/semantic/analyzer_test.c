/*
 * aricode - Ari Code Language
 * Semantic Analyzer Test Suite
 *
 * Tests the GUARDIAN: every error pattern that aricode forbids
 * must be detected and blocked here.
 */

#include "analyzer.h"
#include "symbol_table.h"
#include "types.h"
#include "../parser/ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ================================================================== */
/*  Test framework                                                    */
/* ================================================================== */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do {                                          \
    tests_run++;                                                \
    printf("  [%d] %-55s ", tests_run, #name);                  \
    name();                                                     \
} while(0)

#define ASSERT(cond, ...) do {                                  \
    if (!(cond)) {                                              \
        printf("FAIL\n");                                       \
        printf("       -> ");                                   \
        printf(__VA_ARGS__);                                    \
        printf("\n");                                           \
        tests_failed++;                                         \
        return;                                                 \
    }                                                           \
} while(0)

#define PASS() do { tests_passed++; printf("OK\n"); } while(0)

/* ================================================================== */
/*  AST builder helpers                                               */
/* ================================================================== */

static ASTNode *mk_program(void) {
    return ast_create_node(NODE_PROGRAM, 1, 1);
}

static ASTNode *mk_var(const char *name, const char *type_name,
                        ASTNode *init, int line) {
    ASTNode *node = ast_create_node(NODE_VAR_DECL, line, 1);
    node->string_val = strdup(name);

    if (type_name) {
        ASTNode *ta = ast_create_node(NODE_TYPE_ANNOTATION, line, 1);
        ta->string_val = strdup(type_name);
        ast_add_child(node, ta);
    }

    if (init) {
        ast_add_child(node, init);
    }
    return node;
}

static ASTNode *mk_int(int64_t val, int line) {
    ASTNode *n = ast_create_node(NODE_INT_LITERAL, line, 1);
    n->int_val = val;
    return n;
}

static ASTNode *mk_float(double val, int line) {
    ASTNode *n = ast_create_node(NODE_FLOAT_LITERAL, line, 1);
    n->float_val = val;
    return n;
}

static ASTNode *mk_str(const char *val, int line) {
    ASTNode *n = ast_create_node(NODE_STRING_LITERAL, line, 1);
    n->string_val = strdup(val);
    return n;
}

static ASTNode *mk_bool(int val, int line) {
    ASTNode *n = ast_create_node(NODE_BOOL_LITERAL, line, 1);
    n->bool_val = val;
    return n;
}

static ASTNode *mk_ident(const char *name, int line) {
    ASTNode *n = ast_create_node(NODE_IDENTIFIER, line, 1);
    n->string_val = strdup(name);
    return n;
}

static ASTNode *mk_binop(const char *op, ASTNode *left, ASTNode *right,
                          int line) {
    ASTNode *n = ast_create_node(NODE_BINARY_OP, line, 1);
    n->op = strdup(op);
    ast_add_child(n, left);
    ast_add_child(n, right);
    return n;
}

static ASTNode *mk_block(int line) {
    return ast_create_node(NODE_BLOCK, line, 1);
}

static ASTNode *mk_return(ASTNode *expr, int line) {
    ASTNode *n = ast_create_node(NODE_RETURN, line, 1);
    if (expr) ast_add_child(n, expr);
    return n;
}

static ASTNode *mk_if(ASTNode *cond, ASTNode *then_block,
                       ASTNode *else_block, int line) {
    ASTNode *n = ast_create_node(NODE_IF, line, 1);
    ast_add_child(n, cond);
    ast_add_child(n, then_block);
    if (else_block) ast_add_child(n, else_block);
    return n;
}

static ASTNode *mk_fn(const char *name, const char *ret_type,
                       ASTNode *body, int line) {
    ASTNode *n = ast_create_node(NODE_FN_DECL, line, 1);
    n->string_val = strdup(name);
    if (ret_type) {
        ASTNode *ta = ast_create_node(NODE_TYPE_ANNOTATION, line, 1);
        ta->string_val = strdup(ret_type);
        ast_add_child(n, ta);
    }
    ast_add_child(n, body);
    return n;
}

/* Create fn with params: params are NODE_VAR_DECL children */
static ASTNode *mk_fn_with_params(const char *name, const char *ret_type,
                                   ASTNode **params, size_t param_count,
                                   ASTNode *body, int line) {
    ASTNode *n = ast_create_node(NODE_FN_DECL, line, 1);
    n->string_val = strdup(name);
    for (size_t i = 0; i < param_count; i++)
        ast_add_child(n, params[i]);
    if (ret_type) {
        ASTNode *ta = ast_create_node(NODE_TYPE_ANNOTATION, line, 1);
        ta->string_val = strdup(ret_type);
        ast_add_child(n, ta);
    }
    ast_add_child(n, body);
    return n;
}

static ASTNode *mk_call(const char *fn_name, ASTNode **args, size_t argc,
                         int line) {
    ASTNode *n = ast_create_node(NODE_CALL, line, 1);
    ast_add_child(n, mk_ident(fn_name, line));
    for (size_t i = 0; i < argc; i++)
        ast_add_child(n, args[i]);
    return n;
}

static ASTNode *mk_expr_stmt(ASTNode *expr, int line) {
    ASTNode *n = ast_create_node(NODE_EXPR_STMT, line, 1);
    ast_add_child(n, expr);
    return n;
}

static ASTNode *mk_try_catch(ASTNode *try_block, ASTNode *catch_block,
                              const char *catch_var, int line) {
    ASTNode *n = ast_create_node(NODE_TRY_CATCH, line, 1);
    ast_add_child(n, try_block);
    ast_add_child(n, catch_block);
    if (catch_var) n->op = strdup(catch_var);
    return n;
}

static ASTNode *mk_match(ASTNode *expr, int line) {
    ASTNode *n = ast_create_node(NODE_MATCH, line, 1);
    ast_add_child(n, expr);
    return n;
}

static ASTNode *mk_match_arm(ASTNode *pattern, ASTNode *body, int line) {
    ASTNode *n = ast_create_node(NODE_MATCH_ARM, line, 1);
    ast_add_child(n, pattern);
    ast_add_child(n, body);
    return n;
}

/* Helper to find an error by code */
static bool has_error_code(Analyzer *a, const char *code) {
    for (size_t i = 0; i < a->error_count; i++) {
        if (strcmp(a->errors[i].code, code) == 0) return true;
    }
    return false;
}


/* ================================================================== */
/*  TYPE SYSTEM TESTS                                                 */
/* ================================================================== */

TEST(test_type_creation) {
    AriType *t = type_create(TYPE_I32);
    ASSERT(t != NULL, "type_create returned NULL");
    ASSERT(t->kind == TYPE_I32, "Expected TYPE_I32");
    type_free(t);
    PASS();
}

TEST(test_type_from_name) {
    AriType *t1 = type_from_name("i32");
    ASSERT(t1 && t1->kind == TYPE_I32, "Expected i32");
    type_free(t1);

    AriType *t2 = type_from_name("str");
    ASSERT(t2 && t2->kind == TYPE_STR, "Expected str");
    type_free(t2);

    AriType *t3 = type_from_name("nonsense");
    ASSERT(t3 == NULL, "Expected NULL for unknown type");
    PASS();
}

TEST(test_type_equality) {
    AriType *a = type_create(TYPE_I32);
    AriType *b = type_create(TYPE_I32);
    AriType *c = type_create(TYPE_STR);
    ASSERT(types_equal(a, b), "i32 should equal i32");
    ASSERT(!types_equal(a, c), "i32 should not equal str");
    type_free(a); type_free(b); type_free(c);
    PASS();
}

TEST(test_type_data_loss) {
    AriType *i32 = type_create(TYPE_I32);
    AriType *i64 = type_create(TYPE_I64);
    AriType *f64 = type_create(TYPE_F64);
    AriType *f32 = type_create(TYPE_F32);

    ASSERT(types_loses_data(i32, i64), "i64 -> i32 should lose data");
    ASSERT(!types_loses_data(i64, i32), "i32 -> i64 should NOT lose data");
    ASSERT(types_loses_data(i32, f64), "f64 -> i32 should lose data");
    ASSERT(types_loses_data(f32, f64), "f64 -> f32 should lose data");

    type_free(i32); type_free(i64); type_free(f64); type_free(f32);
    PASS();
}

TEST(test_type_assignment_ok) {
    AriType *i32 = type_create(TYPE_I32);
    AriType *i32b = type_create(TYPE_I32);
    ASSERT(types_can_assign(i32, i32b), "i32 = i32 should be OK");
    type_free(i32); type_free(i32b);
    PASS();
}

TEST(test_type_assignment_str_to_int) {
    AriType *i32 = type_create(TYPE_I32);
    AriType *str = type_create(TYPE_STR);
    ASSERT(!types_can_assign(i32, str), "str -> i32 should be REJECTED");
    type_free(i32); type_free(str);
    PASS();
}

/* ================================================================== */
/*  SYMBOL TABLE TESTS                                                */
/* ================================================================== */

TEST(test_symtab_basic) {
    SymbolTable *st = symtab_create();
    ASSERT(st != NULL, "symtab_create returned NULL");

    Symbol *dup = symtab_define(st, "x", type_create(TYPE_I32),
                                false, true, 1, 1);
    ASSERT(dup == NULL, "First define should succeed (NULL = no dup)");

    Symbol *found = symtab_lookup(st, "x");
    ASSERT(found != NULL, "Should find 'x'");
    ASSERT(found->type->kind == TYPE_I32, "x should be i32");

    symtab_destroy(st);
    PASS();
}

TEST(test_symtab_duplicate) {
    SymbolTable *st = symtab_create();
    symtab_define(st, "x", type_create(TYPE_I32), false, true, 1, 1);
    Symbol *dup = symtab_define(st, "x", type_create(TYPE_STR),
                                false, true, 2, 1);
    ASSERT(dup != NULL, "Duplicate define should return existing symbol");
    /* The second type is leaked here but that's acceptable for a test;
     * in real code the caller would free it. */
    type_free(type_create(TYPE_STR)); /* balance the allocation */
    symtab_destroy(st);
    PASS();
}

TEST(test_symtab_scopes) {
    SymbolTable *st = symtab_create();
    symtab_define(st, "x", type_create(TYPE_I32), false, true, 1, 1);

    symtab_push_scope(st);
    symtab_define(st, "y", type_create(TYPE_STR), false, true, 2, 1);

    ASSERT(symtab_lookup(st, "x") != NULL, "x visible in inner scope");
    ASSERT(symtab_lookup(st, "y") != NULL, "y visible in inner scope");

    Scope *popped = symtab_pop_scope(st);
    scope_free(popped);

    ASSERT(symtab_lookup(st, "x") != NULL, "x still visible after pop");
    ASSERT(symtab_lookup(st, "y") == NULL, "y NOT visible after pop");

    symtab_destroy(st);
    PASS();
}

/* ================================================================== */
/*  ANALYZER TESTS - Level 0 (SILENT - BLOCKS compilation)            */
/* ================================================================== */

/* ARI-S001: Division by constant zero */
TEST(test_S001_div_by_zero_constant) {
    ASTNode *prog = mk_program();
    /* let x: i32 = 10; */
    ast_add_child(prog, mk_var("x", "i32", mk_int(10, 1), 1));
    /* let y: i32 = x / 0; */
    ASTNode *div = mk_binop("/", mk_ident("x", 2), mk_int(0, 2), 2);
    ast_add_child(prog, mk_var("y", "i32", div, 2));

    Analyzer a;
    analyzer_init(&a, prog);
    bool ok = analyzer_analyze(&a);

    ASSERT(!ok, "Compilation should be BLOCKED");
    ASSERT(has_error_code(&a, ARI_S001_CODE),
           "Should have ARI-S001 error");
    ASSERT(a.level0_count > 0, "Should have Level 0 errors");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* ARI-S001: Division by variable without guard */
TEST(test_S001_div_by_var_unguarded) {
    ASTNode *prog = mk_program();
    ast_add_child(prog, mk_var("a", "i32", mk_int(10, 1), 1));
    ast_add_child(prog, mk_var("b", "i32", mk_int(5, 2), 2));
    ASTNode *div = mk_binop("/", mk_ident("a", 3), mk_ident("b", 3), 3);
    ast_add_child(prog, mk_var("c", "i32", div, 3));

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(has_error_code(&a, ARI_S001_CODE),
           "Should flag unguarded division by variable");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* ARI-S002: Option access without matching Some/None */
TEST(test_S002_option_without_match) {
    ASTNode *prog = mk_program();
    /* Simulate: match opt { Some(v) => ... }  -- missing None */
    ASTNode *opt_ident = mk_ident("opt", 1);
    /* We need to make opt have Option type.  We'll declare it first. */
    ASTNode *opt_type = ast_create_node(NODE_TYPE_ANNOTATION, 1, 1);
    opt_type->string_val = strdup("Option");

    ASTNode *opt_decl = ast_create_node(NODE_VAR_DECL, 1, 1);
    opt_decl->string_val = strdup("opt");
    ast_add_child(opt_decl, opt_type);
    ASTNode *none_init = ast_create_node(NODE_NONE, 1, 1);
    ast_add_child(opt_decl, none_init);
    ast_add_child(prog, opt_decl);

    /* match opt { Some(v) => 1 }  -- MISSING None case */
    ASTNode *match = mk_match(mk_ident("opt", 3), 3);
    ASTNode *some_pat = ast_create_node(NODE_SOME, 4, 1);
    ast_add_child(some_pat, mk_ident("v", 4));
    ast_add_child(match, mk_match_arm(some_pat, mk_int(1, 4), 4));
    ast_add_child(prog, match);

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(has_error_code(&a, ARI_S002_CODE),
           "Should flag non-exhaustive Option match (missing None)");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* ARI-S004: Empty catch block */
TEST(test_S004_empty_catch) {
    ASTNode *prog = mk_program();

    ASTNode *try_blk = mk_block(1);
    ast_add_child(try_blk, mk_expr_stmt(mk_int(1, 2), 2));

    ASTNode *catch_blk = mk_block(3);  /* EMPTY! */

    ast_add_child(prog, mk_try_catch(try_blk, catch_blk, "e", 1));

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(has_error_code(&a, ARI_S004_CODE),
           "Should flag empty catch block");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* ARI-S006: Implicit conversion with data loss (i64 -> i32) */
TEST(test_S006_data_loss_i64_to_i32) {
    ASTNode *prog = mk_program();
    /* let big: i64 = 100; */
    ast_add_child(prog, mk_var("big", "i64", mk_int(100, 1), 1));
    /* let small: i32 = big;  -- data loss! */
    ast_add_child(prog, mk_var("small", "i32", mk_ident("big", 2), 2));

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(has_error_code(&a, ARI_S006_CODE),
           "Should flag i64 -> i32 implicit conversion");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* ARI-S006: float to int data loss */
TEST(test_S006_data_loss_f64_to_i32) {
    ASTNode *prog = mk_program();
    ast_add_child(prog, mk_var("pi", "f64", mk_float(3.14, 1), 1));
    ast_add_child(prog, mk_var("x", "i32", mk_ident("pi", 2), 2));

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(has_error_code(&a, ARI_S006_CODE),
           "Should flag f64 -> i32 implicit conversion");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* ================================================================== */
/*  ANALYZER TESTS - Level 1 (LOGIC errors)                           */
/* ================================================================== */

/* Type mismatch: assign str to i32 */
TEST(test_L1_type_mismatch_assign) {
    ASTNode *prog = mk_program();
    /* let x: i32 = "hello"; */
    ast_add_child(prog, mk_var("x", "i32", mk_str("hello", 1), 1));

    Analyzer a;
    analyzer_init(&a, prog);
    bool ok = analyzer_analyze(&a);

    ASSERT(!ok, "Should block compilation");
    ASSERT(a.level1_count > 0, "Should have level 1 errors");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* Undefined variable */
TEST(test_L1_undefined_variable) {
    ASTNode *prog = mk_program();
    /* let x: i32 = y;  -- y is not defined */
    ast_add_child(prog, mk_var("x", "i32", mk_ident("y", 1), 1));

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(a.level1_count > 0, "Should detect undefined variable");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* Duplicate variable in same scope */
TEST(test_L1_duplicate_variable) {
    ASTNode *prog = mk_program();
    ast_add_child(prog, mk_var("x", "i32", mk_int(1, 1), 1));
    ast_add_child(prog, mk_var("x", "i32", mk_int(2, 2), 2));

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(a.level1_count > 0, "Should detect duplicate declaration");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* Wrong number of arguments */
TEST(test_L1_wrong_arg_count) {
    ASTNode *prog = mk_program();

    /* fn add(a: i32, b: i32) -> i32 { return a; } */
    ASTNode *body = mk_block(2);
    ast_add_child(body, mk_return(mk_ident("a", 3), 3));
    ASTNode *params[2];
    params[0] = mk_var("a", "i32", NULL, 1);
    params[1] = mk_var("b", "i32", NULL, 1);
    ast_add_child(prog, mk_fn_with_params("add", "i32", params, 2, body, 1));

    /* add(1)  -- missing one argument */
    ASTNode *args[1];
    args[0] = mk_int(1, 5);
    ast_add_child(prog, mk_expr_stmt(mk_call("add", args, 1, 5), 5));

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(a.level1_count > 0, "Should detect wrong argument count");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* Function return type mismatch */
TEST(test_L1_return_type_mismatch) {
    ASTNode *prog = mk_program();

    /* fn get_num() -> i32 { return "hello"; } */
    ASTNode *body = mk_block(2);
    ast_add_child(body, mk_return(mk_str("hello", 3), 3));
    ast_add_child(prog, mk_fn("get_num", "i32", body, 1));

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(a.level1_count > 0, "Should detect return type mismatch");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* ================================================================== */
/*  ANALYZER TESTS - Level 2 (WARNINGS)                               */
/* ================================================================== */

/* ARI-W001: Unused variable */
TEST(test_W001_unused_variable) {
    ASTNode *prog = mk_program();
    /* let x: i32 = 42;  -- never used */
    ast_add_child(prog, mk_var("x", "i32", mk_int(42, 1), 1));

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(has_error_code(&a, ARI_W001_CODE),
           "Should warn about unused variable");
    ASSERT(a.level2_count > 0, "Should have warnings");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* Unused variable with _ prefix should NOT warn */
TEST(test_W001_underscore_no_warn) {
    ASTNode *prog = mk_program();
    ast_add_child(prog, mk_var("_unused", "i32", mk_int(42, 1), 1));

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(!has_error_code(&a, ARI_W001_CODE),
           "Should NOT warn about _prefixed variable");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* ARI-W002: Unreachable code */
TEST(test_W002_unreachable_code) {
    ASTNode *prog = mk_program();

    ASTNode *body = mk_block(2);
    ast_add_child(body, mk_return(mk_int(1, 3), 3));
    /* Code after return: */
    ast_add_child(body, mk_var("dead", "i32", mk_int(99, 4), 4));
    ast_add_child(prog, mk_fn("foo", "i32", body, 1));

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(has_error_code(&a, ARI_W002_CODE),
           "Should warn about unreachable code after return");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* ARI-W003: Variable shadowing */
TEST(test_W003_shadowing) {
    ASTNode *prog = mk_program();
    /* let x: i32 = 1; */
    ast_add_child(prog, mk_var("x", "i32", mk_int(1, 1), 1));

    /* fn foo() -> void { let x: i32 = 2; } -- shadows outer x */
    ASTNode *body = mk_block(3);
    ast_add_child(body, mk_var("x", "i32", mk_int(2, 4), 4));
    ast_add_child(prog, mk_fn("foo", "void", body, 3));

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(has_error_code(&a, ARI_W003_CODE),
           "Should warn about variable shadowing");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* ARI-W004: Condition always true */
TEST(test_W004_always_true) {
    ASTNode *prog = mk_program();

    /* if (true) { let x: i32 = 1; } */
    ASTNode *then_blk = mk_block(2);
    ast_add_child(then_blk, mk_var("_x", "i32", mk_int(1, 3), 3));
    ast_add_child(prog, mk_if(mk_bool(1, 1), then_blk, NULL, 1));

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(has_error_code(&a, ARI_W004_CODE),
           "Should warn about always-true condition");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* ================================================================== */
/*  ANALYZER TESTS - Scope visibility                                 */
/* ================================================================== */

/* Variable defined in if-block not visible outside */
TEST(test_scope_if_block) {
    ASTNode *prog = mk_program();

    /* if (true) { let inner: i32 = 1; } */
    ASTNode *then_blk = mk_block(2);
    ast_add_child(then_blk, mk_var("inner", "i32", mk_int(1, 3), 3));
    ast_add_child(prog, mk_if(mk_bool(1, 1), then_blk, NULL, 1));

    /* let y: i32 = inner;  -- should fail: inner not visible */
    ast_add_child(prog, mk_var("y", "i32", mk_ident("inner", 5), 5));

    Analyzer a;
    analyzer_init(&a, prog);
    analyzer_analyze(&a);

    ASSERT(a.level1_count > 0,
           "Should detect that 'inner' is not visible outside if-block");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* ================================================================== */
/*  ANALYZER TESTS - Valid code (no errors expected)                   */
/* ================================================================== */

/* Correct type assignment */
TEST(test_valid_i32_assign) {
    ASTNode *prog = mk_program();
    /* let x: i32 = 42; (used below) */
    ast_add_child(prog, mk_var("x", "i32", mk_int(42, 1), 1));
    /* let y: i32 = x; */
    ast_add_child(prog, mk_var("_y", "i32", mk_ident("x", 2), 2));

    Analyzer a;
    analyzer_init(&a, prog);
    bool ok = analyzer_analyze(&a);

    ASSERT(ok, "Valid i32 assignment should pass");
    ASSERT(a.level0_count == 0, "No Level 0 errors expected");
    ASSERT(a.level1_count == 0, "No Level 1 errors expected");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* Widening: i32 -> i64 is OK */
TEST(test_valid_widening) {
    ASTNode *prog = mk_program();
    ast_add_child(prog, mk_var("small", "i32", mk_int(10, 1), 1));
    ast_add_child(prog, mk_var("_big", "i64", mk_ident("small", 2), 2));

    Analyzer a;
    analyzer_init(&a, prog);
    bool ok = analyzer_analyze(&a);

    ASSERT(ok, "i32 -> i64 widening should be OK");
    ASSERT(a.level0_count == 0, "No Level 0 errors");

    analyzer_destroy(&a);
    ast_free(prog);
    PASS();
}

/* ================================================================== */
/*  Main                                                              */
/* ================================================================== */

int main(void) {
    printf("\n=== aricode Semantic Analyzer Tests ===\n\n");

    printf("-- Type System --\n");
    RUN(test_type_creation);
    RUN(test_type_from_name);
    RUN(test_type_equality);
    RUN(test_type_data_loss);
    RUN(test_type_assignment_ok);
    RUN(test_type_assignment_str_to_int);

    printf("\n-- Symbol Table --\n");
    RUN(test_symtab_basic);
    RUN(test_symtab_duplicate);
    RUN(test_symtab_scopes);

    printf("\n-- Level 0: SILENT (BLOCKS compilation) --\n");
    RUN(test_S001_div_by_zero_constant);
    RUN(test_S001_div_by_var_unguarded);
    RUN(test_S002_option_without_match);
    RUN(test_S004_empty_catch);
    RUN(test_S006_data_loss_i64_to_i32);
    RUN(test_S006_data_loss_f64_to_i32);

    printf("\n-- Level 1: LOGIC (programming errors) --\n");
    RUN(test_L1_type_mismatch_assign);
    RUN(test_L1_undefined_variable);
    RUN(test_L1_duplicate_variable);
    RUN(test_L1_wrong_arg_count);
    RUN(test_L1_return_type_mismatch);

    printf("\n-- Level 2: WARNING (suspicious code) --\n");
    RUN(test_W001_unused_variable);
    RUN(test_W001_underscore_no_warn);
    RUN(test_W002_unreachable_code);
    RUN(test_W003_shadowing);
    RUN(test_W004_always_true);

    printf("\n-- Scope Visibility --\n");
    RUN(test_scope_if_block);

    printf("\n-- Valid Code (no errors) --\n");
    RUN(test_valid_i32_assign);
    RUN(test_valid_widening);

    printf("\n========================================\n");
    printf("  Total:  %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("========================================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
