/*
 * AriDecimal Operations - Pure arbitrary precision arithmetic
 *
 * Every operation is pure digit manipulation. No floats. No doubles.
 * Long addition, long subtraction, long multiplication, long division -
 * exactly like you learned in school, but with unlimited digits.
 */

#ifndef ARI_DECIMAL_OPS_H
#define ARI_DECIMAL_OPS_H

#include "decimal.h"

/* --- Arithmetic --- */

/* a + b, full precision */
AriDecimal ari_dec_add(const AriDecimal *a, const AriDecimal *b);

/* a - b, full precision */
AriDecimal ari_dec_sub(const AriDecimal *a, const AriDecimal *b);

/* a * b, digit-by-digit long multiplication */
AriDecimal ari_dec_mul(const AriDecimal *a, const AriDecimal *b);

/* a / b with given number of decimal places. Returns zero-valued result on div-by-zero and prints error. */
AriDecimal ari_dec_div(const AriDecimal *a, const AriDecimal *b, int precision);

/* --- Comparison --- */

/* Returns -1 if a < b, 0 if a == b, 1 if a > b */
int ari_dec_cmp(const AriDecimal *a, const AriDecimal *b);

/* --- Utilities --- */

/* Round to N decimal places (half-up rounding) */
AriDecimal ari_dec_round(const AriDecimal *dec, int places);

/* Truncate to N decimal places (floor toward zero) */
AriDecimal ari_dec_truncate(const AriDecimal *dec, int places);

/* Absolute value (always positive) */
AriDecimal ari_dec_abs(const AriDecimal *dec);

/* Flip sign */
AriDecimal ari_dec_negate(const AriDecimal *dec);

#endif /* ARI_DECIMAL_OPS_H */
