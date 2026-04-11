/*
 * AriDecimal arithmetic operations
 *
 * Pure digit-by-digit arithmetic. Every operation is the algorithm you
 * learned in school: carry, borrow, long multiplication, long division.
 * Not a single float or double anywhere.
 */

#include "decimal_ops.h"
#include <stdio.h>

/* ================================================================
 * INTERNAL HELPERS
 * ================================================================ */

/*
 * Compare absolute values (ignoring sign).
 * Returns -1, 0, or 1.
 */
static int abs_cmp(const AriDecimal *a, const AriDecimal *b) {
    int a_int = a->decimal_point;
    int b_int = b->decimal_point;

    /* strip leading zeros for comparison */
    int a_lz = 0;
    while (a_lz < a_int - 1 && a_lz < a->num_digits && a->digits[a_lz] == 0) a_lz++;
    int b_lz = 0;
    while (b_lz < b_int - 1 && b_lz < b->num_digits && b->digits[b_lz] == 0) b_lz++;

    int a_eff_int = a_int - a_lz;
    int b_eff_int = b_int - b_lz;

    if (a_eff_int != b_eff_int) return a_eff_int > b_eff_int ? 1 : -1;

    /* same number of integer digits - compare digit by digit */
    int a_frac = a->num_digits - a->decimal_point;
    int b_frac = b->num_digits - b->decimal_point;
    int max_frac = a_frac > b_frac ? a_frac : b_frac;
    int total = a_eff_int + max_frac;

    for (int i = 0; i < total; i++) {
        int ai = a_lz + i;
        int bi = b_lz + i;
        uint8_t ad = (ai >= 0 && ai < a->num_digits) ? a->digits[ai] : 0;
        uint8_t bd = (bi >= 0 && bi < b->num_digits) ? b->digits[bi] : 0;
        if (ad != bd) return ad > bd ? 1 : -1;
    }
    return 0;
}

/*
 * Add absolute values of a and b, producing an unsigned result.
 * Both treated as positive. Caller sets the sign.
 */
static AriDecimal abs_add(const AriDecimal *a, const AriDecimal *b) {
    /* Determine alignment: how many fractional digits each has */
    int a_frac = a->num_digits - a->decimal_point;
    int b_frac = b->num_digits - b->decimal_point;
    int max_frac = a_frac > b_frac ? a_frac : b_frac;

    int a_int = a->decimal_point;
    int b_int = b->decimal_point;
    int max_int = a_int > b_int ? a_int : b_int;

    int total = max_int + max_frac;
    /* +1 for possible carry */
    int alloc = total + 1;

    uint8_t *result = (uint8_t *)calloc((size_t)alloc, 1);

    /* Add digit by digit from right to left, aligned on decimal point */
    int carry = 0;
    for (int i = max_frac - 1; i >= -max_int; i--) {
        /* i is offset from decimal point: positive = fractional, negative = integer */
        /* Map to array indices */
        int a_idx = a->decimal_point + i;  /* position in a */
        int b_idx = b->decimal_point + i;  /* position in b */
        int r_idx = max_int + i + 1;       /* position in result (+1 for carry slot) */

        uint8_t ad = (a_idx >= 0 && a_idx < a->num_digits) ? a->digits[a_idx] : 0;
        uint8_t bd = (b_idx >= 0 && b_idx < b->num_digits) ? b->digits[b_idx] : 0;

        int sum = ad + bd + carry;
        carry = sum / 10;
        result[r_idx] = (uint8_t)(sum % 10);
    }
    if (carry) {
        result[0] = (uint8_t)carry;
    }

    AriDecimal d;
    d.digits = result;
    d.num_digits = alloc;
    d.decimal_point = max_int + 1; /* +1 for the carry slot */
    d.sign = 0;
    d.precision = a->precision > b->precision ? a->precision : b->precision;

    ari_dec_normalize(&d);
    return d;
}

