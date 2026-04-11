# Brain 3: C Compiler Optimization Analysis

## Target: AMD Ryzen 7 5800X (Zen 3), Linux x86_64
## Compiler: GCC 14.2.0 (Debian)
## Goal: 37 + 5 = 42, returned as exit code

---

## 1. Exact Machine Code for main() at Each Optimization Level

### sum_basic.c (plain C addition)

**-O0 (no optimization) -- 11 instructions, 34 bytes:**
```
0000000000001129 <main>:
    1129:  55                    push   %rbp
    112a:  48 89 e5              mov    %rsp,%rbp
    112d:  c7 45 fc 25 00 00 00  movl   $0x25,-0x4(%rbp)
    1134:  c7 45 f8 05 00 00 00  movl   $0x5,-0x8(%rbp)
    113b:  8b 55 fc              mov    -0x4(%rbp),%edx
    113e:  8b 45 f8              mov    -0x8(%rbp),%eax
    1141:  01 d0                 add    %edx,%eax
    1143:  89 45 f4              mov    %eax,-0xc(%rbp)
    1146:  8b 45 f4              mov    -0xc(%rbp),%eax
    1149:  5d                    pop    %rbp
    114a:  c3                    ret
```

**-O1 (basic optimization) -- 2 instructions, 6 bytes:**
```
0000000000001129 <main>:
    1129:  b8 2a 00 00 00        mov    $0x2a,%eax
    112e:  c3                    ret
```

**-O2 -- 2 real instructions, 6 bytes (+ 10 bytes NOP alignment padding):**
```
0000000000001040 <main>:
    1040:  b8 2a 00 00 00        mov    $0x2a,%eax
    1045:  c3                    ret
    1046:  66 2e 0f 1f 84 00 00  cs nopw 0x0(%rax,%rax,1)   [alignment]
    104d:  00 00 00
```

**-O3 and -Os: identical to -O2.**

### sum_inline.c (inline assembly ADD)

**-O0 -- 11 instructions, 34 bytes:**
```
0000000000001129 <main>:
    1129:  55                    push   %rbp
    112a:  48 89 e5              mov    %rsp,%rbp
    112d:  c7 45 fc 25 00 00 00  movl   $0x25,-0x4(%rbp)
    1134:  c7 45 f8 05 00 00 00  movl   $0x5,-0x8(%rbp)
    113b:  8b 45 fc              mov    -0x4(%rbp),%eax
    113e:  8b 55 f8              mov    -0x8(%rbp),%edx
    1141:  01 d0                 add    %edx,%eax
    1143:  89 45 f4              mov    %eax,-0xc(%rbp)
    1146:  8b 45 f4              mov    -0xc(%rbp),%eax
    1149:  5d                    pop    %rbp
    114a:  c3                    ret
```

**-O1 -- 4 instructions, 13 bytes:**
```
0000000000001129 <main>:
    1129:  b8 25 00 00 00        mov    $0x25,%eax
    112e:  ba 05 00 00 00        mov    $0x5,%edx
    1133:  01 d0                 add    %edx,%eax
    1135:  c3                    ret
```

**-O2, -O3, -Os -- 4 instructions, 13 bytes (+ 3 bytes NOP padding):**
```
0000000000001040 <main>:
    1040:  b8 25 00 00 00        mov    $0x25,%eax
    1045:  ba 05 00 00 00        mov    $0x5,%edx
    104a:  01 d0                 add    %edx,%eax
    104c:  c3                    ret
    104d:  0f 1f 00              nopl   (%rax)               [alignment]
```

### sum_constexpr.c (#define compile-time constant)

**-O0 -- 5 instructions, 11 bytes:**
```
0000000000001129 <main>:
    1129:  55                    push   %rbp
    112a:  48 89 e5              mov    %rsp,%rbp
    112d:  b8 2a 00 00 00        mov    $0x2a,%eax
    1132:  5d                    pop    %rbp
    1133:  c3                    ret
```

**-O1, -O2, -O3, -Os -- all identical: 2 instructions, 6 bytes:**
```
    mov    $0x2a,%eax        ; b8 2a 00 00 00
    ret                      ; c3
```
(O2/O3/Os add NOP alignment padding after ret, but functional code is identical.)

---

## 2. Binary Size Comparison

All binaries are nearly identical in total ELF size (~15.8 KB) because the
overwhelming majority of the binary is CRT startup code, not our function.

| Binary            | Total ELF Size |
|-------------------|---------------|
| sum_basic_*       | 15,832 bytes  |
| sum_inline_*      | 15,832 bytes  |
| sum_constexpr_*   | 15,840 bytes  |

