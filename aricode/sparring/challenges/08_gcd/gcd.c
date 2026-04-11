/* C - Challenge 08: Euclidean GCD */
/* Compute GCD(462, 1071) = 21 */

int gcd(int a, int b) {
    while (b > 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}

int main(void) {
    return gcd(462, 1071);
}
