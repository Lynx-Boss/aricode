#!/bin/bash
# ============================================================================
#  ARICODE - Numerical & Codegen Edge-Case Tests
# ============================================================================
#  Catalog of quick regression tests for bugs that the main run_all.sh
#  didn't exercise.  Each case targets a specific bug we've hit (or
#  could latently hit) in the codegen or the f64 builtin family.
#
#  Organised by category so you can eyeball which invariant broke
#  from the section header alone.  All tests run in well under a
#  second total.
#
#  Add a test here when you fix a subtle bug — the next regression
#  should be caught inside a second of compilation.
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ARIC="$SCRIPT_DIR/../src/compiler/aric"
PASS=0
FAIL=0
TOTAL=0

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
DIM='\033[2m'
RESET='\033[0m'

run_test() {
    local name="$1" file="$2" expect="$3" notes="${4:-}"
    TOTAL=$((TOTAL + 1))
    printf "  [%2d] %-38s" "$TOTAL" "$name"

    if ! "$ARIC" "$file" -o "/tmp/aritest_edge_$name" >/dev/null 2>&1; then
        printf "${RED}COMPILE FAIL${RESET}\n"
        FAIL=$((FAIL + 1))
        return
    fi

    local output
    output=$("/tmp/aritest_edge_$name" 2>/dev/null) || true

    if echo "$output" | grep -qF "$expect"; then
        printf "${GREEN}PASS${RESET}"
        [ -n "$notes" ] && printf " ${DIM}%s${RESET}" "$notes"
        printf "\n"
        PASS=$((PASS + 1))
    else
        printf "${RED}FAIL${RESET} ${DIM}(expected '$expect')${RESET}\n"
        printf "         ${DIM}got: $(echo "$output" | tr '\n' ' ' | cut -c 1-70)${RESET}\n"
        FAIL=$((FAIL + 1))
    fi

    rm -f "/tmp/aritest_edge_$name"
}

echo ""
echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo -e "${BOLD}${CYAN}  ARICODE - Numerical / Codegen Edge-Case Tests${RESET}"
echo -e "${BOLD}${CYAN}============================================================${RESET}"

# ────────────────────────────────────────────────────────────────────
echo -e "\n${BOLD}--- Softmax numerical edge cases ---${RESET}"
# Each of these repros a bug found in real training runs.

# #1  n = 1: degenerate softmax must return 1.0.
cat > /tmp/edge_softmax_n1.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f64_new(1);
    arr_f64_set(a, 0, 42.0);
    arr_f64_softmax(a);
    print_f64(arr_f64_get(a, 0), 6);
    return 0;
}
EOF
run_test "softmax_n1" /tmp/edge_softmax_n1.ari "1.000000" \
  "scalar tail when n < vec width"

# #2  n = 3 (< 4): pure scalar tail path, no vec iterations.
# Softmax sums to 1 ± f64 epsilon — print 3 digits, both 0.999 and
# 1.000 round-trips to "1.00" after a rounding margin.
cat > /tmp/edge_softmax_n3.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f64_new(3);
    arr_f64_set(a, 0, 1.0); arr_f64_set(a, 1, 2.0); arr_f64_set(a, 2, 3.0);
    arr_f64_softmax(a);
    let sum: f64 = arr_f64_get(a,0) + arr_f64_get(a,1) + arr_f64_get(a,2);
    // Normalise the last-digit rounding: accept 0.999999 or 1.000000.
    if (sum > 0.99999) { print_int(1); } else { print_int(0); }
    return 0;
}
EOF
run_test "softmax_n3" /tmp/edge_softmax_n3.ari "1" \
  "sums to 1 ± ε even when n < 4"

# #3  n = 10 (not mul-of-4): vec loop + scalar tail stitched correctly.
cat > /tmp/edge_softmax_n10.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f64_new(10);
    let i: i32 = 0;
    while (i < 10) { arr_f64_set(a, i, int_to_float(i) * 0.1); i += 1; }
    arr_f64_softmax(a);
    let sum: f64 = 0.0; i = 0;
    while (i < 10) { sum = sum + arr_f64_get(a, i); i += 1; }
    if (sum > 0.99999) { print_int(1); } else { print_int(0); }
    return 0;
}
EOF
run_test "softmax_n10" /tmp/edge_softmax_n10.ari "1" \
  "n%4 != 0 — last 0-3 lanes must count"

