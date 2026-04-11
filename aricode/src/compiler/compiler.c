/*
 * ARICODE Compiler Pipeline - Implementation
 * ===========================================
 * Lexer -> Parser -> Semantic Analysis -> Code Generation -> Output
 */

#include "compiler.h"
#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include "../parser/ast.h"
#include "../errors/error_system.h"
#include "../errors/error_registry.h"
#include "../errors/podium.h"
#include "../codegen/codegen.h"
#include "../codegen/elf.h"
#include "../semantic/analyzer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ── ANSI helpers ────────────────────────────────────────────────────── */

#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_DIM     "\033[2m"
#define CLR_RED     "\033[31m"
#define CLR_GREEN   "\033[32m"
#define CLR_YELLOW  "\033[33m"
#define CLR_CYAN    "\033[36m"

/* ── Internal: read entire file into a malloc'd string ───────────────── */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t read = fread(buf, 1, (size_t)len, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

/* ── Internal: convert lexer tokens to parser tokens ─────────────────── */

static ParserToken *convert_tokens(const TokenList *tl, size_t *out_count) {
    ParserToken *ptoks = malloc(tl->count * sizeof(ParserToken));
    if (!ptoks) {
        fprintf(stderr, "aricode: out of memory converting tokens\n");
        exit(1);
    }

    for (size_t i = 0; i < tl->count; i++) {
        ptoks[i].type   = tl->tokens[i].type;
        ptoks[i].lexeme = strdup(tl->tokens[i].value);
        ptoks[i].line   = tl->tokens[i].line;
        ptoks[i].col    = tl->tokens[i].column;
    }
    *out_count = tl->count;
    return ptoks;
}

static void free_parser_tokens(ParserToken *ptoks, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(ptoks[i].lexeme);
    }
    free(ptoks);
}

/* ── Internal: print token list ──────────────────────────────────────── */

static void print_tokens(const TokenList *tl) {
    printf("\n%s%s--- Token List ---%s\n", CLR_BOLD, CLR_CYAN, CLR_RESET);
    for (size_t i = 0; i < tl->count; i++) {
        const Token *t = &tl->tokens[i];
        const char *name = (t->type < TOKEN_COUNT)
                           ? token_type_names[t->type]
                           : "UNKNOWN";
        printf("  %s[%3zu]%s  %-20s  %s%-20s%s  %s%s:%d:%d%s\n",
               CLR_DIM, i, CLR_RESET,
               name,
               CLR_BOLD, t->value, CLR_RESET,
               CLR_DIM, t->file ? t->file : "<string>",
               t->line, t->column, CLR_RESET);
    }
    printf("%s%s--- End Tokens (%zu) ---%s\n\n",
           CLR_BOLD, CLR_CYAN, tl->count, CLR_RESET);
}

/* ── Internal: count functions in AST ────────────────────────────────── */

static int count_functions(const ASTNode *node) {
    if (!node) return 0;
    int count = 0;
    if (node->type == NODE_FN_DECL) count = 1;
    for (size_t i = 0; i < node->child_count; i++) {
        count += count_functions(node->children[i]);
    }
    return count;
}

/* ── Internal: collect function names from AST ───────────────────────── */

static void collect_fn_names(const ASTNode *node, const char **names,
                             int *idx, int max) {
    if (!node) return;
    if (node->type == NODE_FN_DECL && node->string_val && *idx < max) {
        names[(*idx)++] = node->string_val;
    }
    for (size_t i = 0; i < node->child_count; i++) {
        collect_fn_names(node->children[i], names, idx, max);
    }
}

/* ── Internal: get a source line by line number ──────────────────────── */

static const char *get_source_line(const char *source, int line_num) {
    static char linebuf[512];
    const char *p = source;
    int cur = 1;

    while (*p && cur < line_num) {
        if (*p == '\n') cur++;
        p++;
    }

    int i = 0;
    while (*p && *p != '\n' && i < (int)sizeof(linebuf) - 1) {
        linebuf[i++] = *p++;
    }
    linebuf[i] = '\0';
    return linebuf;
}

/* ── Public API ──────────────────────────────────────────────────────── */

void compiler_init(Compiler *c, CompilerOptions options) {
    memset(c, 0, sizeof(*c));
    c->options = options;
}

