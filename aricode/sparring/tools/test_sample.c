/* Auto-generated test sample for binary analysis tools */
#include <stdio.h>
#include <stdlib.h>

int compute(int a, int b) {
    int result = 0;
    for (int i = 0; i < a; i++) {
        result += b * i;
        if (result > 1000) {
            result = result / 3;
        }
    }
    return result;
}

int main(int argc, char *argv[]) {
    int a = argc > 1 ? atoi(argv[1]) : 10;
    int b = argc > 2 ? atoi(argv[2]) : 5;
    int r = compute(a, b);
    printf("Result: %d\n", r);
    return r;
}
