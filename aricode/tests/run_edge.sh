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

# #7h  arr_f64_sum_range + arr_f64_dot_range — slice reductions.
cat > /tmp/edge_range.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f64_new(10);
    let i: i32 = 0;
    while (i < 10) { arr_f64_set(a, i, int_to_float(i)); i += 1; }
    // Σ a[0..10] = 0+1+...+9 = 45
    print_f64(arr_f64_sum_range(a, 0, 10), 2);
    // Σ a[3..8] = 3+4+5+6+7 = 25
    print_f64(arr_f64_sum_range(a, 3, 5), 2);

    let b: i32 = arr_f64_new(10);
    i = 0;
    while (i < 10) { arr_f64_set(b, i, 2.0); i += 1; }
    // Σ a[2..6] · b[3..7]  where b is all 2.0
    //  = 2·(2+3+4+5) = 28
    print_f64(arr_f64_dot_range(a, 2, b, 3, 4), 2);
    return 0;
}
EOF
run_test "arr_f64_range_reduce" /tmp/edge_range.ari "45.00
25.00
28.00" "slice-aware sum and dot reductions"

# #7g  arr_f64_copy_slice — copy with both src and dst offsets.
cat > /tmp/edge_copy_slice.ari << 'EOF'
fn main() -> i32 {
    // src = 0, 1, 2, ..., 9
    let src: i32 = arr_f64_new(10);
    let i: i32 = 0;
    while (i < 10) { arr_f64_set(src, i, int_to_float(i)); i += 1; }

    // Sentinel-fill dst with 99 so we can see which cells get overwritten.
    let dst: i32 = arr_f64_new(10);
    arr_f64_fill(dst, 99.0);

    // Copy src[2..7] (5 elements) into dst[3..8].  Boundaries stay 99.
    arr_f64_copy_slice(src, 2, dst, 3, 5);
    print_f64(arr_f64_get(dst, 2), 2);   // 99.00 (untouched)
    print_f64(arr_f64_get(dst, 3), 2);   //  2.00 (first copied)
    print_f64(arr_f64_get(dst, 6), 2);   //  5.00 (last of vec)
    print_f64(arr_f64_get(dst, 7), 2);   //  6.00 (scalar tail)
    print_f64(arr_f64_get(dst, 8), 2);   // 99.00 (untouched)
    return 0;
}
EOF
run_test "arr_f64_copy_slice_mid" /tmp/edge_copy_slice.ari "99.00
2.00
5.00
6.00
99.00" "copy_slice reads from src offset and writes to dst offset"

# #7f  arr_f64_copy_at — slice copy with vec + scalar tail.
cat > /tmp/edge_copy_at.ari << 'EOF'
fn main() -> i32 {
    // src = 0, 1, 2, ..., 9
    let src: i32 = arr_f64_new(10);
    let i: i32 = 0;
    while (i < 10) { arr_f64_set(src, i, int_to_float(i)); i += 1; }

    // Copy src[3..8] (5 elements: one vec step + one scalar tail element)
    let dst: i32 = arr_f64_new(5);
    arr_f64_copy_at(src, 3, dst);
    print_f64(arr_f64_get(dst, 0), 2);   // 3.00
    print_f64(arr_f64_get(dst, 3), 2);   // 6.00 (last of vec)
    print_f64(arr_f64_get(dst, 4), 2);   // 7.00 (scalar tail)
    return 0;
}
EOF
run_test "arr_f64_copy_at_slice" /tmp/edge_copy_at.ari "3.00
6.00
7.00" "vec + scalar tail copy from an offset within src"

# #7e  arr_f64_fill — AVX2 broadcast-fill across vec + scalar tail.
cat > /tmp/edge_fill.ari << 'EOF'
fn main() -> i32 {
    // n = 10 exercises 2 vec iterations (8 f64) + 2 scalar tail.
    let a: i32 = arr_f64_new(10);
    arr_f64_fill(a, 3.14);
    print_f64(arr_f64_get(a, 0), 4);   // 3.1400 (vec lane)
    print_f64(arr_f64_get(a, 7), 4);   // 3.1400 (last of vec)
    print_f64(arr_f64_get(a, 9), 4);   // 3.1400 (scalar tail)
    // Fill with 0 — AVX2 vmovupd of a broadcasted zero.
    arr_f64_fill(a, 0.0);
    print_f64(arr_f64_get(a, 9), 4);   // 0.0000
    return 0;
}
EOF
run_test "arr_f64_fill_spot" /tmp/edge_fill.ari "3.1400
3.1400
3.1400
0.0000" "broadcast-fill including the n % 4 scalar tail"

# #7k  conv2d_backward_input_multi — impulse tests the transpose-conv.
#      A single 1.0 at dout[0, 14, 14] with identity-centre kernel
#      should route back to dinput[0, 14, 14].  With a top-left
#      kernel it routes to dinput[0, 13, 13] (shift by (ky-1, kx-1)).
#      Lives in aricode-stdlib; this edge test proves the compiler
#      AND the stdlib math are both correct together.
cat > /tmp/edge_bw_input.ari << 'EOF'
// Inline mini-reimplementation to keep the test compiler-only.
fn bw_input_impulse(dout_idx: i32, w_idx: i32, dinput: i32) -> f64 {
    let dout: i32 = arr_f64_new(784);
    arr_f64_fill(dout, 0.0);
    arr_f64_set(dout, dout_idx, 1.0);
    let weights: i32 = arr_f64_new(9);
    arr_f64_fill(weights, 0.0);
    arr_f64_set(weights, w_idx, 1.0);

    // Flip + transpose (trivially for C_in=C_out=1: weights_flipped[k] = weights[8-k]).
    let w_flipped: i32 = arr_f64_new(9);
    let k: i32 = 0;
    while (k < 9) { arr_f64_set(w_flipped, 8 - k, arr_f64_get(weights, k)); k += 1; }

    // Pad dout to 900.
    let padded: i32 = arr_f64_new(900);
    arr_f64_fill(padded, 0.0);
    let y: i32 = 0;
    while (y < 28) {
        arr_f64_copy_slice(dout, y * 28, padded, (y + 1) * 30 + 1, 28);
        y += 1;
    }
    let zbias: i32 = arr_f64_new(1);
    arr_f64_conv2d_3x3_p1_multi(padded, 1, w_flipped, zbias, dinput, 1);
    return 0.0;
}