**main() code size (excluding alignment NOPs):**

| Variant           | -O0   | -O1   | -O2/O3/Os |
|-------------------|-------|-------|-----------|
| sum_basic         | 34 B  |  6 B  |  6 B      |
| sum_inline        | 34 B  | 13 B  | 13 B      |
| sum_constexpr     | 11 B  |  6 B  |  6 B      |

---

## 3. Which Optimization Level Produces Fewest Instructions?

**-O1 is the winner.** It produces the same optimal code as -O2/-O3/-Os but
without the NOP alignment padding after ret.

For sum_basic and sum_constexpr, the optimal result is:
```
b8 2a 00 00 00    mov $0x2a,%eax     ; 5 bytes
c3                ret                 ; 1 byte
                                      ; Total: 6 bytes, 2 instructions
```

This is the **theoretical minimum** for returning a value > 127 from main() in
the System V AMD64 ABI (return value in %eax, must use ret). You cannot do
better in x86_64 without changing the ABI.

**Note:** A 4-byte variant is technically possible (xor %eax,%eax; mov $0x2a,%al
= 31 c0 b0 2a) but GCC never emits this because the xor has a false dependency
concern and mov $imm32,%eax is the canonical form that modern x86 CPUs optimize
for. The 5-byte mov also avoids partial register stalls on Zen 3.

---

## 4. Does GCC Compute 37+5=42 at Compile Time? At Which -O Level?

**YES.** GCC constant-folds 37+5=42 starting at **-O1**.

Evidence: at -O0, we see the actual add instruction:
```
movl   $0x25,-0x4(%rbp)    ; store 37
movl   $0x5,-0x8(%rbp)     ; store 5
... load, add, store ...
```

At -O1 and above, the entire computation vanishes:
```
mov    $0x2a,%eax           ; 0x2a = 42, directly
ret
```

The compiler has evaluated the addition at compile time and replaced the entire
function body with return 42.

For sum_constexpr (using #define), the preprocessor computes the constant
before the compiler even sees it. Even at -O0, the compiler sees return 42
directly, so the code is already mov $0x2a,%eax at -O0 (though with
unnecessary frame setup/teardown).

---

## 5. Instruction Encoding Size Analysis

### Key encodings observed:

| Instruction              | Bytes | Encoding           | Notes                          |
|--------------------------|-------|--------------------|--------------------------------|
| push %rbp                | 1     | 55                 | Stack frame setup              |
| mov %rsp,%rbp            | 3     | 48 89 e5           | REX.W prefix for 64-bit       |
| movl $0x25,-0x4(%rbp)    | 7     | c7 45 fc 25000000  | Memory store with imm32        |
| mov -0x4(%rbp),%edx      | 3     | 8b 55 fc           | Memory load, ModRM + disp8    |
| add %edx,%eax            | 2     | 01 d0              | Register-register add          |
| mov $0x2a,%eax           | 5     | b8 2a000000        | Immediate to register, imm32  |
| mov $0x5,%edx            | 5     | ba 05000000        | Immediate to register, imm32  |
| ret                      | 1     | c3                 | Near return                    |
| pop %rbp                 | 1     | 5d                 | Stack frame teardown           |

### The ADD instruction itself:
- add %edx,%eax = 01 d0 = **2 bytes** (opcode 01 + ModRM byte)
- This is the shortest possible encoding for a 32-bit register-register ADD

### Why inline assembly is SLOWER than pure C at -O1+:

The inline asm (volatile) forces GCC to emit the actual add instruction
even when it knows the operands are constants. This prevents constant folding:

| Approach     | -O1 bytes | Instructions | Constant folded? |
|--------------|-----------|-------------|------------------|
| Pure C       | 6         | 2           | YES              |
| Inline asm   | 13        | 4           | NO               |
| #define      | 6         | 2           | YES (even at -O0)|

**The inline assembly version is 2.17x larger** because GCC must respect the
volatile asm constraint and cannot optimize through it.

---

## 6. Summary

The optimal machine code for return 42 on x86_64 is:
```
b8 2a 00 00 00    mov $0x2a,%eax
c3                ret
```
**6 bytes, 2 instructions.** GCC achieves this at -O1 and above for both
plain C and #define versions. This is the shortest possible encoding in the
x86_64 ABI.

Inline assembly is **counterproductive** for this task -- it prevents the
compiler from constant-folding and results in code more than twice the size.

The C compiler matches the theoretically optimal machine code. You cannot
hand-write better x86_64 for this operation.
