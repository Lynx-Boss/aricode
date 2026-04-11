/*
 * ARICODE Podium System - Implementation
 * ======================================
 * Performance rating with beautiful terminal display.
 */

#include "podium.h"
#include "error_system.h"  /* for ANSI color defines */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Rank name ────────────────────────────────────────────────────────── */

const char* ari_podium_rank_name(AriPodiumRank rank) {
    switch (rank) {
        case ARI_PODIUM_GOLD:   return "GOLD";
        case ARI_PODIUM_SILVER: return "SILVER";
        case ARI_PODIUM_BRONZE: return "BRONZE";
        case ARI_PODIUM_IRON:   return "IRON";
        default:               return "UNKNOWN";
    }
}

/* ── Rank icon (UTF-8 emoji) ──────────────────────────────────────────── */

const char* ari_podium_rank_icon(AriPodiumRank rank) {
    switch (rank) {
        case ARI_PODIUM_GOLD:   return "\xf0\x9f\xa5\x87";  /* gold medal   */
        case ARI_PODIUM_SILVER: return "\xf0\x9f\xa5\x88";  /* silver medal */
        case ARI_PODIUM_BRONZE: return "\xf0\x9f\xa5\x89";  /* bronze medal */
        case ARI_PODIUM_IRON:   return "\xf0\x9f\xaa\xa8";  /* rock         */
        default:               return "?";
    }
}

/* ── Rank color ───────────────────────────────────────────────────────── */

const char* ari_podium_rank_color(AriPodiumRank rank) {
    switch (rank) {
        case ARI_PODIUM_GOLD:   return ARI_CLR_YELLOW ARI_CLR_BOLD;
        case ARI_PODIUM_SILVER: return ARI_CLR_WHITE ARI_CLR_BOLD;
        case ARI_PODIUM_BRONZE: return ARI_CLR_MAGENTA ARI_CLR_BOLD;  /* copper-ish */
        case ARI_PODIUM_IRON:   return ARI_CLR_RED ARI_CLR_BOLD;
        default:               return ARI_CLR_RESET;
    }
}

/* ── Rate ─────────────────────────────────────────────────────────────── */

AriPodiumEntry ari_podium_rate(
    const char* fn_name,
    int code_bytes,
    int estimated_cycles,
    int optimal_bytes,
    int optimal_cycles,
    const char* suggestion
) {
    AriPodiumEntry entry;
    entry.function_name   = fn_name;
    entry.code_bytes      = code_bytes;
    entry.estimated_cycles = estimated_cycles;
    entry.optimal_bytes   = optimal_bytes;
    entry.optimal_cycles  = optimal_cycles;
    entry.suggestion      = suggestion;

    /* Calculate ratios (avoid division by zero) */
    entry.byte_ratio  = (optimal_bytes > 0)
        ? (double)code_bytes / (double)optimal_bytes
        : (double)code_bytes;
    entry.cycle_ratio = (optimal_cycles > 0)
        ? (double)estimated_cycles / (double)optimal_cycles
        : (double)estimated_cycles;

    /* Rank is determined by the WORSE of the two ratios */
    double worst = entry.byte_ratio > entry.cycle_ratio
        ? entry.byte_ratio : entry.cycle_ratio;

    if (worst <= 1.0) {
        entry.rank = ARI_PODIUM_GOLD;
    } else if (worst <= 1.5) {
        entry.rank = ARI_PODIUM_SILVER;
    } else if (worst <= 3.0) {
        entry.rank = ARI_PODIUM_BRONZE;
    } else {
        entry.rank = ARI_PODIUM_IRON;
    }

    return entry;
}

/* ── Display one entry ────────────────────────────────────────────────── */

