; NASM x86_64 - Challenge 15: Array Sum, return sum%256=103
section .text
global _start
_start:
    sub  rsp, 800          ; 100 * 8 bytes
    ; Fill: data[i] = i*i
    xor  ecx, ecx
.fill:
    cmp  ecx, 100
    jge  .sum
    mov  eax, ecx
    imul eax, ecx
    cdqe
    mov  [rsp + rcx*8], rax
    inc  ecx
    jmp  .fill
.sum:
    xor  eax, eax          ; sum = 0
    xor  ecx, ecx
.add:
    cmp  ecx, 100
    jge  .done
    add  rax, [rsp + rcx*8]
    inc  ecx
    jmp  .add
.done:
    ; sum % 256
    and  eax, 255
    mov  rdi, rax
    add  rsp, 800
    mov  eax, 60
    syscall
