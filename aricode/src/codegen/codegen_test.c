/*
 * aricode - Ari Code Language
 * Code Generator Test Suite
 *
 * Each test builds an AST programmatically, generates x86_64 machine code,
 * writes an ELF binary, executes it, and verifies the exit code matches
 * the expected result.
 */

#define _POSIX_C_SOURCE 200809L

#include "../parser/ast.h"
#include "codegen.h"
#include "elf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/*  Test infrastructure                                               */
/* ------------------------------------------------------------------ */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static const char *TEST_BIN = "/tmp/aricode_test_bin";

/*
 * Compile an AST to an ELF binary, execute it, return the exit code.
 * Returns -1 on compilation failure, -2 on exec failure.
 */
static int compile_and_run(const ASTNode *ast) {
    if (codegen_compile_to_file(ast, TEST_BIN) != 0)
        return -1;

    /* Fork and exec */
    pid_t pid = fork();
    if (pid < 0) return -2;

    if (pid == 0) {
        /* Child: exec the binary */
        execl(TEST_BIN, TEST_BIN, (char *)NULL);
        _exit(127); /* exec failed */
    }

    /* Parent: wait for child */
    int status;
    if (waitpid(pid, &status, 0) < 0) return -2;

    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    return -2;
}

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  TEST %-50s", name); \
    } while (0)