fn main() -> i32 {
    let dinput: i32 = arr_f64_new(784);

    // Case A: dout[14,14] = 1, W[1,1] = 1 (centre) → dinput[14,14] = 1.
    bw_input_impulse(14 * 28 + 14, 4, dinput);
    print_f64(arr_f64_get(dinput, 14 * 28 + 14), 4);
    print_f64(arr_f64_get(dinput, 13 * 28 + 13), 4);

    // Case B: dout[14,14] = 1, W[0,0] = 1 (top-left) → dinput[13,13] = 1.
    bw_input_impulse(14 * 28 + 14, 0, dinput);
    print_f64(arr_f64_get(dinput, 13 * 28 + 13), 4);
    print_f64(arr_f64_get(dinput, 14 * 28 + 14), 4);
    return 0;
}
EOF
run_test "conv2d_backward_input_impulse" /tmp/edge_bw_input.ari "1.0000
0.0000
1.0000
0.0000" "transpose conv (forward with flipped weights) routes gradients correctly"

# #7j  arr_f64_conv2d_3x3_p1_multi — C_in > 1 case via a known impulse.
# 2 input channels, both with a 1.0 impulse at the centre (15, 15) of
# the padded plane (= output position (14, 14)).  With identity kernel
# on channel 0 going to output channel 0 and zero on all others, we
# expect output[0, 14, 14] = 2.0 (both inputs sum into channel 0).
cat > /tmp/edge_conv2d_multi.ari << 'EOF'
fn main() -> i32 {
    let C_IN:  i32 = 2;
    let C_OUT: i32 = 1;

    let padded: i32 = arr_f64_new(C_IN * 900);
    let i: i32 = 0;
    while (i < C_IN * 900) { arr_f64_set(padded, i, 0.0); i += 1; }
    arr_f64_set(padded, 0 * 900 + 15 * 30 + 15, 1.0);
    arr_f64_set(padded, 1 * 900 + 15 * 30 + 15, 1.0);

    // weights[0, 0, :, :] = identity (centre 1, rest 0)
    // weights[0, 1, :, :] = identity too  → sum of two channels.
    let weights: i32 = arr_f64_new(C_OUT * C_IN * 9);
    i = 0;
    while (i < C_OUT * C_IN * 9) { arr_f64_set(weights, i, 0.0); i += 1; }
    arr_f64_set(weights,  4, 1.0);   // w[0, 0, 1, 1] = 1
    arr_f64_set(weights, 13, 1.0);   // w[0, 1, 1, 1] = 1

    let bias: i32 = arr_f64_new(C_OUT);
    arr_f64_set(bias, 0, 0.0);

    let output: i32 = arr_f64_new(C_OUT * 784);
    arr_f64_conv2d_3x3_p1_multi(padded, C_IN, weights, bias, output, C_OUT);

    print_f64(arr_f64_get(output, 14 * 28 + 14), 4);  // 2.0000
    print_f64(arr_f64_get(output, 0), 4);             // 0.0000 (corner, no impulse)
    return 0;
}
EOF
run_test "conv2d_3x3_multi_impulse" /tmp/edge_conv2d_multi.ari "2.0000
0.0000" "2 input channels sum at the centre, zero elsewhere"

# #7d  arr_f64_conv2d_3x3_p1 — spot-check two known-output cases.
# Input is pre-padded (30×30).  Caller owns padding; this builtin is
# the straight-line AVX2 convolution.
cat > /tmp/edge_conv2d.ari << 'EOF'
fn main() -> i32 {
    // padded[30*30] — all zeros except centre at (15, 15) = 1.0.
    let padded: i32 = arr_f64_new(900);
    let i: i32 = 0;
    while (i < 900) { arr_f64_set(padded, i, 0.0); i += 1; }
    arr_f64_set(padded, 15 * 30 + 15, 1.0);

    // 2 output channels.  Channel 0: averaging 1/9 kernel.
    // Channel 1: identity (centre of kernel = 1, rest 0).
    let weights: i32 = arr_f64_new(2 * 9);
    let k: i32 = 0;
    while (k < 9) { arr_f64_set(weights, k, 1.0 / 9.0); k += 1; }
    k = 9;
    while (k < 18) { arr_f64_set(weights, k, 0.0); k += 1; }
    arr_f64_set(weights, 9 + 4, 1.0);   // channel 1, position (1,1)

    let bias: i32 = arr_f64_new(2);
    arr_f64_set(bias, 0, 0.0);
    arr_f64_set(bias, 1, 0.0);

    let output: i32 = arr_f64_new(2 * 784);
    arr_f64_conv2d_3x3_p1(padded, weights, bias, output, 2);

    // Channel 0 (averaging): the 1.0 at padded (15, 15) spreads to the
    // 3×3 neighbourhood in OUTPUT coords.  Padded(15,15) means output
    // positions (14, 14), (14, 13), (14, 15), (13, 14), ..., (15, 15)
    // all receive 1/9.  Elsewhere the output is 0.
    print_f64(arr_f64_get(output, 14 * 28 + 14), 4);   // 0.1111
    print_f64(arr_f64_get(output, 12 * 28 + 12), 4);   // 0.0000
    // Channel 1 (identity): padded(15,15) is the centre of the 3×3
    // window over output(14,14), with kernel centre = 1 → output = 1.
    print_f64(arr_f64_get(output, 784 + 14 * 28 + 14), 4);   // 1.0000
    print_f64(arr_f64_get(output, 784 + 13 * 28 + 13), 4);   // 0.0000
    return 0;
}
EOF
run_test "conv2d_3x3_spot_check" /tmp/edge_conv2d.ari "0.1111
0.0000
1.0000
0.0000" "averaging + identity kernels on centre-pixel input"

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
echo -e "\n${BOLD}--- Threading ---${RESET}"