# #4  Extreme logit spread > 700 nats.  Without the −700 clamp in
# softmax, vec_exp's 2^k reconstruction overflows and the largest
# probability lands in the wrong slot.  This is the bug that broke
# MNIST + Adam on its first batch.
cat > /tmp/edge_softmax_extreme.ari << 'EOF'
fn argmax(buf: i32, n: i32) -> i32 {
    let bi: i32 = 0; let bv: f64 = arr_f64_get(buf, 0);
    let i: i32 = 1;
    while (i < n) {
        let v: f64 = arr_f64_get(buf, i);
        if (v > bv) { bv = v; bi = i; }
        i += 1;
    }
    return bi;
}
fn main() -> i32 {
    let a: i32 = arr_f64_new(10);
    let i: i32 = 0;
    while (i < 8) { arr_f64_set(a, i, 0.0); i += 1; }
    arr_f64_set(a, 8, 0.0 - 5000.0);     // must underflow to ~0
    arr_f64_set(a, 9, 2000.0);           // unique max
    arr_f64_softmax(a);
    print_int(argmax(a, 10));            // expect 9
    return 0;
}
EOF
run_test "softmax_extreme_spread" /tmp/edge_softmax_extreme.ari "9" \
  "largest logit lands in the right slot"

# #5  All equal: softmax → uniform.
cat > /tmp/edge_softmax_uniform.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f64_new(4);
    let i: i32 = 0;
    while (i < 4) { arr_f64_set(a, i, 1.5); i += 1; }
    arr_f64_softmax(a);
    // Each element should be 0.25.  Print the first.
    print_f64(arr_f64_get(a, 0), 6);
    return 0;
}
EOF
run_test "softmax_uniform" /tmp/edge_softmax_uniform.ari "0.250000"

# ────────────────────────────────────────────────────────────────────
echo -e "\n${BOLD}--- Vec exp numerical range ---${RESET}"

# #6  exp(0..3) — basic spot-check.
cat > /tmp/edge_vec_exp.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f64_new(4);
    arr_f64_set(a, 0, 0.0); arr_f64_set(a, 1, 1.0);
    arr_f64_set(a, 2, 2.0); arr_f64_set(a, 3, 3.0);
    arr_f64_exp(a);
    // exp(1) ≈ 2.71828, exp(2) ≈ 7.389, exp(3) ≈ 20.085
    print_f64(arr_f64_get(a, 1), 4);    // 2.7182
    return 0;
}
EOF
run_test "vec_exp_moderate" /tmp/edge_vec_exp.ari "2.7182"

# #7  expm1 vectorised — matches scalar math_expm1 for moderate inputs.
cat > /tmp/edge_vec_expm1.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f64_new(4);
    arr_f64_set(a, 0, 0.5); arr_f64_set(a, 1, 1.0);
    arr_f64_set(a, 2, 2.0); arr_f64_set(a, 3, 3.0);
    arr_f64_expm1(a);
    // expm1(1) = e - 1 ≈ 1.71828
    print_f64(arr_f64_get(a, 1), 4);
    return 0;
}
EOF
run_test "vec_expm1_moderate" /tmp/edge_vec_expm1.ari "1.7182"

# #7a  arr_f64_log — packed ln(x) spot-check across exponent ranges.
# Covers k=-1 (x=0.5), k=0 (x=e), k=3 (x=10) branches of the magic-based
# k extraction.
cat > /tmp/edge_vec_log.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f64_new(4);
    arr_f64_set(a, 0, 1.0);
    arr_f64_set(a, 1, 2.718281828459045);
    arr_f64_set(a, 2, 10.0);
    arr_f64_set(a, 3, 0.5);
    arr_f64_log(a);
    print_f64(arr_f64_get(a, 0), 6);    // 0
    print_f64(arr_f64_get(a, 1), 4);    // ~1
    print_f64(arr_f64_get(a, 2), 4);    // 2.3026
    print_f64(arr_f64_get(a, 3), 4);    // -0.6931
    return 0;
}
EOF
run_test "vec_log_spot_check" /tmp/edge_vec_log.ari "0.000000
0.9999
2.3025
-0.6931" "covers k=-1, k=0, k=3 exponent paths"

