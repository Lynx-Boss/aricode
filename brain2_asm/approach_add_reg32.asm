; Approach: 32-bit register ADD with immediates
BITS 64
section .text
global _start
_start:
    mov edi, 37         ; B8+rd imm32 = BF 25000000  (5 bytes)
    add edi, 5          ; 83 C7 05                    (3 bytes)
    mov eax, 60         ; B8 3C000000                 (5 bytes)
    syscall             ; 0F 05                       (2 bytes)
    ; Total: 15 bytes, ADD = 3 bytes