# #22  thread_spawn used to fall through the child path straight to
# exit(0) without actually calling the spawned function (the child had
# a fresh stack but no way to reach the function entry).  Fix: resolve
# the function address at codegen time via RIP-relative LEA and pre-seed
# it into the top slot of the child's mmap'd stack so the child can
# pop+call.  This test catches any regression where the child silently
# exits without running the function body.
cat > /tmp/edge_thread_spawn.ari << 'EOF'
fn worker() -> i32 {
    print_int(42);
    return 0;
}

fn main() -> i32 {
    let tid: i32 = thread_spawn(worker);
    let i: i32 = 0;
    while (i < 100000000) { i = i + 1; }
    print_int(99);
    return 0;
}
EOF
run_test "thread_spawn_basic" /tmp/edge_thread_spawn.ari "42
99" "child must actually run worker"

# #23  Multiple threads sharing the process — each child mmap's its own
# stack, each call site gets its own RIP-relative patch, CLONE_VM keeps
# the heap shared so print_int's stdout fd reaches the same terminal.
cat > /tmp/edge_thread_multi.ari << 'EOF'
fn worker_a() -> i32 { print_int(1); return 0; }
fn worker_b() -> i32 { print_int(2); return 0; }
fn worker_c() -> i32 { print_int(3); return 0; }

fn main() -> i32 {
    let t1: i32 = thread_spawn(worker_a);
    let t2: i32 = thread_spawn(worker_b);
    let t3: i32 = thread_spawn(worker_c);
    let i: i32 = 0;
    while (i < 200000000) { i = i + 1; }
    print_int(9);
    return 0;
}
EOF
run_test "thread_spawn_multi" /tmp/edge_thread_multi.ari "9" "three children + parent all reach print_int"

# #24  thread_spawn(func, arg) — the worker receives arg in RDI so it
# can operate on shared state (pointer, counter, slot index).  Pre-seed
# two stack slots instead of one and `pop rdi` before the indirect call.
cat > /tmp/edge_thread_spawn_arg.ari << 'EOF'
fn worker(shared: i32) -> i32 {
    arr_set(shared, 0, 123);
    return 0;
}

fn main() -> i32 {
    let buf: i32 = arr_new(1);
    arr_set(buf, 0, 0);
    let tid: i32 = thread_spawn(worker, buf);
    let i: i32 = 0;
    while (i < 100000000) { i = i + 1; }
    print_int(arr_get(buf, 0));
    return 0;
}
EOF
run_test "thread_spawn_arg" /tmp/edge_thread_spawn_arg.ari "123" "worker receives shared-mem pointer"

# #25  atomic_add_i64 under real contention — 4 workers each bump the
# same counter 10_000 times.  Without `lock xadd` the count drops below
# 40_000 because racing non-atomic read-modify-write loses updates.
cat > /tmp/edge_atomic_add.ari << 'EOF'
fn bump_10k(counter: i32) -> i32 {
    let i: i32 = 0;
    while (i < 10000) {
        atomic_add_i64(counter, 0, 1);
        i = i + 1;
    }
    atomic_add_i64(counter, 1, 1);    // "done" marker
    return 0;
}

fn main() -> i32 {
    let a: i32 = arr_new(2);
    arr_set(a, 0, 0);
    arr_set(a, 1, 0);
    let t1: i32 = thread_spawn(bump_10k, a);
    let t2: i32 = thread_spawn(bump_10k, a);
    let t3: i32 = thread_spawn(bump_10k, a);
    let t4: i32 = thread_spawn(bump_10k, a);
    while (arr_get(a, 1) < 4) {
        let pause: i32 = 0;
        while (pause < 1000) { pause = pause + 1; }
    }
    print_int(arr_get(a, 0));
    return 0;
}
EOF
run_test "atomic_add_contention" /tmp/edge_atomic_add.ari "40000" "lock xadd keeps the count exact under 4-way race"

# #26  atomic_add_f64 under contention — x86 has no atomic FADD, so
# it's a `lock cmpxchg` loop on f64 bits.  40 000 sequential 1.0
# increments must yield 40000.00 exactly when split four ways.
cat > /tmp/edge_atomic_f64.ari << 'EOF'
fn f64_bump(sh: i32) -> i32 {
    let i: i32 = 0;
    while (i < 10000) {
        atomic_add_f64(sh, 0, 1.0);
        i = i + 1;
    }
    atomic_add_i64(sh, 1, 1);
    return 0;
}

