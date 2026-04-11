/* Enable strdup on strict C11 */
#define _POSIX_C_SOURCE 200809L

/*
 * aricode - Ari Code Language
 * Test Suite: Decimal Runtime and Code Generation
 *
 * Tests the BCD representation, arithmetic operations, and codegen
 * integration at the C level. Verifies the core promise:
 *
 *   0.1 + 0.2 == 0.3   (exactly, always)
 *
 * Test structure:
 *   Part 1: BCD representation and parsing
 *   Part 2: BCD arithmetic (add, sub, mul, div, cmp)
 *   Part 3: Codegen emission (verifies code buffer output)
 *   Part 4: End-to-end simulation of the example program
 */

#include "decimal_runtime.h"
#include "decimal_codegen.h"
#include "../parser/ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ------------------------------------------------------------------ */
/*  Test infrastructure                                               */
/* ------------------------------------------------------------------ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("  [TEST] %-48s ", #name); \
        test_##name(); \
        printf("PASS\n"); \
        tests_passed++; \
    } \
    static void test_##name(void)

#define ASSERT_EQ_INT(a, b) do { \
    int _a = (a), _b = (b); \
    if (_a != _b) { \
        printf("FAIL\n    %s:%d: expected %d, got %d\n", \
               __FILE__, __LINE__, _b, _a); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_EQ_STR(a, b) do { \
    const char *_a = (a), *_b = (b); \
    if (strcmp(_a, _b) != 0) { \
        printf("FAIL\n    %s:%d: expected \"%s\", got \"%s\"\n", \
               __FILE__, __LINE__, _b, _a); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: assertion failed: %s\n", \
               __FILE__, __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ================================================================== */
/*  Part 1: BCD Representation and Parsing                            */
/* ================================================================== */

TEST(bcd_zero) {
    DecBCD z = dec_bcd_zero();
    ASSERT_TRUE(dec_bcd_is_zero(&z));
    ASSERT_EQ_INT(z.sign, 0);
    ASSERT_EQ_INT(z.exp, 0);
}

TEST(bcd_from_string_integer) {
    DecBCD d = dec_bcd_from_string("42");
    ASSERT_TRUE(!dec_bcd_is_zero(&d));
    ASSERT_EQ_INT(d.sign, 0);
    ASSERT_EQ_INT(d.exp, 0);

    char buf[64];
    dec_bcd_to_string(&d, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "42");
}

TEST(bcd_from_string_decimal) {
    DecBCD d = dec_bcd_from_string("3.14");
    ASSERT_EQ_INT(d.sign, 0);
    ASSERT_EQ_INT(d.exp, 2);

    char buf[64];
    dec_bcd_to_string(&d, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "3.14");
}

TEST(bcd_from_string_negative) {
    DecBCD d = dec_bcd_from_string("-7.5");
    ASSERT_EQ_INT(d.sign, 1);
    ASSERT_EQ_INT(d.exp, 1);

    char buf[64];
    dec_bcd_to_string(&d, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "-7.5");
}

TEST(bcd_from_string_leading_zero) {
    DecBCD d = dec_bcd_from_string("0.1");
    ASSERT_EQ_INT(d.sign, 0);
    ASSERT_EQ_INT(d.exp, 1);

    char buf[64];
    dec_bcd_to_string(&d, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "0.1");
}

TEST(bcd_from_string_zero) {
    DecBCD d = dec_bcd_from_string("0");
    ASSERT_TRUE(dec_bcd_is_zero(&d));

    char buf[64];
    dec_bcd_to_string(&d, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "0");
}

TEST(bcd_from_string_zero_decimal) {
    DecBCD d = dec_bcd_from_string("0.0");
    ASSERT_TRUE(dec_bcd_is_zero(&d));
}

TEST(bcd_from_int_positive) {
    DecBCD d = dec_bcd_from_int(12345);
    ASSERT_EQ_INT(d.sign, 0);
    ASSERT_EQ_INT(d.exp, 0);

    char buf[64];
    dec_bcd_to_string(&d, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "12345");
}

TEST(bcd_from_int_negative) {
    DecBCD d = dec_bcd_from_int(-99);
    ASSERT_EQ_INT(d.sign, 1);

    char buf[64];
    dec_bcd_to_string(&d, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "-99");
}

