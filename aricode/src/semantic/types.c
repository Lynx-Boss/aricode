/*
 * aricode - Ari Code Language
 * Type System Implementation
 */

#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Construction                                                      */
/* ------------------------------------------------------------------ */

AriType *type_create(TypeKind kind) {
    AriType *t = calloc(1, sizeof(AriType));
    if (!t) {
        fprintf(stderr, "aricode: out of memory allocating type\n");
        exit(1);
    }
    t->kind = kind;
    return t;
}

AriType *type_create_option(AriType *inner) {
    AriType *t = type_create(TYPE_OPTION);
    t->inner = inner;
    return t;
}

AriType *type_create_array(AriType *elem) {
    AriType *t = type_create(TYPE_ARRAY);
    t->inner = elem;
    return t;
}

AriType *type_create_map(AriType *key, AriType *value) {
    AriType *t = type_create(TYPE_MAP);
    t->key_type = key;
    t->inner = value;
    return t;
}

AriType *type_create_function(AriType **param_types, size_t param_count,
                              AriType *return_type) {
    AriType *t = type_create(TYPE_FUNCTION);
    t->param_types = param_types;
    t->param_count = param_count;
    t->return_type = return_type;
    return t;
}

AriType *type_clone(const AriType *t) {
    if (!t) return NULL;

    AriType *clone = type_create(t->kind);

    if (t->inner)
        clone->inner = type_clone(t->inner);
    if (t->key_type)
        clone->key_type = type_clone(t->key_type);
    if (t->return_type)
        clone->return_type = type_clone(t->return_type);

    if (t->param_count > 0 && t->param_types) {
        clone->param_types = malloc(t->param_count * sizeof(AriType *));
        if (!clone->param_types) {
            fprintf(stderr, "aricode: out of memory cloning type\n");
            exit(1);
        }
        clone->param_count = t->param_count;
        for (size_t i = 0; i < t->param_count; i++)
            clone->param_types[i] = type_clone(t->param_types[i]);
    }

    return clone;
}

void type_free(AriType *t) {
    if (!t) return;

    type_free(t->inner);
    type_free(t->key_type);
    type_free(t->return_type);

    if (t->param_types) {
        for (size_t i = 0; i < t->param_count; i++)
            type_free(t->param_types[i]);
        free(t->param_types);
    }

    free(t);
}

/* ------------------------------------------------------------------ */
/*  Name <-> Type conversion                                          */
/* ------------------------------------------------------------------ */

AriType *type_from_name(const char *name) {
    if (!name) return NULL;

    /* Primitive types */
    if (strcmp(name, "i8")   == 0) return type_create(TYPE_I8);
    if (strcmp(name, "i16")  == 0) return type_create(TYPE_I16);
    if (strcmp(name, "i32")  == 0) return type_create(TYPE_I32);
    if (strcmp(name, "i64")  == 0) return type_create(TYPE_I64);
    if (strcmp(name, "u8")   == 0) return type_create(TYPE_U8);
    if (strcmp(name, "u16")  == 0) return type_create(TYPE_U16);
    if (strcmp(name, "u32")  == 0) return type_create(TYPE_U32);
    if (strcmp(name, "u64")  == 0) return type_create(TYPE_U64);
    if (strcmp(name, "f32")  == 0) return type_create(TYPE_F32);
    if (strcmp(name, "f64")  == 0) return type_create(TYPE_F64);
    if (strcmp(name, "str")  == 0) return type_create(TYPE_STR);
    if (strcmp(name, "bool") == 0) return type_create(TYPE_BOOL);
    if (strcmp(name, "void") == 0) return type_create(TYPE_VOID);
    if (strcmp(name, "arr")  == 0) return type_create(TYPE_ARRAY);
    if (strcmp(name, "map")  == 0) return type_create(TYPE_MAP);

    return NULL;
}

static const char *_kind_names[] = {
    [TYPE_UNKNOWN]  = "unknown",
    [TYPE_I8]       = "i8",
    [TYPE_I16]      = "i16",
    [TYPE_I32]      = "i32",
    [TYPE_I64]      = "i64",
    [TYPE_U8]       = "u8",
    [TYPE_U16]      = "u16",
    [TYPE_U32]      = "u32",
    [TYPE_U64]      = "u64",
    [TYPE_F32]      = "f32",
    [TYPE_F64]      = "f64",
    [TYPE_STR]      = "str",
    [TYPE_BOOL]     = "bool",
    [TYPE_VOID]     = "void",
    [TYPE_OPTION]   = "Option",
    [TYPE_ARRAY]    = "arr",
    [TYPE_MAP]      = "map",
    [TYPE_FUNCTION] = "fn",
    [TYPE_ERROR]    = "error",
};

