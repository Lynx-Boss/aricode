/* sum_inline.c - Addition using inline assembly */
int main(void) {
    int a = 37;
    int b = 5;
    int result;
    __asm__ volatile (
        "addl %2, %0"
        : "=r" (result)
        : "0" (a), "r" (b)
    );
    return result;
}