#define EXPECT_EXIT(ast, code) \
    do { \
        int rc = compile_and_run(ast); \
        if (rc == (code)) { \
            tests_passed++; \
            printf("[PASS]\n"); \
        } else { \
            tests_failed++; \
            printf("[FAIL] expected %d, got %d\n", (code), rc); \
        } \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Helper: build a minimal function AST                              */
/* ------------------------------------------------------------------ */

/*
 * Build:  fn main() -> i32 { <body_stmts> }
 * wrapped in a NODE_PROGRAM.
 *
 * The caller passes an array of statement nodes that become the body.
 */
static ASTNode *wrap_main(ASTNode **stmts, size_t stmt_count) {
    /* Program root */
    ASTNode *prog = ast_create_node(NODE_PROGRAM, 1, 1);

    /* fn main */
    ASTNode *fn = ast_create_node(NODE_FN_DECL, 1, 1);
    fn->string_val = strdup("main");

    /* params (empty block) */
    ASTNode *params = ast_create_node(NODE_BLOCK, 1, 1);
    ast_add_child(fn, params);

    /* return type annotation */
    ASTNode *ret_type = ast_create_node(NODE_TYPE_ANNOTATION, 1, 1);
    ret_type->string_val = strdup("i32");
    ast_add_child(fn, ret_type);

    /* body block */
    ASTNode *body = ast_create_node(NODE_BLOCK, 1, 1);
    for (size_t i = 0; i < stmt_count; i++)
        ast_add_child(body, stmts[i]);
    ast_add_child(fn, body);

    ast_add_child(prog, fn);
    return prog;
}

/*
 * Convenience: wrap a single return expression in main().
 */
static ASTNode *wrap_return_expr(ASTNode *expr) {
    ASTNode *ret = ast_create_node(NODE_RETURN, 1, 1);
    ast_add_child(ret, expr);
    ASTNode *stmts[] = { ret };
    return wrap_main(stmts, 1);
}

/*
 * Build an integer literal node.
 */
static ASTNode *make_int(int64_t val) {
    ASTNode *n = ast_create_node(NODE_INT_LITERAL, 1, 1);
    n->int_val = val;
    return n;
}

/*
 * Build a binary op node.
 */
static ASTNode *make_binop(const char *op, ASTNode *left, ASTNode *right) {
    ASTNode *n = ast_create_node(NODE_BINARY_OP, 1, 1);
    n->op = strdup(op);
    ast_add_child(n, left);
    ast_add_child(n, right);
    return n;
}

/*
 * Build an identifier node.
 */
static ASTNode *make_ident(const char *name) {
    ASTNode *n = ast_create_node(NODE_IDENTIFIER, 1, 1);
    n->string_val = strdup(name);
    return n;
}

/* ------------------------------------------------------------------ */
/*  Test cases                                                        */
/* ------------------------------------------------------------------ */

static void test_return_constant(void) {
    TEST("return 42");
    ASTNode *ast = wrap_return_expr(make_int(42));
    EXPECT_EXIT(ast, 42);
    ast_free(ast);
}

static void test_return_zero(void) {
    TEST("return 0");
    ASTNode *ast = wrap_return_expr(make_int(0));
    EXPECT_EXIT(ast, 0);
    ast_free(ast);
}

static void test_add(void) {
    TEST("return 37 + 5");
    ASTNode *expr = make_binop("+", make_int(37), make_int(5));
    ASTNode *ast = wrap_return_expr(expr);
    EXPECT_EXIT(ast, 42);
    ast_free(ast);
}

static void test_sub(void) {
    TEST("return 10 - 3");
    ASTNode *expr = make_binop("-", make_int(10), make_int(3));
    ASTNode *ast = wrap_return_expr(expr);
    EXPECT_EXIT(ast, 7);
    ast_free(ast);
}

static void test_mul(void) {
    TEST("return 6 * 7");
    ASTNode *expr = make_binop("*", make_int(6), make_int(7));
    ASTNode *ast = wrap_return_expr(expr);
    EXPECT_EXIT(ast, 42);
    ast_free(ast);
}

static void test_div(void) {
    TEST("return 84 / 2");
    ASTNode *expr = make_binop("/", make_int(84), make_int(2));
    ASTNode *ast = wrap_return_expr(expr);
    EXPECT_EXIT(ast, 42);
    ast_free(ast);
}

static void test_mod(void) {
    TEST("return 47 % 10");
    ASTNode *expr = make_binop("%", make_int(47), make_int(10));
    ASTNode *ast = wrap_return_expr(expr);
    EXPECT_EXIT(ast, 7);
    ast_free(ast);
}

static void test_nested_expr(void) {
    TEST("return (2 + 3) * 8 + 2");
    /* (2 + 3) * 8 + 2 = 42 */
    ASTNode *add1 = make_binop("+", make_int(2), make_int(3));
    ASTNode *mul  = make_binop("*", add1, make_int(8));
    ASTNode *add2 = make_binop("+", mul, make_int(2));
    ASTNode *ast = wrap_return_expr(add2);
    EXPECT_EXIT(ast, 42);
    ast_free(ast);
}

static void test_local_variable(void) {
    TEST("let x: i32 = 40; return x + 2");

    /* let x: i32 = 40; */
    ASTNode *var = ast_create_node(NODE_VAR_DECL, 1, 1);
    var->string_val = strdup("x");
    ASTNode *type = ast_create_node(NODE_TYPE_ANNOTATION, 1, 1);
    type->string_val = strdup("i32");
    ast_add_child(var, type);
    ast_add_child(var, make_int(40));

    /* return x + 2; */
    ASTNode *expr = make_binop("+", make_ident("x"), make_int(2));
    ASTNode *ret = ast_create_node(NODE_RETURN, 1, 1);
    ast_add_child(ret, expr);

    ASTNode *stmts[] = { var, ret };
    ASTNode *ast = wrap_main(stmts, 2);
    EXPECT_EXIT(ast, 42);
    ast_free(ast);
}

static void test_multiple_locals(void) {
    TEST("let a = 10; let b = 32; return a + b");

    ASTNode *va = ast_create_node(NODE_VAR_DECL, 1, 1);
    va->string_val = strdup("a");
    ASTNode *ta = ast_create_node(NODE_TYPE_ANNOTATION, 1, 1);
    ta->string_val = strdup("i32");
    ast_add_child(va, ta);
    ast_add_child(va, make_int(10));

    ASTNode *vb = ast_create_node(NODE_VAR_DECL, 1, 1);
    vb->string_val = strdup("b");
    ASTNode *tb = ast_create_node(NODE_TYPE_ANNOTATION, 1, 1);
    tb->string_val = strdup("i32");
    ast_add_child(vb, tb);
    ast_add_child(vb, make_int(32));

    ASTNode *expr = make_binop("+", make_ident("a"), make_ident("b"));
    ASTNode *ret = ast_create_node(NODE_RETURN, 1, 1);
    ast_add_child(ret, expr);

    ASTNode *stmts[] = { va, vb, ret };
    ASTNode *ast = wrap_main(stmts, 3);
    EXPECT_EXIT(ast, 42);
    ast_free(ast);
}

static void test_function_call(void) {
    TEST("fn add(a, b) -> i32 { return a + b; } main: return add(37, 5)");

    ASTNode *prog = ast_create_node(NODE_PROGRAM, 1, 1);

    /* fn add(a: i32, b: i32) -> i32 { return a + b; } */
    ASTNode *fn_add = ast_create_node(NODE_FN_DECL, 1, 1);
    fn_add->string_val = strdup("add");

    ASTNode *params = ast_create_node(NODE_BLOCK, 1, 1);
    {
        ASTNode *pa = ast_create_node(NODE_VAR_DECL, 1, 1);
        pa->string_val = strdup("a");
        ASTNode *pta = ast_create_node(NODE_TYPE_ANNOTATION, 1, 1);
        pta->string_val = strdup("i32");
        ast_add_child(pa, pta);
        ast_add_child(params, pa);

        ASTNode *pb = ast_create_node(NODE_VAR_DECL, 1, 1);
        pb->string_val = strdup("b");
        ASTNode *ptb = ast_create_node(NODE_TYPE_ANNOTATION, 1, 1);
        ptb->string_val = strdup("i32");
        ast_add_child(pb, ptb);
        ast_add_child(params, pb);
    }
    ast_add_child(fn_add, params);

    ASTNode *ret_type = ast_create_node(NODE_TYPE_ANNOTATION, 1, 1);
    ret_type->string_val = strdup("i32");
    ast_add_child(fn_add, ret_type);

    ASTNode *add_body = ast_create_node(NODE_BLOCK, 1, 1);
    {
        ASTNode *ret = ast_create_node(NODE_RETURN, 1, 1);
        ASTNode *expr = make_binop("+", make_ident("a"), make_ident("b"));
        ast_add_child(ret, expr);
        ast_add_child(add_body, ret);
    }
    ast_add_child(fn_add, add_body);
    ast_add_child(prog, fn_add);

    /* fn main() -> i32 { return add(37, 5); } */
    ASTNode *fn_main = ast_create_node(NODE_FN_DECL, 1, 1);
    fn_main->string_val = strdup("main");

    ASTNode *main_params = ast_create_node(NODE_BLOCK, 1, 1);
    ast_add_child(fn_main, main_params);

    ASTNode *main_ret_type = ast_create_node(NODE_TYPE_ANNOTATION, 1, 1);
    main_ret_type->string_val = strdup("i32");
    ast_add_child(fn_main, main_ret_type);

    ASTNode *main_body = ast_create_node(NODE_BLOCK, 1, 1);
    {
        /* call add(37, 5) */
        ASTNode *call = ast_create_node(NODE_CALL, 1, 1);
        ast_add_child(call, make_ident("add"));
        ast_add_child(call, make_int(37));
        ast_add_child(call, make_int(5));

        ASTNode *ret = ast_create_node(NODE_RETURN, 1, 1);
        ast_add_child(ret, call);
        ast_add_child(main_body, ret);
    }
    ast_add_child(fn_main, main_body);
    ast_add_child(prog, fn_main);

    EXPECT_EXIT(prog, 42);
    ast_free(prog);
}

static void test_comparison_eq(void) {
    TEST("return (42 == 42)  [expect 1]");
    ASTNode *expr = make_binop("==", make_int(42), make_int(42));
    ASTNode *ast = wrap_return_expr(expr);
    EXPECT_EXIT(ast, 1);
    ast_free(ast);
}

static void test_comparison_neq(void) {
    TEST("return (42 != 43)  [expect 1]");
    ASTNode *expr = make_binop("!=", make_int(42), make_int(43));
    ASTNode *ast = wrap_return_expr(expr);
    EXPECT_EXIT(ast, 1);
    ast_free(ast);
}

static void test_comparison_lt(void) {
    TEST("return (3 < 5)     [expect 1]");
    ASTNode *expr = make_binop("<", make_int(3), make_int(5));
    ASTNode *ast = wrap_return_expr(expr);
    EXPECT_EXIT(ast, 1);
    ast_free(ast);
}

static void test_if_then(void) {
    TEST("if (1) { return 42; } return 0");

    /* condition: 1 (true) */
    ASTNode *cond = make_int(1);

    /* then block: return 42 */
    ASTNode *then_block = ast_create_node(NODE_BLOCK, 1, 1);
    ASTNode *ret42 = ast_create_node(NODE_RETURN, 1, 1);
    ast_add_child(ret42, make_int(42));
    ast_add_child(then_block, ret42);

    ASTNode *if_node = ast_create_node(NODE_IF, 1, 1);
    ast_add_child(if_node, cond);
    ast_add_child(if_node, then_block);

    /* fallthrough: return 0 */
    ASTNode *ret0 = ast_create_node(NODE_RETURN, 1, 1);
    ast_add_child(ret0, make_int(0));

    ASTNode *stmts[] = { if_node, ret0 };
    ASTNode *ast = wrap_main(stmts, 2);
    EXPECT_EXIT(ast, 42);
    ast_free(ast);
}

static void test_large_value(void) {
    TEST("return 255");
    ASTNode *ast = wrap_return_expr(make_int(255));
    EXPECT_EXIT(ast, 255);
    ast_free(ast);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("\n");
    printf("==========================================================\n");
    printf("  aricode Code Generator Test Suite (x86_64 Linux)\n");
    printf("==========================================================\n\n");

    test_return_constant();
    test_return_zero();
    test_add();
    test_sub();
    test_mul();
    test_div();
    test_mod();
    test_nested_expr();
    test_local_variable();
    test_multiple_locals();
    test_function_call();
    test_comparison_eq();
    test_comparison_neq();
    test_comparison_lt();
    test_if_then();
    test_large_value();

    printf("\n----------------------------------------------------------\n");
    printf("  Results: %d passed, %d failed, %d total\n",
           tests_passed, tests_failed, tests_run);
    printf("----------------------------------------------------------\n\n");

    /* Cleanup temp file */
    unlink(TEST_BIN);

    return tests_failed > 0 ? 1 : 0;
}
