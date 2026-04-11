# Brain 1: Raw ELF Binary Analysis

## Summary

| Metric | Value |
|---|---|
| Total binary size | 131 bytes |
| ELF header | 64 bytes |
| Program header | 56 bytes (1 PT_LOAD segment) |
| Machine code | 11 bytes |
| Instructions | 6 |
| ADD instructions | 1 |

## Machine Code Byte-by-Byte Breakdown

All 11 bytes of executable code, starting at file offset 0x78 (vaddr 0x400078):

### Instruction 1: `push 37` (2 bytes)
| Offset | Byte | Meaning |
|---|---|---|
| 0x78 | `6A` | Opcode: PUSH imm8 |
| 0x79 | `25` | Immediate value: 37 (0x25) |

Pushes the first operand (37) onto the stack. Using push+pop is 1 byte shorter than `mov edi, 37` (which would be 5 bytes with the B8+rd encoding).

### Instruction 2: `pop rdi` (1 byte)
| Offset | Byte | Meaning |
|---|---|---|
| 0x7A | `5F` | Opcode: POP r64 (rdi = register 7) |

Pops the value 37 into RDI (the first argument register for the exit syscall). No REX prefix needed because POP defaults to 64-bit in long mode.

### Instruction 3: `add edi, 5` (3 bytes)
| Offset | Byte | Meaning |
|---|---|---|
| 0x7B | `83` | Opcode: Immediate Group 1, Ev imm8 (32-bit operand) |
| 0x7C | `C7` | ModR/M: mod=11 (register), reg=000 (/0 = ADD), r/m=111 (edi) |
| 0x7D | `05` | Immediate value: 5 |

**This is the core computation: 37 + 5 = 42.** Uses 32-bit `edi` instead of 64-bit `rdi` to avoid a REX prefix (saves 1 byte). Writing to a 32-bit register in x86_64 automatically zero-extends to 64 bits, so the full RDI register gets the correct value 42.

### Instruction 4: `push 60` (2 bytes)
| Offset | Byte | Meaning |
|---|---|---|
| 0x7E | `6A` | Opcode: PUSH imm8 |
| 0x7F | `3C` | Immediate value: 60 (0x3C) = __NR_exit |

Pushes the exit syscall number onto the stack.

### Instruction 5: `pop rax` (1 byte)
| Offset | Byte | Meaning |
|---|---|---|
| 0x80 | `58` | Opcode: POP r64 (rax = register 0) |

Pops 60 into RAX (syscall number register). The push+pop pair (3 bytes) is shorter than `mov eax, 60` (5 bytes) or `xor eax,eax; mov al, 60` (4 bytes).

### Instruction 6: `syscall` (2 bytes)
| Offset | Byte | Meaning |
|---|---|---|
| 0x81 | `0F` | Two-byte opcode escape prefix |
| 0x82 | `05` | SYSCALL opcode |

Invokes the Linux kernel. With RAX=60 and RDI=42, this calls `exit(42)`.

## CPU Cycle Analysis (AMD Zen 3 / Ryzen 7 5800X)

| Instruction | Latency | Throughput | Port(s) |
|---|---|---|---|
| `push 37` | ~3 cycles (store) | 1/cycle | AGU + STQ |
| `pop rdi` | ~4 cycles (load) | 1/cycle | AGU + LDQ |
| `add edi, 5` | **1 cycle** | 4/cycle | ALU (any of 4 ports) |
| `push 60` | ~3 cycles (store) | 1/cycle | AGU + STQ |
| `pop rax` | ~4 cycles (load) | 1/cycle | AGU + LDQ |
| `syscall` | ~100+ cycles | N/A | Microcode |

**The ADD itself takes exactly 1 cycle and can execute on any of the 4 ALU ports.** The push/pop pairs have a store-forwarding dependency (~4-7 cycles each pair), but the Zen 3 store-forwarding path is well-optimized.

Total user-space cycles before syscall: approximately 12-15 cycles.
The syscall itself dominates at ~100+ cycles for the kernel transition.

## Design Decisions

1. **push/pop instead of mov**: `push imm8 + pop r64` = 3 bytes vs `mov r32, imm32` = 5 bytes. Saves 2 bytes per value load at the cost of a stack round-trip.

2. **32-bit add instead of 8-bit**: `add edi, 5` (3 bytes) vs `add dil, 5` (4 bytes, needs REX prefix 0x40). The 32-bit form is both shorter and faster.

3. **No REX prefixes**: By using 32-bit register names (edi, eax) and the push/pop idiom, we avoid all REX prefixes entirely.

4. **No section headers**: The ELF binary has zero section headers. Only one program header (PT_LOAD) is used. The kernel does not need section headers to execute a binary.

5. **Statically linked, no interpreter**: No PT_INTERP segment, no dynamic linker overhead. The kernel maps our single segment and jumps directly to the entry point.

## Instruction Efficiency Score

- **Bytes for the actual addition**: 3 bytes (`83 C7 05`)
- **Bytes for operand setup + syscall**: 8 bytes
- **Bytes for ELF container**: 120 bytes
- **Ratio of computation to total**: 3/131 = 2.3%
- **Ratio of computation to code**: 3/11 = 27.3%
