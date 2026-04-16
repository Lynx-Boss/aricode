#!/bin/bash
# ============================================================================
#  ARICODE - Automated Test Suite
# ============================================================================
#  Compiles and runs every example, verifies output.
#  Exit 0 = all pass, Exit 1 = failures found.
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ARIC="$SCRIPT_DIR/../src/compiler/aric"
EXAMPLES="$SCRIPT_DIR/../examples"
PASS=0
FAIL=0
TOTAL=0

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
RESET='\033[0m'

run_test() {
    local name="$1" file="$2" input="$3" expect="$4"
    TOTAL=$((TOTAL + 1))
    printf "  [%2d] %-30s" "$TOTAL" "$name"

    # Compile
    if ! "$ARIC" "$file" -o "/tmp/aritest_$name" >/dev/null 2>&1; then
        printf "${RED}COMPILE FAIL${RESET}\n"
        FAIL=$((FAIL + 1))
        return
    fi

    # Run
    local output
    if [ -n "$input" ]; then
        output=$(echo -e "$input" | "/tmp/aritest_$name" 2>/dev/null) || true
    else
        output=$("/tmp/aritest_$name" 2>/dev/null) || true
    fi

    # Check
    if echo "$output" | grep -qF "$expect"; then
        printf "${GREEN}PASS${RESET}\n"
        PASS=$((PASS + 1))
    else
        printf "${RED}FAIL${RESET} ${DIM}(expected '$expect')${RESET}\n"
        FAIL=$((FAIL + 1))
    fi

    rm -f "/tmp/aritest_$name"
}

echo ""
echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo -e "${BOLD}${CYAN}  ARICODE - Automated Test Suite${RESET}"
echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo ""

# ── Basic Output ──
echo -e "${BOLD}--- Basic Output ---${RESET}"
run_test "hello_world"    "$EXAMPLES/hello_world.ari" "" "Hello, World!"
run_test "fibonacci"      "$EXAMPLES/fibonacci.ari"   "" "4181"
run_test "primes"         "$EXAMPLES/primes.ari"      "" "97"

# ── Arithmetic ──
echo -e "${BOLD}--- Arithmetic ---${RESET}"

cat > /tmp/aritest_arith.ari << 'EOF'
fn main() -> i32 {
    print_int(37 + 5);
    print_int(100 - 58);
    print_int(6 * 7);
    print_int(84 / 2);
    print_int(47 % 10);
    return 0;
}
EOF
run_test "add"            "/tmp/aritest_arith.ari" "" "42"

cat > /tmp/aritest_compound.ari << 'EOF'
fn main() -> i32 {
    let x: i32 = 10;
    x += 5;
    x -= 3;
    x *= 4;
    x /= 6;
    print_int(x);
    return 0;
}
EOF
run_test "compound_assign" "/tmp/aritest_compound.ari" "" "8"

# ── Control Flow ──
echo -e "${BOLD}--- Control Flow ---${RESET}"

cat > /tmp/aritest_if.ari << 'EOF'
fn main() -> i32 {
    let x: i32 = 42;
    if (x > 40) { print_str("big"); }
    if (x < 10) { print_str("small"); }
    return 0;
}
EOF
run_test "if_else"        "/tmp/aritest_if.ari" "" "big"

cat > /tmp/aritest_while.ari << 'EOF'
fn main() -> i32 {
    let i: i32 = 0;
    let sum: i32 = 0;
    while (i < 10) { sum += i; i += 1; }
    print_int(sum);
    return 0;
}
EOF
run_test "while_loop"     "/tmp/aritest_while.ari" "" "45"

cat > /tmp/aritest_for.ari << 'EOF'
fn main() -> i32 {
    let sum: i32 = 0;
    for (let i: i32 = 1; i <= 10; i += 1) { sum += i; }
    print_int(sum);
    return 0;
}
EOF
run_test "for_loop"       "/tmp/aritest_for.ari" "" "55"

cat > /tmp/aritest_break.ari << 'EOF'
fn main() -> i32 {
    let i: i32 = 0;
    while (i < 100) {
        if (i == 7) { break; }
        i += 1;
    }
    print_int(i);
    return 0;
}
EOF
run_test "break"          "/tmp/aritest_break.ari" "" "7"

cat > /tmp/aritest_match.ari << 'EOF'
fn main() -> i32 {
    let x: i32 = 2;
    match (x) {
        1 => { print_str("one"); },
        2 => { print_str("two"); },
        3 => { print_str("three"); },
    }
    return 0;
}
EOF
run_test "match"          "/tmp/aritest_match.ari" "" "two"

