#!/bin/bash
# ============================================================================
#  ARICODE SPARRING - Benchmark Runner
# ============================================================================
#  Runs all built binaries and collects performance metrics:
#    - Binary size (bytes)
#    - Text section size (bytes)
#    - Instruction count (via objdump)
#    - Execution time (average over N iterations)
#    - Startup time (single process exec-to-exit)
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
RESULTS_DIR="$SCRIPT_DIR/results"
ITERATIONS="${1:-1000}"

# ANSI colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
RESET='\033[0m'

# ── Challenge definitions ───────────────────────────────────────────────────

declare -A CHALLENGES
CHALLENGES["01_add"]="add:42:Simple Addition (37+5=42)"
CHALLENGES["02_fib"]="fib:55:Fibonacci (fib(10)=55)"
CHALLENGES["03_factorial"]="fact:120:Factorial (5!=120)"
CHALLENGES["05_ackermann"]="ack:125:Ackermann A(3,4)=125 [>10K recursive calls]"
CHALLENGES["06_collatz"]="collatz:178:Collatz(871)=178 [UNSOLVED conjecture, peaks at 190996]"
CHALLENGES["07_mersenne"]="mersenne:1:Mersenne M31=2^31-1 prime verification [23K divisors, 10-digit number]"
CHALLENGES["08_gcd"]="gcd:21:Euclidean GCD(462,1071)=21 [oldest algorithm, ~300 BC]"
CHALLENGES["09_primecount"]="primecount:25:Count primes below 100 [nested loops, trial division]"
CHALLENGES["10_powmod"]="powmod:85:Modular exponentiation 7^19 mod 211 [crypto primitive]"
CHALLENGES["11_isqrt"]="isqrt:127:Integer sqrt(16129)=127 [Newton-Raphson convergence]"
CHALLENGES["12_perceptron"]="perceptron:4:Perceptron AND gate [1000 epochs, neural network]"
CHALLENGES["13_minimax"]="minimax:3:Minimax Nim(15) [game AI, 500K+ tree nodes]"
CHALLENGE_ORDER=("01_add" "02_fib" "03_factorial" "05_ackermann" "06_collatz" "07_mersenne" "08_gcd" "09_primecount" "10_powmod" "11_isqrt" "12_perceptron" "13_minimax")

# ── Setup ───────────────────────────────────────────────────────────────────

mkdir -p "$RESULTS_DIR"

echo ""
echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo -e "${BOLD}${CYAN}  ARICODE SPARRING - Benchmark Runner${RESET}"
echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo ""
echo -e "  Iterations per binary: ${BOLD}${ITERATIONS}${RESET}"
echo -e "  Build directory:       ${DIM}${BUILD_DIR}${RESET}"
echo -e "  Results directory:     ${DIM}${RESULTS_DIR}${RESET}"
echo ""

# ── Helper: get human-friendly label from binary name ───────────────────────

get_label() {
    local bname="$1"
    # Remove the challenge base prefix (add_, fib_, fact_)
    local suffix="${bname#*_}"

    case "$suffix" in
        aricode)        echo "aricode" ;;
        gcc_O0)         echo "C (gcc -O0)" ;;
        gcc_O2)         echo "C (gcc -O2)" ;;
        gcc_O3)         echo "C (gcc -O3)" ;;
        gcc_Os)         echo "C (gcc -Os)" ;;
        gcc_O2_dyn)     echo "C (gcc -O2 dyn)" ;;
        clang_O2)       echo "C (clang -O2)" ;;
        clang_O2_dyn)   echo "C (clang -O2 dyn)" ;;
        rust)           echo "Rust" ;;
        go)             echo "Go" ;;
        asm)            echo "NASM asm" ;;
        *)              echo "$suffix" ;;
    esac
}

# ── Helper: count instructions in text section ──────────────────────────────

count_instructions() {
    local binary="$1"
    local count
    if command -v objdump >/dev/null 2>&1; then
        count=$(objdump -d "$binary" 2>/dev/null | grep -c '^\s*[0-9a-f]\+:' || true)
        if [ "$count" -gt 0 ] 2>/dev/null; then
            echo "$count"
        else
            # For minimal ELF (aricode), estimate from binary size minus ELF header
            local fsize
            fsize=$(stat -c%s "$binary" 2>/dev/null || echo "0")
            if [ "$fsize" -lt 1000 ]; then
                # aricode/NASM minimal binary: code = total - 120 (ELF header + phdr)
                local code_bytes=$((fsize - 120))
                [ "$code_bytes" -lt 0 ] && code_bytes=0
                # Rough estimate: ~3 bytes per instruction average for x86_64
                echo "$((code_bytes / 3))"
            else
                echo "0"
            fi
        fi
    else
        echo "N/A"
    fi
}

# ── Helper: get text section size ───────────────────────────────────────────

get_text_size() {
    local binary="$1"
    local tsize
    if command -v size >/dev/null 2>&1; then
        tsize=$(size "$binary" 2>/dev/null | tail -1 | awk '{print $1}' || true)
        if [ -n "$tsize" ] && [ "$tsize" -gt 0 ] 2>/dev/null; then
            echo "$tsize"
        else
            # For minimal ELF (aricode), code size = total - ELF header (120 bytes)
            local fsize
            fsize=$(stat -c%s "$binary" 2>/dev/null || echo "0")
            if [ "$fsize" -lt 1000 ]; then
                local code_bytes=$((fsize - 120))
                [ "$code_bytes" -lt 0 ] && code_bytes=0
                echo "$code_bytes"
            else
                echo "0"
            fi
        fi
    else
        echo "N/A"
    fi
}

