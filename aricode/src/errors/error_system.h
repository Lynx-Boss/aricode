/*
 * ARICODE Error System
 * ===================
 * Core error management: creation, formatting, emission, and logging.
 *
 * Every error in aricode is:
 *   1. Created with full context (file, line, source, suggestion)
 *   2. Formatted with ANSI colors for terminal display
 *   3. Emitted to stderr so the programmer sees it immediately
 *   4. Logged to the error registry for post-mortem analysis
 */

#ifndef ARI_ERROR_SYSTEM_H
#define ARI_ERROR_SYSTEM_H

#include "error_levels.h"

/* ── ANSI Color Codes ─────────────────────────────────────────────────── */

#define ARI_CLR_RESET   "\033[0m"
#define ARI_CLR_BOLD    "\033[1m"
#define ARI_CLR_DIM     "\033[2m"
#define ARI_CLR_RED     "\033[31m"
#define ARI_CLR_GREEN   "\033[32m"
#define ARI_CLR_YELLOW  "\033[33m"
#define ARI_CLR_BLUE    "\033[34m"
#define ARI_CLR_MAGENTA "\033[35m"
#define ARI_CLR_CYAN    "\033[36m"
#define ARI_CLR_WHITE   "\033[37m"
#define ARI_CLR_BG_RED  "\033[41m"

/* ── Box-drawing characters ───────────────────────────────────────────── */

#define ARI_BOX_TL  "\xe2\x95\x94"  /* top-left corner     */
#define ARI_BOX_TR  "\xe2\x95\x97"  /* top-right corner    */
#define ARI_BOX_BL  "\xe2\x95\x9a"  /* bottom-left corner  */
#define ARI_BOX_BR  "\xe2\x95\x9d"  /* bottom-right corner */
#define ARI_BOX_H   "\xe2\x95\x90"  /* horizontal line     */
#define ARI_BOX_V   "\xe2\x95\x91"  /* vertical line       */
#define ARI_BOX_LT  "\xe2\x95\xa0"  /* left T-junction     */
#define ARI_BOX_RT  "\xe2\x95\xa3"  /* right T-junction    */

/* ── Error formatting buffer size ─────────────────────────────────────── */

#define ARI_ERROR_FMT_BUFSIZE  4096

/* ── Public API ───────────────────────────────────────────────────────── */

/**
 * Create a AriError with all context fields populated.
 * The timestamp is set automatically to the current time.
 */
AriError ari_error_create(
    AriErrorLevel level,
    const char*  code,
    const char*  message,
    const char*  file,
    int          line,
    int          column,
    const char*  source_line,
    const char*  suggestion
);

/**
 * Format an error into a human-readable, ANSI-colored box.
 * Writes into the provided buffer (must be >= ARI_ERROR_FMT_BUFSIZE).
 * Returns a pointer to buf on success, NULL on failure.
 */
char* ari_error_format(const AriError* error, char* buf, int bufsize);

/**
 * Emit an error: format it and print to stderr.
 * Also logs it via ari_error_log.
 */
void ari_error_emit(const AriError* error);

/**
 * Log an error to the active log file (.aricode/errors.log).
 * Writes one JSON line per error.
 */
void ari_error_log(const AriError* error);

/**
 * Get the ANSI color associated with an error level.
 */
const char* ari_error_level_color(AriErrorLevel level);

#endif /* ARI_ERROR_SYSTEM_H */
