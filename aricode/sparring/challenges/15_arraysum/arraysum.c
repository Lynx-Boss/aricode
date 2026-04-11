/* C - Challenge 15: Array Sum, return sum%256 = 158 */
int main(void) {
    int data[100];
    for (int i = 0; i < 100; i++) data[i] = i * i;
    int sum = 0;
    for (int i = 0; i < 100; i++) sum += data[i];
    return sum % 256;
}
