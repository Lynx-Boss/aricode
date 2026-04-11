/*
 * aricode - Ari Code Language
 * Minimal ELF64 Binary Generator
 *
 * Produces a statically-linked, position-dependent ELF binary
 * with a single PT_LOAD segment.  No sections, no dynamic linking,
 * no symbol table -- absolute minimum for a runnable Linux binary.
 *
 * Target: x86_64 Linux.
 */

#ifndef ARICODE_ELF_H
#define ARICODE_ELF_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Result structure                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t    *data;       /* Complete ELF binary in memory           */
    size_t      size;       /* Total binary size                       */
    size_t      code_size;  /* Size of the code segment alone          */
} ElfBinary;

/* ------------------------------------------------------------------ */
/*  API                                                               */
/* ------------------------------------------------------------------ */

/*
 * Build a complete ELF64 binary from raw machine code.
 *
 *   code_buf      -- pointer to x86_64 machine code bytes
 *   code_size     -- number of code bytes
 *   entry_offset  -- byte offset within code_buf of the entry point (_start)
 *
 * Returns a heap-allocated ElfBinary.  Caller owns the memory.
 */
ElfBinary *elf_create(const uint8_t *code_buf, size_t code_size,
                      size_t entry_offset);

/*
 * Write an ElfBinary to disk and make it executable (chmod +x).
 * Returns 0 on success, -1 on failure (with message to stderr).
 */
int elf_write(const char *filename, const ElfBinary *bin);

/*
 * Free an ElfBinary.
 */
void elf_free(ElfBinary *bin);

#endif /* ARICODE_ELF_H */
