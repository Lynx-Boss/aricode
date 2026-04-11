/*
 * Comprehensive test suite for AriDecimal
 *
 * Tests arbitrary precision decimal arithmetic with known mathematical
 * constants and edge cases that BREAK IEEE 754 floating point.
 */

#include "decimal.h"
#include "decimal_ops.h"
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(const char *name, const AriDecimal *result, const char *expected) {
    char buf[256];
    ari_dec_to_string(result, buf, sizeof(buf));
    if (strcmp(buf, expected) == 0) {
        printf("  PASS: %-60s = %s\n", name, buf);
        tests_passed++;
    } else {
        printf("  FAIL: %-60s = %s (expected %s)\n", name, buf, expected);
        tests_failed++;
    }
}

static void check_cmp(const char *name, int got, int expected) {
    if (got == expected) {
        printf("  PASS: %-60s = %d\n", name, got);
        tests_passed++;
    } else {
        printf("  FAIL: %-60s = %d (expected %d)\n", name, got, expected);
        tests_failed++;
    }
}

/* ================================================================ */

static void test_parse_and_stringify(void) {
    printf("\n=== PARSE & STRINGIFY ===\n");

    AriDecimal d1 = ari_dec_from_string("3.14159265358979323846");
    check("parse PI 20 digits", &d1, "3.14159265358979323846");
    ari_dec_free(&d1);

    AriDecimal d2 = ari_dec_from_string("2.71828182845904523536");
    check("parse Euler 20 digits", &d2, "2.71828182845904523536");
    ari_dec_free(&d2);

    AriDecimal d3 = ari_dec_from_string("-42.5");
    check("parse negative", &d3, "-42.5");
    ari_dec_free(&d3);

    AriDecimal d4 = ari_dec_from_string("0");
    check("parse zero", &d4, "0");
    ari_dec_free(&d4);

    AriDecimal d5 = ari_dec_from_string("0.00000000000000000001");
    check("parse tiny", &d5, "0.00000000000000000001");
    ari_dec_free(&d5);

    AriDecimal d6 = ari_dec_from_string("99999999999999999999");
    check("parse large int", &d6, "99999999999999999999");
    ari_dec_free(&d6);

    AriDecimal d7 = ari_dec_from_int(12345);
    check("from_int 12345", &d7, "12345");
    ari_dec_free(&d7);

    AriDecimal d8 = ari_dec_from_int(-99);
    check("from_int -99", &d8, "-99");
    ari_dec_free(&d8);

    AriDecimal d9 = ari_dec_from_int(0);
    check("from_int 0", &d9, "0");
    ari_dec_free(&d9);

    AriDecimal d10 = ari_dec_from_string("100.00");
    check("parse trailing zeros", &d10, "100");
    ari_dec_free(&d10);

    AriDecimal d11 = ari_dec_from_string("007.500");
    check("parse leading/trailing zeros", &d11, "7.5");
    ari_dec_free(&d11);
}

static void test_identity_addition(void) {
    printf("\n=== IDENTITY (add zero) ===\n");

    AriDecimal pi = ari_dec_from_string("3.14159265358979323846");
    AriDecimal zero = ari_dec_from_int(0);
    AriDecimal r = ari_dec_add(&pi, &zero);
    check("PI + 0", &r, "3.14159265358979323846");
    ari_dec_free(&r);

    AriDecimal euler = ari_dec_from_string("2.71828182845904523536");
    AriDecimal r2 = ari_dec_add(&euler, &zero);
    check("Euler + 0", &r2, "2.71828182845904523536");
    ari_dec_free(&r2);

    ari_dec_free(&pi);
    ari_dec_free(&euler);
    ari_dec_free(&zero);
}

