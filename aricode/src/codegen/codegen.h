/*
 * aricode - Ari Code Language
 * Code Generator: AST -> x86_64 Machine Code
 *
 * Takes an AST produced by the parser and generates raw x86_64 machine
 * code targeting Linux System V ABI on AMD Ryzen 7 5800X (Zen 3).
 *
 * The codegen produces a position-dependent code buffer that can be
 * wrapped in an ELF binary via elf_create().
 */

#ifndef ARICODE_CODEGEN_H
#define ARICODE_CODEGEN_H

#include "../parser/ast.h"
#include "elf.h"

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Code buffer                                                       */
/* ------------------------------------------------------------------ */

#define CODEGEN_MAX_CODE   (64 * 1024)   /* 64 KiB max code size     */
#define CODEGEN_MAX_FUNCS  256           /* max function definitions  */
#define CODEGEN_MAX_VARS   256           /* max locals per function   */

/* ELF constants shared with codegen for address calculation */
#define ARICODE_ELF_BASE   0x400000ULL  /* ELF load address           */
#define ARICODE_ELF_EHDR   64           /* ELF64 header size          */
#define ARICODE_ELF_PHDR   56           /* program header entry size  */
#define ARICODE_ELF_PHNUM  2            /* PT_LOAD + PT_GNU_STACK     */
#define ARICODE_ELF_HDR_TOTAL (ARICODE_ELF_EHDR + ARICODE_ELF_PHDR * ARICODE_ELF_PHNUM)

/* ------------------------------------------------------------------ */
/*  Symbol / variable tracking                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;      /* variable name (borrowed from AST)       */
    int32_t     rbp_off;   /* offset from RBP (negative = locals)     */
    int         is_float;  /* 1 if f64/f32, 0 if integer/bool        */
} LocalVar;

typedef struct {
    const char *name;      /* function name (borrowed from AST)       */
    size_t      code_off;  /* byte offset in code buffer              */
    int         param_cnt; /* number of parameters                    */
} FuncEntry;

/* ------------------------------------------------------------------ */
/*  Codegen state                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    /* Output code buffer */
    uint8_t     code[CODEGEN_MAX_CODE];
    size_t      code_size;

    /* Function table */
    FuncEntry   funcs[CODEGEN_MAX_FUNCS];
    size_t      func_count;

    /* Current function's local variables */
    LocalVar    locals[CODEGEN_MAX_VARS];
    size_t      local_count;
    int32_t     stack_offset;   /* current RBP offset for next local  */

    /* Patch list: locations of CALL rel32 that need fixup */
    struct {
        size_t  code_pos;      /* position of the rel32 in code[]     */
        const char *target;    /* target function name                 */
    } call_patches[1024];
    size_t      patch_count;

    /* Entry point offset (set when _start is emitted) */
    size_t      entry_offset;

    /* Current function info (for tail call optimization) */
    const char *current_fn_name;   /* name of function being compiled   */
    size_t      current_fn_entry;  /* code offset of function entry     */

    /* Error handling: try/catch frame stack */
    size_t      catch_targets[32]; /* code offsets of catch entry points */
    size_t      catch_rsp_slots[32]; /* stack slot offsets for saved RSP */
    int         catch_depth;       /* current nesting depth             */

    /* Loop break/continue targets */
    size_t      loop_start[32];    /* continue target for while loops          */
    size_t      loop_end_patches[32][16]; /* break JMP positions to patch    */
    size_t      loop_cont_patches[32][16]; /* continue JMP positions (for)  */
    int         loop_end_count[32]; /* number of break JMPs per level   */
    int         loop_cont_count[32]; /* number of continue JMPs (for)  */
    int         loop_is_for[32];   /* 1=for loop, 0=while loop           */
    int         loop_depth;

    /* Error tracking */
    int         had_error;
    char        error_msg[512];

    /* Error string dedup cache — stores code offsets of embedded error strings */
    struct {
        const char *text;     /* pointer to static error string          */
        size_t      code_pos; /* position in code buffer where embedded  */
        size_t      len;      /* length of the string                    */
    } error_strings[16];
    size_t      error_string_count;
} CodegenState;

/* ------------------------------------------------------------------ */
/*  API                                                               */
/* ------------------------------------------------------------------ */

/*
 * Initialize a codegen state.  Must be called before codegen_generate().
 */
void codegen_init(CodegenState *cg);

/*
 * Generate x86_64 machine code from an AST.
 *
 * Returns 0 on success, -1 on error (check cg->error_msg).
 * On success, the code is in cg->code[0..cg->code_size-1] and
 * cg->entry_offset points to _start.
 */
int codegen_generate(CodegenState *cg, const ASTNode *ast);

/*
 * Convenience: generate code and write an ELF binary in one step.
 * Returns 0 on success.
 */
int codegen_compile_to_file(const ASTNode *ast, const char *output_path);

#endif /* ARICODE_CODEGEN_H */
