/*
 * ARICODE Error Codes
 * ==================
 * Every possible error in Ari Code, organized by severity level.
 *
 * Naming convention:  ARI_<Level><NNN>
 *   S = Silent (Level 0)   - compile-time blocks
 *   L = Logic  (Level 1)   - runtime crashes with full diagnostics
 *   W = Warning (Level 2)  - compile-time warnings, always logged
 *   Y = System (Level 3)   - runtime, must be caught by programmer
 *   C = Catastrophic (Level 4) - runtime, log and terminate
 */

#ifndef ARI_ERROR_CODES_H
#define ARI_ERROR_CODES_H

/* ════════════════════════════════════════════════════════════════════════
 * Level 0 - SILENT (detected at compile time, BLOCKS compilation)
 * These represent code that COULD silently fail. aricode refuses to
 * compile such code. The programmer MUST handle these explicitly.
 * ════════════════════════════════════════════════════════════════════════ */

#define ARI_S001_CODE    "ARI-S001"
#define ARI_S001_MSG     "Unhandled division - possible division by zero"
#define ARI_S001_FIX     "Add a zero-check guard before the division, or use safe_div()."

#define ARI_S002_CODE    "ARI-S002"
#define ARI_S002_MSG     "Unhandled null/None access"
#define ARI_S002_FIX     "Check for null before dereferencing, or use the ?. operator."

#define ARI_S003_CODE    "ARI-S003"
#define ARI_S003_MSG     "Unhandled array bounds access"
#define ARI_S003_FIX     "Validate the index against array length, or use arr.get(i)."

#define ARI_S004_CODE    "ARI-S004"
#define ARI_S004_MSG     "Empty catch block (swallowing errors)"
#define ARI_S004_FIX     "Handle the error explicitly or re-raise it. Never swallow errors."

#define ARI_S005_CODE    "ARI-S005"
#define ARI_S005_MSG     "Unused return value of function that can fail"
#define ARI_S005_FIX     "Assign the return value and check for errors, or use _ = fn() to discard explicitly."

#define ARI_S006_CODE    "ARI-S006"
#define ARI_S006_MSG     "Implicit type conversion with data loss"
#define ARI_S006_FIX     "Use an explicit cast: as<TargetType>(value)."

/* ════════════════════════════════════════════════════════════════════════
 * Level 1 - LOGIC (runtime, crash with full diagnostic info)
 * These are programming errors that slipped past compile-time checks
 * (e.g., values only known at runtime). The program MUST terminate
 * with a full error report so the programmer can fix the bug.
 * ════════════════════════════════════════════════════════════════════════ */

#define ARI_L001_CODE    "ARI-L001"
#define ARI_L001_MSG     "Division by zero"
#define ARI_L001_FIX     "Add a check before dividing: if (b == 0) { error.raise(Level.LOGIC, \"div/0\"); }"

#define ARI_L002_CODE    "ARI-L002"
#define ARI_L002_MSG     "Null/None access"
#define ARI_L002_FIX     "Check for null before accessing: if (ptr != null) { ... }"

#define ARI_L003_CODE    "ARI-L003"
#define ARI_L003_MSG     "Array index out of bounds"
#define ARI_L003_FIX     "Validate index: if (i >= 0 && i < arr.len) { ... }"

#define ARI_L004_CODE    "ARI-L004"
#define ARI_L004_MSG     "Integer overflow"
#define ARI_L004_FIX     "Use checked arithmetic: a.checked_add(b), or widen the type."

#define ARI_L005_CODE    "ARI-L005"
#define ARI_L005_MSG     "Stack overflow"
#define ARI_L005_FIX     "Reduce recursion depth or convert to an iterative algorithm."

#define ARI_L006_CODE    "ARI-L006"
#define ARI_L006_MSG     "Type assertion failed"
#define ARI_L006_FIX     "Use pattern matching instead of type assertion, or verify the type first."

