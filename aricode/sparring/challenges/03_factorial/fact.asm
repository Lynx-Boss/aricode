; NASM x86_64 - Challenge 03: Factorial
; Compute 5! = 120, return as exit code
; Recursive implementation matching the other languages

section .text
global _start

; factorial(n) - argument in rdi, result in rax
factorial:
    push rbp
    mov rbp, rsp
    push rbx                ; callee-saved

    cmp edi, 2
    jl .base_case

    mov ebx, edi            ; save n
    lea edi, [ebx - 1]      ; n-1
    call factorial
    imul eax, ebx           ; n * factorial(n-1)
    jmp .done

.base_case:
    mov eax, 1              ; return 1

.done:
    pop rbx
    pop rbp
    ret

_start:
    mov edi, 5
    call factorial
    mov edi, eax            ; exit code = factorial(5)
    mov eax, 60             ; syscall: exit
    syscall
