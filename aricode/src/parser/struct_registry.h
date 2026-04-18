/*
 * aricode - Ari Code Language
 * Struct Registry
 *
 * Maintains a process-wide table of struct declarations:
 *   name -> { field_name, field_type, ... }
 *
 * Shared between parser (registers), analyzer (validates), and codegen
 * (looks up field indices).  This is intentionally simple: structs are
 * compiled to heap arrays with one 8-byte slot per field, and field
 * access lowers to arr_get/arr_set with a static index.
 */

#ifndef ARICODE_STRUCT_REGISTRY_H
#define ARICODE_STRUCT_REGISTRY_H

#include <stddef.h>

#define STRUCT_REGISTRY_MAX_STRUCTS 128
#define STRUCT_REGISTRY_MAX_FIELDS  32

typedef struct {
    char *name;       /* field name (owned) */
    char *type_name;  /* field type name (owned) e.g. "i32" */
} StructField;

typedef struct {
    char        *name;                                  /* struct name (owned) */
    StructField  fields[STRUCT_REGISTRY_MAX_FIELDS];
    size_t       field_count;
} StructDef;

/*
 * Register a new struct type.  Returns non-zero on success.
 * Duplicates are rejected (returns 0).
 */
int struct_registry_add(const char *name);

/*
 * Add a field to the most recently added struct.
 */
int struct_registry_add_field(const char *struct_name,
                              const char *field_name,
                              const char *field_type);

/*
 * Look up a struct by name.  Returns NULL if not found.
 */
const StructDef *struct_registry_get(const char *name);

/*
 * Return the zero-based index of `field_name` in `struct_name`,
 * or -1 if the struct or field does not exist.
 */
int struct_registry_field_index(const char *struct_name,
                                const char *field_name);

/*
 * Total number of fields for `struct_name`, or -1 if unknown.
 */
int struct_registry_field_count(const char *struct_name);

/*
 * Reset the registry (for tests or multi-compile scenarios).
 */
void struct_registry_reset(void);

#endif /* ARICODE_STRUCT_REGISTRY_H */
