/*
 * ARICODE Error Level Definitions
 * ==============================
 * Core philosophy:
 *   - Silent errors are FORBIDDEN
 *   - Logic errors from bad programming are INADMISSIBLE
 *   - Every error must be shown, logged, and the programmer forced to fix it
 *
 * Written in C for maximum performance.
 */

#ifndef ARI_ERROR_LEVELS_H
#define ARI_ERROR_LEVELS_H

#include <time.h>

/* ── Error Severity Levels ────────────────────────────────────────────── */

typedef enum {
    ARI_LEVEL_SILENT      = 0,  /* FORBIDDEN  - code that could silently fail won't compile */
    ARI_LEVEL_LOGIC       = 1,  /* INADMISSIBLE - programming errors (null, div/0, bounds)  */
    ARI_LEVEL_WARNING     = 2,  /* Suspicious code (unused vars, implicit conversions)      */
    ARI_LEVEL_SYSTEM      = 3,  /* OS/network/file errors - outside programmer's control    */
    ARI_LEVEL_CATASTROPHIC = 4  /* Hardware/OOM - log and exit gracefully                   */
} AriErrorLevel;

/* ── Error Descriptor ─────────────────────────────────────────────────── */

typedef struct {
    AriErrorLevel level;
    const char*  code;          /* e.g. "ARI-L001"                              */
    const char*  message;       /* human-readable description                  */
    const char*  file;          /* source file where the error occurred         */
    int          line;          /* line number in source                        */
    int          column;        /* column number in source                      */
    const char*  source_line;   /* the actual line of source code               */
    const char*  suggestion;    /* how to fix it                                */
    long         timestamp;     /* unix epoch seconds when the error was raised */
} AriError;

/* ── Level name/label helpers ─────────────────────────────────────────── */

static inline const char* ari_level_name(AriErrorLevel lvl) {
    switch (lvl) {
        case ARI_LEVEL_SILENT:       return "SILENT";
        case ARI_LEVEL_LOGIC:        return "LOGIC";
        case ARI_LEVEL_WARNING:      return "WARNING";
        case ARI_LEVEL_SYSTEM:       return "SYSTEM";
        case ARI_LEVEL_CATASTROPHIC: return "CATASTROPHIC";
        default:                    return "UNKNOWN";
    }
}

static inline const char* ari_level_label(AriErrorLevel lvl) {
    switch (lvl) {
        case ARI_LEVEL_SILENT:       return "FORBIDDEN";
        case ARI_LEVEL_LOGIC:        return "INADMISSIBLE";
        case ARI_LEVEL_WARNING:      return "SUSPICIOUS";
        case ARI_LEVEL_SYSTEM:       return "EXTERNAL";
        case ARI_LEVEL_CATASTROPHIC: return "FATAL";
        default:                    return "UNKNOWN";
    }
}

#endif /* ARI_ERROR_LEVELS_H */
