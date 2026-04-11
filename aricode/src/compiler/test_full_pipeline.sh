#!/bin/bash
#
# ARICODE Full Pipeline Test
# ==========================
# Tests end-to-end compilation: .ari source -> parser -> codegen -> ELF binary -> execution
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ARIC="$SCRIPT_DIR/aric"
EXAMPLES="$SCRIPT_DIR/../../examples"
TMPDIR="/tmp/aricode_pipeline_test_$$"

# Colors
RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
CYAN='\033[36m'
BOLD='\033[1m'
DIM='\033[2m'
RESET='\033[0m'

PASS=0
FAIL=0
TOTAL=0

cleanup() {
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

mkdir -p "$TMPDIR"

# Build aric if needed
if [ ! -f "$ARIC" ]; then
    echo -e "${YELLOW}Building aric...${RESET}"
    make -C "$SCRIPT_DIR" > /dev/null 2>&1
fi

run_test() {
    local name="$1"
    local source="$2"
    local expected_exit="$3"
    local output_bin="$TMPDIR/$name"

    TOTAL=$((TOTAL + 1))

    # Compile
    if ! "$ARIC" "$source" -o "$output_bin" > "$TMPDIR/${name}.log" 2>&1; then
        echo -e "  ${RED}[FAIL]${RESET} $name: compilation failed"
        cat "$TMPDIR/${name}.log"
        FAIL=$((FAIL + 1))
        return
    fi

    # Check binary exists
    if [ ! -f "$output_bin" ]; then
        echo -e "  ${RED}[FAIL]${RESET} $name: binary not produced"
        FAIL=$((FAIL + 1))
        return
    fi

    # Check binary is executable
    if [ ! -x "$output_bin" ]; then
        echo -e "  ${RED}[FAIL]${RESET} $name: binary not executable"
        FAIL=$((FAIL + 1))
        return
    fi

    # Execute and check exit code
    set +e
    "$output_bin"
    local actual_exit=$?
    set -e

    if [ "$actual_exit" -eq "$expected_exit" ]; then
        local bin_size
        bin_size=$(stat -c %s "$output_bin" 2>/dev/null || stat -f %z "$output_bin" 2>/dev/null)
        echo -e "  ${GREEN}[PASS]${RESET} $name: exit=$actual_exit (expected $expected_exit), binary=${bin_size} bytes"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}[FAIL]${RESET} $name: exit=$actual_exit (expected $expected_exit)"
        FAIL=$((FAIL + 1))
    fi
}

# --- Inline source tests ---

run_inline_test() {
    local name="$1"
    local source="$2"
    local expected_exit="$3"
    local src_file="$TMPDIR/${name}.ari"

    echo "$source" > "$src_file"
    run_test "$name" "$src_file" "$expected_exit"
}

echo ""
echo -e "${BOLD}${CYAN}=== ARICODE Full Pipeline Tests ===${RESET}"
echo ""

# Test 1: hello.ari (37 + 5 = 42)
echo -e "${DIM}--- Example files ---${RESET}"
run_test "hello" "$EXAMPLES/hello.ari" 42

# Test 2: math.ari (add(37, 5) = 42)
run_test "math" "$EXAMPLES/math.ari" 42

# Test 3: Simple return
echo -e "${DIM}--- Inline tests ---${RESET}"
run_inline_test "return_zero" "fn main() -> i32 { return 0; }" 0

# Test 4: Return literal
run_inline_test "return_99" "fn main() -> i32 { return 99; }" 99

# Test 5: Subtraction
run_inline_test "subtraction" "fn main() -> i32 { return 50 - 8; }" 42

# Test 6: Multiplication
run_inline_test "multiplication" "fn main() -> i32 { return 6 * 7; }" 42

# Test 7: Division
run_inline_test "division" "fn main() -> i32 { return 84 / 2; }" 42

# Test 8: Modulo
run_inline_test "modulo" "fn main() -> i32 { return 142 % 100; }" 42

# Test 9: Variable usage
run_inline_test "variable" "fn main() -> i32 { let x: i32 = 42; return x; }" 42

# Test 10: Nested arithmetic
run_inline_test "nested_arith" "fn main() -> i32 { let a: i32 = 10; let b: i32 = 32; return a + b; }" 42

# Test 11: Function call with args
run_inline_test "func_call" "fn double(x: i32) -> i32 { return x * 2; }
fn main() -> i32 { return double(21); }" 42

# Test 12: Multiple function calls
run_inline_test "multi_func" "fn add(a: i32, b: i32) -> i32 { return a + b; }
fn sub(a: i32, b: i32) -> i32 { return a - b; }
fn main() -> i32 { return add(sub(50, 8), 0); }" 42

# Test 13: If-else
run_inline_test "if_else" "fn main() -> i32 {
  let x: i32 = 1;
  if (x == 1) { return 42; } else { return 0; }
}" 42

# Test 14: Comparison operators
run_inline_test "comparison" "fn main() -> i32 {
  let a: i32 = 10;
  let b: i32 = 20;
  if (a < b) { return 42; } else { return 0; }
}" 42

# Test 15: Default a.out name
echo -e "${DIM}--- Default output name ---${RESET}"
TOTAL=$((TOTAL + 1))
(cd "$TMPDIR" && "$ARIC" "$EXAMPLES/hello.ari" > /dev/null 2>&1) || true
if [ -x "$TMPDIR/a.out" ]; then
    set +e
    "$TMPDIR/a.out"
    actual=$?
    set -e
    if [ "$actual" -eq 42 ]; then
        echo -e "  ${GREEN}[PASS]${RESET} default_output: a.out works (exit=42)"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}[FAIL]${RESET} default_output: a.out exit=$actual (expected 42)"
        FAIL=$((FAIL + 1))
    fi
else
    echo -e "  ${RED}[FAIL]${RESET} default_output: a.out not produced"
    FAIL=$((FAIL + 1))
fi

# --- Summary ---
echo ""
echo -e "${BOLD}${CYAN}=== Results ===${RESET}"
echo -e "  Total:  $TOTAL"
echo -e "  Passed: ${GREEN}${BOLD}$PASS${RESET}"
echo -e "  Failed: ${RED}${BOLD}$FAIL${RESET}"

# Show binary sizes
echo ""
echo -e "${DIM}--- Binary sizes ---${RESET}"
for f in "$TMPDIR"/hello "$TMPDIR"/math; do
    if [ -f "$f" ]; then
        sz=$(stat -c %s "$f" 2>/dev/null || stat -f %z "$f" 2>/dev/null)
        echo -e "  $(basename "$f"): ${sz} bytes"
    fi
done

echo ""

if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}${BOLD}All $TOTAL tests passed.${RESET}"
    exit 0
else
    echo -e "${RED}${BOLD}$FAIL test(s) failed.${RESET}"
    exit 1
fi