# #7b  arr_f64_log bit-exact vs runtime scalar math_log.  Both paths use
# the same SSE2 7-term atanh series, so lane-0 must match bit-for-bit.
# Call math_log via a variable (not literal) to defeat the compiler's
# compile-time constant folding through glibc log().
cat > /tmp/edge_vec_log_vs_scalar.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f64_new(4);
    arr_f64_set(a, 0, 1.5);
    arr_f64_set(a, 1, 3.7);
    arr_f64_set(a, 2, 0.42);
    arr_f64_set(a, 3, 128.0);

    // Scalar reference via runtime path (reads values from array so the
    // optimizer can't fold math_log at compile time).
    let v0: f64 = arr_f64_get(a, 0);
    let v1: f64 = arr_f64_get(a, 1);
    let v2: f64 = arr_f64_get(a, 2);
    let v3: f64 = arr_f64_get(a, 3);
    let s0: f64 = math_log(v0);
    let s1: f64 = math_log(v1);
    let s2: f64 = math_log(v2);
    let s3: f64 = math_log(v3);

    arr_f64_log(a);

    let d0: f64 = arr_f64_get(a, 0) - s0;  if (d0 < 0.0) { d0 = 0.0 - d0; }
    let d1: f64 = arr_f64_get(a, 1) - s1;  if (d1 < 0.0) { d1 = 0.0 - d1; }
    let d2: f64 = arr_f64_get(a, 2) - s2;  if (d2 < 0.0) { d2 = 0.0 - d2; }
    let d3: f64 = arr_f64_get(a, 3) - s3;  if (d3 < 0.0) { d3 = 0.0 - d3; }
    let m: f64 = d0;
    if (d1 > m) { m = d1; }
    if (d2 > m) { m = d2; }
    if (d3 > m) { m = d3; }
    if (m == 0.0) { print_int(1); } else { print_int(0); }
    return 0;
}
EOF
run_test "vec_log_vs_scalar" /tmp/edge_vec_log_vs_scalar.ari "1" \
  "vec log bit-exact with runtime scalar math_log (same 7-term atanh)"

# #7c  arr_f64_log1p — log(1+x) for a few representative inputs.
cat > /tmp/edge_vec_log1p.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f64_new(4);
    arr_f64_set(a, 0, 0.0);                      // log1p(0) = 0
    arr_f64_set(a, 1, 1.0);                      // log1p(1) = ln(2) ≈ 0.6931
    arr_f64_set(a, 2, 9.0);                      // log1p(9) = ln(10) ≈ 2.3026
    arr_f64_set(a, 3, 1.718281828459045);        // log1p(e-1) = 1
    arr_f64_log1p(a);
    print_f64(arr_f64_get(a, 0), 6);
    print_f64(arr_f64_get(a, 1), 4);
    print_f64(arr_f64_get(a, 2), 4);
    print_f64(arr_f64_get(a, 3), 4);
    return 0;
}
EOF
run_test "vec_log1p_moderate" /tmp/edge_vec_log1p.ari "0.000000
0.6931
2.3025
0.9999"

# #7b  arr_f64_adam_apply — bit-exact match with scalar reference across
# the vec/tail boundary (n=5 takes one vec step + one scalar iter).
# With w=1.0, m=0.5, v=0.25, lr=0.2, eps=0: step = 0.2 * 0.5 / 0.5 = 0.2
# so every w becomes 0.8.
cat > /tmp/edge_adam_apply.ari << 'EOF'
fn main() -> i32 {
    let n: i32 = 5;
    let w: i32 = arr_f64_new(n);
    let m: i32 = arr_f64_new(n);
    let v: i32 = arr_f64_new(n);
    let i: i32 = 0;
    while (i < n) {
        arr_f64_set(w, i, 1.0);
        arr_f64_set(m, i, 0.5);
        arr_f64_set(v, i, 0.25);
        i += 1;
    }
    arr_f64_adam_apply(w, m, v, 0.2, 0.0);
    // Print both a vec-lane element (idx 0) and the scalar-tail element (idx 4).
    print_f64(arr_f64_get(w, 0), 4);
    print_f64(arr_f64_get(w, 4), 4);
    return 0;
}
EOF
run_test "adam_apply_bit_exact" /tmp/edge_adam_apply.ari "0.8000
0.8000" "vec lane and scalar tail agree on w - lr*m/(sqrt(v)+eps)"