fn main() -> i32 {
    let sh: i32 = arr_f64_new(2);
    arr_f64_set(sh, 0, 0.0);
    arr_f64_set(sh, 1, 0.0);
    let t1: i32 = thread_spawn(f64_bump, sh);
    let t2: i32 = thread_spawn(f64_bump, sh);
    let t3: i32 = thread_spawn(f64_bump, sh);
    let t4: i32 = thread_spawn(f64_bump, sh);
    while (arr_get(sh, 1) < 4) {
        let pause: i32 = 0;
        while (pause < 1000) { pause = pause + 1; }
    }
    print_f64(arr_f64_get(sh, 0), 2);
    return 0;
}
EOF
run_test "atomic_add_f64_contention" /tmp/edge_atomic_f64.ari "40000.00" "cmpxchg loop keeps f64 sum exact under race"

# ────────────────────────────────────────────────────────────────────
echo -e "\n${BOLD}--- f32 primitives ---${RESET}"

# #27  arr_f32_new + get + set round-trip — boundary uses cvtss2sd /
# cvtsd2ss, the storage backs onto an mmap-allocated buffer with the
# same length-prefix layout as arr_f64_new.  Catches the "stored bits
# survive read-back at f32 precision" invariant.
cat > /tmp/edge_f32_basic.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f32_new(8);
    arr_f32_set(a, 0, 1.5);
    arr_f32_set(a, 1, 2.25);
    arr_f32_set(a, 2, 0.0 - 3.125);
    arr_f32_set(a, 7, 100.5);
    print_f64(arr_f32_get(a, 0), 4);
    print_f64(arr_f32_get(a, 1), 4);
    print_f64(arr_f32_get(a, 2), 4);
    print_f64(arr_f32_get(a, 7), 4);
    print_int(arr_len(a));
    return 0;
}
EOF
run_test "f32_basic_roundtrip" /tmp/edge_f32_basic.ari "1.5000
2.2500
-3.1250
100.5000
8" "f32 round-trip preserves the values that fit"

# #28  arr_f32_dot (AVX2 8-lane vfmadd231ps) vs the same data through
# arr_f64_dot.  Tolerance: ~1e-5 over 1024 elements (f32 mantissa is
# 23 bits ≈ 7 decimal digits; the dot accumulator drops a few bits to
# rounding per lane).
cat > /tmp/edge_f32_dot.ari << 'EOF'
fn main() -> i32 {
    let n: i32 = 1024;
    let af: i32 = arr_f32_new(n);
    let bf: i32 = arr_f32_new(n);
    let ad: i32 = arr_f64_new(n);
    let bd: i32 = arr_f64_new(n);
    let i: i32 = 0;
    while (i < n) {
        let v: f64 = int_to_float(i % 17) / 17.0;
        let w: f64 = int_to_float(i % 31) / 31.0;
        arr_f32_set(af, i, v);
        arr_f32_set(bf, i, w);
        arr_f64_set(ad, i, v);
        arr_f64_set(bd, i, w);
        i = i + 1;
    }
    let r32: f64 = arr_f32_dot(af, bf);
    let r64: f64 = arr_f64_dot(ad, bd);
    let diff: f64 = math_abs(r32 - r64);
    if (diff < 0.001) { print_str("DOT_OK"); }
    return 0;
}
EOF
run_test "f32_dot_vs_f64" /tmp/edge_f32_dot.ari "DOT_OK" "f32 8-lane dot agrees with f64 within 1e-3"

# #29  arr_f32_fill / sum / scale / relu / add_scaled — element-wise
# AVX2 kernels with 8-lane vector body + scalar ss tail.  Each
# verified against expected algebraic identity.
cat > /tmp/edge_f32_kernels.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f32_new(100);
    let b: i32 = arr_f32_new(100);
    arr_f32_fill(a, 1.5);
    arr_f32_fill(b, 4.0);
    let s1: f64 = arr_f32_sum(a);          // 150.0
    arr_f32_scale(a, 2.0);                  // a = 3.0
    arr_f32_set(a, 0, 0.0 - 5.0);
    arr_f32_relu(a);                        // a[0] now 0
    arr_f32_add_scaled(a, b, 0.5);          // a[i] += 0.5*4 = 2.0
    let r0: f64 = arr_f32_get(a, 0);        // 0 + 2 = 2.0
    let r1: f64 = arr_f32_get(a, 1);        // 3 + 2 = 5.0
    if (s1 > 149.99) { if (s1 < 150.01) { print_str("S_OK"); } }
    print_f64(r0, 4);
    print_f64(r1, 4);
    return 0;
}
EOF
run_test "f32_kernels_chain" /tmp/edge_f32_kernels.ari "S_OK
2.0000
5.0000" "fill/sum/scale/relu/add_scaled all preserve algebraic identities"