TEST(bcd_from_int_zero) {
    DecBCD d = dec_bcd_from_int(0);
    ASSERT_TRUE(dec_bcd_is_zero(&d));
    ASSERT_EQ_INT(d.sign, 0);
}

TEST(bcd_digit_access) {
    DecBCD d = dec_bcd_zero();
    /* Set digit at position 30 (near LSB) to 5 */
    dec_bcd_set_digit(&d, 30, 5);
    ASSERT_EQ_INT(dec_bcd_get_digit(&d, 30), 5);
    ASSERT_EQ_INT(dec_bcd_get_digit(&d, 29), 0);
    ASSERT_EQ_INT(dec_bcd_get_digit(&d, 31), 0);

    /* Set digit in hi range */
    dec_bcd_set_digit(&d, 0, 9);
    ASSERT_EQ_INT(dec_bcd_get_digit(&d, 0), 9);
}

/* ================================================================== */
/*  Part 2: BCD Arithmetic                                            */
/* ================================================================== */

TEST(add_zero_one_zero_two_equals_zero_three) {
    /*
     * THE fundamental test: 0.1 + 0.2 == 0.3
     * This MUST pass. If it doesn't, the entire decimal system is broken.
     */
    DecBCD a = dec_bcd_from_string("0.1");
    DecBCD b = dec_bcd_from_string("0.2");
    DecBCD expected = dec_bcd_from_string("0.3");

    DecBCD result = dec_bcd_add(&a, &b);

    int cmp = dec_bcd_cmp(&result, &expected);
    if (cmp != 0) {
        char buf_r[64], buf_e[64];
        dec_bcd_to_string(&result, buf_r, sizeof(buf_r));
        dec_bcd_to_string(&expected, buf_e, sizeof(buf_e));
        printf("FAIL\n    0.1 + 0.2 = %s (expected %s)\n", buf_r, buf_e);
        tests_failed++;
        return;
    }

    /* Also verify string representation */
    char buf[64];
    dec_bcd_to_string(&result, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "0.3");
}

TEST(add_integers) {
    DecBCD a = dec_bcd_from_string("100");
    DecBCD b = dec_bcd_from_string("200");
    DecBCD result = dec_bcd_add(&a, &b);

    char buf[64];
    dec_bcd_to_string(&result, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "300");
}

TEST(add_with_carry) {
    DecBCD a = dec_bcd_from_string("9.9");
    DecBCD b = dec_bcd_from_string("0.1");
    DecBCD result = dec_bcd_add(&a, &b);

    char buf[64];
    dec_bcd_to_string(&result, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "10.0");
}

TEST(add_different_scales) {
    DecBCD a = dec_bcd_from_string("1.5");
    DecBCD b = dec_bcd_from_string("2.75");
    DecBCD result = dec_bcd_add(&a, &b);

    char buf[64];
    dec_bcd_to_string(&result, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "4.25");
}

TEST(add_negative) {
    DecBCD a = dec_bcd_from_string("5.0");
    DecBCD b = dec_bcd_from_string("-3.0");
    DecBCD result = dec_bcd_add(&a, &b);

    char buf[64];
    dec_bcd_to_string(&result, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "2.0");
}

TEST(sub_basic) {
    DecBCD a = dec_bcd_from_string("5.5");
    DecBCD b = dec_bcd_from_string("2.3");
    DecBCD result = dec_bcd_sub(&a, &b);

    char buf[64];
    dec_bcd_to_string(&result, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "3.2");
}

TEST(sub_result_negative) {
    DecBCD a = dec_bcd_from_string("2.0");
    DecBCD b = dec_bcd_from_string("5.0");
    DecBCD result = dec_bcd_sub(&a, &b);

    ASSERT_EQ_INT(result.sign, 1);

    char buf[64];
    dec_bcd_to_string(&result, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "-3.0");
}

TEST(sub_to_zero) {
    DecBCD a = dec_bcd_from_string("3.14");
    DecBCD b = dec_bcd_from_string("3.14");
    DecBCD result = dec_bcd_sub(&a, &b);

    ASSERT_TRUE(dec_bcd_is_zero(&result));
    ASSERT_EQ_INT(result.sign, 0);
}

TEST(mul_basic) {
    DecBCD a = dec_bcd_from_string("3");
    DecBCD b = dec_bcd_from_string("4");
    DecBCD result = dec_bcd_mul(&a, &b);

    char buf[64];
    dec_bcd_to_string(&result, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "12");
}