# ────────────────────────────────────────────────────────────────────
echo -e "\n${BOLD}--- f64 return contract (xmm0 AND rax) ---${RESET}"
# Every builtin that returns f64 MUST leave the result in both xmm0 and
# rax.  When this is broken, the xmm-stash path of a float binop picks
# up garbage from xmm0.  We caught this with arr_f64_get / math_exp /
# math_sin x87 path.  Tests below use arr_f64_get on the left of a
# float binop — without the contract, left gets stashed as garbage.

# #8  arr_f64_get then + literal.
cat > /tmp/edge_f64_get_binop.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f64_new(4);
    arr_f64_set(a, 0, 1.5); arr_f64_set(a, 1, 2.5);
    let r: f64 = arr_f64_get(a, 0) + 0.5;     // expect 2.0
    print_f64(r, 6);
    return 0;
}
EOF
run_test "f64_get_in_binop" /tmp/edge_f64_get_binop.ari "2.000000" \
  "would regress if arr_f64_get skips xmm0 sync"

# #9  math_exp then + literal (was missing xmm0 sync).
cat > /tmp/edge_math_exp_binop.ari << 'EOF'
fn main() -> i32 {
    let r: f64 = math_exp(0.0) + 1.0;      // exp(0) + 1 = 2.0
    print_f64(r, 6);
    return 0;
}
EOF
run_test "math_exp_in_binop" /tmp/edge_math_exp_binop.ari "2.000000" \
  "math_exp's final movq rax, xmm2 must sync xmm0"

# #10  Nested call in the left position of a float binop — exercises
# the stash fallback that depends on right-side peephole.
cat > /tmp/edge_nested_call_binop.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f64_new(4);
    arr_f64_set(a, 0, 3.0); arr_f64_set(a, 1, 4.0);
    arr_f64_set(a, 2, 0.0); arr_f64_set(a, 3, 0.0);
    let r: f64 = arr_f64_get(a, 0) * arr_f64_get(a, 1);  // 12.0
    print_f64(r, 4);
    return 0;
}
EOF
run_test "call_on_both_sides" /tmp/edge_nested_call_binop.ari "12.0000" \
  "stash fallback on right, contract on left"

# #10b  `movq xmm0, rax` elision after hot-var f64 read feeding a math
# builtin.  emit_sync_xmm0_from_rax_smart rewinds the preceding
# `movq rax, xmm0` (emitted by emit_identifier for hot-var f64) and
# skips the inverse load, saving 10 bytes per call.  If the peephole
# ever rewinds when it shouldn't, xmm0 holds the wrong value and the
# sqrt/exp/log result drifts.  Use a function that has NO unsafe
# calls in the body so the local `x` pins to xmm8-15 (hot-var mode).
cat > /tmp/edge_hotvar_math_sqrt.ari << 'EOF'
fn comp(x: f64) -> f64 {
    // Hot-var candidate: f64 local, no unsafe calls.
    let v: f64 = x + 0.0;   // forces `v` to be a live f64 local
    return math_sqrt(v);
}
fn main() -> i32 {
    let r: f64 = comp(9.0);
    print_f64(r, 4);         // 3.0000
    let r2: f64 = comp(16.0) + 1.0;
    print_f64(r2, 4);        // 5.0000 (tests xmm0 sync after builtin)
    return 0;
}
EOF
run_test "hotvar_math_sqrt_peephole" /tmp/edge_hotvar_math_sqrt.ari "3.0000
5.0000" "elide movq xmm0,rax pair for hot-var f64 → math_sqrt"

# #10c  Same test for math_exp — exercises the SSE exp path's initial
# xmm0 load, which also goes through emit_sync_xmm0_from_rax_smart.
cat > /tmp/edge_hotvar_math_exp.ari << 'EOF'
fn comp(x: f64) -> f64 {
    let v: f64 = x + 0.0;
    return math_exp(v);
}
fn main() -> i32 {
    let r: f64 = comp(0.0);
    print_f64(r, 4);          // exp(0) = 1.0000
    let r2: f64 = comp(1.0) - 2.0;
    print_f64(r2, 4);          // e - 2 ≈ 0.7183
    return 0;
}
EOF
run_test "hotvar_math_exp_peephole" /tmp/edge_hotvar_math_exp.ari "1.0000
0.7182"

