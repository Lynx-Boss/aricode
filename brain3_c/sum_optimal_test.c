/* Test: can we get a shorter encoding using xor+mov for small constants? */
int main(void) {
    /* mov $0x2a,%eax is b8 2a 00 00 00 = 5 bytes
       xor %eax,%eax; mov $0x2a,%al = 31 c0 b0 2a = 4 bytes
       But does GCC ever emit this? */
    return 42;
}
