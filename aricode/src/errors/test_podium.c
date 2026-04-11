/*
 * ARICODE Podium System - Test Suite
 * ===================================
 * Tests podium rating, display, and sorting.
 */

#include "podium.h"
#include "error_system.h"  /* for ANSI color defines */

#include <stdio.h>
#include <assert.h>
#include <string.h>

/* ── Helper: section banner ───────────────────────────────────────────── */

static void banner(const char* title) {
    printf("\n%s%s========================================%s\n",
        ARI_CLR_BOLD, ARI_CLR_CYAN, ARI_CLR_RESET);
    printf("%s%s  %s%s\n",
        ARI_CLR_BOLD, ARI_CLR_CYAN, title, ARI_CLR_RESET);
    printf("%s%s========================================%s\n\n",
        ARI_CLR_BOLD, ARI_CLR_CYAN, ARI_CLR_RESET);
}

/* ── Test: Gold rating (optimal) ──────────────────────────────────────── */

static void test_gold(void) {
    banner("TEST: Gold Rating (Optimal)");

    AriPodiumEntry e = ari_podium_rate("fn add()", 2, 1, 2, 1, "Optimal");

    assert(e.rank == ARI_PODIUM_GOLD);
    assert(e.code_bytes == 2);
    assert(e.estimated_cycles == 1);
    assert(e.byte_ratio <= 1.0);
    assert(e.cycle_ratio <= 1.0);

    ari_podium_display(&e);

    printf("\n%sOK%s: Gold rating assigned correctly (ratio=%.2f)\n",
        ARI_CLR_GREEN, ARI_CLR_RESET, e.byte_ratio);
}

/* ── Test: Silver rating (within 1.5x) ────────────────────────────────── */

static void test_silver(void) {
    banner("TEST: Silver Rating (Good)");

    /* 7/5 = 1.4x bytes, 3/2 = 1.5x cycles -> worst is 1.5 -> SILVER */
    AriPodiumEntry e = ari_podium_rate("fn multiply()", 7, 3, 5, 2,
        "Could use LEA instruction (5B 2cyc)");

    assert(e.rank == ARI_PODIUM_SILVER);

    ari_podium_display(&e);

    printf("\n%sOK%s: Silver rating assigned (byte_ratio=%.2f, cycle_ratio=%.2f)\n",
        ARI_CLR_GREEN, ARI_CLR_RESET, e.byte_ratio, e.cycle_ratio);
}

/* ── Test: Bronze rating (within 3x) ──────────────────────────────────── */

static void test_bronze(void) {
    banner("TEST: Bronze Rating (Acceptable)");

    AriPodiumEntry e = ari_podium_rate("fn sort()", 45, 12, 20, 5,
        "Consider loop unrolling");

    assert(e.rank == ARI_PODIUM_BRONZE);

    ari_podium_display(&e);

    printf("\n%sOK%s: Bronze rating assigned (byte_ratio=%.2f, cycle_ratio=%.2f)\n",
        ARI_CLR_GREEN, ARI_CLR_RESET, e.byte_ratio, e.cycle_ratio);
}

/* ── Test: Iron rating (worse than 3x) ────────────────────────────────── */

static void test_iron(void) {
    banner("TEST: Iron Rating (Poor)");

    AriPodiumEntry e = ari_podium_rate("fn parse()", 200, 89, 40, 15,
        "5x over optimal - restructure algorithm");

    assert(e.rank == ARI_PODIUM_IRON);

    ari_podium_display(&e);

    printf("\n%sOK%s: Iron rating assigned (byte_ratio=%.2f, cycle_ratio=%.2f)\n",
        ARI_CLR_GREEN, ARI_CLR_RESET, e.byte_ratio, e.cycle_ratio);
}

/* ── Test: Edge case - exact boundaries ───────────────────────────────── */