void ari_podium_display(const AriPodiumEntry* entry) {
    if (!entry) return;

    const char* icon  = ari_podium_rank_icon(entry->rank);
    const char* color = ari_podium_rank_color(entry->rank);
    const char* name  = ari_podium_rank_name(entry->rank);

    printf("%s %-20s %s%-6s%s %s|%s %3dB %3dcyc %s|%s ",
        icon,
        entry->function_name,
        color, name, ARI_CLR_RESET,
        ARI_CLR_DIM, ARI_CLR_RESET,
        entry->code_bytes, entry->estimated_cycles,
        ARI_CLR_DIM, ARI_CLR_RESET);

    if (entry->rank == ARI_PODIUM_GOLD) {
        printf("%sOptimal%s", ARI_CLR_GREEN ARI_CLR_BOLD, ARI_CLR_RESET);
    } else if (entry->suggestion && entry->suggestion[0] != '\0') {
        if (entry->rank == ARI_PODIUM_IRON) {
            printf("%sWARNING: %s%s", ARI_CLR_RED ARI_CLR_BOLD, entry->suggestion, ARI_CLR_RESET);
        } else {
            printf("%s%s%s", ARI_CLR_DIM, entry->suggestion, ARI_CLR_RESET);
        }
    }

    printf("\n");
}

/* ── Comparison for qsort (sort by rank, then by cycle_ratio) ─────────── */

static int podium_compare(const void* a, const void* b) {
    const AriPodiumEntry* ea = (const AriPodiumEntry*)a;
    const AriPodiumEntry* eb = (const AriPodiumEntry*)b;

    if (ea->rank != eb->rank) return (int)ea->rank - (int)eb->rank;
    if (ea->cycle_ratio < eb->cycle_ratio) return -1;
    if (ea->cycle_ratio > eb->cycle_ratio) return 1;
    return 0;
}

/* ── Display all entries ──────────────────────────────────────────────── */

void ari_podium_display_all(AriPodiumEntry* entries, int count) {
    if (!entries || count <= 0) return;

    /* Sort by rank (gold first, iron last) */
    qsort(entries, count, sizeof(AriPodiumEntry), podium_compare);

    /* Header */
    char hline[512];
    hline[0] = '\0';
    for (int i = 0; i < 55; i++) strcat(hline, ARI_BOX_H);

    /* ARI_BOX_H is 3 bytes UTF-8, so 12 chars = 4 symbols worth of offset */
    printf("\n%s%s", ARI_CLR_BOLD ARI_CLR_CYAN, ARI_BOX_H ARI_BOX_H ARI_BOX_H);
    printf(" ARICODE PODIUM ");
    printf("%s%s\n", hline + 12*3, ARI_CLR_RESET);  /* skip 12 symbols */

    /* Entries */
    for (int i = 0; i < count; i++) {
        ari_podium_display(&entries[i]);
    }

    /* Footer */
    printf("%s%s%s\n",
        ARI_CLR_BOLD ARI_CLR_CYAN,
        hline,
        ARI_CLR_RESET);

    /* Summary stats */
    int gold = 0, silver = 0, bronze = 0, iron = 0;
    for (int i = 0; i < count; i++) {
        switch (entries[i].rank) {
            case ARI_PODIUM_GOLD:   gold++;   break;
            case ARI_PODIUM_SILVER: silver++; break;
            case ARI_PODIUM_BRONZE: bronze++; break;
            case ARI_PODIUM_IRON:   iron++;   break;
        }
    }

    printf("%s  Summary: %s%d gold  %s%d silver  %s%d bronze  %s%d iron%s\n\n",
        ARI_CLR_DIM,
        ARI_CLR_YELLOW ARI_CLR_BOLD, gold,
        ARI_CLR_WHITE ARI_CLR_BOLD, silver,
        ARI_CLR_MAGENTA ARI_CLR_BOLD, bronze,
        ARI_CLR_RED ARI_CLR_BOLD, iron,
        ARI_CLR_RESET);

    if (iron > 0) {
        printf("%s  WARNING: %d function(s) rated IRON - performance is unacceptable.%s\n",
            ARI_CLR_RED ARI_CLR_BOLD, iron, ARI_CLR_RESET);
        printf("%s  Review the suggestions above to improve.%s\n\n",
            ARI_CLR_DIM, ARI_CLR_RESET);
    }
}
