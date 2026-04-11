/*
 * Decimal Precision Sparring - C (long double)
 * Shows that even long double can't fix IEEE 754 decimal representation issues.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

static int check(const char *label, long double got, const char *expected) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.20Lf", got);
    int pass = (strcmp(buf, expected) == 0);
    printf("  %s\n", label);
    printf("    expected: %s\n", expected);
    printf("    got:      %s  [%s]\n", buf, pass ? "PASS" : "FAIL");
    return pass;
}

int main(void) {
    int passes = 0, total = 7;

    printf("=== C long double ===\n\n");

    passes += check("0.1 + 0.2 = 0.3",
                     0.1L + 0.2L,
                     "0.30000000000000000000");

    passes += check("1.0 - 0.9 - 0.1 = 0.0",
                     1.0L - 0.9L - 0.1L,
                     "0.00000000000000000000");

    passes += check("0.1 * 0.1 = 0.01",
                     0.1L * 0.1L,
                     "0.01000000000000000000");

    passes += check("1.0 / 3.0 * 3.0 = 1.0",
                     1.0L / 3.0L * 3.0L,
                     "1.00000000000000000000");

    passes += check("0.3 - 0.2 - 0.1 = 0.0",
                     0.3L - 0.2L - 0.1L,
                     "0.00000000000000000000");

    passes += check("PI to 20 decimals",
                     (long double)3.14159265358979323846L,
                     "3.14159265358979323846");

    passes += check("1.111...1 + 2.222...2 = 3.333...3",
                     1.11111111111111111111L + 2.22222222222222222222L,
                     "3.33333333333333333333");

    printf("\nResult: %d/%d PASS\n", passes, total);
    return 0;
}
