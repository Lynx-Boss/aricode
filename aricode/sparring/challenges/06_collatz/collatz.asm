; NASM x86_64 - Challenge 06: Collatz Conjecture
; Compute the number of steps for 871 to reach 1 = 178 steps
; The Collatz conjecture (1937) remains UNSOLVED.
; collatz_steps(n): n in rdi, result in rax

section .text
global _start

collatz_steps:
    push rbp
    mov  rbp, rsp

    ; if (n == 1) return 0
    cmp  rdi, 1
    jne  .not_one
    xor  eax, eax
    pop  rbp
    ret

.not_one:
    ; if (n % 2 == 0)
    test rdi, 1           ; check LSB
    jnz  .odd

    ; even: return 1 + collatz_steps(n / 2)
    shr  rdi, 1           ; n / 2 (shift right = fast divide by 2)
    call collatz_steps
    inc  rax              ; + 1
    pop  rbp
    ret

.odd:
    ; odd: return 1 + collatz_steps(3*n + 1)
    lea  rdi, [rdi + rdi*2 + 1]  ; 3*n + 1
    call collatz_steps
    inc  rax              ; + 1
    pop  rbp
    ret

_start:
    mov  rdi, 871
    call collatz_steps

    mov  rdi, rax         ; exit code = result
    mov  eax, 60          ; __NR_exit
    syscall
