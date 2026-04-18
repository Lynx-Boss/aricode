/*
 * aricode - Ari Code Language
 * Enum Registry Implementation
 */

#include "enum_registry.h"

#include <stdlib.h>
#include <string.h>

static EnumDef g_enums[ENUM_REGISTRY_MAX_ENUMS];
static size_t  g_enum_count = 0;

static char *er_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

static EnumDef *find_enum(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < g_enum_count; i++) {
        if (g_enums[i].name && strcmp(g_enums[i].name, name) == 0)
            return &g_enums[i];
    }
    return NULL;
}

int enum_registry_add(const char *name) {
    if (!name) return 0;
    if (find_enum(name)) return 0;
    if (g_enum_count >= ENUM_REGISTRY_MAX_ENUMS) return 0;

    EnumDef *e = &g_enums[g_enum_count++];
    e->name          = er_strdup(name);
    e->variant_count = 0;
    return 1;
}

int enum_registry_add_variant(const char *enum_name,
                              const char *variant_name) {
    EnumDef *e = find_enum(enum_name);
    if (!e) return 0;
    if (e->variant_count >= ENUM_REGISTRY_MAX_VARIANTS) return 0;

    for (size_t i = 0; i < e->variant_count; i++) {
        if (e->variants[i].name && variant_name &&
            strcmp(e->variants[i].name, variant_name) == 0)
            return 0;
    }

    EnumVariant *v = &e->variants[e->variant_count++];
    v->name = er_strdup(variant_name);
    return 1;
}

const EnumDef *enum_registry_get(const char *name) {
    return find_enum(name);
}

int enum_registry_variant_index(const char *enum_name,
                                const char *variant_name) {
    const EnumDef *e = find_enum(enum_name);
    if (!e || !variant_name) return -1;
    for (size_t i = 0; i < e->variant_count; i++) {
        if (e->variants[i].name &&
            strcmp(e->variants[i].name, variant_name) == 0)
            return (int)i;
    }
    return -1;
}

int enum_registry_is_enum(const char *name) {
    return find_enum(name) != NULL;
}

void enum_registry_reset(void) {
    for (size_t i = 0; i < g_enum_count; i++) {
        free(g_enums[i].name);
        for (size_t j = 0; j < g_enums[i].variant_count; j++)
            free(g_enums[i].variants[j].name);
    }
    g_enum_count = 0;
}