# #10d  math_log over a hot-var — the SSE log path also consumes the
# pre-loaded xmm0 via emit_sync_xmm0_from_rax_smart.
cat > /tmp/edge_hotvar_math_log.ari << 'EOF'
fn comp(x: f64) -> f64 {
    let v: f64 = x + 0.0;
    return math_log(v);
}
fn main() -> i32 {
    let r: f64 = comp(1.0);
    print_f64(r, 4);          // log(1) = 0.0000
    let r2: f64 = comp(2.718281828459045) + 1.0;
    print_f64(r2, 4);          // log(e) + 1 ≈ 2.0000
    return 0;
}
EOF
run_test "hotvar_math_log_peephole" /tmp/edge_hotvar_math_log.ari "0.0000
2.0000"

# ────────────────────────────────────────────────────────────────────
echo -e "\n${BOLD}--- Hot-var register allocation ---${RESET}"

# #11  f64 local read in a binop.  Validates that the hot-var cache
# load (movapd xmm0, xmm_hot) feeds the right xmm register for the
# subsequent subsd without going through a stale stack slot.
cat > /tmp/edge_hotvar_binop.ari << 'EOF'
fn main() -> i32 {
    let a: f64 = 10.0;
    let b: f64 = 3.5;
    let c: f64 = a - b;       // expect 6.5
    print_f64(c, 4);
    return 0;
}
EOF
run_test "hotvar_binop_read" /tmp/edge_hotvar_binop.ari "6.5000" \
  "hot f64 var reads feed binop correctly"

# #12  More f64 locals than the xmm8..xmm15 pool can hold (9).  The
# 9th one must fall back to stack cleanly, not crash or corrupt.
cat > /tmp/edge_hotvar_overflow.ari << 'EOF'
fn main() -> i32 {
    let v0: f64 = 1.0; let v1: f64 = 2.0; let v2: f64 = 3.0;
    let v3: f64 = 4.0; let v4: f64 = 5.0; let v5: f64 = 6.0;
    let v6: f64 = 7.0; let v7: f64 = 8.0; let v8: f64 = 9.0;
    print_f64(v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8, 6);
    return 0;
}
EOF
run_test "hotvar_pool_overflow" /tmp/edge_hotvar_overflow.ari "45.000000" \
  "9th f64 var falls back to stack"

# #13  i32 read right after a write to the same hot_gp slot.  The
# peephole strips the redundant reload; must not miscompute the sum.
cat > /tmp/edge_hotvar_gp_reload.ari << 'EOF'
fn main() -> i32 {
    let i: i32 = 0;
    let sum: i32 = 0;
    while (i < 1000) {
        i = i + 1;        // writes r12
        sum = sum + i;    // reads r12 immediately
    }
    print_int(sum);        // 1+2+...+1000 = 500500
    return 0;
}
EOF
run_test "hotvar_gp_reload" /tmp/edge_hotvar_gp_reload.ari "500500" \
  "redundant-mov peephole preserves value"

# #14  Mixed f64 + i32 locals with params — both caches live.
cat > /tmp/edge_hotvar_mixed.ari << 'EOF'
fn compute(k: i32, scale: f64) -> f64 {
    let acc: f64 = 0.0;
    let i: i32 = 0;
    while (i < k) {
        acc = acc + int_to_float(i) * scale;
        i = i + 1;
    }
    return acc;
}
fn main() -> i32 {
    let r: f64 = compute(100, 0.01);   // sum(0..99)/100 = 49.5
    print_f64(r, 4);
    return 0;
}
EOF
run_test "hotvar_mixed_types" /tmp/edge_hotvar_mixed.ari "49.5000" \
  "f64 + i32 caches coexist in same function"

# ────────────────────────────────────────────────────────────────────
echo -e "\n${BOLD}--- Branch peephole correctness ---${RESET}"

# #15  setCC+movzx+cmp-0+je rewritten to inverse JCC must match sense.
cat > /tmp/edge_branch_lt.ari << 'EOF'
fn main() -> i32 {
    let i: i32 = 0; let taken: i32 = 0;
    while (i < 10) {
        if (i < 5) { taken = taken + 1; }
        i = i + 1;
    }
    print_int(taken);   // 5
    return 0;
}
EOF
run_test "branch_lt" /tmp/edge_branch_lt.ari "5"

# #16  Inverse: !=, <=, >= — each flips the low condition bit.
cat > /tmp/edge_branch_ne.ari << 'EOF'
fn main() -> i32 {
    let i: i32 = 0; let t: i32 = 0;
    while (i < 10) {
        if (i != 3) { t = t + 1; }
        i = i + 1;
    }
    print_int(t);      // 9
    return 0;
}
EOF
run_test "branch_ne" /tmp/edge_branch_ne.ari "9"