static void test_addition(void) {
    printf("\n=== ADDITION ===\n");

    /* 1.11111111111111111111 + 2.22222222222222222222 = 3.33333333333333333333 */
    AriDecimal a = ari_dec_from_string("1.11111111111111111111");
    AriDecimal b = ari_dec_from_string("2.22222222222222222222");
    AriDecimal r = ari_dec_add(&a, &b);
    check("1.111...1 + 2.222...2", &r, "3.33333333333333333333");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* THE KILLER TEST: 0.1 + 0.2 = 0.3 */
    a = ari_dec_from_string("0.1");
    b = ari_dec_from_string("0.2");
    r = ari_dec_add(&a, &b);
    check("0.1 + 0.2 = 0.3 (IEEE 754 KILLER)", &r, "0.3");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* Tiny numbers */
    a = ari_dec_from_string("0.00000000000000000001");
    b = ari_dec_from_string("0.00000000000000000002");
    r = ari_dec_add(&a, &b);
    check("tiny + tiny", &r, "0.00000000000000000003");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* Large + small */
    a = ari_dec_from_string("99999999999999999999.5");
    b = ari_dec_from_string("0.5");
    r = ari_dec_add(&a, &b);
    check("99999999999999999999.5 + 0.5", &r, "100000000000000000000");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* Carry propagation */
    a = ari_dec_from_string("999.999");
    b = ari_dec_from_string("0.001");
    r = ari_dec_add(&a, &b);
    check("999.999 + 0.001", &r, "1000");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);
}

static void test_negative_addition(void) {
    printf("\n=== NEGATIVE ADDITION ===\n");

    AriDecimal a = ari_dec_from_string("-1.5");
    AriDecimal b = ari_dec_from_string("2.5");
    AriDecimal r = ari_dec_add(&a, &b);
    check("-1.5 + 2.5", &r, "1");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    a = ari_dec_from_string("1.5");
    b = ari_dec_from_string("-2.5");
    r = ari_dec_add(&a, &b);
    check("1.5 + (-2.5)", &r, "-1");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    a = ari_dec_from_string("-3");
    b = ari_dec_from_string("-7");
    r = ari_dec_add(&a, &b);
    check("-3 + (-7)", &r, "-10");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    a = ari_dec_from_string("5");
    b = ari_dec_from_string("-5");
    r = ari_dec_add(&a, &b);
    check("5 + (-5)", &r, "0");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);
}

static void test_subtraction(void) {
    printf("\n=== SUBTRACTION ===\n");

    AriDecimal a = ari_dec_from_string("3.33333333333333333333");
    AriDecimal b = ari_dec_from_string("1.11111111111111111111");
    AriDecimal r = ari_dec_sub(&a, &b);
    check("3.333...3 - 1.111...1", &r, "2.22222222222222222222");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    a = ari_dec_from_string("-1.5");
    b = ari_dec_from_string("2.5");
    r = ari_dec_sub(&a, &b);
    check("-1.5 - 2.5", &r, "-4");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    a = ari_dec_from_string("10");
    b = ari_dec_from_string("3");
    r = ari_dec_sub(&a, &b);
    check("10 - 3", &r, "7");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    a = ari_dec_from_string("0.3");
    b = ari_dec_from_string("0.1");
    r = ari_dec_sub(&a, &b);
    check("0.3 - 0.1", &r, "0.2");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    a = ari_dec_from_string("1");
    b = ari_dec_from_string("1");
    r = ari_dec_sub(&a, &b);
    check("1 - 1", &r, "0");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);
}

