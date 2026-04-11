#!/bin/bash
# run_bench.sh - Run all benchmarks and format results
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# Build first if needed
if [ ! -f bench_cycles ] || [ ! -f bench_bits ]; then
    echo "Binaries not found, building first..."
    bash build.sh
    echo ""
fi

echo "################################################################"
echo "#  Brain 4: CPU Cycle-Level Addition Benchmark Suite           #"
echo "#  Target: AMD Ryzen 7 5800X (Zen 3)                          #"
echo "################################################################"
echo ""

# System info
echo "--- System Info ---"
echo "CPU:    $(grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)"
echo "Cores:  $(nproc)"
echo "Kernel: $(uname -r)"
echo "GCC:    $(gcc --version | head -1)"
echo "Date:   $(date -Iseconds)"
echo ""

# Try to set performance governor (informational only)
GOVERNOR=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "unknown")
echo "CPU Governor: $GOVERNOR"
if [ "$GOVERNOR" != "performance" ]; then
    echo "  WARNING: Not using 'performance' governor. Results may vary."
fi
echo ""

# Run cycle-level benchmark
echo "================================================================"
echo " TEST 1: Addition Instruction Variants (bench_cycles)"
echo "================================================================"
echo ""
./bench_cycles
echo ""
echo ""

# Run bitwise benchmark
echo "================================================================"
echo " TEST 2: Bitwise Addition vs Native ADD (bench_bits)"
echo "================================================================"
echo ""
./bench_bits
echo ""
echo ""

echo "################################################################"
echo "#  Benchmark complete.                                         #"
echo "################################################################"
