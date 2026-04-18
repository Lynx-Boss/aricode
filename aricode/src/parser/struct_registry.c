/*
 * aricode - Ari Code Language
 * Struct Registry Implementation
 */

#include "struct_registry.h"

#include <stdlib.h>
#include <string.h>

static StructDef g_structs[STRUCT_REGISTRY_MAX_STRUCTS];
static size_t    g_struct_count = 0;

static char *sr_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

static StructDef *find_struct(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < g_struct_count; i++) {
        if (g_structs[i].name && strcmp(g_structs[i].name, name) == 0)
            return &g_structs[i];
    }
    return NULL;
}

int struct_registry_add(const char *name) {
    if (!name) return 0;
    if (find_struct(name)) return 0;          /* duplicate */
    if (g_struct_count >= STRUCT_REGISTRY_MAX_STRUCTS) return 0;

    StructDef *s = &g_structs[g_struct_count++];
    s->name        = sr_strdup(name);
    s->field_count = 0;
    return 1;
}

int struct_registry_add_field(const char *struct_name,
                              const char *field_name,
                              const char *field_type) {
    StructDef *s = find_struct(struct_name);
    if (!s) return 0;
    if (s->field_count >= STRUCT_REGISTRY_MAX_FIELDS) return 0;

    /* Reject duplicate field names in same struct. */
    for (size_t i = 0; i < s->field_count; i++) {
        if (s->fields[i].name && field_name &&
            strcmp(s->fields[i].name, field_name) == 0)
            return 0;
    }

    StructField *f = &s->fields[s->field_count++];
    f->name      = sr_strdup(field_name);
    f->type_name = sr_strdup(field_type);
    return 1;
}

const StructDef *struct_registry_get(const char *name) {
    return find_struct(name);
}

int struct_registry_field_index(const char *struct_name,
                                const char *field_name) {
    const StructDef *s = find_struct(struct_name);
    if (!s || !field_name) return -1;
    for (size_t i = 0; i < s->field_count; i++) {
        if (s->fields[i].name &&
            strcmp(s->fields[i].name, field_name) == 0)
            return (int)i;
    }
    return -1;
}

int struct_registry_field_count(const char *struct_name) {
    const StructDef *s = find_struct(struct_name);
    if (!s) return -1;
    return (int)s->field_count;
}

void struct_registry_reset(void) {
    for (size_t i = 0; i < g_struct_count; i++) {
        free(g_structs[i].name);
        for (size_t j = 0; j < g_structs[i].field_count; j++) {
            free(g_structs[i].fields[j].name);
            free(g_structs[i].fields[j].type_name);
        }
    }
    g_struct_count = 0;
}