# #30  arr_f32_matvec / matvec_T / outer_accum vs the f64 reference,
# all three feed into a dense layer's forward + backward update.  Same
# data → same algebraic output (within ~1e-2 over a 32×64 layer with
# ±0.5 weight range — the f32 mantissa drops a few ULPs per FMA).
cat > /tmp/edge_f32_dense.ari << 'EOF'
fn main() -> i32 {
    let m: i32 = 32; let n: i32 = 64;
    let Wf: i32 = arr_f32_new(m * n); let Wd: i32 = arr_f64_new(m * n);
    let xf: i32 = arr_f32_new(n);     let xd: i32 = arr_f64_new(n);
    let bf: i32 = arr_f32_new(m);     let bd: i32 = arr_f64_new(m);
    let yf: i32 = arr_f32_new(m);     let yd: i32 = arr_f64_new(m);
    let dyf: i32 = arr_f32_new(m);    let dyd: i32 = arr_f64_new(m);
    let dxf: i32 = arr_f32_new(n);    let dxd: i32 = arr_f64_new(n);
    let i: i32 = 0;
    while (i < m * n) {
        let v: f64 = int_to_float((i * 7 + 13) % 97) / 97.0 - 0.5;
        arr_f32_set(Wf, i, v); arr_f64_set(Wd, i, v); i = i + 1;
    }
    i = 0;
    while (i < n) {
        let v: f64 = int_to_float(i % 11) / 11.0;
        arr_f32_set(xf, i, v); arr_f64_set(xd, i, v); i = i + 1;
    }
    i = 0;
    while (i < m) {
        arr_f32_set(bf, i, 0.0); arr_f64_set(bd, i, 0.0);
        let dyv: f64 = int_to_float((i * 3) % 5) / 5.0 - 0.4;
        arr_f32_set(dyf, i, dyv); arr_f64_set(dyd, i, dyv);
        i = i + 1;
    }
    arr_f32_matvec(Wf, xf, bf, yf, m, n);
    arr_f64_matvec(Wd, xd, bd, yd, m, n);
    arr_f32_matvec_T(Wf, dyf, dxf, m, n);
    arr_f64_matvec_T(Wd, dyd, dxd, m, n);
    let Wf0: f64 = arr_f32_sum(Wf);
    let Wd0: f64 = arr_f64_sum(Wd);
    arr_f32_outer_accum(Wf, dyf, xf, m, n);
    arr_f64_outer_accum(Wd, dyd, xd, m, n);
    let dWf: f64 = arr_f32_sum(Wf) - Wf0;
    let dWd: f64 = arr_f64_sum(Wd) - Wd0;
    if (math_abs(arr_f32_sum(yf) - arr_f64_sum(yd)) < 0.01) {
        if (math_abs(arr_f32_sum(dxf) - arr_f64_sum(dxd)) < 0.01) {
            if (math_abs(dWf - dWd) < 0.01) { print_str("DENSE_OK"); }
        }
    }
    return 0;
}
EOF
run_test "f32_dense_kernels" /tmp/edge_f32_dense.ari "DENSE_OK" "matvec / matvec_T / outer_accum agree with f64 within 1e-2"

# #31  Latent typing bug: expr_is_float didn't list arr_f32_get / sum
# / dot, so `arr_f32_get(a,i) - arr_f32_get(b,i)` wrongly fell to the
# integer subtract path and corrupted MNIST f32 backward (xent_backward
# returned 0 instead of y - t).  Test pins the float-binop classifier
# against f32-array-read operands.
cat > /tmp/edge_f32_typing.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f32_new(4); let b: i32 = arr_f32_new(4);
    arr_f32_set(a, 0, 0.7); arr_f32_set(b, 0, 0.2);
    let v: f64 = arr_f32_get(a, 0) - arr_f32_get(b, 0);
    let w: f64 = arr_f32_get(a, 0) * arr_f32_get(b, 0);
    print_f64(v, 4);   // 0.5 (within f32 precision)
    print_f64(w, 4);   // 0.14
    return 0;
}
EOF
run_test "f32_get_float_binop" /tmp/edge_f32_typing.ari "0.4999
0.1400" "arr_f32_get appears in expr_is_float so float binops use SSE"

# #32  arr_f32_copy_at / copy_slice — f32 mirrors of the f64 bulk-mem
# kernels at scale=4.  copy_at fills dst from src[offset..]; copy_slice
# places src[src_off..src_off+n) into dst[dst_off..dst_off+n).
cat > /tmp/edge_f32_copy.ari << 'EOF'
fn main() -> i32 {
    let src: i32 = arr_f32_new(20);
    let dst: i32 = arr_f32_new(10);
    let i: i32 = 0;
    while (i < 20) { arr_f32_set(src, i, int_to_float(i) * 0.5); i = i + 1; }
    arr_f32_copy_at(src, 5, dst);
    if (math_abs(arr_f32_sum(dst) - 47.5) < 0.001) { print_str("AT_OK"); }
    arr_f32_copy_slice(src, 3, dst, 2, 5);
    if (math_abs(arr_f32_get(dst, 2) - 1.5) < 0.001) {
        if (math_abs(arr_f32_get(dst, 6) - 3.5) < 0.001) {
            if (math_abs(arr_f32_get(dst, 0) - 2.5) < 0.001) { print_str("SLICE_OK"); }
        }
    }
    return 0;
}
EOF
run_test "f32_copy_kernels" /tmp/edge_f32_copy.ari "AT_OK
SLICE_OK" "f32 copy_at + copy_slice match expected element-wise behaviour"

# #33  arr_f32_adam_apply: one Adam step on a 100-element parameter
# tensor.  f32 result must match f64 within ~1e-3 (single-precision
# error from sqrt/div/cvt rounding compounded over 100 elements).
cat > /tmp/edge_f32_adam.ari << 'EOF'
fn main() -> i32 {
    let n: i32 = 100;
    let Wf: i32 = arr_f32_new(n); let Wd: i32 = arr_f64_new(n);
    let mf: i32 = arr_f32_new(n); let md: i32 = arr_f64_new(n);
    let vf: i32 = arr_f32_new(n); let vd: i32 = arr_f64_new(n);
    let i: i32 = 0;
    while (i < n) {
        let w: f64 = int_to_float(i % 17) / 17.0;
        let mm: f64 = int_to_float((i*3) % 11) / 11.0 - 0.5;
        let vv: f64 = int_to_float((i*7) % 13) / 13.0 + 0.01;
        arr_f32_set(Wf, i, w); arr_f64_set(Wd, i, w);
        arr_f32_set(mf, i, mm); arr_f64_set(md, i, mm);
        arr_f32_set(vf, i, vv); arr_f64_set(vd, i, vv);
        i = i + 1;
    }
    arr_f32_adam_apply(Wf, mf, vf, 0.001, 0.00000001);
    arr_f64_adam_apply(Wd, md, vd, 0.001, 0.00000001);
    if (math_abs(arr_f32_sum(Wf) - arr_f64_sum(Wd)) < 0.01) { print_str("ADAM_OK"); }
    return 0;
}
EOF
run_test "f32_adam_apply" /tmp/edge_f32_adam.ari "ADAM_OK" "f32 adam_apply matches f64 within 1e-2 over 100 elements"

