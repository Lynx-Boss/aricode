/*
 * ARICODE Error System - Test Suite
 * =================================
 * Tests error creation, emission, and logging at every level.
 */

#include "error_levels.h"
#include "error_codes.h"
#include "error_system.h"
#include "error_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ── Helper: section banner ───────────────────────────────────────────── */

static void banner(const char* title) {
    printf("\n%s%s========================================%s\n",
        ARI_CLR_BOLD, ARI_CLR_CYAN, ARI_CLR_RESET);
    printf("%s%s  %s%s\n",
        ARI_CLR_BOLD, ARI_CLR_CYAN, title, ARI_CLR_RESET);
    printf("%s%s========================================%s\n\n",
        ARI_CLR_BOLD, ARI_CLR_CYAN, ARI_CLR_RESET);
}

/* ── Test: Error creation ─────────────────────────────────────────────── */

static void test_error_create(void) {
    banner("TEST: Error Creation");

    AriError err = ari_error_create(
        ARI_LEVEL_LOGIC,
        ARI_L001_CODE,
        ARI_L001_MSG,
        "main.vt",
        15, 8,
        "  return a / b;",
        ARI_L001_FIX
    );

    assert(err.level == ARI_LEVEL_LOGIC);
    assert(strcmp(err.code, "ARI-L001") == 0);
    assert(err.line == 15);
    assert(err.column == 8);
    assert(err.timestamp > 0);

    printf("%sOK%s: Error created with correct fields\n", ARI_CLR_GREEN, ARI_CLR_RESET);
}

/* ── Test: Level 0 - SILENT (FORBIDDEN) ───────────────────────────────── */

static void test_level_silent(void) {
    banner("TEST: Level 0 - SILENT (FORBIDDEN)");

    AriError err = ari_error_create(
        ARI_LEVEL_SILENT,
        ARI_S001_CODE,
        ARI_S001_MSG,
        "math.vt",
        42, 12,
        "  let result = x / y;",
        ARI_S001_FIX
    );
    ari_error_emit(&err);

    err = ari_error_create(
        ARI_LEVEL_SILENT,
        ARI_S004_CODE,
        ARI_S004_MSG,
        "handler.vt",
        88, 3,
        "  catch(e) {}",
        ARI_S004_FIX
    );
    ari_error_emit(&err);

    printf("\n%sOK%s: Silent errors emitted (these block compilation)\n",
        ARI_CLR_GREEN, ARI_CLR_RESET);
}

/* ── Test: Level 1 - LOGIC (INADMISSIBLE) ─────────────────────────────── */

static void test_level_logic(void) {
    banner("TEST: Level 1 - LOGIC (INADMISSIBLE)");

    AriError err = ari_error_create(
        ARI_LEVEL_LOGIC,
        ARI_L001_CODE,
        ARI_L001_MSG,
        "main.vt",
        15, 8,
        "  return a / b;",
        ARI_L001_FIX
    );
    ari_error_emit(&err);

    err = ari_error_create(
        ARI_LEVEL_LOGIC,
        ARI_L003_CODE,
        ARI_L003_MSG,
        "data.vt",
        103, 14,
        "  let val = arr[idx];",
        ARI_L003_FIX
    );
    ari_error_emit(&err);

    printf("\n%sOK%s: Logic errors emitted (these crash the program)\n",
        ARI_CLR_GREEN, ARI_CLR_RESET);
}

/* ── Test: Level 2 - WARNING ──────────────────────────────────────────── */

static void test_level_warning(void) {
    banner("TEST: Level 2 - WARNING");

    AriError err = ari_error_create(
        ARI_LEVEL_WARNING,
        ARI_W001_CODE,
        ARI_W001_MSG,
        "utils.vt",
        7, 5,
        "  let temp = 42;",
        ARI_W001_FIX
    );
    ari_error_emit(&err);

    err = ari_error_create(
        ARI_LEVEL_WARNING,
        ARI_W005_CODE,
        ARI_W005_MSG,
        "parser.vt",
        200, 1,
        "fn parse_everything(input: str) -> Result {",
        ARI_W005_FIX
    );
    ari_error_emit(&err);

    printf("\n%sOK%s: Warnings emitted (logged but compilation proceeds)\n",
        ARI_CLR_GREEN, ARI_CLR_RESET);
}

/* ── Test: Level 3 - SYSTEM ───────────────────────────────────────────── */

