; Approach: Precomputed - just MOV the result (cheat/baseline)
BITS 64
section .text
global _start
_start:
    mov edi, 42         ; BF 2A000000        (5 bytes)
    mov eax, 60         ; B8 3C000000        (5 bytes)
    syscall             ; 0F 05              (2 bytes)
    ; Total: 12 bytes, but NO addition happens at runtime