# #17  Float comparison (uses setb/seta from ucomisd, low bit same).
cat > /tmp/edge_branch_float.ari << 'EOF'
fn main() -> i32 {
    let v: f64 = 3.14;
    if (v > 3.0) {
        print_int(1);
    } else {
        print_int(0);
    }
    return 0;
}
EOF
run_test "branch_float" /tmp/edge_branch_float.ari "1"

# ────────────────────────────────────────────────────────────────────
echo -e "\n${BOLD}--- State isolation between calls ---${RESET}"

# #18  Repeated softmax + forward pass — the xmm-safe fn should not
# leak cached state from one call to the next.  Run a small training
# loop and check final output.
cat > /tmp/edge_repeated_softmax.ari << 'EOF'
fn main() -> i32 {
    let y: i32 = arr_f64_new(10);
    let i: i32 = 0;
    while (i < 100) {
        let j: i32 = 0;
        while (j < 10) {
            arr_f64_set(y, j, int_to_float(j) * 0.1 + int_to_float(i) * 0.001);
            j = j + 1;
        }
        arr_f64_softmax(y);
        i = i + 1;
    }
    // After 100 softmax calls, sum of last one should still be 1 ± ε.
    let sum: f64 = 0.0;
    let k: i32 = 0;
    while (k < 10) { sum = sum + arr_f64_get(y, k); k = k + 1; }
    if (sum > 0.99999) { print_int(1); } else { print_int(0); }
    return 0;
}
EOF
run_test "softmax_repeated" /tmp/edge_repeated_softmax.ari "1" \
  "no state bleed across 100 calls"

# ────────────────────────────────────────────────────────────────────
echo -e "\n${BOLD}--- Short-circuit && and || ---${RESET}"

# #19  && short-circuits when left is false: right must not evaluate
# its side effects.
cat > /tmp/edge_and_sc.ari << 'EOF'
fn sideffect(counter: i32) -> i32 {
    arr_set(counter, 0, arr_get(counter, 0) + 1);
    return 1;
}
fn main() -> i32 {
    let c: i32 = arr_new(1);
    arr_set(c, 0, 0);
    if (0 == 1 && sideffect(c) > 0) { print_int(99); }
    print_int(arr_get(c, 0));    // expect 0 — right never ran
    return 0;
}
EOF
run_test "and_short_circuit" /tmp/edge_and_sc.ari "0" \
  "right not evaluated when left is false"

# #20  || short-circuits when left is true.
cat > /tmp/edge_or_sc.ari << 'EOF'
fn sideffect(counter: i32) -> i32 {
    arr_set(counter, 0, arr_get(counter, 0) + 1);
    return 1;
}
fn main() -> i32 {
    let c: i32 = arr_new(1);
    arr_set(c, 0, 0);
    if (1 == 1 || sideffect(c) > 0) { print_int(1); }
    print_int(arr_get(c, 0));    // expect 0 — right never ran
    return 0;
}
EOF
run_test "or_short_circuit" /tmp/edge_or_sc.ari "0" \
  "right not evaluated when left is true"

# #21  Chained && and ||.
cat > /tmp/edge_chain_and_or.ari << 'EOF'
fn main() -> i32 {
    if (1 == 1 && 2 == 2 && 3 == 3) { print_int(11); }
    if (0 == 1 || 0 == 1 || 1 == 1) { print_int(22); }
    if (1 == 1 && 2 == 2 && 0 == 1) { print_int(33); }  // should NOT print
    if (0 == 1 || 0 == 1 || 0 == 1) { print_int(44); }  // should NOT print
    print_int(99);
    return 0;
}
EOF
run_test "and_or_chains" /tmp/edge_chain_and_or.ari "11
22
99" "three-way chains, no false positives"

# ────────────────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}${CYAN}============================================================${RESET}"
printf "  Total:  ${BOLD}%d${RESET}\n" "$TOTAL"
printf "  Passed: ${GREEN}${BOLD}%d${RESET}\n" "$PASS"
if [ "$FAIL" -gt 0 ]; then
    printf "  Failed: ${RED}${BOLD}%d${RESET}\n" "$FAIL"
else
    printf "  Failed: ${BOLD}%d${RESET}\n" "$FAIL"
fi
echo -e "${BOLD}${CYAN}============================================================${RESET}"

# Cleanup temp sources
rm -f /tmp/edge_*.ari

exit $FAIL
