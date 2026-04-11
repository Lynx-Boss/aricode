; Approach: LEA-based addition (single instruction add)
BITS 64
section .text
global _start
_start:
    mov eax, 37         ; B8 25000000        (5 bytes)
    lea edi, [rax+5]    ; 8D 78 05           (3 bytes)
    mov eax, 60         ; B8 3C000000        (5 bytes)
    syscall             ; 0F 05              (2 bytes)
    ; Total: 15 bytes, LEA = 3 bytes