static void test_boundaries(void) {
    banner("TEST: Boundary Cases");

    /* Exactly at 1.0x -> GOLD */
    AriPodiumEntry e1 = ari_podium_rate("fn exact_1x()", 10, 5, 10, 5, "");
    assert(e1.rank == ARI_PODIUM_GOLD);
    printf("  1.0x -> %s%s%s (correct)\n",
        ari_podium_rank_color(e1.rank), ari_podium_rank_name(e1.rank), ARI_CLR_RESET);

    /* Exactly at 1.5x -> SILVER */
    AriPodiumEntry e2 = ari_podium_rate("fn exact_1_5x()", 15, 5, 10, 5, "");
    assert(e2.rank == ARI_PODIUM_SILVER);
    printf("  1.5x -> %s%s%s (correct)\n",
        ari_podium_rank_color(e2.rank), ari_podium_rank_name(e2.rank), ARI_CLR_RESET);

    /* Exactly at 3.0x -> BRONZE */
    AriPodiumEntry e3 = ari_podium_rate("fn exact_3x()", 30, 5, 10, 5, "");
    assert(e3.rank == ARI_PODIUM_BRONZE);
    printf("  3.0x -> %s%s%s (correct)\n",
        ari_podium_rank_color(e3.rank), ari_podium_rank_name(e3.rank), ARI_CLR_RESET);

    /* 3.1x -> IRON */
    AriPodiumEntry e4 = ari_podium_rate("fn over_3x()", 31, 5, 10, 5, "");
    assert(e4.rank == ARI_PODIUM_IRON);
    printf("  3.1x -> %s%s%s (correct)\n",
        ari_podium_rank_color(e4.rank), ari_podium_rank_name(e4.rank), ARI_CLR_RESET);

    printf("\n%sOK%s: All boundary cases correct\n",
        ARI_CLR_GREEN, ARI_CLR_RESET);
}

/* ── Test: Full podium display ────────────────────────────────────────── */

static void test_full_podium(void) {
    banner("TEST: Full Podium Display");

    AriPodiumEntry entries[6];

    /* Intentionally out of order to test sorting */
    entries[0] = ari_podium_rate("fn parse()",    200, 89, 40, 15,
        "5x over optimal - restructure algorithm");
    entries[1] = ari_podium_rate("fn add()",        2,  1,  2,  1,
        "Optimal");
    entries[2] = ari_podium_rate("fn sort()",      45, 12, 20,  5,
        "Consider loop unrolling");
    entries[3] = ari_podium_rate("fn multiply()",   7,  3,  5,  2,
        "Could use LEA instruction (5B 2cyc)");
    entries[4] = ari_podium_rate("fn fibonacci()", 12,  4, 12,  4,
        "Optimal");
    entries[5] = ari_podium_rate("fn validate()", 150, 60, 30, 10,
        "6x over optimal - too many branches");

    ari_podium_display_all(entries, 6);

    printf("%sOK%s: Full podium displayed with sorting\n",
        ARI_CLR_GREEN, ARI_CLR_RESET);
}

/* ── Test: Rank helpers ───────────────────────────────────────────────── */

static void test_rank_helpers(void) {
    banner("TEST: Rank Helper Functions");

    assert(strcmp(ari_podium_rank_name(ARI_PODIUM_GOLD),   "GOLD") == 0);
    assert(strcmp(ari_podium_rank_name(ARI_PODIUM_SILVER), "SILVER") == 0);
    assert(strcmp(ari_podium_rank_name(ARI_PODIUM_BRONZE), "BRONZE") == 0);
    assert(strcmp(ari_podium_rank_name(ARI_PODIUM_IRON),   "IRON") == 0);

    /* Icons should be non-empty UTF-8 strings */
    assert(strlen(ari_podium_rank_icon(ARI_PODIUM_GOLD)) > 0);
    assert(strlen(ari_podium_rank_icon(ARI_PODIUM_SILVER)) > 0);

    /* Colors should be non-empty ANSI sequences */
    assert(strlen(ari_podium_rank_color(ARI_PODIUM_GOLD)) > 0);
    assert(strlen(ari_podium_rank_color(ARI_PODIUM_IRON)) > 0);

    printf("%sOK%s: All rank helpers return correct values\n",
        ARI_CLR_GREEN, ARI_CLR_RESET);
}

/* ── Main ─────────────────────────────────────────────────────────────── */

int main(void) {
    printf("\n%s%s", ARI_CLR_BOLD, ARI_CLR_CYAN);
    printf("\xe2\x95\x94\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x97\n");
    printf("\xe2\x95\x91     ARICODE PODIUM SYSTEM - TEST SUITE         \xe2\x95\x91\n");
    printf("\xe2\x95\x9a\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x9d\n");
    printf("%s\n", ARI_CLR_RESET);

    /* Run all tests */
    test_gold();
    test_silver();
    test_bronze();
    test_iron();
    test_boundaries();
    test_full_podium();
    test_rank_helpers();

    printf("\n%s%s ALL PODIUM SYSTEM TESTS PASSED %s\n\n",
        ARI_CLR_GREEN, ARI_CLR_BOLD, ARI_CLR_RESET);

    return 0;
}
