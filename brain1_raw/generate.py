#!/usr/bin/env python3
"""
Brain 1: Minimal raw ELF x86_64 binary that computes 37 + 5 = 42
and returns the result as the exit code.

Strategy: Overlap ELF header and program header as much as possible.
Use the smallest possible instruction sequence:
  - mov al, 60       (syscall number for exit = 60)  -- but we need full rax
  - We use:  mov edi, 37; add edi, 5; mov eax, 60; syscall
  - Even better: mov bl, 37; add bl, 5; xchg ebx, edi; mov al, 60; syscall
  - Best: push 42; pop rdi; push 60; pop rax; syscall  (10 bytes)
  - Actually best: mov edi, 42; mov eax, 60; syscall (12 bytes for the add version)

For the ADD challenge we need actual addition:
  push 37; pop rdi; add dil, 5; push 60; pop rax; syscall

But let's be precise about what's minimal for showing the add:
  mov dil, 37   -- 40 B7 25  (3 bytes, REX prefix needed for dil)
  add dil, 5    -- 40 80 C7 05 (4 bytes)
  mov al, 60    -- B0 3C (2 bytes) -- only works if upper rax is 0

Linux guarantees rax=59 after execve... not zero. So we need:
  xor eax, eax  -- 31 C0 (2 bytes, also zeros upper 32 bits)
  mov al, 60    -- B0 3C (2 bytes)

Smallest correct instruction sequence for add + exit:
  xor edi, edi   -- 31 FF (2 bytes) - zero rdi
  add dil, 37    -- 40 80 C7 25 (4 bytes) - add 37
  add dil, 5     -- 40 80 C7 05 (4 bytes) - add 5
  xor eax, eax   -- 31 C0 (2 bytes)
  mov al, 60     -- B0 3C (2 bytes)
  syscall        -- 0F 05 (2 bytes)
  Total: 16 bytes

Even better - use push/pop to avoid REX prefixes:
  push 37        -- 6A 25 (2 bytes)
  pop rdi        -- 5F (1 byte)
  add dil, 5     -- 40 80 C7 05 (4 bytes)
  push 60        -- 6A 3C (2 bytes)
  pop rax        -- 58 (1 byte)
  syscall        -- 0F 05 (2 bytes)
  Total: 12 bytes

Even better - use 32-bit operations to avoid REX:
  push 37        -- 6A 25 (2 bytes)
  pop rdi        -- 5F (1 byte)
  add edi, 5     -- 83 C7 05 (3 bytes) - no REX needed for edi!
  push 60        -- 6A 3C (2 bytes)
  pop rax        -- 58 (1 byte)
  syscall        -- 0F 05 (2 bytes)
  Total: 11 bytes

That's our winner for the computation. Now minimize the ELF container.

Minimum ELF64 header: 64 bytes
Minimum Program header (PT_LOAD): 56 bytes
Total minimum overhead: 120 bytes + 11 bytes code = 131 bytes

BUT we can overlap! The program header can start at offset 64 (right after
the ELF header), and we place code right after. With p_filesz = p_memsz
covering just what we need.

Actually, we can do even more aggressive overlapping using the classic
"tiny ELF" technique - embed the phdr inside the ELF header by starting
the phdr at a carefully chosen offset. For ELF64 the ehdr is 64 bytes
and phdr is 56 bytes. We can start the phdr at offset 8 (inside e_ident
padding bytes) but ELF64 constraints make this tricky.

Let's go with the clean non-overlapping approach first, which is already
very small: 120 + 11 = 131 bytes. But we can trim trailing zeros from
the program header.

Actually the cleanest tiny approach: put code right after both headers.
"""

import struct
import os
import stat