/* ════════════════════════════════════════════════════════════════════════
 * Level 2 - WARNING (compile time, logged but compilation proceeds)
 * These indicate suspicious code that is likely a bug but not
 * provably wrong. Always logged; can be promoted to errors via config.
 * ════════════════════════════════════════════════════════════════════════ */

#define ARI_W001_CODE    "ARI-W001"
#define ARI_W001_MSG     "Unused variable"
#define ARI_W001_FIX     "Remove the variable or prefix with _ to mark intentionally unused."

#define ARI_W002_CODE    "ARI-W002"
#define ARI_W002_MSG     "Unreachable code"
#define ARI_W002_FIX     "Remove the unreachable code or fix the control flow."

#define ARI_W003_CODE    "ARI-W003"
#define ARI_W003_MSG     "Shadowed variable"
#define ARI_W003_FIX     "Rename the inner variable to avoid confusion."

#define ARI_W004_CODE    "ARI-W004"
#define ARI_W004_MSG     "Comparison always true/false"
#define ARI_W004_FIX     "Remove the redundant comparison or fix the logic."

#define ARI_W005_CODE    "ARI-W005"
#define ARI_W005_MSG     "Function too complex (cyclomatic complexity exceeds threshold)"
#define ARI_W005_FIX     "Break the function into smaller, focused functions."

/* ════════════════════════════════════════════════════════════════════════
 * Level 3 - SYSTEM (runtime, MUST be caught by the programmer)
 * These come from the operating environment and are outside the
 * programmer's direct control, but MUST be handled. Uncaught system
 * errors are promoted to LOGIC errors (the programmer should have
 * anticipated them).
 * ════════════════════════════════════════════════════════════════════════ */

#define ARI_Y001_CODE    "ARI-Y001"
#define ARI_Y001_MSG     "File not found"
#define ARI_Y001_FIX     "Check if the file exists before opening, or handle the error in a catch block."

#define ARI_Y002_CODE    "ARI-Y002"
#define ARI_Y002_MSG     "Network error"
#define ARI_Y002_FIX     "Add retry logic and a timeout handler."

#define ARI_Y003_CODE    "ARI-Y003"
#define ARI_Y003_MSG     "Permission denied"
#define ARI_Y003_FIX     "Check file/resource permissions before access."

#define ARI_Y004_CODE    "ARI-Y004"
#define ARI_Y004_MSG     "Timeout"
#define ARI_Y004_FIX     "Increase the timeout or add a fallback path."

/* ════════════════════════════════════════════════════════════════════════
 * Level 4 - CATASTROPHIC (runtime, log everything and die gracefully)
 * These indicate the runtime environment itself is compromised.
 * The only safe action is to log as much diagnostic info as possible
 * and terminate immediately.
 * ════════════════════════════════════════════════════════════════════════ */

#define ARI_C001_CODE    "ARI-C001"
#define ARI_C001_MSG     "Out of memory"
#define ARI_C001_FIX     "Reduce memory usage or increase system memory."

#define ARI_C002_CODE    "ARI-C002"
#define ARI_C002_MSG     "Stack corruption"
#define ARI_C002_FIX     "Check for buffer overflows and wild pointer writes."

#define ARI_C003_CODE    "ARI-C003"
#define ARI_C003_MSG     "Hardware fault"
#define ARI_C003_FIX     "Check hardware integrity; this is outside software control."

/* ── Total error code count per level ──────────────────────────────────── */

#define ARI_SILENT_COUNT       6
#define ARI_LOGIC_COUNT        6
#define ARI_WARNING_COUNT      5
#define ARI_SYSTEM_COUNT       4
#define ARI_CATASTROPHIC_COUNT 3
#define ARI_TOTAL_ERROR_CODES  (ARI_SILENT_COUNT + ARI_LOGIC_COUNT + ARI_WARNING_COUNT + ARI_SYSTEM_COUNT + ARI_CATASTROPHIC_COUNT)

#endif /* ARI_ERROR_CODES_H */
