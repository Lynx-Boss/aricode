/*
 * aricode - Ari Code Language
 * Enum Registry
 *
 * Maintains a process-wide table of enum declarations:
 *   name -> { Variant0=0, Variant1=1, ... }
 *
 * Shared between parser (registers), analyzer (validates), and codegen
 * (resolves variants to integer constants).  Enums are plain i32 values:
 * each variant gets a zero-based index, and `Name::Variant` is lowered
 * at parse time to an integer literal.
 */

#ifndef ARICODE_ENUM_REGISTRY_H
#define ARICODE_ENUM_REGISTRY_H

#include <stddef.h>

#define ENUM_REGISTRY_MAX_ENUMS    128
#define ENUM_REGISTRY_MAX_VARIANTS 64

typedef struct {
    char *name;   /* variant name (owned) */
} EnumVariant;

typedef struct {
    char         *name;
    EnumVariant   variants[ENUM_REGISTRY_MAX_VARIANTS];
    size_t        variant_count;
} EnumDef;

int enum_registry_add(const char *name);

int enum_registry_add_variant(const char *enum_name,
                              const char *variant_name);

const EnumDef *enum_registry_get(const char *name);

/* Returns variant index (>=0) or -1 if unknown. */
int enum_registry_variant_index(const char *enum_name,
                                const char *variant_name);

/* Returns 1 if `name` is a registered enum. */
int enum_registry_is_enum(const char *name);

void enum_registry_reset(void);

#endif /* ARICODE_ENUM_REGISTRY_H */
