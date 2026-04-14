/*
 * aricode - Ari Code Language
 * Minimal ELF64 Binary Generator
 *
 * Layout:
 *   [ELF64 Header]  (64 bytes)
 *   [Program Header] (56 bytes)  -- single PT_LOAD
 *   [Code]           (N bytes)
 *
 * Total header overhead: 120 bytes.
 * The code is loaded at virtual address 0x400000 + 120.
 */

#include "elf.h"
#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/*  ELF64 constants (derived from codegen.h shared definitions)       */
/* ------------------------------------------------------------------ */

#define ELF_EHDR_SIZE  ARICODE_ELF_EHDR
#define ELF_PHDR_SIZE  ARICODE_ELF_PHDR
#define ELF_PHDR_COUNT ARICODE_ELF_PHNUM
#define ELF_HDR_TOTAL  ARICODE_ELF_HDR_TOTAL

#define LOAD_ADDR      ARICODE_ELF_BASE

/* ELF identification */
#define ELFMAG0        0x7F
#define ELFCLASS64     2
#define ELFDATA2LSB    1
#define EV_CURRENT     1
#define ELFOSABI_NONE  0
#define ET_EXEC        2
#define EM_X86_64      62
#define PT_LOAD        1
#define PT_GNU_STACK   0x6474E551
#define PF_X           1
#define PF_R           4
#define PF_W           2

/* ------------------------------------------------------------------ */
/*  Build the binary                                                  */
/* ------------------------------------------------------------------ */

ElfBinary *elf_create(const uint8_t *code_buf, size_t code_size,
                      size_t entry_offset) {
    size_t total = ELF_HDR_TOTAL + code_size;

    uint8_t *buf = calloc(1, total);
    if (!buf) {
        fprintf(stderr, "aricode/elf: out of memory\n");
        return NULL;
    }

    /* ---- ELF64 Header (64 bytes) ---- */
    uint8_t *e = buf;

    /* e_ident[16] */
    e[0] = ELFMAG0; e[1] = 'E'; e[2] = 'L'; e[3] = 'F';
    e[4] = ELFCLASS64;     /* 64-bit */
    e[5] = ELFDATA2LSB;    /* little-endian */
    e[6] = EV_CURRENT;     /* ELF version */
    e[7] = ELFOSABI_NONE;  /* System V ABI */
    /* e_ident[8..15] = 0 (padding) */

    /* e_type = ET_EXEC */
    uint16_t v16;
    v16 = ET_EXEC;        memcpy(e + 16, &v16, 2);
    /* e_machine = EM_X86_64 */
    v16 = EM_X86_64;      memcpy(e + 18, &v16, 2);
    /* e_version = EV_CURRENT */
    uint32_t v32 = EV_CURRENT;
    memcpy(e + 20, &v32, 4);

    /* e_entry: virtual address of _start */
    uint64_t v64 = LOAD_ADDR + ELF_HDR_TOTAL + entry_offset;
    memcpy(e + 24, &v64, 8);

    /* e_phoff: program header offset (immediately after ELF header) */
    v64 = ELF_EHDR_SIZE;
    memcpy(e + 32, &v64, 8);

    /* e_shoff = 0 (no section headers) */
    v64 = 0;
    memcpy(e + 40, &v64, 8);

    /* e_flags = 0 */
    v32 = 0;
    memcpy(e + 48, &v32, 4);

    /* e_ehsize */
    v16 = ELF_EHDR_SIZE;  memcpy(e + 52, &v16, 2);
    /* e_phentsize */
    v16 = ELF_PHDR_SIZE;  memcpy(e + 54, &v16, 2);
    /* e_phnum = 2 (PT_LOAD + PT_GNU_STACK) */
    v16 = ELF_PHDR_COUNT; memcpy(e + 56, &v16, 2);
    /* e_shentsize = 0 */
    v16 = 0;              memcpy(e + 58, &v16, 2);
    /* e_shnum = 0 */
    v16 = 0;              memcpy(e + 60, &v16, 2);
    /* e_shstrndx = 0 */
    v16 = 0;              memcpy(e + 62, &v16, 2);

    /* ---- Program Header (56 bytes) ---- */
    uint8_t *p = buf + ELF_EHDR_SIZE;

    /* p_type = PT_LOAD */
    v32 = PT_LOAD;        memcpy(p + 0, &v32, 4);
    /* p_flags = PF_R | PF_X */
    v32 = PF_R | PF_X;    memcpy(p + 4, &v32, 4);
    /* p_offset = 0 (load from start of file) */
    v64 = 0;              memcpy(p + 8, &v64, 8);
    /* p_vaddr */
    v64 = LOAD_ADDR;      memcpy(p + 16, &v64, 8);
    /* p_paddr (same) */
    v64 = LOAD_ADDR;      memcpy(p + 24, &v64, 8);
    /* p_filesz = total file size */
    v64 = total;           memcpy(p + 32, &v64, 8);
    /* p_memsz = same (no BSS) */
    v64 = total;           memcpy(p + 40, &v64, 8);
    /* p_align = 0x1000 (page alignment) */
    v64 = 0x1000;          memcpy(p + 48, &v64, 8);

    /* ---- PT_GNU_STACK Program Header (56 bytes) ----
     * Marks the stack as non-executable (NX).
     * Without this, the kernel may allow code execution on the stack,
     * enabling shellcode injection attacks. */
    uint8_t *p2 = buf + ELF_EHDR_SIZE + ELF_PHDR_SIZE;

    /* p_type = PT_GNU_STACK */
    v32 = PT_GNU_STACK;   memcpy(p2 + 0, &v32, 4);
    /* p_flags = PF_R | PF_W (read+write, NO execute) */
    v32 = PF_R | PF_W;   memcpy(p2 + 4, &v32, 4);
    /* All other fields = 0 (offset, vaddr, paddr, filesz, memsz, align) */
    /* Already zeroed by calloc */

    /* ---- Code ---- */
    memcpy(buf + ELF_HDR_TOTAL, code_buf, code_size);

    /* ---- Package result ---- */
    ElfBinary *bin = malloc(sizeof(ElfBinary));
    if (!bin) {
        free(buf);
        fprintf(stderr, "aricode/elf: out of memory\n");
        return NULL;
    }
    bin->data      = buf;
    bin->size      = total;
    bin->code_size = code_size;

    return bin;
}

/* ------------------------------------------------------------------ */
/*  Write to disk                                                     */
/* ------------------------------------------------------------------ */

int elf_write(const char *filename, const ElfBinary *bin) {
    if (!bin || !bin->data) return -1;

    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "aricode/elf: cannot open '%s' for writing\n", filename);
        return -1;
    }

    size_t written = fwrite(bin->data, 1, bin->size, f);
    fclose(f);

    if (written != bin->size) {
        fprintf(stderr, "aricode/elf: short write to '%s'\n", filename);
        return -1;
    }

    /* chmod +x */
    if (chmod(filename, 0755) != 0) {
        fprintf(stderr, "aricode/elf: chmod failed on '%s'\n", filename);
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Cleanup                                                           */
/* ------------------------------------------------------------------ */

void elf_free(ElfBinary *bin) {
    if (!bin) return;
    free(bin->data);
    free(bin);
}