# #34  arr_f32_conv2d_3x3_p1: 8-lane vec body (3 chunks) + 4-lane xmm
# tail (1 chunk) for the 28-wide row.  Catches the tail-disp32 double-
# offset bug seen in bring-up where rdx already carried 96 at tail
# entry and the macro added 96 again.
cat > /tmp/edge_f32_conv2d.ari << 'EOF'
fn main() -> i32 {
    let pf: i32 = arr_f32_new(900); let pd: i32 = arr_f64_new(900);
    let Wf: i32 = arr_f32_new(8 * 9); let Wd: i32 = arr_f64_new(8 * 9);
    let bf: i32 = arr_f32_new(8); let bd: i32 = arr_f64_new(8);
    let yf: i32 = arr_f32_new(8 * 784); let yd: i32 = arr_f64_new(8 * 784);
    let i: i32 = 0;
    while (i < 900) {
        let v: f64 = math_sin(int_to_float(i) * 0.13) * 0.5;
        let row: i32 = i / 30;
        let col: i32 = i - row * 30;
        if (row == 0 || row == 29 || col == 0 || col == 29) { v = 0.0; }
        arr_f32_set(pf, i, v); arr_f64_set(pd, i, v);
        i = i + 1;
    }
    i = 0;
    while (i < 8 * 9) {
        let v: f64 = math_cos(int_to_float(i) * 0.21) * 0.3;
        arr_f32_set(Wf, i, v); arr_f64_set(Wd, i, v);
        i = i + 1;
    }
    i = 0;
    while (i < 8) {
        let v: f64 = int_to_float(i) * 0.01;
        arr_f32_set(bf, i, v); arr_f64_set(bd, i, v);
        i = i + 1;
    }
    arr_f32_conv2d_3x3_p1(pf, Wf, bf, yf, 8);
    arr_f64_conv2d_3x3_p1(pd, Wd, bd, yd, 8);
    if (math_abs(arr_f32_sum(yf) - arr_f64_sum(yd)) < 1.0) { print_str("CONV_OK"); }
    return 0;
}
EOF
run_test "f32_conv2d_3x3" /tmp/edge_f32_conv2d.ari "CONV_OK" "f32 3x3 conv (vec body + xmm tail) matches f64 within 1.0 over 6272 outputs"

# #35  arr_f32_mul length contract: dst MAY be larger than src1 (a)
# without the kernel reading past `a` — `n` is taken from `arr_len(a)`,
# not from `arr_len(dst)`.  Catches the OOB-via-oversized-dst bug
# that a shared `tmp_g2` Adam scratch buffer was shaped to expose.
cat > /tmp/edge_f32_mul_oversized.ari << 'EOF'
fn main() -> i32 {
    let a: i32 = arr_f32_new(8);          // small src
    let b: i32 = arr_f32_new(8);
    let dst: i32 = arr_f32_new(1024);     // large dst (Adam-style scratch)
    arr_f32_fill(dst, 999.0);             // sentinel; should remain past i=7
    let i: i32 = 0;
    while (i < 8) {
        arr_f32_set(a, i, int_to_float(i + 1));
        arr_f32_set(b, i, 2.0);
        i = i + 1;
    }
    arr_f32_mul(dst, a, b);
    // First 8 dst slots: 1*2, 2*2, 3*2, ..., 8*2 = 2, 4, 6, ..., 16 → sum 72.
    // Slots 8..1023 must stay 999.0 — kernel must NOT touch them.
    let head: f64 = 0.0;
    i = 0;
    while (i < 8) { head = head + arr_f32_get(dst, i); i = i + 1; }
    let tail: f64 = arr_f32_get(dst, 100) + arr_f32_get(dst, 500) + arr_f32_get(dst, 1023);
    if (head > 71.99) { if (head < 72.01) {
        if (tail > 2996.99) { if (tail < 2997.01) { print_str("MUL_OK"); } } } }
    return 0;
}
EOF
run_test "f32_mul_oversized_dst" /tmp/edge_f32_mul_oversized.ari "MUL_OK" "arr_f32_mul reads n from src1, not dst — large dst stays untouched past arr_len(a)"

# #36  arr_f32_dot_range: AVX2 dot product over a slice of two f32
# arrays.  Same shape as arr_f64_dot_range but 8-lane vfmadd231ps.
# Catches scale=4 SIB miscoding regressions and the cvtss2sd return.
cat > /tmp/edge_f32_dr.ari << 'EOF'
fn main() -> i32 {
    let n: i32 = 100;
    let af: i32 = arr_f32_new(n); let bf: i32 = arr_f32_new(n);
    let ad: i32 = arr_f64_new(n); let bd: i32 = arr_f64_new(n);
    let i: i32 = 0;
    while (i < n) {
        let v: f64 = int_to_float(i % 17) / 17.0;
        let w: f64 = int_to_float(i % 31) / 31.0;
        arr_f32_set(af, i, v); arr_f32_set(bf, i, w);
        arr_f64_set(ad, i, v); arr_f64_set(bd, i, w);
        i = i + 1;
    }
    let rf: f64 = arr_f32_dot_range(af, 10, bf, 5, 30);
    let rd: f64 = arr_f64_dot_range(ad, 10, bd, 5, 30);
    if (math_abs(rf - rd) < 0.001) { print_str("DR_OK"); }
    return 0;
}
EOF
run_test "f32_dot_range" /tmp/edge_f32_dr.ari "DR_OK" "f32 dot_range over slice [10..40)·[5..35) matches f64 within 1e-3"

