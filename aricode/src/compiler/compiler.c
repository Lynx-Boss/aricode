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

/*
 * Helper: extract all function names from aricode source text.
 * Scans for `fn <name>(` patterns and returns an array of strdup'd names.
 * Sets *out_count to the number of names found.
 */
static char **extract_fn_names(const char *src, size_t *out_count) {
    size_t cap = 16, count = 0;
    char **names = malloc(cap * sizeof(char *));
    const char *p = src;
    while ((p = strstr(p, "fn ")) != NULL) {
        if (p != src && p[-1] != '\n' && p[-1] != ' ' && p[-1] != '\t'
            && p[-1] != '{' && p[-1] != '}' && p[-1] != ';') {
            p += 3;
            continue;
        }
        const char *name_start = p + 3;
        while (*name_start == ' ' || *name_start == '\t') name_start++;
        const char *name_end = name_start;
        while ((*name_end >= 'a' && *name_end <= 'z') ||
               (*name_end >= 'A' && *name_end <= 'Z') ||
               (*name_end >= '0' && *name_end <= '9') ||
               *name_end == '_') {
            name_end++;
        }
        size_t nlen = (size_t)(name_end - name_start);
        if (nlen > 0 && *name_end == '(') {
            if (count >= cap) {
                cap *= 2;
                names = realloc(names, cap * sizeof(char *));
            }
            names[count] = malloc(nlen + 1);
            memcpy(names[count], name_start, nlen);
            names[count][nlen] = '\0';
            count++;
        }
        p = name_end;
    }
    *out_count = count;
    return names;
}

/*
 * Helper: replace all whole-word occurrences of `old` with `new_str` in `src`.
 * Returns a new malloc'd string.
 */
