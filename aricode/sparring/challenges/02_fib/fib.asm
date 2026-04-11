; NASM x86_64 - Challenge 02: Fibonacci
; Compute fib(10) = 55, return as exit code
; Recursive implementation matching the other languages

section .text
global _start

; fib(n) - argument in rdi, result in rax
fib:
    push rbp
    mov rbp, rsp
    push rbx                ; callee-saved
    push r12                ; callee-saved

    cmp edi, 2
    jl .base_case

    mov r12d, edi           ; save n
    lea edi, [r12d - 1]     ; n-1
    call fib
    mov ebx, eax            ; save fib(n-1)

    lea edi, [r12d - 2]     ; n-2
    call fib
    add eax, ebx            ; fib(n-1) + fib(n-2)
    jmp .done

.base_case:
    mov eax, edi            ; return n

.done:
    pop r12
    pop rbx
    pop rbp
    ret

_start:
    mov edi, 10
    call fib
    mov edi, eax            ; exit code = fib(10)
    mov eax, 60             ; syscall: exit
    syscall