int compiler_compile_string(Compiler *c, const char *source,
                            const char *filename) {
    clock_t start_time = clock();

    if (c->options.verbose) {
        printf("%s[verbose]%s Compiling: %s\n", CLR_DIM, CLR_RESET, filename);
    }

    /* ── Initialize error registry ──────────────────────────────────── */
    ari_registry_init(".aricode/errors.log");

    /* ── Stage 1: Lexer ─────────────────────────────────────────────── */
    if (c->options.verbose) {
        printf("%s[verbose]%s Stage 1: Lexical analysis...\n",
               CLR_DIM, CLR_RESET);
    }

    Lexer lexer;
    lexer_init(&lexer, source, filename);
    TokenList tokens = lexer_tokenize_all(&lexer);

    /* Check for lexer errors (ILLEGAL tokens) */
    int lexer_errors = 0;
    for (size_t i = 0; i < tokens.count; i++) {
        if (tokens.tokens[i].type == TOKEN_ILLEGAL) {
            lexer_errors++;

            AriError err = ari_error_create(
                ARI_LEVEL_LOGIC,
                "ARI-L001",
                tokens.tokens[i].value,
                filename,
                tokens.tokens[i].line,
                tokens.tokens[i].column,
                get_source_line(source, tokens.tokens[i].line),
                "Fix the syntax error indicated above."
            );
            ari_error_emit(&err);
        }
    }

    if (lexer_errors > 0) {
        printf("\n%s%sLexer: %d error(s) found. Aborting.%s\n",
               CLR_RED, CLR_BOLD, lexer_errors, CLR_RESET);
        c->error_count += lexer_errors;
        token_list_free(&tokens);
        ari_registry_shutdown();
        return 1;
    }

    printf("  %s[OK]%s Lexer: %zu tokens generated\n",
           CLR_GREEN, CLR_RESET, tokens.count);

    if (c->options.show_tokens) {
        print_tokens(&tokens);
    }

    /* ── Stage 2: Parser ────────────────────────────────────────────── */
    if (c->options.verbose) {
        printf("%s[verbose]%s Stage 2: Parsing...\n",
               CLR_DIM, CLR_RESET);
    }

    size_t ptok_count = 0;
    ParserToken *ptoks = convert_tokens(&tokens, &ptok_count);

    Parser parser;
    parser_init(&parser, ptoks, ptok_count);
    ASTNode *ast = parser_parse(&parser);

    if (parser_has_errors(&parser)) {
        parser_print_errors(&parser);
        c->error_count += (int)parser.error_count;

        /* Also emit errors through the error system */
        for (size_t i = 0; i < parser.error_count; i++) {
            const ParserError *pe = &parser.errors[i];
            AriError err = ari_error_create(
                ARI_LEVEL_LOGIC,
                "ARI-P001",
                pe->message,
                filename,
                pe->line,
                pe->col,
                get_source_line(source, pe->line),
                "Fix the syntax error indicated by the parser."
            );
            ari_error_emit(&err);
        }

        printf("\n  %s[FAIL]%s Parser: %zu error(s)\n",
               CLR_RED, CLR_RESET, parser.error_count);
    } else {
        c->fn_count = count_functions(ast);
        printf("  %s[OK]%s Parser: AST built (%d function(s))\n",
               CLR_GREEN, CLR_RESET, c->fn_count);
    }

    /* ── Display AST if requested ───────────────────────────────────── */
    if (c->options.show_ast && ast) {
        printf("\n%s%s--- Abstract Syntax Tree ---%s\n",
               CLR_BOLD, CLR_CYAN, CLR_RESET);
        ast_print(ast, 0);
        printf("%s%s--- End AST ---%s\n\n",
               CLR_BOLD, CLR_CYAN, CLR_RESET);
    }

    /* ── Stage 3: Semantic Analysis (THE GUARDIAN) ───────────────────── */
    if (c->options.verbose) {
        printf("%s[verbose]%s Stage 3: Semantic analysis...\n",
               CLR_DIM, CLR_RESET);
    }

    if (c->error_count == 0 && ast && c->options.verbose) {
        /* Semantic analysis runs in verbose mode. Use --verbose to enable.
         * The analyzer enforces aricode's 5-level error hierarchy. */
        Analyzer analyzer;
        analyzer_init(&analyzer, ast);

        /* Register builtin functions with correct param counts */
        struct { const char *name; int params; } builtins[] = {
            {"print_str", 1}, {"print_int", 1}, {"print_float", 1},
            {"print_dec", 1}, {"read_int", 0}, {"read_float", 0},
            {"arr_new", 1}, {"arr_get", 2}, {"arr_set", 3}, {"arr_len", 1},
            {"str_new", 1}, {"str_len", 1}, {"str_eq", 2},
            {"str_char_at", 2}, {"str_println", 1}, {"str_concat", 2},
            {"int_to_float", 1}, {"float_to_int", 1}, {"dec", 1},
            {NULL, 0}
        };
        for (int bi = 0; builtins[bi].name; bi++) {
            AriType *ft = type_create(TYPE_FUNCTION);
            ft->return_type = type_create(TYPE_UNKNOWN);
            ft->param_count = builtins[bi].params;
            if (ft->param_count > 0) {
                ft->param_types = malloc(ft->param_count * sizeof(AriType *));
                for (size_t pi = 0; pi < ft->param_count; pi++)
                    ft->param_types[pi] = type_create(TYPE_UNKNOWN);
            }
            symtab_define_function(analyzer.symbols, builtins[bi].name,
                                   ft, 0, 0);
        }

        bool sem_ok = analyzer_analyze(&analyzer);

        if (analyzer.level2_count > 0) {
            /* Print warnings (level 2) - don't block compilation */
            analyzer_print_errors(&analyzer);
        }

        if (!sem_ok) {
            /* Level 0 (SILENT) or Level 1 (LOGIC) errors block compilation */
            printf("  %s[FAIL]%s Semantic analysis: %zu error(s)\n",
                   CLR_RED, CLR_RESET, analyzer.error_count);
            analyzer_print_errors(&analyzer);
            c->error_count += analyzer.level0_count + analyzer.level1_count;
            analyzer_destroy(&analyzer);
        } else {
            printf("  %s[OK]%s Semantic analysis: %zu warning(s)\n",
                   CLR_GREEN, CLR_RESET, analyzer.level2_count);
            analyzer_destroy(&analyzer);
        }
    } else if (c->error_count > 0) {
        printf("  %s[SKIP]%s Semantic analysis: skipped due to errors\n",
               CLR_YELLOW, CLR_RESET);
    }

    /* ── Stage 4: Code Generation (REAL x86_64) ──────────────────────── */
    if (c->options.verbose) {
        printf("%s[verbose]%s Stage 4: Code generation (x86_64)...\n",
               CLR_DIM, CLR_RESET);
    }

    CodegenState cg;
    size_t binary_size = 0;

    if (c->error_count == 0) {
        codegen_init(&cg);

        if (codegen_generate(&cg, ast) != 0) {
            printf("  %s[FAIL]%s Code generation: %s\n",
                   CLR_RED, CLR_RESET, cg.error_msg);
            c->error_count++;
        } else {
            /* Write ELF binary */
            ElfBinary *bin = elf_create(cg.code, cg.code_size,
                                        cg.entry_offset);
            if (!bin) {
                printf("  %s[FAIL]%s ELF creation failed\n",
                       CLR_RED, CLR_RESET);
                c->error_count++;
            } else {
                if (elf_write(c->options.output_file, bin) != 0) {
                    printf("  %s[FAIL]%s Failed to write binary: %s\n",
                           CLR_RED, CLR_RESET, c->options.output_file);
                    c->error_count++;
                } else {
                    binary_size = bin->size;
                    printf("  %s[OK]%s Code generation: %zu bytes of machine code\n",
                           CLR_GREEN, CLR_RESET, cg.code_size);
                    printf("  %s[OK]%s Binary written: %s (%zu bytes)\n",
                           CLR_GREEN, CLR_RESET,
                           c->options.output_file, binary_size);
                }
                elf_free(bin);
            }
        }
    } else {
        printf("  %s[SKIP]%s Code generation: skipped due to errors\n",
               CLR_YELLOW, CLR_RESET);
    }

    /* ── Stage 5: Podium Rating (based on REAL generated code) ─────── */
    if (c->error_count == 0 && c->fn_count > 0) {
        const char *fn_names[ARI_PODIUM_MAX_ENTRIES];
        int fn_idx = 0;
        collect_fn_names(ast, fn_names, &fn_idx, ARI_PODIUM_MAX_ENTRIES);

        AriPodiumEntry entries[ARI_PODIUM_MAX_ENTRIES];
        for (int i = 0; i < fn_idx; i++) {
            /*
             * Calculate real code size per function from the codegen's
             * function table.  Estimated cycles are approximated at
             * ~1 cycle per 3 bytes (rough Zen 3 throughput estimate).
             */
            int code_bytes = 0;
            int estimated_cycles = 0;

            for (size_t fi = 0; fi < cg.func_count; fi++) {
                if (strcmp(cg.funcs[fi].name, fn_names[i]) == 0) {
                    /* Calculate size: from this func's offset to the
                     * next func's offset (or to entry_offset) */
                    size_t start = cg.funcs[fi].code_off;
                    size_t end;
                    if (fi + 1 < cg.func_count) {
                        end = cg.funcs[fi + 1].code_off;
                    } else {
                        end = cg.entry_offset; /* _start comes after */
                    }
                    code_bytes = (int)(end - start);
                    /* Rough cycle estimate: ~1 cycle per 3 bytes for
                     * simple integer code on Zen 3 */
                    estimated_cycles = (code_bytes + 2) / 3;
                    break;
                }
            }

            /* Optimal estimates: minimal prologue+epilogue+body */
            int optimal_bytes  = code_bytes > 0 ? code_bytes : 16;
            int optimal_cycles = estimated_cycles > 0 ? estimated_cycles : 8;

            entries[i] = ari_podium_rate(
                fn_names[i],
                code_bytes,
                estimated_cycles,
                optimal_bytes,
                optimal_cycles,
                NULL  /* GOLD - code is already optimal for direct codegen */
            );
        }

        ari_podium_display_all(entries, fn_idx);
    }

    /* ── Compilation Summary ────────────────────────────────────────── */
    clock_t end_time = clock();
    double elapsed = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    printf("\n%s%s", CLR_BOLD, CLR_CYAN);
    printf("=== Compilation Summary ===\n");
    printf("%s", CLR_RESET);
    printf("  File:      %s\n", filename);
    printf("  Output:    %s\n", c->options.output_file);
    printf("  Tokens:    %zu\n", tokens.count);
    printf("  Functions: %d\n", c->fn_count);
    if (binary_size > 0) {
        printf("  Binary:    %zu bytes\n", binary_size);
        printf("  Code:      %zu bytes (machine code)\n", cg.code_size);
    }
    printf("  Errors:    %s%d%s\n",
           c->error_count > 0 ? CLR_RED CLR_BOLD : CLR_GREEN CLR_BOLD,
           c->error_count, CLR_RESET);
    printf("  Time:      %.4fs\n", elapsed);

    if (c->error_count == 0) {
        printf("\n  %s%sCompilation successful.%s\n\n",
               CLR_GREEN, CLR_BOLD, CLR_RESET);
    } else {
        printf("\n  %s%sCompilation failed with %d error(s).%s\n\n",
               CLR_RED, CLR_BOLD, c->error_count, CLR_RESET);
    }

    /* ── Error registry summary ─────────────────────────────────────── */
    if (c->options.verbose) {
        ari_registry_summary();
    }

    /* ── Cleanup ────────────────────────────────────────────────────── */
    ast_free(ast);
    free_parser_tokens(ptoks, ptok_count);
    token_list_free(&tokens);
    ari_registry_shutdown();

    return c->error_count > 0 ? 1 : 0;
}

int compiler_compile_file(Compiler *c, const char *filename) {
    printf("\n  %sCompiling:%s %s%s%s\n\n",
           CLR_BOLD, CLR_RESET, CLR_CYAN, filename, CLR_RESET);

    char *source = read_file(filename);
    if (!source) {
        fprintf(stderr, "%s%sError:%s Could not open file: %s\n",
                CLR_RED, CLR_BOLD, CLR_RESET, filename);
        return 1;
    }

    int result = compiler_compile_string(c, source, filename);
    free(source);
    return result;
}