/*
 * Subtract absolute values: |a| - |b|, assuming |a| >= |b|.
 * Caller must ensure this and set sign.
 */
static AriDecimal abs_sub(const AriDecimal *a, const AriDecimal *b) {
    int a_frac = a->num_digits - a->decimal_point;
    int b_frac = b->num_digits - b->decimal_point;
    int max_frac = a_frac > b_frac ? a_frac : b_frac;

    int a_int = a->decimal_point;
    int b_int = b->decimal_point;
    int max_int = a_int > b_int ? a_int : b_int;

    int total = max_int + max_frac;
    uint8_t *result = (uint8_t *)calloc((size_t)total, 1);

    int borrow = 0;
    for (int i = max_frac - 1; i >= -max_int; i--) {
        int a_idx = a->decimal_point + i;
        int b_idx = b->decimal_point + i;
        int r_idx = max_int + i;

        int ad = (a_idx >= 0 && a_idx < a->num_digits) ? (int)a->digits[a_idx] : 0;
        int bd = (b_idx >= 0 && b_idx < b->num_digits) ? (int)b->digits[b_idx] : 0;

        int diff = ad - bd - borrow;
        if (diff < 0) {
            diff += 10;
            borrow = 1;
        } else {
            borrow = 0;
        }
        result[r_idx] = (uint8_t)diff;
    }

    AriDecimal d;
    d.digits = result;
    d.num_digits = total;
    d.decimal_point = max_int;
    d.sign = 0;
    d.precision = a->precision > b->precision ? a->precision : b->precision;

    ari_dec_normalize(&d);
    return d;
}

/* ================================================================
 * PUBLIC API
 * ================================================================ */

/*
 * ADDITION: a + b
 *
 * Cases:
 *   (+a) + (+b) = abs_add(a,b), positive
 *   (-a) + (-b) = abs_add(a,b), negative
 *   (+a) + (-b) = abs_sub or negate, depends on magnitude
 *   (-a) + (+b) = same as above, flipped
 */
AriDecimal ari_dec_add(const AriDecimal *a, const AriDecimal *b) {
    if (a->sign == b->sign) {
        /* Same sign: add magnitudes, keep sign */
        AriDecimal r = abs_add(a, b);
        r.sign = a->sign;
        if (ari_dec_is_zero(&r)) r.sign = 0;
        return r;
    }

    /* Different signs: subtract smaller magnitude from larger */
    int cmp = abs_cmp(a, b);
    if (cmp == 0) {
        return ari_dec_from_int(0);
    } else if (cmp > 0) {
        AriDecimal r = abs_sub(a, b);
        r.sign = a->sign;
        if (ari_dec_is_zero(&r)) r.sign = 0;
        return r;
    } else {
        AriDecimal r = abs_sub(b, a);
        r.sign = b->sign;
        if (ari_dec_is_zero(&r)) r.sign = 0;
        return r;
    }
}

/*
 * SUBTRACTION: a - b = a + (-b)
 */
AriDecimal ari_dec_sub(const AriDecimal *a, const AriDecimal *b) {
    AriDecimal neg_b = ari_dec_copy(b);
    neg_b.sign = neg_b.sign ? 0 : 1;
    if (ari_dec_is_zero(&neg_b)) neg_b.sign = 0;
    AriDecimal r = ari_dec_add(a, &neg_b);
    ari_dec_free(&neg_b);
    return r;
}

/*
 * MULTIPLICATION: a * b
 *
 * Classic long multiplication, digit by digit.
 * Result decimal places = a's decimal places + b's decimal places.
 */