static void test_multiplication(void) {
    printf("\n=== MULTIPLICATION ===\n");

    AriDecimal a = ari_dec_from_string("1.00000000000000000001");
    AriDecimal b = ari_dec_from_int(2);
    AriDecimal r = ari_dec_mul(&a, &b);
    check("1.00000000000000000001 * 2", &r, "2.00000000000000000002");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    a = ari_dec_from_string("12345.6789");
    b = ari_dec_from_string("0.0001");
    r = ari_dec_mul(&a, &b);
    check("12345.6789 * 0.0001", &r, "1.23456789");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* Very small * very small */
    a = ari_dec_from_string("0.00000000000000000001");
    b = ari_dec_from_string("0.00000000000000000001");
    r = ari_dec_mul(&a, &b);
    check("1e-20 * 1e-20", &r, "0.0000000000000000000000000000000000000001");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* Negative */
    a = ari_dec_from_string("-5");
    b = ari_dec_from_string("3");
    r = ari_dec_mul(&a, &b);
    check("-5 * 3", &r, "-15");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    a = ari_dec_from_string("-4");
    b = ari_dec_from_string("-6");
    r = ari_dec_mul(&a, &b);
    check("-4 * -6", &r, "24");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* Multiply by zero */
    a = ari_dec_from_string("123456789.987654321");
    b = ari_dec_from_int(0);
    r = ari_dec_mul(&a, &b);
    check("big * 0", &r, "0");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* 99 * 99 */
    a = ari_dec_from_string("99");
    b = ari_dec_from_string("99");
    r = ari_dec_mul(&a, &b);
    check("99 * 99", &r, "9801");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);
}

static void test_division(void) {
    printf("\n=== DIVISION ===\n");

    /* 1 / 3 = 0.33333... (20 places) */
    AriDecimal a = ari_dec_from_int(1);
    AriDecimal b = ari_dec_from_int(3);
    AriDecimal r = ari_dec_div(&a, &b, 20);
    check("1 / 3 (20 places)", &r, "0.33333333333333333333");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* 22 / 7 = 3.14285714285714285714... (20 places) */
    a = ari_dec_from_int(22);
    b = ari_dec_from_int(7);
    r = ari_dec_div(&a, &b, 20);
    check("22 / 7 (20 places)", &r, "3.14285714285714285714");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* 10 / 3 = 3.33333... */
    a = ari_dec_from_int(10);
    b = ari_dec_from_int(3);
    r = ari_dec_div(&a, &b, 20);
    check("10 / 3 (20 places)", &r, "3.33333333333333333333");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* 1 / 8 = 0.125 exact */
    a = ari_dec_from_int(1);
    b = ari_dec_from_int(8);
    r = ari_dec_div(&a, &b, 20);
    check("1 / 8", &r, "0.125");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* 100 / 4 = 25 */
    a = ari_dec_from_int(100);
    b = ari_dec_from_int(4);
    r = ari_dec_div(&a, &b, 10);
    check("100 / 4", &r, "25");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* Negative division */
    a = ari_dec_from_int(-10);
    b = ari_dec_from_int(3);
    r = ari_dec_div(&a, &b, 20);
    check("-10 / 3", &r, "-3.33333333333333333333");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* Division by zero */
    printf("  (Expecting error message below)\n");
    a = ari_dec_from_int(5);
    b = ari_dec_from_int(0);
    r = ari_dec_div(&a, &b, 10);
    check("5 / 0 (should be 0 + error)", &r, "0");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);

    /* 1 / 7 (20 places) = 0.14285714285714285714 */
    a = ari_dec_from_int(1);
    b = ari_dec_from_int(7);
    r = ari_dec_div(&a, &b, 20);
    check("1 / 7 (20 places)", &r, "0.14285714285714285714");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);
}

static void test_comparison(void) {
    printf("\n=== COMPARISON ===\n");

    AriDecimal a = ari_dec_from_string("3.14");
    AriDecimal b = ari_dec_from_string("2.71");
    check_cmp("3.14 vs 2.71", ari_dec_cmp(&a, &b), 1);
    check_cmp("2.71 vs 3.14", ari_dec_cmp(&b, &a), -1);
    ari_dec_free(&a); ari_dec_free(&b);

    a = ari_dec_from_string("1.000");
    b = ari_dec_from_string("1");
    check_cmp("1.000 vs 1", ari_dec_cmp(&a, &b), 0);
    ari_dec_free(&a); ari_dec_free(&b);

    a = ari_dec_from_string("-5");
    b = ari_dec_from_string("3");
    check_cmp("-5 vs 3", ari_dec_cmp(&a, &b), -1);
    ari_dec_free(&a); ari_dec_free(&b);

    a = ari_dec_from_string("-2");
    b = ari_dec_from_string("-10");
    check_cmp("-2 vs -10", ari_dec_cmp(&a, &b), 1);
    ari_dec_free(&a); ari_dec_free(&b);

    a = ari_dec_from_int(0);
    b = ari_dec_from_int(0);
    check_cmp("0 vs 0", ari_dec_cmp(&a, &b), 0);
    ari_dec_free(&a); ari_dec_free(&b);
}