# #37  arr_f32_adam_apply on a sparse-moment buffer — only one lane in
# the second 8-lane chunk has non-zero (m, v).  This catches the bug
# where the m-chunk vmovups had VEX X̃=0 and silently used R14 as the
# index, so every iter past the first re-read m[0..7] instead of
# advancing. CNN AdamW would diverge to NaN on first step. The first
# 8-lane chunk update (and the fully-populated #33 above) cannot
# detect this — only a sparse pattern past the first chunk does.
cat > /tmp/edge_f32_adam_sparse.ari << 'EOF'
fn main() -> i32 {
    let n: i32 = 32;
    let W: i32 = arr_f32_new(n);
    let m: i32 = arr_f32_new(n);
    let v: i32 = arr_f32_new(n);
    arr_f32_fill(W, 0.1);
    arr_f32_fill(m, 0.0);
    arr_f32_fill(v, 0.0);
    arr_f32_set(m, 17, 0.001);   // lane 17 → second-half chunk
    arr_f32_set(v, 17, 0.000001);
    arr_f32_adam_apply(W, m, v, 0.001, 0.0000001);
    let w17: f64 = arr_f32_get(W, 17);
    if (math_abs(w17 - 0.099) < 0.0005) { print_str("ADAM_SPARSE_OK"); }
    return 0;
}
EOF
run_test "f32_adam_sparse" /tmp/edge_f32_adam_sparse.ari "ADAM_SPARSE_OK" "f32 adam_apply advances RSI through every 8-lane chunk (sparse m,v at lane 17 must update W[17])"

# #38  arr_f32_exp: promote-to-f64 + vec_exp_body + narrow.  Compare
# against scalar math_exp at three representative points to catch
# both the cvtps2pd/cvtpd2ps wrappers and the polynomial precision.
cat > /tmp/edge_f32_exp.ari << 'EOF'
fn main() -> i32 {
    let n: i32 = 4;
    let buf: i32 = arr_f32_new(n);
    arr_f32_set(buf, 0, 0.0);
    arr_f32_set(buf, 1, 1.0);
    arr_f32_set(buf, 2, 2.0);
    arr_f32_set(buf, 3, -3.0);
    arr_f32_exp(buf);
    let ok: i32 = 1;
    if (math_abs(arr_f32_get(buf, 0) - 1.0)        > 0.0001) { ok = 0; }
    if (math_abs(arr_f32_get(buf, 1) - 2.718281)   > 0.0001) { ok = 0; }
    if (math_abs(arr_f32_get(buf, 2) - 7.389056)   > 0.001 ) { ok = 0; }
    if (math_abs(arr_f32_get(buf, 3) - 0.049787)   > 0.0001) { ok = 0; }
    if (ok == 1) { print_str("EXP_OK"); }
    return 0;
}
EOF
run_test "f32_exp" /tmp/edge_f32_exp.ari "EXP_OK" "f32 exp matches scalar math_exp at 0/1/2/-3 within 1e-3"

# #39  arr_f32_softmax: stable in-place softmax, must produce a valid
# probability distribution (positives summing to 1) regardless of the
# input shift level.  Test with a moderately large positive shift to
# stress the (-max) broadcast path.
cat > /tmp/edge_f32_softmax.ari << 'EOF'
fn main() -> i32 {
    let n: i32 = 8;
    let buf: i32 = arr_f32_new(n);
    arr_f32_set(buf, 0, 100.0);  arr_f32_set(buf, 1, 101.0);
    arr_f32_set(buf, 2, 102.0);  arr_f32_set(buf, 3, 103.0);
    arr_f32_set(buf, 4, 100.0);  arr_f32_set(buf, 5, 101.0);
    arr_f32_set(buf, 6, 102.0);  arr_f32_set(buf, 7, 103.0);
    arr_f32_softmax(buf);
    let total: f64 = 0.0;
    let any_neg: i32 = 0;
    let i: i32 = 0;
    while (i < n) {
        let v: f64 = arr_f32_get(buf, i);
        if (v < 0.0) { any_neg = 1; }
        total = total + v;
        i += 1;
    }
    if (any_neg == 0 && math_abs(total - 1.0) < 0.0001) { print_str("SOFTMAX_OK"); }
    return 0;
}
EOF
run_test "f32_softmax" /tmp/edge_f32_softmax.ari "SOFTMAX_OK" "f32 softmax stable under +100 shift; produces valid distribution summing to 1.0 ± 1e-4"

# #40  futex_wait + futex_wake: parent blocks until 4 workers finish a
# fixed amount of work then signal.  Verifies the parent didn't busy-
# loop (user/real should approach N_WORKERS rather than N_WORKERS+1)
# but that's wall-clock dependent — here we just verify the barrier
# completes correctly under heavy worker load.
cat > /tmp/edge_futex.ari << 'EOF'
fn worker(desc: i32) -> i32 {
    let ctr: i32 = arr_get(desc, 0);
    let n:   i32 = arr_get(desc, 1);
    let x: i32 = 0;
    while (x < 5000000) { x += 1; }
    let prev: i32 = atomic_add_i64(ctr, 0, 1);
    if (prev + 1 == n) { futex_wake(ctr, 0, 2147483647); }
    return 0;
}
fn main() -> i32 {
    let N: i32 = 4;
    let ctr: i32 = arr_new(1);
    arr_set(ctr, 0, 0);
    let i: i32 = 0;
    while (i < N) {
        let d: i32 = arr_new(2);
        arr_set(d, 0, ctr); arr_set(d, 1, N);
        let tid: i32 = thread_spawn(worker, d);
        i += 1;
    }
    while (arr_get(ctr, 0) < N) {
        let cur: i32 = arr_get(ctr, 0);
        if (cur < N) { futex_wait(ctr, 0, cur); }
    }
    if (arr_get(ctr, 0) == N) { print_str("FUTEX_OK"); }
    return 0;
}
EOF
run_test "futex_barrier" /tmp/edge_futex.ari "FUTEX_OK" "futex_wait/futex_wake barrier across 4 workers reaches N=4"