AriDecimal ari_dec_mul(const AriDecimal *a, const AriDecimal *b) {
    int a_frac = a->num_digits - a->decimal_point;
    int b_frac = b->num_digits - b->decimal_point;
    int result_frac = a_frac + b_frac;

    int result_len = a->num_digits + b->num_digits;
    uint8_t *result = (uint8_t *)calloc((size_t)result_len, 1);

    /* Long multiplication: multiply each digit of b by each digit of a */
    for (int i = a->num_digits - 1; i >= 0; i--) {
        int carry = 0;
        for (int j = b->num_digits - 1; j >= 0; j--) {
            int r_idx = i + j + 1;
            int prod = (int)a->digits[i] * (int)b->digits[j] + result[r_idx] + carry;
            carry = prod / 10;
            result[r_idx] = (uint8_t)(prod % 10);
        }
        result[i] += (uint8_t)carry;
    }

    AriDecimal d;
    d.digits = result;
    d.num_digits = result_len;
    d.decimal_point = result_len - result_frac;
    d.sign = (a->sign != b->sign) ? 1 : 0;
    d.precision = a->precision > b->precision ? a->precision : b->precision;

    ari_dec_normalize(&d);
    if (ari_dec_is_zero(&d)) d.sign = 0;
    return d;
}

/*
 * Helper: compare two digit arrays of equal length.
 * rem[0..len-1] vs div[0..len-1]. Returns -1, 0, 1.
 */
static int digit_array_cmp(const uint8_t *aa, int alen, const uint8_t *bb, int blen) {
    if (alen != blen) return alen > blen ? 1 : -1;
    for (int i = 0; i < alen; i++) {
        if (aa[i] != bb[i]) return aa[i] > bb[i] ? 1 : -1;
    }
    return 0;
}

/*
 * Helper: subtract divisor from remainder in-place.
 * rem[0..rem_len-1] -= div[0..div_len-1], right-aligned.
 * Assumes rem >= div.
 */
static void digit_array_sub(uint8_t *rem, int rem_len, const uint8_t *div, int div_len) {
    int borrow = 0;
    for (int i = 0; i < div_len; i++) {
        int ri = rem_len - 1 - i;
        int di = div_len - 1 - i;
        int diff = (int)rem[ri] - (int)div[di] - borrow;
        if (diff < 0) { diff += 10; borrow = 1; }
        else borrow = 0;
        rem[ri] = (uint8_t)diff;
    }
    /* propagate borrow into higher digits of remainder */
    for (int i = div_len; i < rem_len && borrow; i++) {
        int ri = rem_len - 1 - i;
        int diff = (int)rem[ri] - borrow;
        if (diff < 0) { diff += 10; borrow = 1; }
        else borrow = 0;
        rem[ri] = (uint8_t)diff;
    }
}

/*
 * DIVISION: a / b with specified precision (decimal places)
 *
 * Strategy: treat both numbers as integers by removing the decimal point,
 * track the decimal shift, then do classic schoolbook long division
 * bringing down zeros until we have enough quotient digits.
 */
