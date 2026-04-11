/*
 * ARICODE Error System - Implementation
 * =====================================
 * Beautifully formatted, ANSI-colored error output.
 * Every error is impossible to ignore.
 */

#include "error_system.h"
#include "error_levels.h"
#include "error_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Internal constants ───────────────────────────────────────────────── */

#define BOX_WIDTH 60

/* ── Color per level ──────────────────────────────────────────────────── */

const char* ari_error_level_color(AriErrorLevel level) {
    switch (level) {
        case ARI_LEVEL_SILENT:       return ARI_CLR_BG_RED ARI_CLR_WHITE ARI_CLR_BOLD;
        case ARI_LEVEL_LOGIC:        return ARI_CLR_RED ARI_CLR_BOLD;
        case ARI_LEVEL_WARNING:      return ARI_CLR_YELLOW ARI_CLR_BOLD;
        case ARI_LEVEL_SYSTEM:       return ARI_CLR_MAGENTA ARI_CLR_BOLD;
        case ARI_LEVEL_CATASTROPHIC: return ARI_CLR_BG_RED ARI_CLR_WHITE ARI_CLR_BOLD;
        default:                    return ARI_CLR_WHITE;
    }
}

/* ── Create ───────────────────────────────────────────────────────────── */

AriError ari_error_create(
    AriErrorLevel level,
    const char*  code,
    const char*  message,
    const char*  file,
    int          line,
    int          column,
    const char*  source_line,
    const char*  suggestion
) {
    AriError err;
    err.level       = level;
    err.code        = code;
    err.message     = message;
    err.file        = file;
    err.line        = line;
    err.column      = column;
    err.source_line = source_line;
    err.suggestion  = suggestion;
    err.timestamp   = (long)time(NULL);
    return err;
}

/* ── Helper: repeat a string N times into buf ─────────────────────────── */

static void repeat_str(char* buf, const char* s, int n) {
    buf[0] = '\0';
    for (int i = 0; i < n; i++) {
        strcat(buf, s);
    }
}

/* ── Format ───────────────────────────────────────────────────────────── */

