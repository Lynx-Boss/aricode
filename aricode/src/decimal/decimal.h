/*
 * AriDecimal - Arbitrary Precision Decimal for aricode
 *
 * Pure integer-based decimal arithmetic. NO IEEE 754 floats anywhere.
 * Handles 50+ decimal places with perfect accuracy.
 *
 * 0.1 + 0.2 == 0.3 exactly. Always.
 */

#ifndef ARI_DECIMAL_H
#define ARI_DECIMAL_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Default precision (max decimal places) */
#define ARI_DEC_DEFAULT_PRECISION 50

/*
 * Arbitrary precision decimal number.
 * Stored as: sign + array of single digits + decimal point position.
 * No floating point anywhere - pure integer arithmetic on digits.
 */
typedef struct {
    uint8_t *digits;      /* Array of digits (0-9), most significant first */
    int num_digits;       /* Total number of digits stored */
    int decimal_point;    /* Number of INTEGER digits (position from left) */
    int sign;             /* 0 = positive/zero, 1 = negative */
    int precision;        /* Maximum decimal places to maintain */
} AriDecimal;

/* --- Core lifecycle --- */

/* Parse a decimal string like "3.14159265358979323846" or "-42.5" */
AriDecimal ari_dec_from_string(const char *str);

/* Create from a 64-bit integer */
AriDecimal ari_dec_from_int(int64_t val);

/* Write formatted string into buf. Returns number of chars written (excl NUL). */
int ari_dec_to_string(const AriDecimal *dec, char *buf, int buf_size);

/* Free all memory owned by dec */
void ari_dec_free(AriDecimal *dec);

/* Deep copy */
AriDecimal ari_dec_copy(const AriDecimal *dec);

/* --- Internal helpers (exposed for ops module) --- */

/* Remove leading zeros from integer part and trailing zeros from fractional part */
void ari_dec_normalize(AriDecimal *dec);

/* Return the digit at a given position (0 if out of range) */
static inline uint8_t ari_dec_get_digit(const AriDecimal *dec, int pos) {
    if (pos < 0 || pos >= dec->num_digits) return 0;
    return dec->digits[pos];
}

/* Check if the value is zero */
int ari_dec_is_zero(const AriDecimal *dec);

#endif /* ARI_DECIMAL_H */
