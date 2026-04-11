/*
 * ARICODE Error Registry
 * =====================
 * Persistent error log: every error that occurs during compilation
 * or runtime is stored as a JSON line in the registry file.
 *
 * The registry supports:
 *   - Initialization with a custom log path
 *   - Adding errors (thread-safe via simple file append)
 *   - Querying errors by level
 *   - Printing a summary of all recorded errors
 */

#ifndef ARI_ERROR_REGISTRY_H
#define ARI_ERROR_REGISTRY_H

#include "error_levels.h"

/* ── Maximum errors kept in memory for queries ────────────────────────── */

#define ARI_REGISTRY_MAX_ENTRIES  4096
#define ARI_REGISTRY_PATH_MAX     512

/* ── In-memory registry (for queries and summaries) ───────────────────── */

typedef struct {
    char     log_path[ARI_REGISTRY_PATH_MAX];
    AriError  entries[ARI_REGISTRY_MAX_ENTRIES];
    int      count;
    int      initialized;
    int      counts_by_level[5];  /* index = AriErrorLevel */
} AriErrorRegistry;

/* ── Public API ───────────────────────────────────────────────────────── */

/**
 * Initialize the registry. Creates the log directory if needed.
 * log_path: path to the JSON-lines log file (e.g., ".aricode/errors.log").
 * Returns 0 on success, -1 on failure.
 */
int ari_registry_init(const char* log_path);

/**
 * Add an error to the registry.
 * Appends a JSON line to the log file and stores in memory.
 */
void ari_registry_add(const AriError* error);

/**
 * Query errors by level. Fills result[] with up to max_count matching
 * errors. Returns the number of errors copied.
 */
int ari_registry_query(AriErrorLevel level, AriError* result, int max_count);

/**
 * Print a summary of all recorded errors to stdout.
 */
void ari_registry_summary(void);

/**
 * Get a pointer to the global registry (for inspection/testing).
 */
AriErrorRegistry* ari_registry_get(void);

/**
 * Shut down the registry and flush any pending writes.
 */
void ari_registry_shutdown(void);

#endif /* ARI_ERROR_REGISTRY_H */
