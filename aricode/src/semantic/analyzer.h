/*
 * aricode - Ari Code Language
 * Semantic Analyzer
 *
 * The GUARDIAN of aricode.  Walks the AST produced by the parser and
 * enforces the language's error philosophy:
 *
 *   Level 0 (SILENT)  -> BLOCKS compilation.  Code that could silently
 *                         fail is FORBIDDEN.
 *   Level 1 (LOGIC)   -> Compile-time detectable programming errors.
 *   Level 2 (WARNING) -> Suspicious but not provably wrong code.
 *
 * No bad code ever becomes a binary.
 */

#ifndef ARICODE_ANALYZER_H
#define ARICODE_ANALYZER_H

#include "symbol_table.h"
#include "types.h"
#include "../parser/ast.h"
#include "../errors/error_levels.h"
#include "../errors/error_codes.h"

#include <stdbool.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Semantic error list                                                */
/* ------------------------------------------------------------------ */

#define ANALYZER_MAX_ERRORS 256

typedef struct {
    AriErrorLevel level;
    const char   *code;        /* e.g. "ARI-S001" (static string)       */
    char          message[512];/* formatted message                     */
    const char   *suggestion;  /* how to fix (static string)            */
    int           line;
    int           col;
} SemanticError;

/* ------------------------------------------------------------------ */
/*  Analyzer state                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    ASTNode        *root;           /* AST root (not owned)              */
    SymbolTable    *symbols;        /* scoped symbol table               */

    SemanticError   errors[ANALYZER_MAX_ERRORS];
    size_t          error_count;

    /* Tracking state */
    AriType        *current_fn_return_type;  /* return type of current fn */
    bool            in_function;             /* inside a function body?   */
    bool            after_return;            /* unreachable code detector */

    /* Counts per level for quick summary */
    size_t          level0_count;   /* SILENT  - blocks compilation      */
    size_t          level1_count;   /* LOGIC   - programming errors      */
    size_t          level2_count;   /* WARNING - suspicious code         */
} Analyzer;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/* Initialize the analyzer with an AST root node. */
void analyzer_init(Analyzer *a, ASTNode *root);

/* Run the full semantic analysis pass.
 * Returns true if compilation should proceed (no Level 0/1 errors). */
bool analyzer_analyze(Analyzer *a);

/* Returns true if there are any errors that block compilation
 * (Level 0 SILENT or Level 1 LOGIC). */
bool analyzer_has_blocking_errors(const Analyzer *a);

/* Returns true if there are any errors at all (including warnings). */
bool analyzer_has_errors(const Analyzer *a);

/* Print all collected errors to stderr in a nicely formatted way. */
void analyzer_print_errors(const Analyzer *a);

/* Clean up analyzer resources (symbol table, etc.). */
void analyzer_destroy(Analyzer *a);

#endif /* ARICODE_ANALYZER_H */
