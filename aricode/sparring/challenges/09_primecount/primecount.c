/* C - Challenge 09: Prime Counting */
/* Count primes below 100 = 25 */

int is_prime(int n) {
    if (n < 2) return 0;
    if (n % 2 == 0) return n == 2;
    int d = 3;
    while (d * d <= n) {
        if (n % d == 0) return 0;
        d += 2;
    }
    return 1;
}

int main(void) {
    int count = 0;
    for (int n = 2; n < 100; n++) {
        if (is_prime(n)) count++;
    }
    return count;
}
