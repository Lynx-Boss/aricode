; NASM x86_64 - Challenge 12: Perceptron Neural Network
; Train AND gate, 1000 epochs, fixed-point (scale=1000), return correct (4)

section .data
    divisor: dq 1000

section .text
global _start

; div1000: divide eax by 1000, result in eax
div1000:
    cdq
    mov  ecx, 1000
    idiv ecx
    ret

train:
    push rbx
    push r12
    push r13
    push r14
    push r15

    xor  r12d, r12d         ; w1 = 0
    xor  r13d, r13d         ; w2 = 0
    xor  r14d, r14d         ; bias = 0
    xor  r15d, r15d         ; epoch = 0

.epoch:
    cmp  r15d, 1000
    jge  .test

    ; lr = 100
    ; Sample (0,0)->0: out=bias
    mov  eax, r14d
    xor  esi, esi
    cmp  eax, 500
    jle  .s0
    mov  esi, 1000
.s0: neg  esi                ; err = -pred
    imul eax, esi, 100      ; lr*err
    call div1000
    add  r14d, eax

    ; Sample (0,1)->0: out=w2+bias
    mov  eax, r13d
    add  eax, r14d
    xor  esi, esi
    cmp  eax, 500
    jle  .s1
    mov  esi, 1000
.s1: neg  esi
    imul eax, esi, 100
    call div1000
    add  r13d, eax
    add  r14d, eax

    ; Sample (1,0)->0: out=w1+bias
    mov  eax, r12d
    add  eax, r14d
    xor  esi, esi
    cmp  eax, 500
    jle  .s2
    mov  esi, 1000
.s2: neg  esi
    imul eax, esi, 100
    call div1000
    add  r12d, eax
    add  r14d, eax

    ; Sample (1,1)->1: out=w1+w2+bias
    mov  eax, r12d
    add  eax, r13d
    add  eax, r14d
    xor  esi, esi
    cmp  eax, 500
    jle  .s3
    mov  esi, 1000
.s3: mov  edi, 1000
    sub  edi, esi            ; err = 1000 - pred
    imul eax, edi, 100
    call div1000
    add  r12d, eax
    add  r13d, eax
    add  r14d, eax

    inc  r15d
    jmp  .epoch

.test:
    xor  eax, eax
    cmp  r14d, 500
    jg   .t1
    inc  eax
.t1:
    mov  ecx, r13d
    add  ecx, r14d
    cmp  ecx, 500
    jg   .t2
    inc  eax
.t2:
    mov  ecx, r12d
    add  ecx, r14d
    cmp  ecx, 500
    jg   .t3
    inc  eax
.t3:
    mov  ecx, r12d
    add  ecx, r13d
    add  ecx, r14d
    cmp  ecx, 500
    jle  .done
    inc  eax
.done:
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    ret

_start:
    call train
    mov  rdi, rax
    mov  eax, 60
    syscall
