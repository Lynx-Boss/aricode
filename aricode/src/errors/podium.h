/*
 * ARICODE Podium System
 * ====================
 * Every function compiled by aricode receives a performance rating.
 *
 *   GOLD   - Optimal: minimum possible cycles and bytes
 *   SILVER - Good: within 1.5x of optimal
 *   BRONZE - Acceptable: within 3x of optimal
 *   IRON   - Poor: worse than 3x, compiler warns loudly
 *
 * The podium ensures programmers are always aware of their code's
 * efficiency and have actionable suggestions for improvement.
 */

#ifndef ARI_PODIUM_H
#define ARI_PODIUM_H

/* ── Podium Rank ──────────────────────────────────────────────────────── */

typedef enum {
    ARI_PODIUM_GOLD   = 1,   /* Optimal - minimum possible cycles/bytes   */
    ARI_PODIUM_SILVER = 2,   /* Good - within 1.5x of optimal             */
    ARI_PODIUM_BRONZE = 3,   /* Acceptable - within 3x of optimal         */
    ARI_PODIUM_IRON   = 4    /* Poor - worse than 3x, compiler warns      */
} AriPodiumRank;

/* ── Podium Entry ─────────────────────────────────────────────────────── */

typedef struct {
    const char*  function_name;
    AriPodiumRank rank;
    int          code_bytes;       /* bytes of machine code generated      */
    int          estimated_cycles; /* estimated CPU cycles per invocation   */
    int          optimal_bytes;    /* theoretical minimum bytes             */
    int          optimal_cycles;   /* theoretical minimum cycles            */
    double       byte_ratio;       /* code_bytes / optimal_bytes            */
    double       cycle_ratio;      /* estimated_cycles / optimal_cycles     */
    const char*  suggestion;       /* how to improve (for non-gold)         */
} AriPodiumEntry;

/* ── Maximum entries in a podium display ──────────────────────────────── */

#define ARI_PODIUM_MAX_ENTRIES  256

/* ── Public API ───────────────────────────────────────────────────────── */

/**
 * Rate a function's performance.
 * The rank is determined by the worse of byte_ratio and cycle_ratio.
 */
AriPodiumEntry ari_podium_rate(
    const char* fn_name,
    int code_bytes,
    int estimated_cycles,
    int optimal_bytes,
    int optimal_cycles,
    const char* suggestion
);

/**
 * Pretty-print a single podium entry to stdout.
 */
void ari_podium_display(const AriPodiumEntry* entry);

/**
 * Display the full podium after compilation.
 * Entries are sorted: GOLD first, then SILVER, BRONZE, IRON.
 */
void ari_podium_display_all(AriPodiumEntry* entries, int count);

/**
 * Get the rank name as a string (e.g., "GOLD").
 */
const char* ari_podium_rank_name(AriPodiumRank rank);

/**
 * Get the medal/icon for a rank.
 */
const char* ari_podium_rank_icon(AriPodiumRank rank);

/**
 * Get the ANSI color for a rank.
 */
const char* ari_podium_rank_color(AriPodiumRank rank);

#endif /* ARI_PODIUM_H */