static void test_utilities(void) {
    printf("\n=== UTILITIES ===\n");

    AriDecimal a = ari_dec_from_string("3.14159265358979323846");
    AriDecimal r = ari_dec_round(&a, 2);
    check("round PI to 2", &r, "3.14");
    ari_dec_free(&r);

    r = ari_dec_round(&a, 5);
    check("round PI to 5", &r, "3.14159");
    ari_dec_free(&r);

    r = ari_dec_truncate(&a, 3);
    check("truncate PI to 3", &r, "3.141");
    ari_dec_free(&r);
    ari_dec_free(&a);

    a = ari_dec_from_string("-42.5");
    AriDecimal ab = ari_dec_abs(&a);
    check("abs(-42.5)", &ab, "42.5");
    ari_dec_free(&ab);

    AriDecimal neg = ari_dec_negate(&a);
    check("negate(-42.5)", &neg, "42.5");
    ari_dec_free(&neg);
    ari_dec_free(&a);

    a = ari_dec_from_int(0);
    check_cmp("is_zero(0)", ari_dec_is_zero(&a), 1);
    ari_dec_free(&a);

    a = ari_dec_from_string("0.001");
    check_cmp("is_zero(0.001)", ari_dec_is_zero(&a), 0);
    ari_dec_free(&a);

    /* Round with carry propagation: 9.99 rounded to 1 place = 10 */
    a = ari_dec_from_string("9.95");
    r = ari_dec_round(&a, 1);
    check("round 9.95 to 1", &r, "10");
    ari_dec_free(&a); ari_dec_free(&r);
}

static void test_copy(void) {
    printf("\n=== COPY ===\n");
    AriDecimal a = ari_dec_from_string("123.456");
    AriDecimal b = ari_dec_copy(&a);
    check("copy 123.456", &b, "123.456");

    /* Modify original, copy should be unaffected */
    a.digits[0] = 9;
    check("copy independent", &b, "123.456");
    ari_dec_free(&a);
    ari_dec_free(&b);
}

static void test_high_precision(void) {
    printf("\n=== HIGH PRECISION (50 digits) ===\n");

    /* PI to 50 digits */
    AriDecimal pi = ari_dec_from_string("3.14159265358979323846264338327950288419716939937510");
    check("PI 50 digits parse", &pi, "3.1415926535897932384626433832795028841971693993751");
    ari_dec_free(&pi);

    /* 1/3 to 50 places */
    AriDecimal one = ari_dec_from_int(1);
    AriDecimal three = ari_dec_from_int(3);
    AriDecimal r = ari_dec_div(&one, &three, 50);
    check("1/3 to 50 places", &r, "0.33333333333333333333333333333333333333333333333333");
    ari_dec_free(&one); ari_dec_free(&three); ari_dec_free(&r);

    /* Add two 30-digit fractions */
    AriDecimal a = ari_dec_from_string("0.111111111111111111111111111111");
    AriDecimal b = ari_dec_from_string("0.888888888888888888888888888889");
    r = ari_dec_add(&a, &b);
    check("0.111...1 + 0.888...9 = 1", &r, "1");
    ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);
}

/* ================================================================ */

int main(void) {
    printf("============================================\n");
    printf("  AriDecimal Test Suite\n");
    printf("  Pure Arbitrary Precision - No Floats!\n");
    printf("============================================\n");

    test_parse_and_stringify();
    test_identity_addition();
    test_addition();
    test_negative_addition();
    test_subtraction();
    test_multiplication();
    test_division();
    test_comparison();
    test_utilities();
    test_copy();
    test_high_precision();

    printf("\n============================================\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("============================================\n");

    return tests_failed > 0 ? 1 : 0;
}
