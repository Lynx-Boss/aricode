; Approach: ADD two registers (8-bit)
BITS 64
section .text
global _start
_start:
    mov al, 37          ; B0 25              (2 bytes)
    mov cl, 5           ; B1 05              (2 bytes)
    add al, cl          ; 00 C8              (2 bytes)
    movzx edi, al       ; 0F B6 F8           (3 bytes)
    mov al, 60          ; B0 3C              (2 bytes)
    syscall             ; 0F 05              (2 bytes)
    ; Total: 13 bytes, but 3 instructions for the add