cat > /tmp/aritest_ternary.ari << 'EOF'
fn main() -> i32 {
    let x: i32 = 5 > 3 ? 42 : 0;
    print_int(x);
    return 0;
}
EOF
run_test "ternary"        "/tmp/aritest_ternary.ari" "" "42"

# ── Functions ──
echo -e "${BOLD}--- Functions ---${RESET}"

cat > /tmp/aritest_fn.ari << 'EOF'
fn add(a: i32, b: i32) -> i32 { return a + b; }
fn main() -> i32 { print_int(add(37, 5)); return 0; }
EOF
run_test "functions"      "/tmp/aritest_fn.ari" "" "42"

cat > /tmp/aritest_recur.ari << 'EOF'
fn fib(n: i32) -> i32 {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}
fn main() -> i32 { print_int(fib(10)); return 0; }
EOF
run_test "recursion"      "/tmp/aritest_recur.ari" "" "55"

# ── Types ──
echo -e "${BOLD}--- Types ---${RESET}"

cat > /tmp/aritest_f64.ari << 'EOF'
fn main() -> i32 {
    let a: f64 = 3.14;
    let b: f64 = 2.0;
    print_float(a + b);
    return 0;
}
EOF
run_test "f64_arithmetic" "/tmp/aritest_f64.ari" "" "5.14"

cat > /tmp/aritest_dec.ari << 'EOF'
fn main() -> i32 {
    print_dec(dec("0.1") + dec("0.2"));
    return 0;
}
EOF
run_test "decimal_exact"  "/tmp/aritest_dec.ari" "" "0.3"

# ── Data Structures ──
echo -e "${BOLD}--- Data Structures ---${RESET}"

cat > /tmp/aritest_arr.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_new(3);
    arr_set(a, 0, 10);
    arr_set(a, 1, 20);
    arr_set(a, 2, 30);
    print_int(arr_get(a, 1));
    print_int(arr_len(a));
    return 0;
}
EOF
run_test "arrays"         "/tmp/aritest_arr.ari" "" "20"

cat > /tmp/aritest_str.ari << 'EOF'
fn main() -> i32 {
    let s: i32 = str_new("hello");
    print_int(str_len(s));
    let a: i32 = str_new("test");
    let b: i32 = str_new("test");
    print_int(str_eq(a, b));
    return 0;
}
EOF
run_test "strings"        "/tmp/aritest_str.ari" "" "5"

cat > /tmp/aritest_concat.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = str_new("Hello");
    let b: i32 = str_new(" World!");
    let c: i32 = str_concat(a, b);
    str_println(c);
    return 0;
}
EOF
run_test "str_concat"     "/tmp/aritest_concat.ari" "" "Hello World!"

run_test "sort"           "$EXAMPLES/sort.ari" "" "93"
run_test "structs"        "$EXAMPLES/structs.ari" "" "25"

# ── Error Handling ──
echo -e "${BOLD}--- Error Handling ---${RESET}"
run_test "try_catch"      "$EXAMPLES/error_handling.ari" "" "ERROR caught!"

# ── I/O ──
echo -e "${BOLD}--- I/O ---${RESET}"

cat > /tmp/aritest_read.ari << 'EOF'
fn main() -> i32 {
    let x: i32 = read_int();
    print_int(x * 2);
    return 0;
}
EOF
run_test "read_int"       "/tmp/aritest_read.ari" "21" "42"
run_test "file_io"        "$EXAMPLES/fileio.ari" "" "Hello from aricode!"

# ── SIMD / Arrays ──
echo -e "${BOLD}--- SIMD ---${RESET}"

cat > /tmp/aritest_simd.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_new(5);
    arr_set(a, 0, 10);
    arr_set(a, 1, 20);
    arr_set(a, 2, 30);
    arr_set(a, 3, 40);
    arr_set(a, 4, 50);
    print_int(arr_sum(a));
    return 0;
}
EOF
run_test "arr_sum"        "/tmp/aritest_simd.ari" "" "150"

cat > /tmp/aritest_fill.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_new(4);
    arr_fill(a, 7);
    print_int(arr_get(a, 0));
    print_int(arr_get(a, 3));
    return 0;
}
EOF
run_test "arr_fill"       "/tmp/aritest_fill.ari" "" "7"

cat > /tmp/aritest_dot.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_new(3);
    let b: i32 = arr_new(3);
    arr_set(a, 0, 1); arr_set(a, 1, 2); arr_set(a, 2, 3);
    arr_set(b, 0, 4); arr_set(b, 1, 5); arr_set(b, 2, 6);
    print_int(arr_dot(a, b));
    return 0;
}
EOF
run_test "arr_dot"        "/tmp/aritest_dot.ari" "" "32"