const char *type_to_string(const AriType *t) {
    if (!t) return "null";
    if (t->kind >= 0 && t->kind < TYPE_KIND_COUNT)
        return _kind_names[t->kind];
    return "unknown";
}

/* ------------------------------------------------------------------ */
/*  Type queries                                                      */
/* ------------------------------------------------------------------ */

bool type_is_integer(const AriType *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
    case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
        return true;
    default:
        return false;
    }
}

bool type_is_numeric(const AriType *t) {
    if (!t) return false;
    return type_is_integer(t) || t->kind == TYPE_F32 || t->kind == TYPE_F64;
}

int type_bit_width(const AriType *t) {
    if (!t) return 0;
    switch (t->kind) {
    case TYPE_I8:  case TYPE_U8:  return 8;
    case TYPE_I16: case TYPE_U16: return 16;
    case TYPE_I32: case TYPE_U32: case TYPE_F32: return 32;
    case TYPE_I64: case TYPE_U64: case TYPE_F64: return 64;
    default: return 0;
    }
}

bool type_is_signed(const AriType *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
    case TYPE_F32: case TYPE_F64:
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/*  Type comparison and compatibility                                 */
/* ------------------------------------------------------------------ */

bool types_equal(const AriType *a, const AriType *b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;

    /* For compound types, check inner types */
    switch (a->kind) {
    case TYPE_OPTION:
    case TYPE_ARRAY:
        return types_equal(a->inner, b->inner);
    case TYPE_MAP:
        return types_equal(a->key_type, b->key_type) &&
               types_equal(a->inner, b->inner);
    case TYPE_FUNCTION:
        if (a->param_count != b->param_count) return false;
        for (size_t i = 0; i < a->param_count; i++) {
            if (!types_equal(a->param_types[i], b->param_types[i]))
                return false;
        }
        return types_equal(a->return_type, b->return_type);
    default:
        return true;
    }
}

bool types_loses_data(const AriType *target, const AriType *source) {
    if (!target || !source) return false;

    /* Float to integer always loses data */
    if ((source->kind == TYPE_F32 || source->kind == TYPE_F64) &&
        type_is_integer(target)) {
        return true;
    }

    /* f64 to f32 can lose precision */
    if (source->kind == TYPE_F64 && target->kind == TYPE_F32) {
        return true;
    }

    /* Integer to float: i64/u64 -> f32 loses precision */
    if (type_is_integer(source) && (target->kind == TYPE_F32 || target->kind == TYPE_F64)) {
        if (type_bit_width(source) > type_bit_width(target)) {
            return true;
        }
    }

    /* Wider integer to narrower integer */
    if (type_is_integer(source) && type_is_integer(target)) {
        int src_w = type_bit_width(source);
        int tgt_w = type_bit_width(target);
        if (src_w > tgt_w) return true;

        /* Same width but signed -> unsigned can lose data (negative values) */
        if (src_w == tgt_w && type_is_signed(source) && !type_is_signed(target))
            return true;
        /* Unsigned -> signed of same width can overflow */
        if (src_w == tgt_w && !type_is_signed(source) && type_is_signed(target))
            return true;
    }

    return false;
}

bool types_can_assign(const AriType *target, const AriType *source) {
    if (!target || !source) return false;

    /* Unknown type is always assignable (we don't have enough info) */
    if (target->kind == TYPE_UNKNOWN || source->kind == TYPE_UNKNOWN)
        return true;

    /* Exact match is always OK */
    if (types_equal(target, source))
        return true;

    /* Numeric widening without data loss is OK */
    if (type_is_numeric(target) && type_is_numeric(source)) {
        return !types_loses_data(target, source);
    }

    /* Bool → integer is always safe (bool is 0 or 1) */
    if (source->kind == TYPE_BOOL && type_is_integer(target))
        return true;

    return false;
}

bool types_compatible(const AriType *a, const AriType *b) {
    if (!a || !b) return false;

    /* Unknown is compatible with anything */
    if (a->kind == TYPE_UNKNOWN || b->kind == TYPE_UNKNOWN)
        return true;

    /* Same type is always compatible */
    if (types_equal(a, b))
        return true;

    /* Numeric types are compatible with each other for operations */
    if (type_is_numeric(a) && type_is_numeric(b))
        return true;

    return false;
}
