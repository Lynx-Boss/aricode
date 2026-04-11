/* C - Challenge 11: Integer Square Root (Newton's Method) */
/* Compute isqrt(16129) = 127 */

int isqrt(int n) {
    if (n < 2) return n;
    int x = n;
    int y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

int main(void) {
    return isqrt(16129);
}