# #41  atomic_add_f64 NaN guard.  Without the guard, a single NaN
# delta poisons the slot forever — the cmpxchg compares NaN-bits
# against NaN-bits (succeeds), addsd then keeps producing NaN, so
# every subsequent add is a no-op stuck at NaN.  Guard makes the NaN
# call a no-op instead, leaving the counter usable for the rest of
# the run.
cat > /tmp/edge_atomic_nan.ari << 'EOF'
fn make_nan() -> f64 {
    let z: f64 = 0.0;
    return z / z;
}
fn main() -> i32 {
    let ctr: i32 = arr_f64_new(1);
    arr_f64_set(ctr, 0, 0.0);
    atomic_add_f64(ctr, 0, 1.0);
    atomic_add_f64(ctr, 0, make_nan());
    atomic_add_f64(ctr, 0, 5.0);
    if (math_abs(arr_f64_get(ctr, 0) - 6.0) < 0.0001) { print_str("NAN_GUARD_OK"); }
    return 0;
}
EOF
run_test "atomic_f64_nan_guard" /tmp/edge_atomic_nan.ari "NAN_GUARD_OK" "atomic_add_f64 with NaN delta is a no-op (counter stays usable for finite adds)"

# #42  Hot-GP `i = i + 1` direct-add path must keep rax in sync.  An
# earlier version of the fast path emitted just `add r_v, imm` and
# left rax stale.  The unrolled while loop's first condition check
# was emitted while a prelude `mov r_v, rax` was still the most
# recent instruction, so a peephole (load_local) skipped the rax
# reload at that site.  After the body wrapped back to the top, rax
# held the previous-iter's i instead of the current r_v, allowing
# one extra iteration — sum(0..k-1) became sum(1..k).  Caught with
# k=10: 0+1+…+9 should be 45.0, the bug returned 55.0.
cat > /tmp/edge_hotgp_loop_rax.ari << 'EOF'
fn compute(k: i32) -> f64 {
    let acc: f64 = 0.0;
    let i: i32 = 0;
    while (i < k) {
        acc = acc + int_to_float(i);
        i = i + 1;
    }
    return acc;
}
fn main() -> i32 {
    if (math_abs(compute(10) - 45.0) < 0.0001) { print_str("HOTGP_OK"); }
    return 0;
}
EOF
run_test "hotgp_loop_rax_sync" /tmp/edge_hotgp_loop_rax.ari "HOTGP_OK" "hot-GP i=i+1 direct add must leave rax = r_v so the next loop-top condition reads current i"

# #43  Hot-GP subtraction: `i = i - 1` countdown.  Different opcode
# (sub /5 vs add /0) and different end condition; ensures the sub
# leg of the fast path also keeps rax in sync.
cat > /tmp/edge_hotgp_sub.ari << 'EOF'
fn count_down(start: i32) -> i32 {
    let i: i32 = start;
    let s: i32 = 0;
    while (i > 0) {
        s = s + i;
        i = i - 1;
    }
    return s;
}
fn main() -> i32 {
    print_int(count_down(10));   // 10+9+...+1 = 55
    return 0;
}
EOF
run_test "hotgp_sub" /tmp/edge_hotgp_sub.ari "55" "hot-GP i=i-1 direct sub keeps rax synced for next loop check"

# #44  Hot-GP register-register: `s = s + i` where both are hot-GP.
# Exercises the second leg of the fast path (Form 2: add r_v, r_w).
# Without the trailing rax sync the unrolled loop's mid-check reads
# stale rax from the body's last expression — caught here because
# the inner sum exercises that exact pattern.
cat > /tmp/edge_hotgp_regreg.ari << 'EOF'
fn sum_to(n: i32) -> i32 {
    let s: i32 = 0;
    let i: i32 = 1;
    while (i <= n) {
        s = s + i;
        i = i + 1;
    }
    return s;
}
fn main() -> i32 {
    print_int(sum_to(100));   // 100*101/2 = 5050
    return 0;
}
EOF
run_test "hotgp_regreg" /tmp/edge_hotgp_regreg.ari "5050" "hot-GP s=s+i both-register fast path: sum_to(100) = 5050"

# #45  Large immediate that overflows imm8 and forces the 0x81 form.
# A positive 128 (just past imm8 boundary) and a 100000 (full imm32)
# both cover the 7-byte branch.
cat > /tmp/edge_hotgp_imm32.ari << 'EOF'
fn main() -> i32 {
    let v: i32 = 0;
    v = v + 128;        // imm8 boundary: must use 0x81
    v = v + 100000;     // full imm32
    v = v - 200;        // imm32 sub (200 > 127)
    print_int(v);       // 128 + 100000 - 200 = 99928
    return 0;
}
EOF
run_test "hotgp_imm32" /tmp/edge_hotgp_imm32.ari "99928" "hot-GP fast path: imm32 form (0x81) for ±128 and beyond"

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
