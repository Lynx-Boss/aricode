; NASM x86_64 - Challenge 13: Minimax Game AI
; Nim(15): full tree search, return optimal move (3)

section .text
global _start

; minimax(stones=rdi, maximizing=rsi) -> rax
minimax:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13

    test rdi, rdi
    jnz  .not_terminal
    ; terminal: return maximizing ? -1 : 1
    test rsi, rsi
    jz   .min_wins
    mov  eax, -1
    jmp  .ret
.min_wins:
    mov  eax, 1
    jmp  .ret

.not_terminal:
    mov  r12, rdi           ; r12 = stones
    mov  r13, rsi           ; r13 = maximizing

    test rsi, rsi
    jz   .minimizing

    ; maximizing
    mov  ebx, -100          ; best = -100
    mov  ecx, 1             ; take = 1
.max_loop:
    cmp  ecx, 3
    jg   .max_done
    cmp  ecx, r12d
    jg   .max_done
    push rcx
    mov  rdi, r12
    sub  rdi, rcx
    xor  esi, esi           ; minimizing
    call minimax
    pop  rcx
    cmp  eax, ebx
    jle  .max_skip
    mov  ebx, eax
.max_skip:
    inc  ecx
    jmp  .max_loop
.max_done:
    mov  eax, ebx
    jmp  .ret

.minimizing:
    mov  ebx, 100           ; best = 100
    mov  ecx, 1
.min_loop:
    cmp  ecx, 3
    jg   .min_done
    cmp  ecx, r12d
    jg   .min_done
    push rcx
    mov  rdi, r12
    sub  rdi, rcx
    mov  esi, 1             ; maximizing
    call minimax
    pop  rcx
    cmp  eax, ebx
    jge  .min_skip
    mov  ebx, eax
.min_skip:
    inc  ecx
    jmp  .min_loop
.min_done:
    mov  eax, ebx

.ret:
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret

_start:
    ; find best move for 15 stones
    mov  r12d, -100         ; best_score
    mov  r13d, 1            ; best_move
    mov  ecx, 1             ; take
.find:
    cmp  ecx, 3
    jg   .found
    push rcx
    mov  rdi, 15
    sub  rdi, rcx
    xor  esi, esi
    call minimax
    pop  rcx
    cmp  eax, r12d
    jle  .skip
    mov  r12d, eax
    mov  r13d, ecx
.skip:
    inc  ecx
    jmp  .find
.found:
    mov  rdi, r13
    mov  eax, 60
    syscall
