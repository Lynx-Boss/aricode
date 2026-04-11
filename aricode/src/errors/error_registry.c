/*
 * ARICODE Error Registry - Implementation
 * =======================================
 * JSON-lines log file + in-memory index for queries.
 */

#include "error_registry.h"
#include "error_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* ── Global registry instance ─────────────────────────────────────────── */

static AriErrorRegistry g_registry = {0};

/* ── Helper: ensure parent directory exists ───────────────────────────── */

static int ensure_dir(const char* path) {
    char tmp[ARI_REGISTRY_PATH_MAX];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    /* Find the last '/' and null-terminate there to get the directory */
    char* slash = strrchr(tmp, '/');
    if (!slash) return 0;  /* no directory component */
    *slash = '\0';

    struct stat st;
    if (stat(tmp, &st) == 0) return 0;  /* already exists */

    if (mkdir(tmp, 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

/* ── Escape a string for JSON output ──────────────────────────────────── */

static void json_escape(FILE* fp, const char* s) {
    if (!s) { fprintf(fp, "null"); return; }
    fputc('"', fp);
    for (; *s; s++) {
        switch (*s) {
            case '"':  fprintf(fp, "\\\""); break;
            case '\\': fprintf(fp, "\\\\"); break;
            case '\n': fprintf(fp, "\\n");  break;
            case '\r': fprintf(fp, "\\r");  break;
            case '\t': fprintf(fp, "\\t");  break;
            default:   fputc(*s, fp);       break;
        }
    }
    fputc('"', fp);
}

/* ── Init ─────────────────────────────────────────────────────────────── */

int ari_registry_init(const char* log_path) {
    if (!log_path) return -1;

    memset(&g_registry, 0, sizeof(g_registry));
    strncpy(g_registry.log_path, log_path, ARI_REGISTRY_PATH_MAX - 1);

    if (ensure_dir(log_path) != 0) {
        fprintf(stderr, "[aricode] WARNING: Could not create log directory for %s\n", log_path);
        /* Continue anyway - we'll try to write and report failure later */
    }

    /* Truncate the log file to start fresh for this session */
    FILE* fp = fopen(log_path, "w");
    if (fp) {
        fprintf(fp, "{\"event\":\"session_start\",\"timestamp\":%ld}\n", (long)time(NULL));
        fclose(fp);
    }

    g_registry.initialized = 1;
    return 0;
}

/* ── Add ──────────────────────────────────────────────────────────────── */

void ari_registry_add(const AriError* error) {
    if (!error) return;

    /* Store in memory if space available */
    if (g_registry.count < ARI_REGISTRY_MAX_ENTRIES) {
        g_registry.entries[g_registry.count] = *error;
        g_registry.count++;
    }

    /* Update level counts */
    if (error->level >= ARI_LEVEL_SILENT && error->level <= ARI_LEVEL_CATASTROPHIC) {
        g_registry.counts_by_level[error->level]++;
    }

    /* Append to log file */
    if (!g_registry.initialized) return;

    FILE* fp = fopen(g_registry.log_path, "a");
    if (!fp) return;

    fprintf(fp, "{\"level\":%d,\"level_name\":", error->level);
    json_escape(fp, ari_level_name(error->level));
    fprintf(fp, ",\"code\":");
    json_escape(fp, error->code);
    fprintf(fp, ",\"message\":");
    json_escape(fp, error->message);
    fprintf(fp, ",\"file\":");
    json_escape(fp, error->file);
    fprintf(fp, ",\"line\":%d,\"column\":%d", error->line, error->column);
    fprintf(fp, ",\"source_line\":");
    json_escape(fp, error->source_line);
    fprintf(fp, ",\"suggestion\":");
    json_escape(fp, error->suggestion);
    fprintf(fp, ",\"timestamp\":%ld}\n", error->timestamp);

    fclose(fp);
}

/* ── Query ────────────────────────────────────────────────────────────── */

int ari_registry_query(AriErrorLevel level, AriError* result, int max_count) {
    if (!result || max_count <= 0) return 0;

    int found = 0;
    for (int i = 0; i < g_registry.count && found < max_count; i++) {
        if (g_registry.entries[i].level == level) {
            result[found++] = g_registry.entries[i];
        }
    }
    return found;
}

/* ── Summary ──────────────────────────────────────────────────────────── */

void ari_registry_summary(void) {
    const char* level_colors[] = {
        ARI_CLR_BG_RED ARI_CLR_WHITE ARI_CLR_BOLD,  /* SILENT */
        ARI_CLR_RED ARI_CLR_BOLD,                    /* LOGIC */
        ARI_CLR_YELLOW ARI_CLR_BOLD,                 /* WARNING */
        ARI_CLR_MAGENTA ARI_CLR_BOLD,                /* SYSTEM */
        ARI_CLR_BG_RED ARI_CLR_WHITE ARI_CLR_BOLD    /* CATASTROPHIC */
    };

    printf("\n%s%s", ARI_CLR_BOLD, ARI_CLR_CYAN);
    printf("=== ARICODE ERROR REGISTRY SUMMARY ===\n");
    printf("%s", ARI_CLR_RESET);
    printf("%sTotal errors: %d%s\n\n", ARI_CLR_BOLD, g_registry.count, ARI_CLR_RESET);

    const char* level_names[] = {"SILENT", "LOGIC", "WARNING", "SYSTEM", "CATASTROPHIC"};
    const char* level_labels[] = {"FORBIDDEN", "INADMISSIBLE", "SUSPICIOUS", "EXTERNAL", "FATAL"};

    for (int i = 0; i < 5; i++) {
        int cnt = g_registry.counts_by_level[i];
        printf("  %s%-14s%s %-14s : %s%d%s\n",
            level_colors[i], level_names[i], ARI_CLR_RESET,
            level_labels[i],
            cnt > 0 ? ARI_CLR_BOLD : ARI_CLR_DIM,
            cnt,
            ARI_CLR_RESET);
    }

    printf("\n");

    if (g_registry.initialized) {
        printf("%sLog file: %s%s\n", ARI_CLR_DIM, g_registry.log_path, ARI_CLR_RESET);
    }

    /* Verdict */
    if (g_registry.counts_by_level[ARI_LEVEL_SILENT] > 0) {
        printf("\n%s COMPILATION BLOCKED: %d silent error(s) detected. %s\n",
            ARI_CLR_BG_RED ARI_CLR_WHITE ARI_CLR_BOLD,
            g_registry.counts_by_level[ARI_LEVEL_SILENT],
            ARI_CLR_RESET);
    } else if (g_registry.counts_by_level[ARI_LEVEL_LOGIC] > 0) {
        printf("\n%s RUNTIME CRASH: %d logic error(s) - these are bugs. %s\n",
            ARI_CLR_RED ARI_CLR_BOLD,
            g_registry.counts_by_level[ARI_LEVEL_LOGIC],
            ARI_CLR_RESET);
    } else if (g_registry.count == 0) {
        printf("\n%s All clear. Zero errors. %s\n",
            ARI_CLR_GREEN ARI_CLR_BOLD, ARI_CLR_RESET);
    }

    printf("\n");
}

/* ── Get global registry ──────────────────────────────────────────────── */

AriErrorRegistry* ari_registry_get(void) {
    return &g_registry;
}

/* ── Shutdown ─────────────────────────────────────────────────────────── */

void ari_registry_shutdown(void) {
    if (g_registry.initialized) {
        FILE* fp = fopen(g_registry.log_path, "a");
        if (fp) {
            fprintf(fp, "{\"event\":\"session_end\",\"timestamp\":%ld,\"total_errors\":%d}\n",
                (long)time(NULL), g_registry.count);
            fclose(fp);
        }
    }
    memset(&g_registry, 0, sizeof(g_registry));
}
