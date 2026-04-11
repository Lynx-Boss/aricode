; NASM x86_64 - Challenge 07: Mersenne Prime Verification
; Verify M31 = 2^31 - 1 = 2,147,483,647 is prime
; Iterative trial division (native loop - assembly at its best)
; Tests ~23,170 odd divisors from 3 to sqrt(M31)

section .text
global _start

; is_prime(n): n in rdi, result in rax (1=prime, 0=not)
is_prime:
    ; Check odd divisors from 3, step 2
    mov  rsi, 3             ; d = 3

.loop:
    ; unsigned divide: rax = n / d, rdx = n % d
    mov  rax, rdi           ; rax = n
    xor  edx, edx           ; clear rdx for DIV
    div  rsi                ; rax = n/d, rdx = n%d

    ; if n/d < d, we checked all divisors -> prime
    cmp  rax, rsi
    jb   .prime

    ; if n%d == 0, found a divisor -> not prime
    test rdx, rdx
    jz   .not_prime

    ; d += 2 (next odd divisor)
    add  rsi, 2
    jmp  .loop

.prime:
    mov  eax, 1
    ret

.not_prime:
    xor  eax, eax
    ret

_start:
    mov  rdi, 2147483647    ; M31 = 2^31 - 1
    call is_prime

    mov  rdi, rax           ; exit code = result
    mov  eax, 60            ; __NR_exit
    syscall
