; =============================================================================
; sum.asm - Hyper-optimized addition: 37 + 5 = 42
; Target: AMD Ryzen 7 5800X (Zen 3), Linux x86_64
; Assembler: NASM
;
; WINNER: 8-bit ADD immediate to AL (2 bytes for the add)
; Total program: 3 instructions, 7 bytes of code
; =============================================================================

BITS 64

section .text
global _start

_start:
    ; Move 37 into AL using a 2-byte MOV (B0 imm8)
    mov al, 37          ; B0 25         (2 bytes)

    ; Add 5 to AL using the special short-form ADD AL,imm8 (2 bytes)
    ; Opcode 04 is the dedicated "ADD AL, imm8" — no ModR/M byte needed
    add al, 5           ; 04 05         (2 bytes)

    ; AL now contains 42. Move it to EDI for syscall exit code.
    ; Use MOVZX to zero-extend AL into EDI cleanly (3 bytes)
    movzx edi, al       ; 0F B6 F8      (3 bytes)

    ; Exit syscall: rax=60, rdi=exit_code
    mov al, 60          ; B0 3C         (2 bytes)
    syscall             ; 0F 05         (2 bytes)

    ; Total code: 11 bytes, but the ADD itself is only 2 bytes
    ; The critical addition path: 2 instructions, 4 bytes, 1 cycle
