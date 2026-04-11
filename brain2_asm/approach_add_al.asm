; Approach: 8-bit AL short-form (THE WINNER - same as sum.asm)
BITS 64
section .text
global _start
_start:
    mov al, 37          ; B0 25              (2 bytes)
    add al, 5           ; 04 05              (2 bytes)
    movzx edi, al       ; 0F B6 F8           (3 bytes)
    mov al, 60          ; B0 3C              (2 bytes)
    syscall             ; 0F 05              (2 bytes)
    ; Total: 11 bytes, ADD = 2 bytes
