; NASM x86_64 - Challenge 14: Leibniz Pi Approximation
; 10000 terms, return floor(pi * 50) = 157

section .data
    one:    dq 1.0
    two:    dq 2.0
    four:   dq 4.0
    fifty:  dq 50.0

section .text
global _start

_start:
    ; xmm0 = sum, xmm1 = sign, xmm2 = denom
    pxor    xmm0, xmm0          ; sum = 0
    movsd   xmm1, [rel one]     ; sign = 1.0
    movsd   xmm2, [rel one]     ; denom = 1.0
    movsd   xmm5, [rel two]     ; const 2.0
    mov     ecx, 10000

.loop:
    ; sum += sign / denom
    movsd   xmm3, xmm1
    divsd   xmm3, xmm2
    addsd   xmm0, xmm3
    ; sign = -sign
    pxor    xmm4, xmm4
    subsd   xmm4, xmm1
    movsd   xmm1, xmm4
    ; denom += 2
    addsd   xmm2, xmm5
    dec     ecx
    jnz     .loop

    ; pi = sum * 4 * 50
    mulsd   xmm0, [rel four]
    mulsd   xmm0, [rel fifty]
    cvttsd2si rdi, xmm0

    mov     eax, 60
    syscall
