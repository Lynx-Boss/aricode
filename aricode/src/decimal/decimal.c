/*
 * AriDecimal core implementation
 *
 * Pure digit-based decimal representation. Not a single float in sight.
 */

#include "decimal.h"

/* ---------- helpers ---------- */

static AriDecimal ari_dec_alloc(int num_digits, int decimal_point, int sign, int precision) {
    AriDecimal d;
    d.num_digits = num_digits;
    d.decimal_point = decimal_point;
    d.sign = sign;
    d.precision = precision;
    d.digits = (uint8_t *)calloc((size_t)num_digits, 1);
    return d;
}

/* ---------- public API ---------- */

AriDecimal ari_dec_from_string(const char *str) {
    if (!str || *str == '\0') {
        return ari_dec_from_int(0);
    }

    const char *p = str;
    int sign = 0;

    /* optional sign */
    if (*p == '-') { sign = 1; p++; }
    else if (*p == '+') { p++; }

    /* skip leading zeros (but keep at least one before dot) */
    const char *start = p;
    while (*p == '0' && *(p + 1) != '\0' && *(p + 1) != '.') p++;
    start = p;

    /* count integer digits and fractional digits */
    int int_digits = 0;
    int frac_digits = 0;
    int saw_dot = 0;
    const char *scan = start;
    while (*scan) {
        if (*scan == '.') {
            saw_dot = 1;
        } else if (*scan >= '0' && *scan <= '9') {
            if (!saw_dot) int_digits++;
            else frac_digits++;
        }
        scan++;
    }

    if (int_digits == 0) int_digits = 1; /* at least "0" */

    int total = int_digits + frac_digits;
    AriDecimal d = ari_dec_alloc(total, int_digits, sign, ARI_DEC_DEFAULT_PRECISION);

    /* fill digits */
    int idx = 0;
    p = start;

    /* handle the case where input starts with '.' like ".5" */
    if (*start == '.') {
        d.digits[0] = 0;
        idx = 0; /* decimal_point is 1, we already set int_digits=1 */
    }

    p = start;
    while (*p) {
        if (*p == '.') { p++; continue; }
        if (*p >= '0' && *p <= '9') {
            d.digits[idx++] = (uint8_t)(*p - '0');
        }
        p++;
    }

    /* if the value is all zeros, force positive */
    if (ari_dec_is_zero(&d)) d.sign = 0;

    return d;
}

AriDecimal ari_dec_from_int(int64_t val) {
    int sign = 0;
    uint64_t abs_val;

    if (val < 0) {
        sign = 1;
        /* handle INT64_MIN carefully */
        abs_val = (uint64_t)(-(val + 1)) + 1ULL;
    } else {
        abs_val = (uint64_t)val;
    }

    if (abs_val == 0) {
        AriDecimal d = ari_dec_alloc(1, 1, 0, ARI_DEC_DEFAULT_PRECISION);
        d.digits[0] = 0;
        return d;
    }

    /* count digits */
    int count = 0;
    uint64_t tmp = abs_val;
    while (tmp > 0) { count++; tmp /= 10; }

    AriDecimal d = ari_dec_alloc(count, count, sign, ARI_DEC_DEFAULT_PRECISION);
    tmp = abs_val;
    for (int i = count - 1; i >= 0; i--) {
        d.digits[i] = (uint8_t)(tmp % 10);
        tmp /= 10;
    }
    return d;
}

int ari_dec_to_string(const AriDecimal *dec, char *buf, int buf_size) {
    if (!dec || !buf || buf_size < 2) return 0;

    int pos = 0;
    int max = buf_size - 1;

    /* sign */
    if (dec->sign && !ari_dec_is_zero(dec)) {
        if (pos < max) buf[pos++] = '-';
    }

    int int_part = dec->decimal_point;
    int frac_part = dec->num_digits - dec->decimal_point;

    /* integer digits */
    if (int_part <= 0) {
        if (pos < max) buf[pos++] = '0';
    } else {
        for (int i = 0; i < int_part; i++) {
            if (pos < max) {
                buf[pos++] = '0' + (char)ari_dec_get_digit(dec, i);
            }
        }
    }

    /* fractional digits */
    if (frac_part > 0) {
        if (pos < max) buf[pos++] = '.';

        /* if decimal_point < 0 we need leading zeros in the fraction */
        if (dec->decimal_point < 0) {
            int leading = -dec->decimal_point;
            for (int i = 0; i < leading && pos < max; i++) {
                buf[pos++] = '0';
            }
        }

        for (int i = int_part; i < dec->num_digits; i++) {
            if (pos < max) {
                buf[pos++] = '0' + (char)ari_dec_get_digit(dec, i);
            }
        }

        /* strip trailing zeros from fractional part */
        while (pos > 0 && buf[pos - 1] == '0') pos--;
        /* don't strip the dot itself if we just removed everything */
        if (pos > 0 && buf[pos - 1] == '.') pos--;
    }

    buf[pos] = '\0';
    return pos;
}

void ari_dec_free(AriDecimal *dec) {
    if (dec && dec->digits) {
        free(dec->digits);
        dec->digits = NULL;
        dec->num_digits = 0;
    }
}

AriDecimal ari_dec_copy(const AriDecimal *dec) {
    AriDecimal c;
    c.num_digits = dec->num_digits;
    c.decimal_point = dec->decimal_point;
    c.sign = dec->sign;
    c.precision = dec->precision;
    c.digits = (uint8_t *)malloc((size_t)dec->num_digits);
    memcpy(c.digits, dec->digits, (size_t)dec->num_digits);
    return c;
}

int ari_dec_is_zero(const AriDecimal *dec) {
    for (int i = 0; i < dec->num_digits; i++) {
        if (dec->digits[i] != 0) return 0;
    }
    return 1;
}

void ari_dec_normalize(AriDecimal *dec) {
    if (!dec || !dec->digits || dec->num_digits == 0) return;

    /* Remove leading zeros from integer part (keep at least 1 integer digit) */
    while (dec->decimal_point > 1 && dec->num_digits > 0 && dec->digits[0] == 0) {
        memmove(dec->digits, dec->digits + 1, (size_t)(dec->num_digits - 1));
        dec->num_digits--;
        dec->decimal_point--;
    }

    /* Remove trailing zeros from fractional part */
    int frac_end = dec->num_digits;
    while (frac_end > dec->decimal_point && frac_end > 0 && dec->digits[frac_end - 1] == 0) {
        frac_end--;
    }
    dec->num_digits = frac_end;

    /* Ensure at least one digit */
    if (dec->num_digits == 0) {
        dec->num_digits = 1;
        dec->decimal_point = 1;
        dec->digits[0] = 0;
        dec->sign = 0;
    }

    /* Zero is always positive */
    if (ari_dec_is_zero(dec)) dec->sign = 0;
}