AriDecimal ari_dec_div(const AriDecimal *a, const AriDecimal *b, int precision) {
    /* Division by zero check */
    if (ari_dec_is_zero(b)) {
        fprintf(stderr, "[aricode ERROR] Division by zero\n");
        return ari_dec_from_int(0);
    }

    /* 0 / anything = 0 */
    if (ari_dec_is_zero(a)) {
        return ari_dec_from_int(0);
    }

    int result_sign = (a->sign != b->sign) ? 1 : 0;

    /*
     * Convert a and b to pure integer digit arrays (strip decimal point).
     * a = A_int * 10^{-a_frac}, b = B_int * 10^{-b_frac}
     * a/b = (A_int / B_int) * 10^{b_frac - a_frac}
     *
     * We'll do integer long division of A_int by B_int, generating enough
     * digits by appending zeros to the dividend.
     */
    int a_frac = a->num_digits - a->decimal_point;
    int b_frac = b->num_digits - b->decimal_point;

    /* Build divisor: strip leading zeros */
    int div_start = 0;
    while (div_start < b->num_digits - 1 && b->digits[div_start] == 0) div_start++;
    int div_len = b->num_digits - div_start;
    uint8_t *divisor = (uint8_t *)malloc((size_t)div_len);
    memcpy(divisor, b->digits + div_start, (size_t)div_len);

    /* Build dividend: strip leading zeros from a's digits */
    int dvd_start = 0;
    while (dvd_start < a->num_digits - 1 && a->digits[dvd_start] == 0) dvd_start++;
    int a_sigdig = a->num_digits - dvd_start;

    /*
     * We need enough quotient digits. The integer division A/B gives
     * some digits, then we need 'precision' more fractional digits.
     * But the decimal shift is (b_frac - a_frac), so the integer part
     * of the result uses (a_int_digits - b_int_digits + 1) digits roughly.
     * To be safe, we generate a_sigdig + precision + div_len + 2 dividend digits.
     */
    int extra_zeros = precision + div_len + 2;
    int dvd_len = a_sigdig + extra_zeros;
    uint8_t *dividend = (uint8_t *)calloc((size_t)dvd_len, 1);
    memcpy(dividend, a->digits + dvd_start, (size_t)a_sigdig);
    /* rest already zero from calloc */

    /* Long division */
    int q_cap = dvd_len + 1;
    uint8_t *quotient = (uint8_t *)calloc((size_t)q_cap, 1);
    int q_len = 0;

    /* Remainder buffer */
    int rem_cap = dvd_len + div_len + 2;
    uint8_t *rem = (uint8_t *)calloc((size_t)rem_cap, 1);
    int rem_len = 0;

    for (int i = 0; i < dvd_len; i++) {
        /* Bring down next digit into remainder */
        rem[rem_len] = dividend[i];
        rem_len++;

        /* Strip leading zeros from remainder (but keep at least 1 digit) */
        while (rem_len > 1 && rem[0] == 0) {
            memmove(rem, rem + 1, (size_t)(rem_len - 1));
            rem_len--;
        }

        /* Determine quotient digit: how many times divisor fits in remainder */
        int q_digit = 0;

        /* remainder < divisor means q_digit = 0 */
        while (digit_array_cmp(rem, rem_len, divisor, div_len) >= 0) {
            digit_array_sub(rem, rem_len, divisor, div_len);
            q_digit++;

            /* Strip leading zeros */
            while (rem_len > 1 && rem[0] == 0) {
                memmove(rem, rem + 1, (size_t)(rem_len - 1));
                rem_len--;
            }
        }

        quotient[q_len++] = (uint8_t)q_digit;

        /* If remainder is exactly zero and we've processed all real dividend digits,
           and we've generated enough quotient digits, we can stop early */
        if (rem_len == 1 && rem[0] == 0 && i >= a_sigdig - 1) {
            /* Pad remaining quotient with zeros for the decimal position calc */
            /* Actually, let's just continue - it'll produce zeros anyway */
        }
    }

    /*
     * Place the decimal point.
     * The dividend was A_int (a's digits as integer) followed by extra_zeros zeros.
     * So our quotient = floor(A_int * 10^extra_zeros / B_int).
     * The actual value is quotient * 10^{-extra_zeros} * 10^{b_frac - a_frac}
     *                   = quotient * 10^{b_frac - a_frac - extra_zeros}
     *
     * So decimal_point (integer digits) = q_len + (b_frac - a_frac - extra_zeros)
     *                                   = q_len - extra_zeros + b_frac - a_frac
     */
    int dec_pt = q_len - extra_zeros + (b_frac - a_frac);

    /* If dec_pt <= 0, we need to prepend zeros */
    if (dec_pt <= 0) {
        int shift = 1 - dec_pt;
        uint8_t *new_q = (uint8_t *)calloc((size_t)(q_len + shift), 1);
        memcpy(new_q + shift, quotient, (size_t)q_len);
        free(quotient);
        quotient = new_q;
        q_len += shift;
        dec_pt = 1;
    }

    AriDecimal d;
    d.digits = quotient;
    d.num_digits = q_len;
    d.decimal_point = dec_pt;
    d.sign = result_sign;
    d.precision = precision;

    free(dividend);
    free(divisor);
    free(rem);

    ari_dec_normalize(&d);

    /* Trim to requested precision */
    int frac_digits = d.num_digits - d.decimal_point;
    if (frac_digits > precision) {
        d.num_digits = d.decimal_point + precision;
        /* Remove trailing zeros */
        while (d.num_digits > d.decimal_point && d.digits[d.num_digits - 1] == 0) {
            d.num_digits--;
        }
    }

    if (ari_dec_is_zero(&d)) d.sign = 0;
    return d;
}