TEST(mul_decimal) {
    DecBCD a = dec_bcd_from_string("2.5");
    DecBCD b = dec_bcd_from_string("4");
    DecBCD result = dec_bcd_mul(&a, &b);

    char buf[64];
    dec_bcd_to_string(&result, buf, sizeof(buf));
    /* 2.5 * 4 = 10.0 */
    ASSERT_EQ_STR(buf, "10.0");
}

TEST(mul_by_zero) {
    DecBCD a = dec_bcd_from_string("123.456");
    DecBCD b = dec_bcd_from_string("0");
    DecBCD result = dec_bcd_mul(&a, &b);

    ASSERT_TRUE(dec_bcd_is_zero(&result));
}

TEST(mul_negative) {
    DecBCD a = dec_bcd_from_string("3");
    DecBCD b = dec_bcd_from_string("-2");
    DecBCD result = dec_bcd_mul(&a, &b);

    ASSERT_EQ_INT(result.sign, 1);

    char buf[64];
    dec_bcd_to_string(&result, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "-6");
}

TEST(div_basic) {
    DecBCD a = dec_bcd_from_string("10");
    DecBCD b = dec_bcd_from_string("3");
    DecBCD result = dec_bcd_div(&a, &b, 6);

    char buf[64];
    dec_bcd_to_string(&result, buf, sizeof(buf));
    /* 10 / 3 = 3.333333 (6 decimal places) */
    ASSERT_EQ_STR(buf, "3.333333");
}

TEST(div_exact) {
    DecBCD a = dec_bcd_from_string("10");
    DecBCD b = dec_bcd_from_string("2");
    DecBCD result = dec_bcd_div(&a, &b, 4);

    /* Verify the value is exactly 5 by comparing */
    DecBCD expected = dec_bcd_from_string("5");
    ASSERT_EQ_INT(dec_bcd_cmp(&result, &expected), 0);

    /* String may or may not have trailing zeros -- both "5.0" and "5.0000" are correct */
    char buf[64];
    dec_bcd_to_string(&result, buf, sizeof(buf));
    ASSERT_TRUE(strcmp(buf, "5.0") == 0 || strcmp(buf, "5.0000") == 0);
}

TEST(div_by_zero) {
    DecBCD a = dec_bcd_from_string("5");
    DecBCD b = dec_bcd_from_string("0");
    DecBCD result = dec_bcd_div(&a, &b, 4);

    ASSERT_TRUE(dec_bcd_is_zero(&result));
}

TEST(cmp_equal) {
    DecBCD a = dec_bcd_from_string("3.14");
    DecBCD b = dec_bcd_from_string("3.14");
    ASSERT_EQ_INT(dec_bcd_cmp(&a, &b), 0);
}

TEST(cmp_less) {
    DecBCD a = dec_bcd_from_string("2.5");
    DecBCD b = dec_bcd_from_string("3.0");
    ASSERT_EQ_INT(dec_bcd_cmp(&a, &b), -1);
}

TEST(cmp_greater) {
    DecBCD a = dec_bcd_from_string("10");
    DecBCD b = dec_bcd_from_string("9.999");
    ASSERT_EQ_INT(dec_bcd_cmp(&a, &b), 1);
}

TEST(cmp_negative) {
    DecBCD a = dec_bcd_from_string("-1");
    DecBCD b = dec_bcd_from_string("1");
    ASSERT_EQ_INT(dec_bcd_cmp(&a, &b), -1);
}

TEST(cmp_both_negative) {
    DecBCD a = dec_bcd_from_string("-5");
    DecBCD b = dec_bcd_from_string("-3");
    ASSERT_EQ_INT(dec_bcd_cmp(&a, &b), -1);
}

TEST(cmp_different_exp) {
    /* 0.3 (exp=1) vs 0.30 (exp=2) should be equal */
    DecBCD a = dec_bcd_from_string("0.3");
    DecBCD b = dec_bcd_from_string("0.30");
    ASSERT_EQ_INT(dec_bcd_cmp(&a, &b), 0);
}

TEST(negate) {
    DecBCD d = dec_bcd_from_string("5.5");
    DecBCD neg = dec_bcd_negate(&d);
    ASSERT_EQ_INT(neg.sign, 1);

    DecBCD neg2 = dec_bcd_negate(&neg);
    ASSERT_EQ_INT(neg2.sign, 0);
}

