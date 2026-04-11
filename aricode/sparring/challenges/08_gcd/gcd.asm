; NASM x86_64 - Challenge 08: Euclidean GCD
; Compute GCD(462, 1071) = 21

section .text
global _start

gcd:
    mov  rax, rdi       ; a
    mov  rcx, rsi       ; b
.loop:
    test rcx, rcx
    jz   .done
    xor  edx, edx
    div  rcx            ; rax = a/b, rdx = a%b
    mov  rax, rcx       ; a = old b
    mov  rcx, rdx       ; b = a % b
    jmp  .loop
.done:
    ret

_start:
    mov  rdi, 462
    mov  rsi, 1071
    call gcd
    mov  rdi, rax
    mov  eax, 60
    syscall
