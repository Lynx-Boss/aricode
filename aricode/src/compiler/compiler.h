/*
 * ARICODE Compiler Pipeline
 * =========================
 * Connects: Lexer -> Parser -> (Semantic Analysis) -> (CodeGen) -> Output
 *
 * Central orchestration for the aricode compilation process.
 */

#ifndef ARICODE_COMPILER_H
#define ARICODE_COMPILER_H

#include <stdbool.h>

/* ── Compiler Options ────────────────────────────────────────────────── */

typedef struct {
    const char *input_file;
    const char *output_file;
    bool        show_ast;
    bool        show_tokens;
    bool        verbose;
    bool        use_avx2;       /* --avx2: enable AVX2 SIMD (4x i64) */
} CompilerOptions;

/* ── Compiler State ──────────────────────────────────────────────────── */

typedef struct {
    CompilerOptions options;
    int             error_count;
    int             warning_count;
    int             fn_count;
} Compiler;

/* ── Public API ──────────────────────────────────────────────────────── */

/**
 * Initialize the compiler with the given options.
 */
void compiler_init(Compiler *c, CompilerOptions options);

/**
 * Compile a .ari source file. Returns 0 on success, non-zero on failure.
 */
int compiler_compile_file(Compiler *c, const char *filename);

/**
 * Compile from a string. filename is used for error reporting.
 * Returns 0 on success, non-zero on failure.
 */
int compiler_compile_string(Compiler *c, const char *source, const char *filename);

#endif /* ARICODE_COMPILER_H */
