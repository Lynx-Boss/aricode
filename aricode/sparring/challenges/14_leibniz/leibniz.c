/* C - Challenge 14: Leibniz Pi Approximation */
/* 10000 terms, return floor(pi * 50) = 157 */
int main(void) {
    double sum = 0.0, sign = 1.0, denom = 1.0;
    for (int i = 0; i < 10000; i++) {
        sum += sign / denom;
        sign = -sign;
        denom += 2.0;
    }
    return (int)(sum * 4.0 * 50.0);
}