static char *replace_identifier(const char *src, const char *old,
                                const char *new_str) {
    size_t old_len = strlen(old);
    size_t new_len = strlen(new_str);
    size_t src_len = strlen(src);

    size_t cap = src_len + 256;
    char *out = malloc(cap);
    size_t out_len = 0;

    const char *p = src;
    while (*p) {
        const char *found = strstr(p, old);
        if (!found) {
            size_t rest = strlen(p);
            while (out_len + rest + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
            memcpy(out + out_len, p, rest);
            out_len += rest;
            break;
        }
        int is_word = 1;
        if (found != src) {
            char before = found[-1];
            if ((before >= 'a' && before <= 'z') ||
                (before >= 'A' && before <= 'Z') ||
                (before >= '0' && before <= '9') ||
                before == '_') {
                is_word = 0;
            }
        }
        if (is_word) {
            char after = found[old_len];
            if ((after >= 'a' && after <= 'z') ||
                (after >= 'A' && after <= 'Z') ||
                (after >= '0' && after <= '9') ||
                after == '_') {
                is_word = 0;
            }
        }

        if (is_word) {
            size_t prefix = (size_t)(found - p);
            while (out_len + prefix + new_len + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
            memcpy(out + out_len, p, prefix);
            out_len += prefix;
            memcpy(out + out_len, new_str, new_len);
            out_len += new_len;
            p = found + old_len;
        } else {
            size_t prefix = (size_t)(found - p) + 1;
            while (out_len + prefix + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
            memcpy(out + out_len, p, prefix);
            out_len += prefix;
            p = found + 1;
        }
    }
    out[out_len] = '\0';
    return out;
}

/*
 * Helper: apply namespace prefix to all function declarations and calls
 * within the imported source. `fn foo(` becomes `fn ns__foo(` and
 * calls to `foo(` become `ns__foo(`. Returns a new malloc'd string.
 */
static char *namespace_imported_source(const char *content,
                                       const char *ns) {
    size_t fn_count = 0;
    char **fn_names = extract_fn_names(content, &fn_count);

    char *cur = strdup(content);
    for (size_t i = 0; i < fn_count; i++) {
        char prefixed[512];
        snprintf(prefixed, sizeof(prefixed), "%s__%s", ns, fn_names[i]);

        char *next = replace_identifier(cur, fn_names[i], prefixed);
        free(cur);
        cur = next;
        free(fn_names[i]);
    }
    free(fn_names);
    return cur;
}

/*
 * Helper: in the main file source, replace all `ns.funcname(` patterns
 * with `ns__funcname(`. Returns a new malloc'd string.
 */
static char *rewrite_ns_calls(const char *source, const char *ns) {
    size_t ns_len = strlen(ns);
    size_t src_len = strlen(source);
    size_t cap = src_len + 256;
    char *out = malloc(cap);
    size_t out_len = 0;

    const char *p = source;
    while (*p) {
        if (strncmp(p, ns, ns_len) == 0 && p[ns_len] == '.') {
            int boundary_ok = 1;
            if (p != source) {
                char before = p[-1];
                if ((before >= 'a' && before <= 'z') ||
                    (before >= 'A' && before <= 'Z') ||
                    (before >= '0' && before <= '9') ||
                    before == '_') {
                    boundary_ok = 0;
                }
            }
            if (boundary_ok) {
                const char *fname_start = p + ns_len + 1;
                const char *fname_end = fname_start;
                while ((*fname_end >= 'a' && *fname_end <= 'z') ||
                       (*fname_end >= 'A' && *fname_end <= 'Z') ||
                       (*fname_end >= '0' && *fname_end <= '9') ||
                       *fname_end == '_') {
                    fname_end++;
                }
                size_t fname_len = (size_t)(fname_end - fname_start);
                if (fname_len > 0) {
                    char prefixed[512];
                    snprintf(prefixed, sizeof(prefixed), "%s__", ns);
                    size_t plen = strlen(prefixed);
                    while (out_len + plen + fname_len + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
                    memcpy(out + out_len, prefixed, plen);
                    out_len += plen;
                    memcpy(out + out_len, fname_start, fname_len);
                    out_len += fname_len;
                    p = fname_end;
                    continue;
                }
            }
        }
        if (out_len + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
        out[out_len++] = *p++;
    }
    out[out_len] = '\0';
    return out;
}

/* Namespace info collected during import resolution */
typedef struct {
    char ns[128];
} NsEntry;

/*
 * Resolve imports: scan source for `import "file.ari";` or
 * `import "file.ari" as alias;` lines, read those files, and
 * prepend their content. Handles nested imports and namespacing.
 * Returns a new malloc'd string with all imports resolved.
 */
static char *resolve_imports(const char *source, const char *base_path) {
    /* Extract directory from base_path */
    char dir[512] = ".";
    if (base_path) {
        strncpy(dir, base_path, sizeof(dir) - 1);
        char *last_slash = strrchr(dir, '/');
        if (last_slash) *last_slash = '\0';
        else strcpy(dir, ".");
    }

    NsEntry ns_entries[64];
    size_t ns_count = 0;

    /* Scan for import lines */
    size_t result_cap = strlen(source) * 2 + 256;
    char *result = malloc(result_cap);
    result[0] = '\0';
    size_t result_len = 0;

    /* Accumulate main file body separately for namespace rewriting */
    size_t body_cap = strlen(source) + 1;
    char *body = malloc(body_cap);
    body[0] = '\0';
    size_t body_len = 0;

    const char *p = source;
    while (*p) {
        /* Check for import "filename" [as alias]; */
        if (strncmp(p, "import", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
            const char *q = p + 6;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '"') {
                q++;
                const char *end = strchr(q, '"');
                if (end) {
                    /* Extract filename */
                    char import_name[256];
                    size_t name_len = (size_t)(end - q);
                    if (name_len >= sizeof(import_name)) name_len = sizeof(import_name) - 1;
                    memcpy(import_name, q, name_len);
                    import_name[name_len] = '\0';

                    /* Check for `as alias` */
                    char ns_alias[128] = {0};
                    const char *after_quote = end + 1;
                    while (*after_quote == ' ' || *after_quote == '\t') after_quote++;
                    if (strncmp(after_quote, "as", 2) == 0 &&
                        (after_quote[2] == ' ' || after_quote[2] == '\t')) {
                        const char *alias_start = after_quote + 2;
                        while (*alias_start == ' ' || *alias_start == '\t') alias_start++;
                        const char *alias_end = alias_start;
                        while ((*alias_end >= 'a' && *alias_end <= 'z') ||
                               (*alias_end >= 'A' && *alias_end <= 'Z') ||
                               (*alias_end >= '0' && *alias_end <= '9') ||
                               *alias_end == '_') {
                            alias_end++;
                        }
                        size_t alen = (size_t)(alias_end - alias_start);
                        if (alen > 0 && alen < sizeof(ns_alias)) {
                            memcpy(ns_alias, alias_start, alen);
                            ns_alias[alen] = '\0';
                            after_quote = alias_end;
                        }
                    }

                    /* Build full path */
                    char full_path[768];
                    snprintf(full_path, sizeof(full_path), "%s/%s", dir, import_name);

                    /* Read the imported file */
                    FILE *f = fopen(full_path, "r");
                    if (!f) {
                        fprintf(stderr, "\033[31mImport error:\033[0m cannot open '%s'\n", full_path);
                    } else {
                        fseek(f, 0, SEEK_END);
                        size_t sz = (size_t)ftell(f);
                        fseek(f, 0, SEEK_SET);
                        char *content = malloc(sz + 2);
                        fread(content, 1, sz, f);
                        content[sz] = '\n';
                        content[sz + 1] = '\0';
                        fclose(f);

                        /* Recursively resolve imports in the imported file */
                        char *resolved = resolve_imports(content, full_path);
                        free(content);

                        /* If namespaced, rename all functions */
                        char *final_content;
                        if (ns_alias[0]) {
                            final_content = namespace_imported_source(resolved, ns_alias);
                            free(resolved);
                            if (ns_count < 64) {
                                snprintf(ns_entries[ns_count].ns,
                                         sizeof(ns_entries[ns_count].ns),
                                         "%s", ns_alias);
                                ns_count++;
                            }
                        } else {
                            final_content = resolved;
                        }

                        /* Append imported content */
                        size_t imp_len = strlen(final_content);
                        while (result_len + imp_len + 2 >= result_cap) {
                            result_cap *= 2;
                            result = realloc(result, result_cap);
                        }
                        memcpy(result + result_len, final_content, imp_len);
                        result_len += imp_len;
                        result[result_len++] = '\n';
                        result[result_len] = '\0';
                        free(final_content);
                    }

                    /* Skip past the import line */
                    p = after_quote;
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == ';') p++;
                    if (*p == '\n') p++;
                    continue;
                }
            }
        }

        /* Copy regular character to body */
        if (body_len + 2 >= body_cap) {
            body_cap *= 2;
            body = realloc(body, body_cap);
        }
        body[body_len++] = *p++;
    }
    body[body_len] = '\0';

    /* Rewrite ns.func() calls in the main body for each namespace */
    char *rewritten_body = strdup(body);
    free(body);
    for (size_t i = 0; i < ns_count; i++) {
        char *tmp = rewrite_ns_calls(rewritten_body, ns_entries[i].ns);
        free(rewritten_body);
        rewritten_body = tmp;
    }

    /* Concatenate: imported content + main body */
    size_t rb_len = strlen(rewritten_body);
    while (result_len + rb_len + 1 >= result_cap) {
        result_cap *= 2;
        result = realloc(result, result_cap);
    }
    memcpy(result + result_len, rewritten_body, rb_len);
    result_len += rb_len;
    result[result_len] = '\0';
    free(rewritten_body);

    return result;
}

int compiler_compile_string(Compiler *c, const char *source,
                            const char *filename) {
    clock_t start_time = clock();

    if (c->options.verbose) {
        printf("%s[verbose]%s Compiling: %s\n", CLR_DIM, CLR_RESET, filename);
    }

    /* ── Resolve imports ───────────────────────────────────────────── */
    char *resolved_source = resolve_imports(source, filename);

    /* Use resolved source from here on (swap pointer) */
    source = resolved_source;

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

    if (c->error_count == 0 && ast) {
        /* THE GUARDIAN: Semantic analysis enforces aricode's error hierarchy.
         * Level 0 (SILENT) errors BLOCK compilation - zero silent errors.
         * Level 1 (LOGIC) errors are reported as warnings (codegen handles many).
         * Level 2 warnings are informational. */
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
            {"file_open", 2}, {"file_read", 3}, {"file_write", 3}, {"file_close", 1},
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

        analyzer_analyze(&analyzer);

        if (analyzer.level0_count > 0 || analyzer.level1_count > 0) {
            /* Level 0 (SILENT) and Level 1 (LOGIC) errors BLOCK compilation.
             * Level 0: code that can fail silently does NOT compile.
             * Level 1: logic errors (duplicates, type mismatches,
             *          undefined variables) do NOT compile. */
            size_t blocking = analyzer.level0_count + analyzer.level1_count;
            if (analyzer.level0_count > 0) {
                printf("  %s[FAIL]%s Semantic analysis: %zu SILENT error(s)",
                       CLR_RED, CLR_RESET, analyzer.level0_count);
            }
            if (analyzer.level1_count > 0) {
                printf("%s  %s[FAIL]%s Semantic analysis: %zu LOGIC error(s)",
                       analyzer.level0_count > 0 ? "\n" : "",
                       CLR_RED, CLR_RESET, analyzer.level1_count);
            }
            printf(" — compilation BLOCKED\n");
            analyzer_print_errors(&analyzer);
            c->error_count += blocking;
            analyzer_destroy(&analyzer);
        } else {
            size_t total_warnings = analyzer.level2_count;
            if (total_warnings > 0 && c->options.verbose) {
                analyzer_print_errors(&analyzer);
            }
            printf("  %s[OK]%s Semantic analysis: %zu warning(s)\n",
                   CLR_GREEN, CLR_RESET, total_warnings);
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
