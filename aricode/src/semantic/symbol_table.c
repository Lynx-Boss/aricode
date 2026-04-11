/*
 * aricode - Ari Code Language
 * Symbol Table Implementation
 */

#include "symbol_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

static Scope *scope_create(Scope *parent) {
    Scope *s = calloc(1, sizeof(Scope));
    if (!s) {
        fprintf(stderr, "aricode: out of memory allocating scope\n");
        exit(1);
    }
    s->capacity = SCOPE_INITIAL_CAP;
    s->symbols  = calloc(s->capacity, sizeof(Symbol));
    if (!s->symbols) {
        fprintf(stderr, "aricode: out of memory allocating symbols\n");
        exit(1);
    }
    s->parent = parent;
    return s;
}

void scope_free(Scope *scope) {
    if (!scope) return;
    for (size_t i = 0; i < scope->count; i++) {
        free(scope->symbols[i].name);
        type_free(scope->symbols[i].type);
    }
    free(scope->symbols);
    free(scope);
}

static Symbol *scope_lookup(Scope *scope, const char *name) {
    if (!scope || !name) return NULL;
    for (size_t i = 0; i < scope->count; i++) {
        if (strcmp(scope->symbols[i].name, name) == 0)
            return &scope->symbols[i];
    }
    return NULL;
}

static Symbol *scope_define(Scope *scope, const char *name, AriType *type,
                            bool is_const, bool is_initialized,
                            bool is_function, int line, int col) {
    /* Check for duplicate in this scope */
    Symbol *existing = scope_lookup(scope, name);
    if (existing) return existing;

    /* Grow if needed */
    if (scope->count == scope->capacity) {
        size_t new_cap = scope->capacity * 2;
        Symbol *tmp = realloc(scope->symbols, new_cap * sizeof(Symbol));
        if (!tmp) {
            fprintf(stderr, "aricode: out of memory growing symbol table\n");
            exit(1);
        }
        scope->symbols  = tmp;
        scope->capacity = new_cap;
    }

    Symbol *sym = &scope->symbols[scope->count++];
    memset(sym, 0, sizeof(Symbol));
    sym->name           = strdup(name);
    sym->type           = type;  /* takes ownership */
    sym->is_const       = is_const;
    sym->is_initialized = is_initialized;
    sym->is_used        = false;
    sym->is_function    = is_function;
    sym->line           = line;
    sym->col            = col;

    return NULL;  /* success: no duplicate */
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

SymbolTable *symtab_create(void) {
    SymbolTable *st = calloc(1, sizeof(SymbolTable));
    if (!st) {
        fprintf(stderr, "aricode: out of memory allocating symbol table\n");
        exit(1);
    }
    st->current = scope_create(NULL);  /* global scope */
    return st;
}

void symtab_destroy(SymbolTable *st) {
    if (!st) return;
    while (st->current) {
        Scope *parent = st->current->parent;
        scope_free(st->current);
        st->current = parent;
    }
    free(st);
}

void symtab_push_scope(SymbolTable *st) {
    st->current = scope_create(st->current);
}

Scope *symtab_pop_scope(SymbolTable *st) {
    if (!st->current) return NULL;
    Scope *popped = st->current;
    st->current = popped->parent;
    popped->parent = NULL;  /* detach */
    return popped;
}

Symbol *symtab_define(SymbolTable *st, const char *name, AriType *type,
                      bool is_const, bool is_initialized, int line, int col) {
    return scope_define(st->current, name, type, is_const, is_initialized,
                        false, line, col);
}

Symbol *symtab_define_function(SymbolTable *st, const char *name,
                               AriType *fn_type, int line, int col) {
    return scope_define(st->current, name, fn_type, true, true,
                        true, line, col);
}

Symbol *symtab_lookup(SymbolTable *st, const char *name) {
    for (Scope *s = st->current; s; s = s->parent) {
        Symbol *sym = scope_lookup(s, name);
        if (sym) return sym;
    }
    return NULL;
}

Symbol *symtab_lookup_current(SymbolTable *st, const char *name) {
    return scope_lookup(st->current, name);
}

void symtab_mark_used(SymbolTable *st, const char *name) {
    Symbol *sym = symtab_lookup(st, name);
    if (sym) sym->is_used = true;
}

void symtab_mark_initialized(SymbolTable *st, const char *name) {
    Symbol *sym = symtab_lookup(st, name);
    if (sym) sym->is_initialized = true;
}