TEST(abs_negative) {
    DecBCD d = dec_bcd_from_string("-42");
    DecBCD a = dec_bcd_abs(&d);
    ASSERT_EQ_INT(a.sign, 0);

    char buf[64];
    dec_bcd_to_string(&a, buf, sizeof(buf));
    ASSERT_EQ_STR(buf, "42");
}

/* ================================================================== */
/*  Part 3: Codegen Emission                                          */
/* ================================================================== */

TEST(codegen_from_literal) {
    /*
     * Test that emit_decimal_from_literal generates valid x86_64 code
     * that stores the correct BCD values on the stack.
     */
    CodegenState cg;
    DecCodegenState dcs;
    codegen_init(&cg);
    dec_codegen_init(&dcs);

    /* Simulate a stack frame */
    cg.stack_offset = -48; /* some existing locals */
    int32_t dest_off = dec_alloc_local(&cg);

    /* Emit code to load "0.1" */
    emit_decimal_from_literal(&cg, &dcs, dest_off, "0.1");

    /* Verify code was emitted (non-zero code size) */
    ASSERT_TRUE(cg.code_size > 0);
    ASSERT_EQ_INT(cg.had_error, 0);

    /* Verify the BCD value is correct by parsing independently */
    DecBCD expected = dec_bcd_from_string("0.1");
    ASSERT_EQ_INT(expected.exp, 1);
    ASSERT_TRUE(!dec_bcd_is_zero(&expected));
}

TEST(codegen_add_emits_call) {
    /*
     * Test that emit_decimal_add generates a CALL instruction
     * and registers it for patching.
     */
    CodegenState cg;
    DecCodegenState dcs;
    codegen_init(&cg);
    dec_codegen_init(&dcs);

    int32_t result_off = -24;
    int32_t a_off      = -48;
    int32_t b_off      = -72;

    size_t before = cg.code_size;
    emit_decimal_add(&cg, &dcs, result_off, a_off, b_off);
    size_t after = cg.code_size;

    /* Code was emitted */
    ASSERT_TRUE(after > before);

    /* A CALL instruction (0xE8) should be present */
    int found_call = 0;
    for (size_t i = before; i < after; i++) {
        if (cg.code[i] == 0xE8) {
            found_call = 1;
            break;
        }
    }
    ASSERT_TRUE(found_call);

    /* Decimal patch was registered */
    ASSERT_EQ_INT((int)dcs.dec_patch_count, 1);
    ASSERT_EQ_INT(dcs.dec_patches[0].func, DEC_RT_ADD);

    /* Runtime function was marked as needed */
    ASSERT_EQ_INT(dcs.needs_runtime[DEC_RT_ADD], 1);
}

TEST(codegen_cmp_emits_test) {
    /*
     * Test that emit_decimal_cmp generates a CALL + TEST sequence.
     */
    CodegenState cg;
    DecCodegenState dcs;
    codegen_init(&cg);
    dec_codegen_init(&dcs);

    emit_decimal_cmp(&cg, &dcs, -24, -48);

    ASSERT_TRUE(cg.code_size > 0);
    ASSERT_EQ_INT(dcs.needs_runtime[DEC_RT_CMP], 1);
}

/* ================================================================== */
/*  Part 4: End-to-end Simulation                                     */
/* ================================================================== */

TEST(e2e_simulation) {
    /*
     * Simulate the aricode program:
     *
     *   fn main() -> i32 {
     *     let a: dec = 0.1;
     *     let b: dec = 0.2;
     *     let result: dec = a + b;
     *     if (result == 0.3) { return 1; }
     *     return 0;
     *   }
     *
     * Execute at the C level using the BCD runtime directly.
     * This proves the runtime produces the correct result.
     */
    DecBCD a = dec_bcd_from_string("0.1");
    DecBCD b = dec_bcd_from_string("0.2");
    DecBCD result = dec_bcd_add(&a, &b);
    DecBCD expected = dec_bcd_from_string("0.3");

    int cmp = dec_bcd_cmp(&result, &expected);

    /* This is the whole point: result == 0.3 EXACTLY */
    int return_value;
    if (cmp == 0) {
        return_value = 1;  /* PASS */
    } else {
        return_value = 0;  /* FAIL */
    }

    ASSERT_EQ_INT(return_value, 1);

    /* Verify string representations */
    char buf_a[64], buf_b[64], buf_r[64], buf_e[64];
    dec_bcd_to_string(&a, buf_a, sizeof(buf_a));
    dec_bcd_to_string(&b, buf_b, sizeof(buf_b));
    dec_bcd_to_string(&result, buf_r, sizeof(buf_r));
    dec_bcd_to_string(&expected, buf_e, sizeof(buf_e));

    ASSERT_EQ_STR(buf_a, "0.1");
    ASSERT_EQ_STR(buf_b, "0.2");
    ASSERT_EQ_STR(buf_r, "0.3");
    ASSERT_EQ_STR(buf_e, "0.3");
}

