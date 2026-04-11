/*
 * Decimal Precision Sparring - aricode AriDecimal
 *
 * Pure decimal digit-array arithmetic. NO IEEE 754 floats anywhere.
 * Every number is stored as an array of digits 0-9 with an explicit
 * decimal point position. Addition, subtraction, multiplication, and
 * division are performed digit-by-digit, exactly like pencil-and-paper
 * arithmetic.
 *
 * This is what aricode's decimal library produces.
 * Self-contained implementation matching the aricode/src/decimal/ API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ================================================================
 * AriDecimal - self-contained implementation
 * ================================================================ */

#define ARI_DEC_DEFAULT_PRECISION 50

typedef struct {
    uint8_t *digits;      /* digits 0-9, MSD first */
    int num_digits;
    int decimal_point;    /* number of integer digits */
    int sign;             /* 0=positive, 1=negative */
    int precision;
} AriDecimal;

static AriDecimal ari_dec_create(int num_digits, int decimal_point, int sign) {
    AriDecimal d;
    d.num_digits = num_digits;
    d.decimal_point = decimal_point;
    d.sign = sign;
    d.precision = ARI_DEC_DEFAULT_PRECISION;
    d.digits = (uint8_t *)calloc(num_digits > 0 ? num_digits : 1, 1);
    return d;
}

static void ari_dec_free(AriDecimal *d) {
    free(d->digits);
    d->digits = NULL;
    d->num_digits = 0;
}

static void ari_dec_normalize(AriDecimal *d) {
    /* Remove leading zeros from integer part (keep at least one) */
    while (d->decimal_point > 1 && d->num_digits > 0 && d->digits[0] == 0) {
        memmove(d->digits, d->digits + 1, d->num_digits - 1);
        d->num_digits--;
        d->decimal_point--;
    }
    /* Remove trailing zeros from fractional part */
    int frac_digits = d->num_digits - d->decimal_point;
    while (frac_digits > 0 && d->digits[d->num_digits - 1] == 0) {
        d->num_digits--;
        frac_digits--;
    }
    /* If empty, ensure at least "0" */
    if (d->num_digits == 0) {
        d->num_digits = 1;
        d->decimal_point = 1;
        d->digits[0] = 0;
        d->sign = 0;
    }
    /* If all zeros, sign = 0 */
    int all_zero = 1;
    for (int i = 0; i < d->num_digits; i++) {
        if (d->digits[i] != 0) { all_zero = 0; break; }
    }
    if (all_zero) d->sign = 0;
}

static AriDecimal ari_dec_from_string(const char *str) {
    int len = strlen(str);
    int sign = 0;
    int start = 0;

    if (str[0] == '-') { sign = 1; start = 1; }
    else if (str[0] == '+') { start = 1; }

    /* Find decimal point */
    int dot_pos = -1;
    for (int i = start; i < len; i++) {
        if (str[i] == '.') { dot_pos = i; break; }
    }

    int int_digits, frac_digits;
    if (dot_pos < 0) {
        int_digits = len - start;
        frac_digits = 0;
    } else {
        int_digits = dot_pos - start;
        frac_digits = len - dot_pos - 1;
    }

    int total = int_digits + frac_digits;
    if (total == 0) total = 1;

    AriDecimal d = ari_dec_create(total, int_digits > 0 ? int_digits : 1, sign);

    int idx = 0;
    for (int i = start; i < len; i++) {
        if (str[i] == '.') continue;
        d.digits[idx++] = str[i] - '0';
    }

    ari_dec_normalize(&d);
    return d;
}

static int ari_dec_to_string(const AriDecimal *d, char *buf, int buf_size) {
    int pos = 0;
    if (d->sign && !(d->num_digits == 1 && d->digits[0] == 0)) {
        buf[pos++] = '-';
    }

    int int_part = d->decimal_point;
    if (int_part <= 0) {
        buf[pos++] = '0';
    } else {
        for (int i = 0; i < int_part && i < d->num_digits; i++) {
            buf[pos++] = '0' + d->digits[i];
        }
    }

    int frac_digits = d->num_digits - d->decimal_point;
    if (frac_digits > 0) {
        buf[pos++] = '.';
        int frac_start = d->decimal_point < 0 ? 0 : d->decimal_point;
        /* If decimal_point < number of integer digits in the string, handle leading zeros */
        if (d->decimal_point < 0) {
            for (int i = 0; i < -d->decimal_point; i++) buf[pos++] = '0';
            for (int i = 0; i < d->num_digits; i++) buf[pos++] = '0' + d->digits[i];
        } else {
            for (int i = frac_start; i < d->num_digits; i++) {
                buf[pos++] = '0' + d->digits[i];
            }
        }
    }

    buf[pos] = '\0';
    return pos;
}