# ── Helper: measure execution time (average of N runs) ─────────────────────

measure_exec_time() {
    local binary="$1"
    local iters="$2"
    local start end elapsed avg

    # Use clock_gettime via date for nanosecond precision
    # Run all iterations in a tight loop
    start=$(date +%s%N)
    for ((i = 0; i < iters; i++)); do
        "$binary" >/dev/null 2>&1 || true
    done
    end=$(date +%s%N)

    elapsed=$((end - start))
    avg=$((elapsed / iters))
    echo "$avg"
}

# ── Helper: measure single startup time ────────────────────────────────────

measure_startup_time() {
    local binary="$1"
    local start end elapsed

    # Average of 100 runs for startup
    local runs=100
    start=$(date +%s%N)
    for ((i = 0; i < runs; i++)); do
        "$binary" >/dev/null 2>&1 || true
    done
    end=$(date +%s%N)

    elapsed=$((end - start))
    echo $((elapsed / runs))
}

# ── Benchmark loop ──────────────────────────────────────────────────────────

for challenge_key in "${CHALLENGE_ORDER[@]}"; do
    IFS=':' read -r base expected desc <<< "${CHALLENGES[$challenge_key]}"
    out_dir="$BUILD_DIR/$challenge_key"
    result_file="$RESULTS_DIR/${challenge_key}.csv"

    echo -e "${BOLD}${CYAN}--- Benchmarking: $desc ---${RESET}"
    echo ""

    # CSV header
    echo "label,binary_name,binary_size,text_size,instructions,exec_time_ns,startup_time_ns" > "$result_file"

    # Python special case (interpreted)
    py_file="$SCRIPT_DIR/challenges/$challenge_key/$base.py"
    if [ -f "$py_file" ] && command -v python3 >/dev/null 2>&1; then
        printf "    %-30s" "Python3"

        # Measure python execution time
        py_start=$(date +%s%N)
        py_runs=100
        for ((i = 0; i < py_runs; i++)); do
            python3 "$py_file" >/dev/null 2>&1 || true
        done
        py_end=$(date +%s%N)
        py_avg=$(( (py_end - py_start) / py_runs ))

        # Startup time
        py_startup_start=$(date +%s%N)
        py_startup_runs=50
        for ((i = 0; i < py_startup_runs; i++)); do
            python3 -c "pass" >/dev/null 2>&1 || true
        done
        py_startup_end=$(date +%s%N)
        py_startup=$(( (py_startup_end - py_startup_start) / py_startup_runs ))

        # Python script size
        py_size=$(stat -c%s "$py_file" 2>/dev/null || echo "0")

        printf "${GREEN}done${RESET}  ${DIM}(avg ${py_avg}ns)${RESET}\n"
        echo "Python3,python3,$py_size,N/A,N/A,$py_avg,$py_startup" >> "$result_file"
    fi

    # Compiled binaries
    for binary in "$out_dir"/${base}_*; do
        [ -x "$binary" ] || continue
        bname=$(basename "$binary")
        label=$(get_label "$bname")

        printf "    %-30s" "$label"

        # Binary size
        bin_size=$(stat -c%s "$binary" 2>/dev/null || echo "0")

        # Text section size
        text_size=$(get_text_size "$binary")

        # Instruction count
        instr_count=$(count_instructions "$binary")

        # Execution time
        exec_time=$(measure_exec_time "$binary" "$ITERATIONS")

        # Startup time
        startup_time=$(measure_startup_time "$binary")

        printf "${GREEN}done${RESET}  ${DIM}(size=${bin_size}B, avg=${exec_time}ns)${RESET}\n"

        echo "$label,$bname,$bin_size,$text_size,$instr_count,$exec_time,$startup_time" >> "$result_file"
    done

    echo ""
done

# ── Display results tables ──────────────────────────────────────────────────

echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo -e "${BOLD}${CYAN}  BENCHMARK RESULTS${RESET}"
echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo ""

for challenge_key in "${CHALLENGE_ORDER[@]}"; do
    IFS=':' read -r base expected desc <<< "${CHALLENGES[$challenge_key]}"
    result_file="$RESULTS_DIR/${challenge_key}.csv"

    echo -e "${BOLD}${CYAN}--- $desc ---${RESET}"
    echo ""

    # Table header
    printf "  ${BOLD}%-22s %10s %10s %8s %12s %12s${RESET}\n" \
        "Implementation" "Bin Size" "Text Size" "Instrs" "Exec (ns)" "Startup (ns)"
    printf "  %-22s %10s %10s %8s %12s %12s\n" \
        "----------------------" "----------" "----------" "--------" "------------" "------------"

    # Read and display
    tail -n +2 "$result_file" | sort -t',' -k3 -n | while IFS=',' read -r label bname bin_size text_size instr exec_time startup; do
        printf "  %-22s %10s %10s %8s %12s %12s\n" \
            "$label" "$bin_size" "$text_size" "$instr" "$exec_time" "$startup"
    done

    echo ""
done

echo -e "${BOLD}Results saved to: ${CYAN}${RESULTS_DIR}/${RESET}"
echo ""
