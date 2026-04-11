; Approach: 64-bit register ADD with REX prefix
BITS 64
section .text
global _start
_start:
    mov rdi, 37         ; 48 C7 C7 25000000  (7 bytes, or 48 BF + imm64 = 10 bytes)
    add rdi, 5          ; 48 83 C7 05        (4 bytes)
    mov rax, 60         ; 48 C7 C0 3C000000  (7 bytes)
    syscall             ; 0F 05              (2 bytes)
    ; Total: 20 bytes, ADD = 4 bytes