/* Format to exactly N decimal places */
static int ari_dec_to_string_fixed(const AriDecimal *d, char *buf, int buf_size, int places) {
    int pos = 0;
    if (d->sign) {
        /* Check if truly zero */
        int all_zero = 1;
        for (int i = 0; i < d->num_digits; i++) {
            if (d->digits[i] != 0) { all_zero = 0; break; }
        }
        if (!all_zero) buf[pos++] = '-';
    }

    int int_part = d->decimal_point;
    if (int_part <= 0) {
        buf[pos++] = '0';
    } else {
        for (int i = 0; i < int_part; i++) {
            if (i < d->num_digits)
                buf[pos++] = '0' + d->digits[i];
            else
                buf[pos++] = '0';
        }
    }

    buf[pos++] = '.';
    for (int i = 0; i < places; i++) {
        int digit_idx = d->decimal_point + i;
        if (digit_idx >= 0 && digit_idx < d->num_digits)
            buf[pos++] = '0' + d->digits[digit_idx];
        else
            buf[pos++] = '0';
    }
    buf[pos] = '\0';
    return pos;
}

static int ari_dec_is_zero(const AriDecimal *d) {
    for (int i = 0; i < d->num_digits; i++) {
        if (d->digits[i] != 0) return 0;
    }
    return 1;
}

/* Compare absolute values. Returns -1, 0, 1 */
static int ari_dec_cmp_abs(const AriDecimal *a, const AriDecimal *b) {
    int a_int = a->decimal_point;
    int b_int = b->decimal_point;
    if (a_int != b_int) return a_int > b_int ? 1 : -1;

    int max_digits = a->num_digits > b->num_digits ? a->num_digits : b->num_digits;
    for (int i = 0; i < max_digits; i++) {
        uint8_t da = (i < a->num_digits) ? a->digits[i] : 0;
        uint8_t db = (i < b->num_digits) ? b->digits[i] : 0;
        if (da != db) return da > db ? 1 : -1;
    }
    return 0;
}

/* Add absolute values (ignoring sign) */
static AriDecimal ari_dec_add_abs(const AriDecimal *a, const AriDecimal *b) {
    /* Align on decimal point */
    int a_frac = a->num_digits - a->decimal_point;
    int b_frac = b->num_digits - b->decimal_point;
    int max_frac = a_frac > b_frac ? a_frac : b_frac;

    int a_int = a->decimal_point;
    int b_int = b->decimal_point;
    int max_int = a_int > b_int ? a_int : b_int;

    int total = max_int + max_frac + 1; /* +1 for potential carry */
    AriDecimal result = ari_dec_create(total, max_int + 1, 0);

    /* Fill from right */
    int carry = 0;
    for (int i = max_frac - 1; i >= 0; i--) {
        int a_idx = a->decimal_point + i;
        int b_idx = b->decimal_point + i;
        int da = (a_idx >= 0 && a_idx < a->num_digits) ? a->digits[a_idx] : 0;
        int db = (b_idx >= 0 && b_idx < b->num_digits) ? b->digits[b_idx] : 0;
        int sum = da + db + carry;
        carry = sum / 10;
        result.digits[max_int + 1 + i] = sum % 10;
    }
    for (int i = max_int - 1; i >= 0; i--) {
        int a_pos = i - (max_int - a_int);
        int b_pos = i - (max_int - b_int);
        int da = (a_pos >= 0 && a_pos < a->num_digits) ? a->digits[a_pos] : 0;
        int db = (b_pos >= 0 && b_pos < b->num_digits) ? b->digits[b_pos] : 0;
        int sum = da + db + carry;
        carry = sum / 10;
        result.digits[i + 1] = sum % 10;
    }
    result.digits[0] = carry;

    ari_dec_normalize(&result);
    return result;
}