/*
 * COMPARISON: a vs b
 * Returns -1 (a<b), 0 (a==b), 1 (a>b)
 */
int ari_dec_cmp(const AriDecimal *a, const AriDecimal *b) {
    int a_zero = ari_dec_is_zero(a);
    int b_zero = ari_dec_is_zero(b);

    if (a_zero && b_zero) return 0;
    if (a_zero) return b->sign ? 1 : -1;
    if (b_zero) return a->sign ? -1 : 1;

    /* Different signs */
    if (a->sign && !b->sign) return -1;
    if (!a->sign && b->sign) return 1;

    /* Same sign: compare magnitudes */
    int cmp = abs_cmp(a, b);
    if (a->sign) cmp = -cmp; /* both negative: larger magnitude = smaller value */
    return cmp;
}

/*
 * ROUND to N decimal places (half-up)
 */
AriDecimal ari_dec_round(const AriDecimal *dec, int places) {
    int frac = dec->num_digits - dec->decimal_point;
    if (frac <= places) {
        return ari_dec_copy(dec);
    }

    AriDecimal r = ari_dec_copy(dec);
    int cut_pos = r.decimal_point + places;

    /* Check if the digit after the cut is >= 5 */
    int round_up = 0;
    if (cut_pos < r.num_digits) {
        round_up = (r.digits[cut_pos] >= 5) ? 1 : 0;
    }

    r.num_digits = cut_pos;

    if (round_up) {
        /* Add 1 to the last remaining digit, propagate carry */
        int carry = 1;
        for (int i = r.num_digits - 1; i >= 0 && carry; i--) {
            int sum = r.digits[i] + carry;
            r.digits[i] = (uint8_t)(sum % 10);
            carry = sum / 10;
        }
        if (carry) {
            /* Need to expand: shift everything right */
            uint8_t *new_d = (uint8_t *)calloc((size_t)(r.num_digits + 1), 1);
            new_d[0] = 1;
            memcpy(new_d + 1, r.digits, (size_t)r.num_digits);
            free(r.digits);
            r.digits = new_d;
            r.num_digits++;
            r.decimal_point++;
        }
    }

    ari_dec_normalize(&r);
    return r;
}

/*
 * TRUNCATE to N decimal places (toward zero)
 */
AriDecimal ari_dec_truncate(const AriDecimal *dec, int places) {
    int frac = dec->num_digits - dec->decimal_point;
    if (frac <= places) {
        return ari_dec_copy(dec);
    }

    AriDecimal r = ari_dec_copy(dec);
    r.num_digits = r.decimal_point + places;
    ari_dec_normalize(&r);
    return r;
}

/*
 * ABSOLUTE VALUE
 */
AriDecimal ari_dec_abs(const AriDecimal *dec) {
    AriDecimal r = ari_dec_copy(dec);
    r.sign = 0;
    return r;
}

/*
 * NEGATE (flip sign)
 */
AriDecimal ari_dec_negate(const AriDecimal *dec) {
    AriDecimal r = ari_dec_copy(dec);
    if (!ari_dec_is_zero(&r)) {
        r.sign = r.sign ? 0 : 1;
    }
    return r;
}
