; NASM x86_64 - Challenge 10: Modular Exponentiation
; Compute 7^19 mod 211 = 85

section .text
global _start

; powmod(base=rdi, exp=rsi, m=rdx) -> rax
powmod:
    mov  r8, rdx            ; r8 = m
    mov  rcx, rsi           ; rcx = exp
    ; base = base % m
    mov  rax, rdi
    xor  edx, edx
    div  r8
    mov  rdi, rdx           ; rdi = base % m
    mov  rax, 1             ; result = 1
.loop:
    test rcx, rcx
    jz   .done
    test rcx, 1
    jz   .skip_mul
    ; result = result * base % m
    imul rax, rdi
    xor  edx, edx
    div  r8
    mov  rax, rdx           ; result = (result*base) % m
.skip_mul:
    shr  rcx, 1             ; exp >>= 1
    ; base = base * base % m
    mov  r9, rax            ; save result
    mov  rax, rdi
    imul rax, rdi
    xor  edx, edx
    div  r8
    mov  rdi, rdx           ; base = (base*base) % m
    mov  rax, r9            ; restore result
    jmp  .loop
.done:
    ret

_start:
    mov  rdi, 7
    mov  rsi, 19
    mov  rdx, 211
    call powmod
    mov  rdi, rax
    mov  eax, 60
    syscall
