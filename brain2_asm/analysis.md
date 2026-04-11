# Brain 2: NASM Assembly - Hyper-Optimized Addition Analysis

## Target
- **CPU**: AMD Ryzen 7 5800X (Zen 3 microarchitecture)
- **ISA**: x86_64
- **OS**: Linux (syscall ABI)
- **Operation**: 37 + 5 = 42 (returned as exit code)

---

## Winner: 8-bit AL Short-Form ADD

### Disassembly (objdump -d -M intel)

```
0000000000400080 <.text>:
  400080:	b0 25                	mov    al,0x25
  400082:	04 05                	add    al,0x5
  400084:	0f b6 f8             	movzx  edi,al
  400087:	b0 3c                	mov    al,0x3c
  400089:	0f 05                	syscall
```

### Key Metrics
| Metric                        | Value    |
|-------------------------------|----------|
| **Total code bytes**          | 11       |
| **ADD instruction bytes**     | 2        |
| **Total instructions**        | 5        |
| **Addition instructions**     | 2 (mov + add) |
| **Binary size (ELF)**         | 352      |
| **Zen 3 cycles for ADD**      | 1        |

### Why This Wins

The x86 ISA has **dedicated short-form opcodes** for operations on the AL register:
- `B0 imm8` = MOV AL, imm8 (2 bytes, no ModR/M)
- `04 imm8` = ADD AL, imm8 (2 bytes, no ModR/M)

These are the shortest possible encodings for loading a value and adding to it. The `04` opcode is a legacy accumulator short-form dating back to 8086 -- it skips the ModR/M byte entirely.

---

## All Approaches Compared

### Approach 1: 8-bit AL Short-Form ADD (WINNER)
```
  400080:  b0 25        mov    al,0x25          ; 2 bytes
  400082:  04 05        add    al,0x5           ; 2 bytes
  400084:  0f b6 f8     movzx  edi,al           ; 3 bytes
  400087:  b0 3c        mov    al,0x3c          ; 2 bytes
  400089:  0f 05        syscall                 ; 2 bytes
```
- **Code size**: 11 bytes
- **ADD encoding**: 2 bytes (opcode 04 + imm8)
- **Zen 3 ADD latency**: 1 cycle
- **Zen 3 ADD throughput**: 4/cycle (any ALU pipe)

### Approach 2: 32-bit Register ADD
```
  400080:  bf 25 00 00 00   mov    edi,0x25     ; 5 bytes
  400085:  83 c7 05         add    edi,0x5      ; 3 bytes
  400088:  b8 3c 00 00 00   mov    eax,0x3c     ; 5 bytes
  40008d:  0f 05            syscall             ; 2 bytes
```
- **Code size**: 15 bytes (+36% vs winner)
- **ADD encoding**: 3 bytes (opcode 83 + ModR/M + imm8)
- **Zen 3 ADD latency**: 1 cycle
- **Advantage**: No MOVZX needed; MOV EDI zero-extends to RDI
- **Disadvantage**: MOV r32,imm32 is 5 bytes (imm32 encoding even for small values)

### Approach 3: 64-bit Register ADD (REX prefix)
```
  400080:  bf 25 00 00 00       mov    edi,0x25     ; 5 bytes (NASM optimizes to 32-bit)
  400085:  48 83 c7 05          add    rdi,0x5      ; 4 bytes
  400089:  b8 3c 00 00 00       mov    eax,0x3c     ; 5 bytes
  40008e:  0f 05                syscall             ; 2 bytes
```
- **Code size**: 16 bytes (+45% vs winner)
- **ADD encoding**: 4 bytes (REX + opcode 83 + ModR/M + imm8)
- **Zen 3 ADD latency**: 1 cycle
- **Disadvantage**: REX prefix adds 1 byte for zero benefit

