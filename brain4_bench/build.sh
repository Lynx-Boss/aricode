#!/bin/bash
# build.sh - Build benchmarks for AMD Ryzen 7 5800X (Zen 3)
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

CC="${CC:-gcc}"
CFLAGS="-O2 -march=znver3 -Wall -Wextra -std=c11"

echo "=== Brain 4 Benchmark Build ==="
echo "Compiler: $($CC --version | head -1)"
echo "Flags:    $CFLAGS"
echo ""

echo "Building bench_cycles..."
$CC $CFLAGS -o bench_cycles bench_cycles.c
echo "  -> bench_cycles OK"

echo "Building bench_bits..."
$CC $CFLAGS -o bench_bits bench_bits.c
echo "  -> bench_bits OK"

echo ""
echo "Build complete."
