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

/* Max code/data buffer.  Holds the compiled .text plus any inline
 * payloads emitted by embed_file (e.g. CNN weight blobs and full
 * transformer encoders baked into a deploy binary).  1 GiB is a
 * mmap-backed virtual reservation, so the cost is address space — not
 * RSS — until weights actually populate the buffer.  Sized to fit a
 * full GPT-2-small f32 (~650 MB weights + small code) with headroom
 * for prompt/scratch staging; smaller models still pay nothing extra. */
#define CODEGEN_MAX_CODE   (1024 * 1024 * 1024)
#define CODEGEN_MAX_FUNCS  256           /* max function definitions  */
#define CODEGEN_MAX_VARS   2048          /* max locals per function   */

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
    int         hot_xmm;   /* if >=0, this f64 var is cached in that
                            * xmm reg (range xmm8..xmm15) across the
                            * function body — set when the whole
                            * function is xmm-safe (no calls that
                            * clobber xmm8+).  -1 = stack-only.    */
    int         hot_gp;    /* if >=0, this i32 var is cached in the
                            * GP register with that index (range
                            * r12..r15, which are callee-saved so
                            * they survive anywhere our xmm-safe
                            * whitelist may branch).  -1 = stack. */
    const char *struct_type; /* if not NULL, this variable is a struct
                              * (borrowed name of the struct type) */
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
    /* Output code buffer.  Heap-allocated by codegen_init() so the
     * struct itself stays small enough for stack allocation. */
    uint8_t    *code;
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

    /* Nesting depth of float binary ops currently being emitted.  Used
     * to pick a free xmm as a stash register (xmm2 + depth) so the left
     * operand can sit there during right-operand evaluation instead of
     * round-tripping through the stack.  Capped at 6 levels. */
    int         float_depth;

    /* Register-allocated f64 locals live in xmm8..xmm15.  `next_hot_xmm`
     * is the next free slot while emitting the current function body; it
     * resets to 8 at function entry, and gets to 16 if we run out. */
    int         next_hot_xmm;

    /* Register-allocated i32 locals live in r12..r15 (callee-saved so
     * they survive the whitelist of xmm-safe builtins — none of
     * which touch the upper integer regs).  Reset to 12 at function
     * entry in xmm-safe mode, else 16 (disabled). */
    int         next_hot_gp;

    /* Non-zero iff the current function is in xmm-safe mode.  Used by
     * emit_function / emit_return to decide whether to save r12..r15
     * (four pushes/pops around the body). */
    int         in_xmm_safe_fn;

    /* Error tracking */
    int         had_error;
    char        error_msg[512];

    /* Feature flags */
    int         use_avx2;          /* 1 = emit AVX2 (256-bit) instructions */
    int         precision;         /* 6=fast(5 terms), 8=default(7), 15=strict(10) */

    /* Runtime-error handler dedup cache.  Per unique error message we
     * emit the full handler block (embedded string, write to stderr,
     * try/catch unwind, exit) exactly once — the first time a caller
     * hits it.  Subsequent callers get a 5-byte `jmp rel32` to the
     * cached handler entry point, saving ~35 bytes per repeat.  Big
     * wins on programs with many integer divisions. */
    struct {
        const char *text;         /* pointer to static error string      */
        size_t      code_pos;     /* position of embedded string bytes   */
        size_t      handler_pos;  /* position of the handler entry point */
        size_t      len;
    } error_strings[16];
    size_t      error_string_count;
} CodegenState;

/* ------------------------------------------------------------------ */
/*  Macros shared with codegen modules                                */
/* ------------------------------------------------------------------ */

/* Declared in codegen.c; prints a clear message and exits.  Used by
 * the EMIT macro below as the bail-out path when the program would
 * overflow the compile-time code buffer. */
void cg_error_oom(CodegenState *cg);

/* Advance the code cursor.  Bails out fatally (via cg_error_oom) if
 * the program exceeds the compile-time code buffer — clean error
 * beats segfault for users hitting the ceiling on large programs. */
#define EMIT(cg, count) do {                                          \
    (cg)->code_size += (count);                                       \
    if ((cg)->code_size > CODEGEN_MAX_CODE) cg_error_oom(cg);         \
} while (0)
#define BUF(cg) ((cg)->code + (cg)->code_size)

/* ------------------------------------------------------------------ */
/*  Internal helpers (shared between codegen.c and codegen_builtins.c)*/
/* ------------------------------------------------------------------ */

void cg_error(CodegenState *cg, const char *fmt, ...);
int  emit_expression(CodegenState *cg, const ASTNode *node);
void emit_runtime_error(CodegenState *cg, const char *errmsg, size_t errmsg_len);
void emit_builtin_print_str(CodegenState *cg, const ASTNode *arg);
void emit_builtin_print_int(CodegenState *cg, const ASTNode *arg);
void emit_builtin_read_int(CodegenState *cg);
LocalVar *add_local(CodegenState *cg, const char *name);
LocalVar *find_local(CodegenState *cg, const char *name);

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