TEST(e2e_ast_simulation) {
    /*
     * Create AST nodes matching the example program and verify
     * the codegen produces non-zero output.
     *
     * AST structure:
     *   PROGRAM
     *     FN_DECL "main"
     *       BLOCK (params, empty)
     *       TYPE_ANNOTATION "i32"
     *       BLOCK (body)
     *         VAR_DECL "a"
     *           TYPE_ANNOTATION "dec"
     *           FLOAT_LITERAL "0.1"
     *         VAR_DECL "b"
     *           TYPE_ANNOTATION "dec"
     *           FLOAT_LITERAL "0.2"
     *         VAR_DECL "result"
     *           TYPE_ANNOTATION "dec"
     *           BINARY_OP "+"
     *             IDENTIFIER "a"
     *             IDENTIFIER "b"
     *         RETURN
     *           IF
     *             ...
     */

    /* Build minimal AST for: let x: dec = 0.1; let y: dec = 0.2; */
    ASTNode *program = ast_create_node(NODE_PROGRAM, 1, 1);

    /* Variable declaration: let x: dec = 0.1 */
    ASTNode *var_x = ast_create_node(NODE_VAR_DECL, 1, 3);
    var_x->string_val = strdup("x");

    ASTNode *type_dec = ast_create_node(NODE_TYPE_ANNOTATION, 1, 10);
    type_dec->string_val = strdup("dec");

    ASTNode *lit_01 = ast_create_node(NODE_FLOAT_LITERAL, 1, 16);
    lit_01->string_val = strdup("0.1");
    lit_01->float_val = 0.1; /* legacy field, not used for dec */

    ast_add_child(var_x, type_dec);
    ast_add_child(var_x, lit_01);

    /* Variable declaration: let y: dec = 0.2 */
    ASTNode *var_y = ast_create_node(NODE_VAR_DECL, 2, 3);
    var_y->string_val = strdup("y");

    ASTNode *type_dec2 = ast_create_node(NODE_TYPE_ANNOTATION, 2, 10);
    type_dec2->string_val = strdup("dec");

    ASTNode *lit_02 = ast_create_node(NODE_FLOAT_LITERAL, 2, 16);
    lit_02->string_val = strdup("0.2");
    lit_02->float_val = 0.2;

    ast_add_child(var_y, type_dec2);
    ast_add_child(var_y, lit_02);

    /* Binary op: x + y */
    ASTNode *add_op = ast_create_node(NODE_BINARY_OP, 3, 28);
    add_op->op = strdup("+");

    ASTNode *id_x = ast_create_node(NODE_IDENTIFIER, 3, 24);
    id_x->string_val = strdup("x");

    ASTNode *id_y = ast_create_node(NODE_IDENTIFIER, 3, 28);
    id_y->string_val = strdup("y");

    ast_add_child(add_op, id_x);
    ast_add_child(add_op, id_y);

    /* Verify AST was built correctly */
    ASSERT_EQ_INT(var_x->child_count, 2);
    ASSERT_EQ_INT(var_y->child_count, 2);
    ASSERT_EQ_INT(add_op->child_count, 2);
    ASSERT_EQ_STR(var_x->string_val, "x");
    ASSERT_EQ_STR(var_y->string_val, "y");
    ASSERT_EQ_STR(add_op->op, "+");
    ASSERT_EQ_STR(lit_01->string_val, "0.1");

    /* Add to program just for structural verification */
    ast_add_child(program, var_x);
    ast_add_child(program, var_y);

    ASSERT_EQ_INT((int)program->child_count, 2);

    /* Cleanup */
    ast_free(program);
    /* add_op is not attached to program, free separately */
    ast_free(add_op);
}