/* Subtract absolute values: |a| - |b| (assumes |a| >= |b|) */
static AriDecimal ari_dec_sub_abs(const AriDecimal *a, const AriDecimal *b) {
    int a_frac = a->num_digits - a->decimal_point;
    int b_frac = b->num_digits - b->decimal_point;
    int max_frac = a_frac > b_frac ? a_frac : b_frac;

    int a_int = a->decimal_point;
    int b_int = b->decimal_point;
    int max_int = a_int > b_int ? a_int : b_int;

    int total = max_int + max_frac;
    AriDecimal result = ari_dec_create(total, max_int, 0);

    int borrow = 0;
    for (int i = max_frac - 1; i >= 0; i--) {
        int a_idx = a->decimal_point + i;
        int b_idx = b->decimal_point + i;
        int da = (a_idx >= 0 && a_idx < a->num_digits) ? a->digits[a_idx] : 0;
        int db = (b_idx >= 0 && b_idx < b->num_digits) ? b->digits[b_idx] : 0;
        int diff = da - db - borrow;
        if (diff < 0) { diff += 10; borrow = 1; } else { borrow = 0; }
        result.digits[max_int + i] = diff;
    }
    for (int i = max_int - 1; i >= 0; i--) {
        int a_pos = i - (max_int - a_int);
        int b_pos = i - (max_int - b_int);
        int da = (a_pos >= 0 && a_pos < a->num_digits) ? a->digits[a_pos] : 0;
        int db = (b_pos >= 0 && b_pos < b->num_digits) ? b->digits[b_pos] : 0;
        int diff = da - db - borrow;
        if (diff < 0) { diff += 10; borrow = 1; } else { borrow = 0; }
        result.digits[i] = diff;
    }

    ari_dec_normalize(&result);
    return result;
}

static AriDecimal ari_dec_add(const AriDecimal *a, const AriDecimal *b) {
    if (a->sign == b->sign) {
        AriDecimal r = ari_dec_add_abs(a, b);
        r.sign = a->sign;
        if (ari_dec_is_zero(&r)) r.sign = 0;
        return r;
    }
    /* Different signs: subtract smaller abs from larger */
    int cmp = ari_dec_cmp_abs(a, b);
    if (cmp == 0) {
        return ari_dec_from_string("0");
    } else if (cmp > 0) {
        AriDecimal r = ari_dec_sub_abs(a, b);
        r.sign = a->sign;
        return r;
    } else {
        AriDecimal r = ari_dec_sub_abs(b, a);
        r.sign = b->sign;
        return r;
    }
}

static AriDecimal ari_dec_sub(const AriDecimal *a, const AriDecimal *b) {
    AriDecimal neg_b;
    neg_b.digits = b->digits;
    neg_b.num_digits = b->num_digits;
    neg_b.decimal_point = b->decimal_point;
    neg_b.sign = b->sign ? 0 : 1;
    neg_b.precision = b->precision;
    return ari_dec_add(a, &neg_b);
}

static AriDecimal ari_dec_mul(const AriDecimal *a, const AriDecimal *b) {
    int total_digits = a->num_digits + b->num_digits;
    int result_int = a->decimal_point + b->decimal_point;
    int result_sign = (a->sign != b->sign) ? 1 : 0;

    uint8_t *temp = (uint8_t *)calloc(total_digits + 1, 1);

    for (int i = a->num_digits - 1; i >= 0; i--) {
        int carry = 0;
        for (int j = b->num_digits - 1; j >= 0; j--) {
            int pos = i + j + 1;
            int prod = a->digits[i] * b->digits[j] + temp[pos] + carry;
            temp[pos] = prod % 10;
            carry = prod / 10;
        }
        temp[i] += carry;
    }

    AriDecimal result = ari_dec_create(total_digits, result_int, result_sign);
    memcpy(result.digits, temp, total_digits);
    free(temp);

    ari_dec_normalize(&result);
    return result;
}

