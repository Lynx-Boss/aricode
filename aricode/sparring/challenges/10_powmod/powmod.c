/* C - Challenge 10: Modular Exponentiation */
/* Compute 7^19 mod 211 = 85 (RSA cryptographic primitive) */

int powmod(int base, int exp, int m) {
    int result = 1;
    int b = base % m;
    while (exp > 0) {
        if (exp & 1) result = result * b % m;
        exp >>= 1;
        b = b * b % m;
    }
    return result;
}

int main(void) {
    return powmod(7, 19, 211);
}
