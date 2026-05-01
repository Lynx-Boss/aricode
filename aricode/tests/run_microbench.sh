#!/bin/bash
# ============================================================================
#  ARICODE — AVX2 kernel microbenchmarks
# ============================================================================
#  Times each kernel in a tight call loop with constant-shape inputs.  Used
#  to gate kernel-level optimisations (dual-accumulator FMA, vshufps
#  hsum, etc.) — without numbers here, those changes are speculation.
#
#  Methodology
#    - Each kernel is called N times in a hot loop.  N is sized so the
#      total wall is in the 50-300 ms band — enough to drown OS noise,
#      short enough to iterate.
#    - Output of each call is folded into a checksum that's printed at
#      the end, so the optimiser can't DCE the loop.
#    - Best of 3 trials reported, in ns/call.
#    - All buffers are pre-filled before the timing loop so allocator
#      heat-up isn't measured.
#
#  Add a kernel here when you ship a new one or change one in a way that
#  could plausibly affect throughput.
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ARIC="$SCRIPT_DIR/../src/compiler/aric"

CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
RESET='\033[0m'

bench() {
    # bench <name> <N> <ari-source-file>
    local name="$1" n="$2" src="$3"
    local bin="/tmp/aribench_$name"

    "$ARIC" "$src" -o "$bin" >/dev/null 2>&1 || {
        printf "  %-40s ${BOLD}COMPILE FAIL${RESET}\n" "$name"
        return
    }

    # Best of 3 — wall time in ns via shell arithmetic on date +%s%N.
    local best_ns=999999999999
    for i in 1 2 3; do
        local t0 t1 dt
        t0=$(date +%s%N)
        "$bin" >/dev/null
        t1=$(date +%s%N)
        dt=$((t1 - t0))
        if [ "$dt" -lt "$best_ns" ]; then best_ns=$dt; fi
    done

    local per_call_ns=$((best_ns / n))
    local per_call_us=$(awk "BEGIN { printf \"%.3f\", $best_ns / 1000.0 / $n }")
    local total_ms=$(awk "BEGIN { printf \"%.1f\", $best_ns / 1000000.0 }")

    printf "  %-40s ${BOLD}%8s µs/call${RESET}  ${DIM}(%s calls in %s ms, best of 3)${RESET}\n" \
        "$name" "$per_call_us" "$n" "$total_ms"

    rm -f "$bin"
}

echo
echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo -e "${BOLD}${CYAN}  ARICODE AVX2 kernel microbenchmarks${RESET}"
echo -e "${BOLD}${CYAN}============================================================${RESET}"

# ─── arr_f32_matvec  (m × n)   ──────────────────────────────────────
# Two shapes: cnn2 fc1 (3136 → 64) and cnn2 fc2 (64 → 10).
N=10000
cat > /tmp/aribench_matvec_3136_64.ari <<EOF
fn main() -> i32 {
    let m: i32 = 64;
    let n: i32 = 3136;
    let W: i32 = arr_f32_new(m * n);
    let x: i32 = arr_f32_new(n);
    let b: i32 = arr_f32_new(m);
    let y: i32 = arr_f32_new(m);
    arr_f32_fill(W, 0.001);
    arr_f32_fill(x, 0.5);
    arr_f32_fill(b, 0.0);

    let i: i32 = 0;
    let acc: f64 = 0.0;
    while (i < ${N}) {
        arr_f32_matvec(W, x, b, y, m, n);
        acc = acc + arr_f32_get(y, 0);
        i = i + 1;
    }
    print_f64(acc, 6);
    return 0;
}
EOF
bench "arr_f32_matvec (m=64, n=3136)" $N /tmp/aribench_matvec_3136_64.ari

cat > /tmp/aribench_matvec_64_10.ari <<EOF
fn main() -> i32 {
    let m: i32 = 10;
    let n: i32 = 64;
    let W: i32 = arr_f32_new(m * n);
    let x: i32 = arr_f32_new(n);
    let b: i32 = arr_f32_new(m);
    let y: i32 = arr_f32_new(m);
    arr_f32_fill(W, 0.001);
    arr_f32_fill(x, 0.5);
    arr_f32_fill(b, 0.0);

    let i: i32 = 0;
    let acc: f64 = 0.0;
    while (i < ${N}) {
        arr_f32_matvec(W, x, b, y, m, n);
        acc = acc + arr_f32_get(y, 0);
        i = i + 1;
    }
    print_f64(acc, 6);
    return 0;
}
EOF
bench "arr_f32_matvec (m=10, n=64)" $N /tmp/aribench_matvec_64_10.ari