def build_elf():
    # Virtual address where we load (standard low address)
    VADDR = 0x400000

    # ELF header: 64 bytes for ELF64
    EHDR_SIZE = 64
    PHDR_SIZE = 56

    code_offset = EHDR_SIZE + PHDR_SIZE  # = 120

    # Machine code instructions (11 bytes):
    # push 37       6A 25
    # pop rdi       5F
    # add edi, 5    83 C7 05
    # push 60       6A 3C
    # pop rax       58
    # syscall       0F 05
    code = bytes([
        0x6A, 0x25,             # push 37
        0x5F,                   # pop rdi
        0x83, 0xC7, 0x05,       # add edi, 5
        0x6A, 0x3C,             # push 60
        0x58,                   # pop rax
        0x0F, 0x05,             # syscall
    ])

    total_size = code_offset + len(code)  # 120 + 11 = 131
    entry_point = VADDR + code_offset

    # ELF64 Header (64 bytes)
    ehdr = bytearray()
    ehdr += b'\x7fELF'          # e_ident[0..3]: magic
    ehdr += bytes([2])           # e_ident[4]: ELFCLASS64
    ehdr += bytes([1])           # e_ident[5]: ELFDATA2LSB (little-endian)
    ehdr += bytes([1])           # e_ident[6]: EV_CURRENT
    ehdr += bytes([0])           # e_ident[7]: ELFOSABI_NONE
    ehdr += bytes(8)             # e_ident[8..15]: padding
    ehdr += struct.pack('<H', 2)        # e_type: ET_EXEC
    ehdr += struct.pack('<H', 0x3E)     # e_machine: EM_X86_64
    ehdr += struct.pack('<I', 1)        # e_version: EV_CURRENT
    ehdr += struct.pack('<Q', entry_point)  # e_entry
    ehdr += struct.pack('<Q', EHDR_SIZE)    # e_phoff (phdr right after ehdr)
    ehdr += struct.pack('<Q', 0)        # e_shoff (no section headers)
    ehdr += struct.pack('<I', 0)        # e_flags
    ehdr += struct.pack('<H', EHDR_SIZE)    # e_ehsize
    ehdr += struct.pack('<H', PHDR_SIZE)    # e_phentsize
    ehdr += struct.pack('<H', 1)        # e_phnum
    ehdr += struct.pack('<H', 0)        # e_shentsize
    ehdr += struct.pack('<H', 0)        # e_shnum
    ehdr += struct.pack('<H', 0)        # e_shstrndx

    assert len(ehdr) == EHDR_SIZE, f"ELF header is {len(ehdr)} bytes, expected {EHDR_SIZE}"

    # Program Header (56 bytes) - PT_LOAD
    phdr = bytearray()
    phdr += struct.pack('<I', 1)        # p_type: PT_LOAD
    phdr += struct.pack('<I', 5)        # p_flags: PF_R | PF_X
    phdr += struct.pack('<Q', 0)        # p_offset: load from start of file
    phdr += struct.pack('<Q', VADDR)    # p_vaddr
    phdr += struct.pack('<Q', VADDR)    # p_paddr
    phdr += struct.pack('<Q', total_size)   # p_filesz
    phdr += struct.pack('<Q', total_size)   # p_memsz
    phdr += struct.pack('<Q', 0x1000)   # p_align

    assert len(phdr) == PHDR_SIZE, f"Program header is {len(phdr)} bytes, expected {PHDR_SIZE}"

    # Assemble complete binary
    binary = bytes(ehdr) + bytes(phdr) + code
    assert len(binary) == total_size

    # Write binary
    output_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sum.bin')
    with open(output_path, 'wb') as f:
        f.write(binary)

    # Make executable
    os.chmod(output_path, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)

    print(f"Generated: {output_path}")
    print(f"Total size: {len(binary)} bytes")
    print(f"  ELF header:      {EHDR_SIZE} bytes")
    print(f"  Program header:   {PHDR_SIZE} bytes")
    print(f"  Machine code:     {len(code)} bytes")
    print(f"Entry point:  0x{entry_point:x}")
    print()
    print("Machine code disassembly:")
    print(f"  6A 25       push 37")
    print(f"  5F          pop rdi")
    print(f"  83 C7 05    add edi, 5")
    print(f"  6A 3C       push 60")
    print(f"  58          pop rax")
    print(f"  0F 05       syscall")
    print(f"  --- {len(code)} bytes, 6 instructions ---")

if __name__ == '__main__':
    build_elf()
