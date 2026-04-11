/*
 * Decimal Precision Sparring - C (double / IEEE 754)
 * Shows how C's double fails at precise decimal arithmetic.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

static int check(const char *label, double got, const char *expected) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.20f", got);
    int pass = (strcmp(buf, expected) == 0);
    printf("  %s\n", label);
    printf("    expected: %s\n", expected);
    printf("    got:      %s  [%s]\n", buf, pass ? "PASS" : "FAIL");
    return pass;
}

int main(void) {
    int passes = 0, total = 7;
    char exp_buf[64];

    printf("=== C double (IEEE 754) ===\n\n");

    /* We need expected strings formatted the same way */
    snprintf(exp_buf, sizeof(exp_buf), "%.20f", 0.3);
    /* But we want the TRUE expected, not the float-approximated one */
    /* Use the exact decimal string */

    passes += check("0.1 + 0.2 = 0.3",
                     0.1 + 0.2,
                     "0.30000000000000000000");

    passes += check("1.0 - 0.9 - 0.1 = 0.0",
                     1.0 - 0.9 - 0.1,
                     "0.00000000000000000000");

    passes += check("0.1 * 0.1 = 0.01",
                     0.1 * 0.1,
                     "0.01000000000000000000");

    passes += check("1.0 / 3.0 * 3.0 = 1.0",
                     1.0 / 3.0 * 3.0,
                     "1.00000000000000000000");

    passes += check("0.3 - 0.2 - 0.1 = 0.0",
                     0.3 - 0.2 - 0.1,
                     "0.00000000000000000000");

    passes += check("PI to 20 decimals",
                     M_PI,
                     "3.14159265358979323846");

    passes += check("1.111...1 + 2.222...2 = 3.333...3",
                     1.11111111111111111111 + 2.22222222222222222222,
                     "3.33333333333333333333");

    printf("\nResult: %d/%d PASS\n", passes, total);
    return 0;
}