# ─── arr_i8_matvec_f32  ─────────────────────────────────────────────
# int8-quantised matvec — cnn2 fc1 shape, with the same scale used by
# the packer.  Should be near-equal to arr_f32_matvec since the i8
# load + dequant is amortised over 8-lane chunks.
cat > /tmp/aribench_i8_matvec.ari <<EOF
fn main() -> i32 {
    let m: i32 = 64;
    let n: i32 = 3136;
    let W: i32 = arr_i8_new(m * n);
    let x: i32 = arr_f32_new(n);
    let y: i32 = arr_f32_new(m);
    arr_f32_fill(x, 0.5);

    let i: i32 = 0;
    let acc: f64 = 0.0;
    while (i < ${N}) {
        arr_i8_matvec_f32(W, x, y, m, 0.001);
        acc = acc + arr_f32_get(y, 0);
        i = i + 1;
    }
    print_f64(acc, 6);
    return 0;
}
EOF
# arr_i8_new isn't a thing; use the analogous-builtin shape via i8 file.
# Fall back to allocating raw bytes in an i64 buffer + reinterpreting.
# (skip if unavailable.)
if ! grep -q "arr_i8_new" /home/serverbig/git-proyect/aricoderoot/aricode/src/codegen/codegen_builtins.c; then
    rm -f /tmp/aribench_i8_matvec.ari
    printf "  %-40s ${DIM}(skipped: arr_i8_new not in codegen)${RESET}\n" "arr_i8_matvec_f32"
else
    bench "arr_i8_matvec_f32 (m=64, n=3136)" $N /tmp/aribench_i8_matvec.ari
fi

# ─── arr_f32_conv2d_3x3_p1  (single-channel) ────────────────────────
# cnn2 conv1 shape: 1 input channel, 8 output channels, 28×28 spatial.
N=10000
cat > /tmp/aribench_conv_single.ari <<EOF
fn main() -> i32 {
    let pad: i32 = arr_f32_new(900);
    let W:   i32 = arr_f32_new(8 * 9);
    let b:   i32 = arr_f32_new(8);
    let out: i32 = arr_f32_new(8 * 28 * 28);
    arr_f32_fill(pad, 0.5);
    arr_f32_fill(W,   0.1);
    arr_f32_fill(b,   0.0);

    let i: i32 = 0;
    let acc: f64 = 0.0;
    while (i < ${N}) {
        arr_f32_conv2d_3x3_p1(pad, W, b, out, 8);
        acc = acc + arr_f32_get(out, 0);
        i = i + 1;
    }
    print_f64(acc, 6);
    return 0;
}
EOF
bench "arr_f32_conv2d_3x3_p1 (C_out=8)" $N /tmp/aribench_conv_single.ari

# ─── arr_f32_conv2d_3x3_p1_multi  (multi-channel) ───────────────────
# cnn2 conv2 shape: 8 input channels, 16 output channels, 28×28 spatial.
N=2000
cat > /tmp/aribench_conv_multi.ari <<EOF
fn main() -> i32 {
    let pad: i32 = arr_f32_new(8 * 900);
    let W:   i32 = arr_f32_new(16 * 8 * 9);
    let b:   i32 = arr_f32_new(16);
    let out: i32 = arr_f32_new(16 * 28 * 28);
    arr_f32_fill(pad, 0.5);
    arr_f32_fill(W,   0.05);
    arr_f32_fill(b,   0.0);

    let i: i32 = 0;
    let acc: f64 = 0.0;
    while (i < ${N}) {
        arr_f32_conv2d_3x3_p1_multi(pad, 8, W, b, out, 16);
        acc = acc + arr_f32_get(out, 0);
        i = i + 1;
    }
    print_f64(acc, 6);
    return 0;
}
EOF
bench "arr_f32_conv2d_3x3_p1_multi (C_in=8, C_out=16)" $N /tmp/aribench_conv_multi.ari

# ─── arr_f32_dot  (n)  ──────────────────────────────────────────────
N=100000
cat > /tmp/aribench_dot.ari <<EOF
fn main() -> i32 {
    let n: i32 = 1024;
    let a: i32 = arr_f32_new(n);
    let b: i32 = arr_f32_new(n);
    arr_f32_fill(a, 0.1);
    arr_f32_fill(b, 0.2);

    let i: i32 = 0;
    let acc: f64 = 0.0;
    while (i < ${N}) {
        acc = acc + arr_f32_dot(a, b);
        i = i + 1;
    }
    print_f64(acc, 6);
    return 0;
}
EOF
bench "arr_f32_dot (n=1024)" $N /tmp/aribench_dot.ari

echo
echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo -e "${DIM}  Numbers above are wall-clock for a tight call loop.${RESET}"
echo -e "${DIM}  Compare them before/after a kernel change to gate the patch.${RESET}"
echo

# Cleanup
rm -f /tmp/aribench_*.ari
