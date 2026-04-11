; NASM x86_64 - Challenge 09: Prime Counting
; Count primes below 100 = 25

section .text
global _start

is_prime:
    ; n in rdi, result in rax
    cmp  rdi, 2
    jl   .not_prime
    je   .prime
    test rdi, 1
    jz   .not_prime         ; even > 2 = not prime
    mov  rsi, 3             ; d = 3
.check:
    mov  rax, rsi
    imul rax, rsi           ; d * d
    cmp  rax, rdi
    jg   .prime             ; d*d > n => prime
    mov  rax, rdi
    xor  edx, edx
    div  rsi                ; n / d
    test rdx, rdx
    jz   .not_prime         ; divisible
    add  rsi, 2
    jmp  .check
.prime:
    mov  eax, 1
    ret
.not_prime:
    xor  eax, eax
    ret

_start:
    xor  ebx, ebx          ; count = 0
    mov  r12, 2             ; n = 2
.loop:
    cmp  r12, 100
    jge  .done
    mov  rdi, r12
    call is_prime
    add  ebx, eax           ; count += result
    inc  r12
    jmp  .loop
.done:
    mov  rdi, rbx
    mov  eax, 60
    syscall