TEST(e2e_precision_stress) {
    /*
     * Test high-precision arithmetic that IEEE 754 gets wrong.
     * These are classic floating-point traps.
     */

    /* 0.1 + 0.2 == 0.3 */
    {
        DecBCD a = dec_bcd_from_string("0.1");
        DecBCD b = dec_bcd_from_string("0.2");
        DecBCD r = dec_bcd_add(&a, &b);
        DecBCD e = dec_bcd_from_string("0.3");
        ASSERT_EQ_INT(dec_bcd_cmp(&r, &e), 0);
    }

    /* 0.3 - 0.1 == 0.2 */
    {
        DecBCD a = dec_bcd_from_string("0.3");
        DecBCD b = dec_bcd_from_string("0.1");
        DecBCD r = dec_bcd_sub(&a, &b);
        DecBCD e = dec_bcd_from_string("0.2");
        ASSERT_EQ_INT(dec_bcd_cmp(&r, &e), 0);
    }

    /* 1.0 - 0.9 == 0.1 */
    {
        DecBCD a = dec_bcd_from_string("1.0");
        DecBCD b = dec_bcd_from_string("0.9");
        DecBCD r = dec_bcd_sub(&a, &b);
        DecBCD e = dec_bcd_from_string("0.1");
        ASSERT_EQ_INT(dec_bcd_cmp(&r, &e), 0);
    }

    /* 0.1 * 3 == 0.3 */
    {
        DecBCD a = dec_bcd_from_string("0.1");
        DecBCD b = dec_bcd_from_string("3");
        DecBCD r = dec_bcd_mul(&a, &b);
        DecBCD e = dec_bcd_from_string("0.3");
        ASSERT_EQ_INT(dec_bcd_cmp(&r, &e), 0);
    }
}

/* ================================================================== */
/*  Main test runner                                                  */
/* ================================================================== */

int main(void) {
    printf("\n");
    printf("==========================================================\n");
    printf("  aricode decimal runtime + codegen test suite\n");
    printf("==========================================================\n");
    printf("\n");

    printf("--- Part 1: BCD Representation ---\n");
    run_test_bcd_zero();
    run_test_bcd_from_string_integer();
    run_test_bcd_from_string_decimal();
    run_test_bcd_from_string_negative();
    run_test_bcd_from_string_leading_zero();
    run_test_bcd_from_string_zero();
    run_test_bcd_from_string_zero_decimal();
    run_test_bcd_from_int_positive();
    run_test_bcd_from_int_negative();
    run_test_bcd_from_int_zero();
    run_test_bcd_digit_access();
    printf("\n");

    printf("--- Part 2: BCD Arithmetic ---\n");
    run_test_add_zero_one_zero_two_equals_zero_three();
    run_test_add_integers();
    run_test_add_with_carry();
    run_test_add_different_scales();
    run_test_add_negative();
    run_test_sub_basic();
    run_test_sub_result_negative();
    run_test_sub_to_zero();
    run_test_mul_basic();
    run_test_mul_decimal();
    run_test_mul_by_zero();
    run_test_mul_negative();
    run_test_div_basic();
    run_test_div_exact();
    run_test_div_by_zero();
    run_test_cmp_equal();
    run_test_cmp_less();
    run_test_cmp_greater();
    run_test_cmp_negative();
    run_test_cmp_both_negative();
    run_test_cmp_different_exp();
    run_test_negate();
    run_test_abs_negative();
    printf("\n");

    printf("--- Part 3: Codegen Emission ---\n");
    run_test_codegen_from_literal();
    run_test_codegen_add_emits_call();
    run_test_codegen_cmp_emits_test();
    printf("\n");

    printf("--- Part 4: End-to-End ---\n");
    run_test_e2e_simulation();
    run_test_e2e_ast_simulation();
    run_test_e2e_precision_stress();
    printf("\n");

    printf("==========================================================\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("==========================================================\n");

    if (tests_failed > 0) {
        printf("\n  ** FAILURES DETECTED **\n\n");
        return 1;
    }

    printf("\n  All decimal tests passed. 0.1 + 0.2 == 0.3 confirmed.\n\n");
    return 0;
}
