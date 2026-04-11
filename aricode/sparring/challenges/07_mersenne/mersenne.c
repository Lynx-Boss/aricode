/* C - Challenge 07: Mersenne Prime Verification */
/* Verify M31 = 2^31 - 1 = 2,147,483,647 is prime */
/* Recursive trial division against ~23,170 odd divisors */

int is_prime_rec(int n, int d) {
    if (n / d < d) return 1;
    if (n % d == 0) return 0;
    return is_prime_rec(n, d + 2);
}

int main(void) {
    return is_prime_rec(2147483647, 3);
}