### Approach 4: LEA-Based Addition
```
  400080:  b8 25 00 00 00   mov    eax,0x25     ; 5 bytes
  400085:  8d 78 05         lea    edi,[rax+0x5] ; 3 bytes
  400088:  b8 3c 00 00 00   mov    eax,0x3c     ; 5 bytes
  40008d:  0f 05            syscall             ; 2 bytes
```
- **Code size**: 15 bytes (+36% vs winner)
- **LEA encoding**: 3 bytes (opcode 8D + ModR/M + disp8)
- **Zen 3 LEA latency**: 1 cycle (simple LEA, AGU pipe)
- **Note**: LEA uses AGU pipe (only 2/cycle on Zen 3 vs 4/cycle for ADD)

### Approach 5: ADD Two 8-bit Registers
```
  400080:  b0 25        mov    al,0x25          ; 2 bytes
  400082:  b1 05        mov    cl,0x5           ; 2 bytes
  400084:  00 c8        add    al,cl            ; 2 bytes
  400086:  0f b6 f8     movzx  edi,al           ; 3 bytes
  400089:  b0 3c        mov    al,0x3c          ; 2 bytes
  40008b:  0f 05        syscall                 ; 2 bytes
```
- **Code size**: 13 bytes (+18% vs winner)
- **ADD encoding**: 2 bytes (opcode 00 + ModR/M)
- **Disadvantage**: Requires extra MOV to load second operand; 6 instructions total

### Approach 6: Precomputed MOV (Baseline/Cheat)
```
  400080:  bf 2a 00 00 00   mov    edi,0x2a     ; 5 bytes
  400085:  b8 3c 00 00 00   mov    eax,0x3c     ; 5 bytes
  40008a:  0f 05            syscall             ; 2 bytes
```
- **Code size**: 12 bytes -- no addition occurs at runtime (disqualified)

---

## Summary Table

| Approach               | Code Bytes | ADD Bytes | Instructions | Zen 3 Cycles (ADD) | Binary Size |
|------------------------|-----------|-----------|--------------|---------------------|-------------|
| **8-bit AL (WINNER)**  | **11**    | **2**     | **5**        | **1**               | **352**     |
| 32-bit Register ADD    | 15        | 3         | 4            | 1                   | 352         |
| 64-bit Register ADD    | 16        | 4         | 4            | 1                   | 360         |
| LEA-based              | 15        | 3         | 4            | 1                   | 352         |
| Two 8-bit Registers    | 13        | 2         | 6            | 1                   | 352         |
| Precomputed (cheat)    | 12        | 0         | 3            | 0                   | 352         |

---

## Zen 3 Microarchitecture Analysis

### Pipeline Details for the Winner

On AMD Zen 3:
1. **MOV AL, 0x25** (B0 25): May be eliminated by move elimination.
2. **ADD AL, 0x5** (04 05): 1-cycle latency, dispatched to any of 4 ALU pipes. Zen 3 handles partial registers cleanly.
3. **MOVZX EDI, AL** (0F B6 F8): 1 cycle, can often be eliminated by move elimination.
4. **MOV AL, 0x3C** (B0 3C): Sets up syscall number.
5. **SYSCALL** (0F 05): Microcode sequence, ~20+ cycles (kernel transition).

**Critical path for the addition**: MOV AL + ADD AL = 2 instructions, 4 bytes, ~1-2 cycles.

### Why 8-bit AL Wins Over 32-bit

The 32-bit approach wastes bytes:
- `MOV EDI, imm32` must encode a full 32-bit immediate (5 bytes) even for value 37
- `ADD EDI, imm8` needs a ModR/M byte (3 bytes total)

The 8-bit AL approach exploits legacy accumulator short-forms (no ModR/M) to save 4 bytes. The extra MOVZX costs 3 bytes, but net savings is still 4 bytes.

### Fetch/Decode Advantage

The 11-byte winner fits in a single 32-byte fetch window and decodes in 2 cycles (4+1 instructions). All approaches fit in one fetch window, but smaller code = better icache utilization.

---

## Conclusion

The **8-bit AL short-form ADD** wins: 11 bytes total, 2 bytes for the ADD itself, 1 cycle on Zen 3. It exploits x86 legacy accumulator opcodes (04h = ADD AL,imm8) that skip the ModR/M byte entirely -- the most compact addition encoding possible in x86_64.
