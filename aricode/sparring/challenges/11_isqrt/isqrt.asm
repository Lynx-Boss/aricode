; NASM x86_64 - Challenge 11: Integer Square Root (Newton's Method)
; Compute isqrt(16129) = 127

section .text
global _start

isqrt:
    ; n in rdi, result in rax
    cmp  rdi, 2
    jl   .trivial
    mov  rax, rdi           ; x = n
    inc  rax
    shr  rax, 1             ; y = (n+1)/2
    mov  rcx, rdi           ; rcx = x (starts as n)
    xchg rax, rcx           ; rax=n(x), rcx=y
.loop:
    cmp  rcx, rax
    jge  .done              ; y >= x => converged
    mov  rax, rcx           ; x = y
    ; y = (x + n/x) / 2
    push rax                ; save x
    mov  rax, rdi           ; rax = n
    xor  edx, edx
    div  rcx                ; rax = n / x
    pop  rcx                ; rcx = x
    add  rax, rcx           ; x + n/x
    shr  rax, 1             ; / 2
    xchg rax, rcx           ; rax=x, rcx=new_y
    jmp  .loop
.done:
    ret
.trivial:
    mov  rax, rdi
    ret

_start:
    mov  rdi, 16129
    call isqrt
    mov  rdi, rax
    mov  eax, 60
    syscall