static AriDecimal ari_dec_div(const AriDecimal *a, const AriDecimal *b, int precision) {
    if (ari_dec_is_zero(b)) {
        fprintf(stderr, "Division by zero\n");
        return ari_dec_from_string("0");
    }

    int result_sign = (a->sign != b->sign) ? 1 : 0;

    /* Convert to long division on digit arrays */
    /* Scale both numbers to integers by removing decimal points */
    int a_frac = a->num_digits - a->decimal_point;
    int b_frac = b->num_digits - b->decimal_point;

    /* We'll do division by building the quotient one digit at a time */
    /* Work with copies as positive integers * 10^scale */

    /* Dividend digits (treating as integer by appending precision extra zeros) */
    int extra = precision + b_frac + 1;
    int div_len = a->num_digits + extra;
    uint8_t *dividend = (uint8_t *)calloc(div_len, 1);
    memcpy(dividend, a->digits, a->num_digits);

    /* Divisor digits */
    int dvsr_len = b->num_digits;
    uint8_t *divisor = (uint8_t *)calloc(dvsr_len, 1);
    memcpy(divisor, b->digits, b->num_digits);

    /* Remove leading zeros from divisor */
    int dvsr_start = 0;
    while (dvsr_start < dvsr_len - 1 && divisor[dvsr_start] == 0) dvsr_start++;

    int eff_dvsr_len = dvsr_len - dvsr_start;

    /* Simple long division */
    int quot_len = div_len;
    uint8_t *quotient = (uint8_t *)calloc(quot_len, 1);

    /* Use a remainder buffer */
    int rem_cap = div_len + eff_dvsr_len + 10;
    uint8_t *remainder = (uint8_t *)calloc(rem_cap, 1);
    int rem_len = 0;

    for (int i = 0; i < div_len; i++) {
        /* Bring down next digit */
        remainder[rem_len++] = dividend[i];

        /* Remove leading zeros from remainder */
        int rem_start = 0;
        while (rem_start < rem_len - 1 && remainder[rem_start] == 0) rem_start++;
        int eff_rem = rem_len - rem_start;

        /* How many times does divisor go into remainder? */
        int q = 0;
        if (eff_rem > eff_dvsr_len ||
            (eff_rem == eff_dvsr_len && memcmp(remainder + rem_start, divisor + dvsr_start, eff_dvsr_len) >= 0)) {

            /* Trial subtraction */
            for (q = 1; q <= 9; q++) {
                /* Multiply divisor by (q+1) and compare - but simpler to just subtract repeatedly */
                /* Actually, let's do it properly: subtract divisor from remainder q times */
            }
            /* Better approach: binary-search or repeated subtraction */
            q = 0;
            while (1) {
                /* Check if remainder >= divisor */
                int cmp;
                int rs = 0;
                while (rs < rem_len - 1 && remainder[rs] == 0) rs++;
                int er = rem_len - rs;

                if (er < eff_dvsr_len) break;
                if (er > eff_dvsr_len) {
                    cmp = 1;
                } else {
                    cmp = memcmp(remainder + rs, divisor + dvsr_start, eff_dvsr_len);
                }
                if (cmp < 0) break;

                /* Subtract divisor from remainder, aligned at right */
                int borrow = 0;
                for (int j = 0; j < eff_dvsr_len; j++) {
                    int rpos = rem_len - 1 - j;
                    int dpos = dvsr_len - 1 - j;
                    int diff = remainder[rpos] - divisor[dpos] - borrow;
                    if (diff < 0) { diff += 10; borrow = 1; } else { borrow = 0; }
                    remainder[rpos] = diff;
                }
                /* Handle remaining borrow */
                for (int j = rem_len - eff_dvsr_len - 1; j >= 0 && borrow; j--) {
                    int diff = remainder[j] - borrow;
                    if (diff < 0) { diff += 10; borrow = 1; } else { borrow = 0; }
                    remainder[j] = diff;
                }
                q++;
            }
        }
        quotient[i] = q;
    }

    /* The decimal point in the quotient */
    int quot_decimal = a->decimal_point - b_frac + (dvsr_start);

    /* Build result */
    int max_out = quot_decimal + precision + 2;
    if (max_out > quot_len) max_out = quot_len;

    AriDecimal result = ari_dec_create(max_out, quot_decimal, result_sign);
    memcpy(result.digits, quotient, max_out);

    free(dividend);
    free(divisor);
    free(quotient);
    free(remainder);

    ari_dec_normalize(&result);
    return result;
}

/* Round to N decimal places */
static AriDecimal ari_dec_round(const AriDecimal *d, int places) {
    int frac_digits = d->num_digits - d->decimal_point;
    if (frac_digits <= places) {
        /* Nothing to round - make a copy */
        AriDecimal r = ari_dec_create(d->num_digits, d->decimal_point, d->sign);
        memcpy(r.digits, d->digits, d->num_digits);
        return r;
    }

    int keep = d->decimal_point + places;
    if (keep <= 0) {
        /* Round to zero or single digit */
        return ari_dec_from_string("0");
    }

    AriDecimal r = ari_dec_create(keep, d->decimal_point, d->sign);
    memcpy(r.digits, d->digits, keep);

    /* Check rounding digit */
    if (keep < d->num_digits && d->digits[keep] >= 5) {
        /* Round up */
        int carry = 1;
        for (int i = keep - 1; i >= 0 && carry; i--) {
            int val = r.digits[i] + carry;
            r.digits[i] = val % 10;
            carry = val / 10;
        }
        if (carry) {
            /* Need to expand */
            uint8_t *new_digits = (uint8_t *)calloc(keep + 1, 1);
            new_digits[0] = 1;
            memcpy(new_digits + 1, r.digits, keep);
            free(r.digits);
            r.digits = new_digits;
            r.num_digits = keep + 1;
            r.decimal_point++;
        }
    }

    ari_dec_normalize(&r);
    return r;
}