char* ari_error_format(const AriError* error, char* buf, int bufsize) {
    if (!error || !buf || bufsize < ARI_ERROR_FMT_BUFSIZE) return NULL;

    const char* clr = ari_error_level_color(error->level);
    const char* lvl_name  = ari_level_name(error->level);
    const char* lvl_label = ari_level_label(error->level);

    char hline[256];
    repeat_str(hline, ARI_BOX_H, BOX_WIDTH - 2);

    int written = 0;
    int remaining = bufsize;

    /* Top border */
    written += snprintf(buf + written, remaining - written,
        "%s" ARI_BOX_TL ARI_BOX_H ARI_BOX_H " ARICODE ERROR " "%s" ARI_BOX_TR ARI_CLR_RESET "\n",
        clr, hline + 28);

    /* Level line */
    written += snprintf(buf + written, remaining - written,
        "%s" ARI_BOX_V ARI_CLR_RESET " Level: %s%s (%s)" ARI_CLR_RESET "\n",
        clr, clr, lvl_name, lvl_label);

    /* Code line */
    written += snprintf(buf + written, remaining - written,
        "%s" ARI_BOX_V ARI_CLR_RESET " Code:  %s%s" ARI_CLR_RESET "\n",
        clr, ARI_CLR_BOLD, error->code);

    /* File:line:col */
    written += snprintf(buf + written, remaining - written,
        "%s" ARI_BOX_V ARI_CLR_RESET " File:  %s%s:%d:%d" ARI_CLR_RESET "\n",
        clr, ARI_CLR_CYAN, error->file, error->line, error->column);

    /* Separator */
    written += snprintf(buf + written, remaining - written,
        "%s" ARI_BOX_LT "%s" ARI_BOX_RT ARI_CLR_RESET "\n",
        clr, hline);

    /* Blank line */
    written += snprintf(buf + written, remaining - written,
        "%s" ARI_BOX_V ARI_CLR_RESET "\n", clr);

    /* Source context: line before (dimmed) */
    if (error->line > 1) {
        written += snprintf(buf + written, remaining - written,
            "%s" ARI_BOX_V ARI_CLR_RESET "   %s%4d " ARI_CLR_DIM ARI_BOX_V ARI_CLR_RESET " %s...(context)%s\n",
            clr, ARI_CLR_DIM, error->line - 1, ARI_CLR_DIM, ARI_CLR_RESET);
    }

    /* Source context: the offending line (highlighted) */
    written += snprintf(buf + written, remaining - written,
        "%s" ARI_BOX_V ARI_CLR_RESET " %s\xe2\x86\x92%s %s%4d " ARI_BOX_V ARI_CLR_RESET " %s%s%s\n",
        clr, ARI_CLR_RED ARI_CLR_BOLD, ARI_CLR_RESET,
        ARI_CLR_RED, error->line,
        ARI_CLR_RED ARI_CLR_BOLD, error->source_line, ARI_CLR_RESET);

    /* Source context: line after (dimmed) */
    written += snprintf(buf + written, remaining - written,
        "%s" ARI_BOX_V ARI_CLR_RESET "   %s%4d " ARI_CLR_DIM ARI_BOX_V ARI_CLR_RESET " %s...(context)%s\n",
        clr, ARI_CLR_DIM, error->line + 1, ARI_CLR_DIM, ARI_CLR_RESET);

    /* Column indicator */
    if (error->column > 0) {
        char spaces[128] = {0};
        int pad = 10 + error->column;
        if (pad > 120) pad = 120;
        memset(spaces, ' ', pad);
        spaces[pad] = '\0';
        written += snprintf(buf + written, remaining - written,
            "%s" ARI_BOX_V ARI_CLR_RESET "%s%s^^^^^^^^%s\n",
            clr, spaces, ARI_CLR_RED ARI_CLR_BOLD, ARI_CLR_RESET);
    }

    /* Blank line */
    written += snprintf(buf + written, remaining - written,
        "%s" ARI_BOX_V ARI_CLR_RESET "\n", clr);

    /* Error message */
    written += snprintf(buf + written, remaining - written,
        "%s" ARI_BOX_V ARI_CLR_RESET " %sERROR:%s %s\n",
        clr, ARI_CLR_RED ARI_CLR_BOLD, ARI_CLR_RESET, error->message);

    /* Blank line */
    written += snprintf(buf + written, remaining - written,
        "%s" ARI_BOX_V ARI_CLR_RESET "\n", clr);

    /* Suggestion */
    if (error->suggestion && error->suggestion[0] != '\0') {
        written += snprintf(buf + written, remaining - written,
            "%s" ARI_BOX_V ARI_CLR_RESET " %sFIX:%s %s\n",
            clr, ARI_CLR_GREEN ARI_CLR_BOLD, ARI_CLR_RESET, error->suggestion);

        written += snprintf(buf + written, remaining - written,
            "%s" ARI_BOX_V ARI_CLR_RESET "\n", clr);
    }

    /* Bottom border */
    written += snprintf(buf + written, remaining - written,
        "%s" ARI_BOX_BL "%s" ARI_BOX_BR ARI_CLR_RESET "\n",
        clr, hline);

    (void)written;  /* suppress unused warning */
    return buf;
}

/* ── Emit ─────────────────────────────────────────────────────────────── */

void ari_error_emit(const AriError* error) {
    if (!error) return;

    char buf[ARI_ERROR_FMT_BUFSIZE];
    if (ari_error_format(error, buf, sizeof(buf))) {
        fprintf(stderr, "\n%s\n", buf);
    }

    /* Always log */
    ari_error_log(error);
}

/* ── Log (JSON line to file) ──────────────────────────────────────────── */

void ari_error_log(const AriError* error) {
    if (!error) return;

    /* Use the registry if initialized, otherwise write directly */
    ari_registry_add(error);
}
