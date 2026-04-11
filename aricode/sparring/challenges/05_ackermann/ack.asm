; NASM x86_64 - Challenge 05: Ackermann Function
; Compute A(3,4) = 125
; The Ackermann function grows faster than any primitive recursive function.
; ack(m, n): m in rdi, n in rsi, result in rax

section .text
global _start

ack:
    push rbp
    mov  rbp, rsp

    ; if (m == 0) return n + 1
    test rdi, rdi
    jnz  .m_nonzero
    lea  rax, [rsi + 1]
    pop  rbp
    ret

.m_nonzero:
    ; if (n == 0) return ack(m-1, 1)
    test rsi, rsi
    jnz  .both_nonzero
    dec  rdi              ; m - 1
    mov  rsi, 1           ; n = 1
    pop  rbp
    jmp  ack              ; tail call

.both_nonzero:
    ; return ack(m-1, ack(m, n-1))
    push rdi              ; save m

    ; First: compute ack(m, n-1)
    dec  rsi              ; n - 1
    call ack              ; result in rax

    ; Second: compute ack(m-1, ack_result)
    pop  rdi              ; restore m
    dec  rdi              ; m - 1
    mov  rsi, rax         ; n = ack(m, n-1)
    pop  rbp
    jmp  ack              ; tail call

_start:
    mov  rdi, 3
    mov  rsi, 4
    call ack

    mov  rdi, rax         ; exit code = result
    mov  eax, 60          ; __NR_exit
    syscall
