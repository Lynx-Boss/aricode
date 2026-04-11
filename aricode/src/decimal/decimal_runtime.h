/*
 * aricode - Ari Code Language
 * Decimal Runtime - BCD-based Arbitrary Precision Arithmetic
 *
 * This module implements decimal arithmetic using Binary Coded Decimal (BCD).
 * Each digit occupies 4 bits (one nibble). A 32-digit number fits in 16 bytes
 * (two 64-bit values: hi and lo).
 *
 * Memory layout of DecBCD:
 *   hi:  digits  0..15  (most significant)
 *   lo:  digits 16..31  (least significant)
 *   exp: position of decimal point (number of fractional digits)
 *   sign: 0 = positive, 1 = negative
 *
 * Example: 3.14 is stored as:
 *   digits = 00000000 00000003 14000000 00000000
 *   exp = 2  (2 fractional digits)
 *   sign = 0
 *
 * This representation is FASTER than string-based digit manipulation because:
 *   - BCD addition/subtraction uses simple nibble-by-nibble ops with carry
 *   - No malloc, no string parsing in the hot path
 *   - Two 64-bit registers hold the full number
 *   - DAA/DAS logic is implemented manually (invalid in 64-bit mode)
 *
 * The runtime functions are designed to be embedded directly into the
 * generated binary. When the compiler encounters a decimal operation,
 * it emits a CALL to these embedded functions.
 */

#ifndef ARI_DECIMAL_RUNTIME_H
#define ARI_DECIMAL_RUNTIME_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  BCD Decimal Representation - 32 digits in 16 bytes                */
/* ------------------------------------------------------------------ */

#define DEC_BCD_DIGITS    32   /* total digits (integer + fractional)  */
#define DEC_BCD_BYTES     16   /* DEC_BCD_DIGITS / 2                   */
#define DEC_BCD_MAX_EXP   31   /* max fractional digits                */
#define DEC_BCD_INT_SLOTS 16   /* digits reserved for integer part     */

/*
 * Packed BCD decimal number.
 * 32 digits stored as nibbles in hi:lo (big-endian digit order).
 *
 * Digit mapping:
 *   digit[0]  = (hi >> 60) & 0xF   (most significant)
 *   digit[1]  = (hi >> 56) & 0xF
 *   ...
 *   digit[15] = (hi >>  0) & 0xF
 *   digit[16] = (lo >> 60) & 0xF
 *   ...
 *   digit[31] = (lo >>  0) & 0xF   (least significant)
 *
 * The decimal point sits `exp` digits from the right.
 * So the value is: sign * (all_digits_as_integer) * 10^(-exp)
 */
typedef struct {
    uint64_t hi;     /* digits 0-15  (nibble-packed, big-endian)  */
    uint64_t lo;     /* digits 16-31 (nibble-packed, big-endian)  */
    int32_t  exp;    /* number of fractional digits               */
    int32_t  sign;   /* 0 = non-negative, 1 = negative            */
} DecBCD;

/* ------------------------------------------------------------------ */
/*  Construction / conversion                                         */
/* ------------------------------------------------------------------ */

/*
 * Create a zero-valued BCD decimal.
 */
DecBCD dec_bcd_zero(void);

/*
 * Parse a decimal string into BCD form.
 * Handles: "0.1", "-3.14", "42", "0.30000000000000004", etc.
 * Returns zero on parse failure.
 */
DecBCD dec_bcd_from_string(const char *str);

/*
 * Create a BCD decimal from a 64-bit signed integer.
 */
DecBCD dec_bcd_from_int(int64_t val);

/*
 * Convert BCD decimal to a human-readable string.
 * Writes into buf (must have space for at least 40 chars).
 * Returns number of chars written (excluding NUL).
 */
int dec_bcd_to_string(const DecBCD *d, char *buf, int buf_size);

/* ------------------------------------------------------------------ */
/*  Digit access helpers                                              */
/* ------------------------------------------------------------------ */

/*
 * Get the value of digit at position pos (0 = most significant).
 */
static inline uint8_t dec_bcd_get_digit(const DecBCD *d, int pos) {
    if (pos < 0 || pos >= DEC_BCD_DIGITS) return 0;
    if (pos < 16) {
        return (uint8_t)((d->hi >> (60 - pos * 4)) & 0xF);
    } else {
        return (uint8_t)((d->lo >> (60 - (pos - 16) * 4)) & 0xF);
    }
}

/*
 * Set the digit at position pos to val (0-9).
 */
static inline void dec_bcd_set_digit(DecBCD *d, int pos, uint8_t val) {
    if (pos < 0 || pos >= DEC_BCD_DIGITS) return;
    val &= 0xF;
    if (pos < 16) {
        int shift = 60 - pos * 4;
        d->hi &= ~((uint64_t)0xF << shift);
        d->hi |= ((uint64_t)val << shift);
    } else {
        int shift = 60 - (pos - 16) * 4;
        d->lo &= ~((uint64_t)0xF << shift);
        d->lo |= ((uint64_t)val << shift);
    }
}

/* ------------------------------------------------------------------ */
/*  Arithmetic operations                                             */
/* ------------------------------------------------------------------ */

/*
 * BCD addition: a + b
 * Handles sign, alignment, and carry propagation.
 */
DecBCD dec_bcd_add(const DecBCD *a, const DecBCD *b);

/*
 * BCD subtraction: a - b
 */
DecBCD dec_bcd_sub(const DecBCD *a, const DecBCD *b);

/*
 * BCD multiplication: a * b
 * Uses schoolbook digit-by-digit multiplication.
 */
DecBCD dec_bcd_mul(const DecBCD *a, const DecBCD *b);

/*
 * BCD division: a / b with up to `precision` fractional digits.
 * Uses long division algorithm.
 * Returns zero on division by zero.
 */
DecBCD dec_bcd_div(const DecBCD *a, const DecBCD *b, int precision);

/* ------------------------------------------------------------------ */
/*  Comparison                                                        */
/* ------------------------------------------------------------------ */

/*
 * Compare two BCD decimals.
 * Returns: -1 if a < b, 0 if a == b, 1 if a > b
 */
int dec_bcd_cmp(const DecBCD *a, const DecBCD *b);

/* ------------------------------------------------------------------ */
/*  Utility                                                           */
/* ------------------------------------------------------------------ */

/*
 * Check if the value is zero.
 */
int dec_bcd_is_zero(const DecBCD *d);

/*
 * Normalize: remove trailing fractional zeros (decrease exp),
 * and remove leading integer zeros.
 */
void dec_bcd_normalize(DecBCD *d);

/*
 * Negate the value (flip sign, unless zero).
 */
DecBCD dec_bcd_negate(const DecBCD *d);

/*
 * Absolute value.
 */
DecBCD dec_bcd_abs(const DecBCD *d);

/*
 * Shift all digits left by `n` positions (multiply by 10^n).
 * Digits shifted out of the MSB are lost.
 */
void dec_bcd_shift_left(DecBCD *d, int n);

/*
 * Shift all digits right by `n` positions (divide by 10^n).
 * Digits shifted out of the LSB are lost.
 */
void dec_bcd_shift_right(DecBCD *d, int n);

#endif /* ARI_DECIMAL_RUNTIME_H */
