/*
 * aricode - Ari Code Language
 * Type System
 *
 * Defines the type representation used throughout semantic analysis.
 * Every value in aricode has a concrete type; implicit conversions
 * that could lose data are FORBIDDEN (ARI-S006).
 */

#ifndef ARICODE_TYPES_H
#define ARICODE_TYPES_H

#include <stdbool.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Type kinds                                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    TYPE_UNKNOWN = 0,

    /* Integer types */
    TYPE_I8,
    TYPE_I16,
    TYPE_I32,
    TYPE_I64,

    /* Unsigned integer types */
    TYPE_U8,
    TYPE_U16,
    TYPE_U32,
    TYPE_U64,

    /* Floating point */
    TYPE_F32,
    TYPE_F64,

    /* Arbitrary precision decimal (BCD-based, no IEEE 754) */
    TYPE_DEC,

    /* Other primitives */
    TYPE_STR,
    TYPE_BOOL,
    TYPE_VOID,

    /* Compound types */
    TYPE_OPTION,      /* Option<T> */
    TYPE_ARRAY,       /* arr<T>    */
    TYPE_MAP,         /* map<K,V>  */
    TYPE_FUNCTION,    /* fn(A,B)->R */
    TYPE_ERROR,       /* error type */

    TYPE_KIND_COUNT
} TypeKind;

/* ------------------------------------------------------------------ */
/*  AriType - full type descriptor                                    */
/* ------------------------------------------------------------------ */

typedef struct AriType AriType;

struct AriType {
    TypeKind    kind;

    /* For compound types: inner type(s).  Heap-allocated, owned. */
    AriType    *inner;       /* Option<T>, arr<T>: the T */
    AriType    *key_type;    /* map<K,V>: the K          */

    /* For function types */
    AriType   **param_types; /* parameter types           */
    size_t      param_count;
    AriType    *return_type;  /* return type              */
};

/* ------------------------------------------------------------------ */
/*  API                                                               */
/* ------------------------------------------------------------------ */

/* Create a simple (non-compound) type. */
AriType *type_create(TypeKind kind);

/* Create an Option<inner> type. */
AriType *type_create_option(AriType *inner);

/* Create an arr<elem> type. */
AriType *type_create_array(AriType *elem);

/* Create a map<key, value> type. */
AriType *type_create_map(AriType *key, AriType *value);

/* Create a function type.  Takes ownership of param_types array and return_type. */
AriType *type_create_function(AriType **param_types, size_t param_count,
                              AriType *return_type);

/* Deep-copy a type. */
AriType *type_clone(const AriType *t);

/* Free a type and all owned sub-types. */
void type_free(AriType *t);

/* Convert a type-name string (e.g. "i32", "str", "bool") to an AriType.
 * Returns NULL if the name is not recognized. */
AriType *type_from_name(const char *name);

/* Convert an AriType to a human-readable string (for error messages).
 * Returns a static or malloc'd string; caller must free if dynamic. */
const char *type_to_string(const AriType *t);

/* Check if two types are structurally equal. */
bool types_equal(const AriType *a, const AriType *b);

/* Check if `source` can be assigned to `target` without data loss.
 * Returns true if the assignment is safe. */
bool types_can_assign(const AriType *target, const AriType *source);

/* Check if two types are compatible for binary operations. */
bool types_compatible(const AriType *a, const AriType *b);

/* Returns true if assigning source to target would lose data
 * (e.g., i64 -> i32). */
bool types_loses_data(const AriType *target, const AriType *source);

/* Returns true if the type is numeric (any int or float). */
bool type_is_numeric(const AriType *t);

/* Returns true if the type is an integer (signed or unsigned). */
bool type_is_integer(const AriType *t);

/* Returns the bit width of a numeric type (0 if not numeric). */
int type_bit_width(const AriType *t);

/* Returns true if the type is signed. */
bool type_is_signed(const AriType *t);

#endif /* ARICODE_TYPES_H */
