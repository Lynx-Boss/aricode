/* C - Challenge 03: Factorial */
/* Compute 5! = 120, return as exit code */

int factorial(int n) {
    if (n < 2) return 1;
    return n * factorial(n - 1);
}

int main(void) {
    return factorial(5);
}
