/*
 * aricode - Ari Code Language
 * Symbol Table with Scoped Nesting
 *
 * Provides a chain of scopes (global -> function -> block -> ...).
 * Each scope contains symbols (variables, constants, functions).
 * When a scope is popped, unused-variable warnings can be emitted.
 */

#ifndef ARICODE_SYMBOL_TABLE_H
#define ARICODE_SYMBOL_TABLE_H

#include "types.h"
#include <stdbool.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Symbol                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    char       *name;           /* identifier name (owned)          */
    AriType    *type;           /* resolved type (owned)            */
    bool        is_const;       /* declared with `const`            */
    bool        is_initialized; /* has been assigned a value        */
    bool        is_used;        /* has been read at least once      */
    bool        is_function;    /* true if this is a function decl  */
    int         line;           /* declaration line                 */
    int         col;            /* declaration column               */
} Symbol;

/* ------------------------------------------------------------------ */
/*  Scope                                                             */
/* ------------------------------------------------------------------ */

#define SCOPE_INITIAL_CAP 16

typedef struct Scope Scope;

struct Scope {
    Symbol     *symbols;
    size_t      count;
    size_t      capacity;
    Scope      *parent;         /* enclosing scope (NULL for global) */
};

/* ------------------------------------------------------------------ */
/*  Symbol Table                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    Scope      *current;        /* innermost active scope            */
} SymbolTable;

/* ------------------------------------------------------------------ */
/*  API                                                               */
/* ------------------------------------------------------------------ */

/* Create a new symbol table with one global scope. */
SymbolTable *symtab_create(void);

/* Destroy the symbol table and all scopes/symbols. */
void symtab_destroy(SymbolTable *st);

/* Push a new nested scope (entering a function body, if-block, etc.). */
void symtab_push_scope(SymbolTable *st);

/* Pop the innermost scope.  Returns the scope so the caller can inspect
 * unused variables before freeing.  Caller must call scope_free(). */
Scope *symtab_pop_scope(SymbolTable *st);

/* Free a scope returned by symtab_pop_scope(). */
void scope_free(Scope *scope);

/* Define a new symbol in the current scope.
 * Returns NULL on success, or a pointer to the existing symbol if
 * a symbol with the same name already exists in the CURRENT scope
 * (duplicate declaration). */
Symbol *symtab_define(SymbolTable *st, const char *name, AriType *type,
                      bool is_const, bool is_initialized, int line, int col);

/* Define a function symbol in the current scope. */
Symbol *symtab_define_function(SymbolTable *st, const char *name,
                               AriType *fn_type, int line, int col);

/* Look up a symbol by name, searching from the current scope outward.
 * Returns NULL if not found. */
Symbol *symtab_lookup(SymbolTable *st, const char *name);

/* Look up a symbol ONLY in the current scope (for duplicate detection). */
Symbol *symtab_lookup_current(SymbolTable *st, const char *name);

/* Mark a symbol as used (called when the variable is read). */
void symtab_mark_used(SymbolTable *st, const char *name);

/* Mark a symbol as initialized (called on assignment). */
void symtab_mark_initialized(SymbolTable *st, const char *name);

#endif /* ARICODE_SYMBOL_TABLE_H */
