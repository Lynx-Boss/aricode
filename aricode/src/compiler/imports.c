/*
 * aricode - Import Resolution
 *
 * Handles multi-file compilation:
 *   import "file.ari";           — plain import (concatenation)
 *   import "file.ari" as ns;     — namespaced import (ns.func() → ns__func())
 *
 * Strategy: text-level preprocessing before lexing/parsing.
 * Imported file content is prepended, namespace prefixes are applied
 * via string replacement, and ns.func() calls are rewritten to ns__func().
 */

#include "imports.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Helper: extract function names from source                        */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/*  Helper: whole-word identifier replacement                         */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/*  Helper: apply namespace prefix to imported functions               */
/* ------------------------------------------------------------------ */

static char *namespace_imported_source(const char *content, const char *ns) {
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

/* ------------------------------------------------------------------ */
/*  Helper: rewrite ns.func() calls to ns__func()                     */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/*  Namespace tracking                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    char ns[128];
} NsEntry;

/* ------------------------------------------------------------------ */
/*  Import cycle detection                                             */
/* ------------------------------------------------------------------ */

#define MAX_IMPORT_DEPTH 64

static char import_stack[MAX_IMPORT_DEPTH][768];
static size_t import_depth = 0;

static int import_stack_contains(const char *path) {
    for (size_t i = 0; i < import_depth; i++) {
        if (strcmp(import_stack[i], path) == 0) return 1;
    }
    return 0;
}

static void import_stack_push(const char *path) {
    if (import_depth < MAX_IMPORT_DEPTH) {
        strncpy(import_stack[import_depth], path, 767);
        import_stack[import_depth][767] = '\0';
        import_depth++;
    }
}

static void import_stack_pop(void) {
    if (import_depth > 0) import_depth--;
}

/* ------------------------------------------------------------------ */
/*  Public: resolve all imports                                        */
/* ------------------------------------------------------------------ */

char *resolve_imports(const char *source, const char *base_path) {
    /* Reset and push current file to import stack (for cycle detection) */
    if (base_path && import_depth == 0) {
        import_depth = 0; /* reset for new compilation */
        import_stack_push(base_path);
    }

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

                    /* Check for circular imports */
                    if (import_stack_contains(full_path)) {
                        fprintf(stderr, "\033[31mImport error:\033[0m circular import detected: '%s'\n", full_path);
                        /* Skip this import line */
                        p = after_quote;
                        while (*p == ' ' || *p == '\t') p++;
                        if (*p == ';') p++;
                        if (*p == '\n') p++;
                        continue;
                    }

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
                        import_stack_push(full_path);
                        char *resolved = resolve_imports(content, full_path);
                        import_stack_pop();
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