# ── f64 Arrays ──
echo -e "${BOLD}--- f64 Arrays ---${RESET}"

cat > /tmp/aritest_f64.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f64_new(3);
    arr_f64_set(a, 0, 1.5);
    arr_f64_set(a, 1, 2.5);
    arr_f64_set(a, 2, 3.0);
    print_float(arr_f64_sum(a));
    return 0;
}
EOF
run_test "f64_sum"        "/tmp/aritest_f64.ari" "" "7.00"

cat > /tmp/aritest_f64dot.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f64_new(2);
    let b: i32 = arr_f64_new(2);
    arr_f64_set(a, 0, 2.0); arr_f64_set(a, 1, 3.0);
    arr_f64_set(b, 0, 4.0); arr_f64_set(b, 1, 5.0);
    print_float(arr_f64_dot(a, b));
    return 0;
}
EOF
run_test "f64_dot"        "/tmp/aritest_f64dot.ari" "" "23.00"

# ── Math ──
echo -e "${BOLD}--- Math ---${RESET}"

cat > /tmp/aritest_sqrt.ari << 'EOF'
fn main() -> i32 {
    print_float(math_sqrt(25.0));
    return 0;
}
EOF
run_test "math_sqrt"      "/tmp/aritest_sqrt.ari" "" "5.00"

cat > /tmp/aritest_exp.ari << 'EOF'
fn main() -> i32 {
    print_float(math_exp(0.0));
    return 0;
}
EOF
run_test "math_exp"       "/tmp/aritest_exp.ari" "" "1.00"

cat > /tmp/aritest_abs.ari << 'EOF'
fn main() -> i32 {
    let x: f64 = 0.0 - 5.0;
    print_float(math_abs(x));
    return 0;
}
EOF
run_test "math_abs"       "/tmp/aritest_abs.ari" "" "5.000000"

# ── Security ──
echo -e "${BOLD}--- Security ---${RESET}"

cat > /tmp/aritest_bounds.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_new(3);
    arr_set(a, 0, 42);
    print_int(arr_get(a, 0));
    print_int(arr_get(a, 99));
    return 0;
}
EOF
run_test "bounds_check"   "/tmp/aritest_bounds.ari" "" "42"

cat > /tmp/aritest_divzero.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = 0;
    let b: i32 = 10 / a;
    print_int(b);
    return 0;
}
EOF
# Should print error message, not crash
output=$("$ARIC" /tmp/aritest_divzero.ari -o /tmp/aritest_divzero_bin >/dev/null 2>&1 && /tmp/aritest_divzero_bin 2>&1 || true)
TOTAL=$((TOTAL + 1))
printf "  [%2d] %-30s" "$TOTAL" "div_zero_guard"
if echo "$output" | grep -q "Runtime error"; then
    printf "${GREEN}PASS${RESET}\n"
    PASS=$((PASS + 1))
else
    printf "${RED}FAIL${RESET}\n"
    FAIL=$((FAIL + 1))
fi

# ── Imports ──
echo -e "${BOLD}--- Imports ---${RESET}"

mkdir -p /tmp/aritest_import
echo 'fn add_lib(a: i32, b: i32) -> i32 { return a + b; }' > /tmp/aritest_import/lib.ari
echo 'import "lib.ari";' > /tmp/aritest_import/main.ari
echo 'fn main() -> i32 { print_int(add_lib(30, 12)); return 0; }' >> /tmp/aritest_import/main.ari
run_test "plain_import"   "/tmp/aritest_import/main.ari" "" "42"

echo 'import "lib.ari" as lib;' > /tmp/aritest_import/ns_main.ari
echo 'fn main() -> i32 { print_int(lib.add_lib(30, 12)); return 0; }' >> /tmp/aritest_import/ns_main.ari
run_test "ns_import"      "/tmp/aritest_import/ns_main.ari" "" "42"

# ── Memory ──
echo -e "${BOLD}--- Memory ---${RESET}"

cat > /tmp/aritest_memfree.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_new(10);
    arr_set(a, 0, 99);
    print_int(arr_get(a, 0));
    mem_free(a);
    print_str("freed");
    return 0;
}
EOF
run_test "mem_free"       "/tmp/aritest_memfree.ari" "" "99"

# ── Summary ──
echo ""
echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo -e "  Total:  ${BOLD}${TOTAL}${RESET}"
echo -e "  Passed: ${GREEN}${BOLD}${PASS}${RESET}"
echo -e "  Failed: ${RED}${BOLD}${FAIL}${RESET}"
echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo ""

[ "$FAIL" -eq 0 ]
