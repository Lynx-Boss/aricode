; NASM x86_64 - Challenge 01: Simple Addition
; Return 37 + 5 = 42 as exit code

section .text
global _start

_start:
    mov edi, 37
    add edi, 5
    mov eax, 60         ; syscall: exit
    syscall