/* ================================================================
 * Test harness
 * ================================================================ */

static int check(const char *label, AriDecimal *got, const char *expected) {
    char buf[128];
    ari_dec_to_string_fixed(got, buf, sizeof(buf), 20);
    int pass = (strcmp(buf, expected) == 0);
    printf("  %s\n", label);
    printf("    expected: %s\n", expected);
    printf("    got:      %s  [%s]\n", buf, pass ? "PASS" : "FAIL");
    return pass;
}

int main(void) {
    int passes = 0, total = 7;

    printf("=== aricode AriDecimal (pure digit-array, NO floats) ===\n\n");

    /* Test 1: 0.1 + 0.2 = 0.3 */
    {
        AriDecimal a = ari_dec_from_string("0.1");
        AriDecimal b = ari_dec_from_string("0.2");
        AriDecimal r = ari_dec_add(&a, &b);
        passes += check("0.1 + 0.2 = 0.3", &r, "0.30000000000000000000");
        ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);
    }

    /* Test 2: 1.0 - 0.9 - 0.1 = 0.0 */
    {
        AriDecimal a = ari_dec_from_string("1.0");
        AriDecimal b = ari_dec_from_string("0.9");
        AriDecimal c = ari_dec_from_string("0.1");
        AriDecimal t = ari_dec_sub(&a, &b);
        AriDecimal r = ari_dec_sub(&t, &c);
        passes += check("1.0 - 0.9 - 0.1 = 0.0", &r, "0.00000000000000000000");
        ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&c);
        ari_dec_free(&t); ari_dec_free(&r);
    }

    /* Test 3: 0.1 * 0.1 = 0.01 */
    {
        AriDecimal a = ari_dec_from_string("0.1");
        AriDecimal b = ari_dec_from_string("0.1");
        AriDecimal r = ari_dec_mul(&a, &b);
        passes += check("0.1 * 0.1 = 0.01", &r, "0.01000000000000000000");
        ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);
    }

    /* Test 4: 1.0 / 3.0 * 3.0 = 1.0 (with rounding) */
    {
        AriDecimal a = ari_dec_from_string("1.0");
        AriDecimal b = ari_dec_from_string("3.0");
        AriDecimal three = ari_dec_from_string("3.0");
        AriDecimal div_result = ari_dec_div(&a, &b, 40);
        AriDecimal mul_result = ari_dec_mul(&div_result, &three);
        AriDecimal r = ari_dec_round(&mul_result, 20);
        passes += check("1.0 / 3.0 * 3.0 = 1.0", &r, "1.00000000000000000000");
        ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&three);
        ari_dec_free(&div_result); ari_dec_free(&mul_result); ari_dec_free(&r);
    }

    /* Test 5: 0.3 - 0.2 - 0.1 = 0.0 */
    {
        AriDecimal a = ari_dec_from_string("0.3");
        AriDecimal b = ari_dec_from_string("0.2");
        AriDecimal c = ari_dec_from_string("0.1");
        AriDecimal t = ari_dec_sub(&a, &b);
        AriDecimal r = ari_dec_sub(&t, &c);
        passes += check("0.3 - 0.2 - 0.1 = 0.0", &r, "0.00000000000000000000");
        ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&c);
        ari_dec_free(&t); ari_dec_free(&r);
    }

    /* Test 6: PI to 20 decimals */
    {
        AriDecimal pi = ari_dec_from_string("3.14159265358979323846");
        passes += check("PI to 20 decimals", &pi, "3.14159265358979323846");
        ari_dec_free(&pi);
    }

    /* Test 7: 1.111...1 + 2.222...2 = 3.333...3 */
    {
        AriDecimal a = ari_dec_from_string("1.11111111111111111111");
        AriDecimal b = ari_dec_from_string("2.22222222222222222222");
        AriDecimal r = ari_dec_add(&a, &b);
        passes += check("1.111...1 + 2.222...2 = 3.333...3", &r, "3.33333333333333333333");
        ari_dec_free(&a); ari_dec_free(&b); ari_dec_free(&r);
    }

    printf("\nResult: %d/%d PASS  (no external libraries, pure digit arithmetic)\n", passes, total);
    return 0;
}