static void test_level_system(void) {
    banner("TEST: Level 3 - SYSTEM");

    AriError err = ari_error_create(
        ARI_LEVEL_SYSTEM,
        ARI_Y001_CODE,
        ARI_Y001_MSG,
        "io.vt",
        33, 10,
        "  let f = File.open(path);",
        ARI_Y001_FIX
    );
    ari_error_emit(&err);

    printf("\n%sOK%s: System error emitted (must be caught by programmer)\n",
        ARI_CLR_GREEN, ARI_CLR_RESET);
}

/* ── Test: Level 4 - CATASTROPHIC ─────────────────────────────────────── */

static void test_level_catastrophic(void) {
    banner("TEST: Level 4 - CATASTROPHIC");

    AriError err = ari_error_create(
        ARI_LEVEL_CATASTROPHIC,
        ARI_C001_CODE,
        ARI_C001_MSG,
        "allocator.vt",
        512, 1,
        "  let buf = alloc(1_000_000_000);",
        ARI_C001_FIX
    );
    ari_error_emit(&err);

    printf("\n%sOK%s: Catastrophic error emitted (log and die)\n",
        ARI_CLR_GREEN, ARI_CLR_RESET);
}

/* ── Test: Registry queries ───────────────────────────────────────────── */

static void test_registry_queries(void) {
    banner("TEST: Registry Queries");

    AriError results[16];
    int count;

    count = ari_registry_query(ARI_LEVEL_SILENT, results, 16);
    printf("  SILENT errors in registry: %d\n", count);
    assert(count == 2);

    count = ari_registry_query(ARI_LEVEL_LOGIC, results, 16);
    printf("  LOGIC errors in registry:  %d\n", count);
    assert(count == 2);

    count = ari_registry_query(ARI_LEVEL_WARNING, results, 16);
    printf("  WARNING errors in registry: %d\n", count);
    assert(count == 2);

    count = ari_registry_query(ARI_LEVEL_SYSTEM, results, 16);
    printf("  SYSTEM errors in registry:  %d\n", count);
    assert(count == 1);

    count = ari_registry_query(ARI_LEVEL_CATASTROPHIC, results, 16);
    printf("  CATASTROPHIC errors in registry: %d\n", count);
    assert(count == 1);

    printf("\n%sOK%s: All registry queries passed\n",
        ARI_CLR_GREEN, ARI_CLR_RESET);
}

/* ── Test: Error formatting ───────────────────────────────────────────── */

static void test_error_format(void) {
    banner("TEST: Error Format Buffer");

    AriError err = ari_error_create(
        ARI_LEVEL_LOGIC,
        ARI_L002_CODE,
        ARI_L002_MSG,
        "ptr.vt",
        50, 5,
        "  val = ptr.value;",
        ARI_L002_FIX
    );

    char buf[ARI_ERROR_FMT_BUFSIZE];
    char* result = ari_error_format(&err, buf, sizeof(buf));
    assert(result != NULL);
    assert(strlen(buf) > 0);

    /* Verify the buffer contains key info */
    assert(strstr(buf, "ARI-L002") != NULL);
    assert(strstr(buf, "LOGIC") != NULL);

    printf("%sOK%s: Error formatted into buffer (%zu bytes)\n",
        ARI_CLR_GREEN, ARI_CLR_RESET, strlen(buf));
}

/* ── Main ─────────────────────────────────────────────────────────────── */

int main(void) {
    printf("\n%s%s", ARI_CLR_BOLD, ARI_CLR_CYAN);
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║     ARICODE ERROR SYSTEM - TEST SUITE         ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    printf("%s\n", ARI_CLR_RESET);

    /* Initialize the registry */
    ari_registry_init(".aricode/errors.log");

    /* Run all tests */
    test_error_create();
    test_level_silent();
    test_level_logic();
    test_level_warning();
    test_level_system();
    test_level_catastrophic();
    test_registry_queries();
    test_error_format();

    /* Print registry summary */
    ari_registry_summary();

    /* Shutdown */
    ari_registry_shutdown();

    printf("%s%s ALL ERROR SYSTEM TESTS PASSED %s\n\n",
        ARI_CLR_BG_RED, ARI_CLR_WHITE ARI_CLR_BOLD, ARI_CLR_RESET);

    /* Actually use green for success */
    printf("%s%s ALL ERROR SYSTEM TESTS PASSED %s\n\n",
        ARI_CLR_GREEN, ARI_CLR_BOLD, ARI_CLR_RESET);

    return 0;
}
