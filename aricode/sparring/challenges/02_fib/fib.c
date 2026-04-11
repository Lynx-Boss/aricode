/* C - Challenge 02: Fibonacci */
/* Compute fib(10) = 55, return as exit code */

int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    return fib(10);
}
