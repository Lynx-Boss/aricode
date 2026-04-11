#!/bin/bash
# ============================================================================
#  ARICODE SPARRING - Build All Challenges
# ============================================================================
#  Builds all challenge programs in every available language/compiler.
#  Skips any compiler not found on the system.
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ARIC="$SCRIPT_DIR/../src/compiler/aric"
BUILD_DIR="$SCRIPT_DIR/build"

# ANSI colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
RESET='\033[0m'

# ── Detect available compilers ──────────────────────────────────────────────

declare -A COMPILERS
check_compiler() {
    local name="$1" cmd="$2"
    if command -v "$cmd" >/dev/null 2>&1; then
        COMPILERS["$name"]="$(command -v "$cmd")"
        printf "  ${GREEN}[OK]${RESET}  %-20s %s\n" "$name" "$(command -v "$cmd")"
    else
        printf "  ${YELLOW}[--]${RESET}  %-20s ${DIM}not found, skipping${RESET}\n" "$name"
    fi
}

echo ""
echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo -e "${BOLD}${CYAN}  ARICODE SPARRING - Build System${RESET}"
echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo ""
echo -e "${BOLD}Detecting compilers:${RESET}"
echo ""

check_compiler "aricode"  "true"  # We check aric separately
if [ -x "$ARIC" ]; then
    COMPILERS["aricode"]="$ARIC"
    printf "  ${GREEN}[OK]${RESET}  %-20s %s\n" "aricode (aric)" "$ARIC"
else
    printf "  ${RED}[!!]${RESET}  %-20s ${RED}aric not found at $ARIC${RESET}\n" "aricode (aric)"
    echo -e "\n${RED}ERROR: aricode compiler not found. Build it first:${RESET}"
    echo "  cd ../src/compiler && make"
    exit 1
fi

check_compiler "gcc"      "gcc"
check_compiler "clang"    "clang"
check_compiler "rustc"    "rustc"
check_compiler "go"       "go"
check_compiler "nasm"     "nasm"
check_compiler "python3"  "python3"
check_compiler "ld"       "ld"

echo ""

# ── Challenge definitions ───────────────────────────────────────────────────

declare -A CHALLENGES
CHALLENGES["01_add"]="add:42"
CHALLENGES["02_fib"]="fib:55"
CHALLENGES["03_factorial"]="fact:120"
CHALLENGES["05_ackermann"]="ack:125"
CHALLENGES["06_collatz"]="collatz:178"
CHALLENGES["07_mersenne"]="mersenne:1"
CHALLENGES["08_gcd"]="gcd:21"
CHALLENGES["09_primecount"]="primecount:25"
CHALLENGES["10_powmod"]="powmod:85"
CHALLENGES["11_isqrt"]="isqrt:127"
CHALLENGES["12_perceptron"]="perceptron:4"
CHALLENGES["13_minimax"]="minimax:3"

# Sorted keys
CHALLENGE_ORDER=("01_add" "02_fib" "03_factorial" "05_ackermann" "06_collatz" "07_mersenne" "08_gcd" "09_primecount" "10_powmod" "11_isqrt" "12_perceptron" "13_minimax")

# ── Build functions ─────────────────────────────────────────────────────────

build_count=0
fail_count=0

build_one() {
    local label="$1" outfile="$2"
    shift 2
    local cmd=("$@")

    printf "    %-35s" "$label"

    if "${cmd[@]}" >/dev/null 2>&1; then
        local size
        size=$(stat -c%s "$outfile" 2>/dev/null || echo "?")
        printf "${GREEN}OK${RESET}  ${DIM}(%s bytes)${RESET}\n" "$size"
        build_count=$((build_count + 1))
        return 0
    else
        printf "${RED}FAIL${RESET}\n"
        fail_count=$((fail_count + 1))
        return 1
    fi
}

# ── Build loop ──────────────────────────────────────────────────────────────

mkdir -p "$BUILD_DIR"

for challenge_key in "${CHALLENGE_ORDER[@]}"; do
    IFS=':' read -r base expected <<< "${CHALLENGES[$challenge_key]}"
    challenge_dir="$SCRIPT_DIR/challenges/$challenge_key"

    echo -e "${BOLD}${CYAN}--- Challenge: $challenge_key ($base) ---${RESET}"
    echo ""

    out_dir="$BUILD_DIR/$challenge_key"
    mkdir -p "$out_dir"

    # aricode
    if [ -x "$ARIC" ] && [ -f "$challenge_dir/$base.ari" ]; then
        build_one "aricode" \
            "$out_dir/${base}_aricode" \
            "$ARIC" "$challenge_dir/$base.ari" -o "$out_dir/${base}_aricode"
    fi

    # GCC variants
    if [ -n "${COMPILERS[gcc]:-}" ] && [ -f "$challenge_dir/$base.c" ]; then
        build_one "C (gcc -O0)" \
            "$out_dir/${base}_gcc_O0" \
            gcc -O0 -static -o "$out_dir/${base}_gcc_O0" "$challenge_dir/$base.c"

        build_one "C (gcc -O2)" \
            "$out_dir/${base}_gcc_O2" \
            gcc -O2 -static -o "$out_dir/${base}_gcc_O2" "$challenge_dir/$base.c"

        build_one "C (gcc -O3)" \
            "$out_dir/${base}_gcc_O3" \
            gcc -O3 -static -o "$out_dir/${base}_gcc_O3" "$challenge_dir/$base.c"

        build_one "C (gcc -Os)" \
            "$out_dir/${base}_gcc_Os" \
            gcc -Os -static -o "$out_dir/${base}_gcc_Os" "$challenge_dir/$base.c"

        # Also build dynamically linked for comparison
        build_one "C (gcc -O2 dynamic)" \
            "$out_dir/${base}_gcc_O2_dyn" \
            gcc -O2 -o "$out_dir/${base}_gcc_O2_dyn" "$challenge_dir/$base.c"
    fi

    # Clang
    if [ -n "${COMPILERS[clang]:-}" ] && [ -f "$challenge_dir/$base.c" ]; then
        build_one "C (clang -O2)" \
            "$out_dir/${base}_clang_O2" \
            clang -O2 -static -o "$out_dir/${base}_clang_O2" "$challenge_dir/$base.c"

        build_one "C (clang -O2 dynamic)" \
            "$out_dir/${base}_clang_O2_dyn" \
            clang -O2 -o "$out_dir/${base}_clang_O2_dyn" "$challenge_dir/$base.c"
    fi

    # Rust
    if [ -n "${COMPILERS[rustc]:-}" ] && [ -f "$challenge_dir/$base.rs" ]; then
        build_one "Rust (opt-level=2)" \
            "$out_dir/${base}_rust" \
            rustc -C opt-level=2 -o "$out_dir/${base}_rust" "$challenge_dir/$base.rs"
    fi

    # Go
    if [ -n "${COMPILERS[go]:-}" ] && [ -f "$challenge_dir/$base.go" ]; then
        build_one "Go" \
            "$out_dir/${base}_go" \
            go build -o "$out_dir/${base}_go" "$challenge_dir/$base.go"
    fi

    # NASM + ld
    if [ -n "${COMPILERS[nasm]:-}" ] && [ -n "${COMPILERS[ld]:-}" ] && [ -f "$challenge_dir/$base.asm" ]; then
        # Two-step build
        printf "    %-35s" "NASM asm"
        if nasm -f elf64 -o "$out_dir/${base}_asm.o" "$challenge_dir/$base.asm" 2>/dev/null && \
           ld -s -n -o "$out_dir/${base}_asm" "$out_dir/${base}_asm.o" 2>/dev/null; then
            asm_size=$(stat -c%s "$out_dir/${base}_asm" 2>/dev/null || echo "?")
            printf "${GREEN}OK${RESET}  ${DIM}(%s bytes)${RESET}\n" "$asm_size"
            build_count=$((build_count + 1))
            rm -f "$out_dir/${base}_asm.o"
        else
            printf "${RED}FAIL${RESET}\n"
            fail_count=$((fail_count + 1))
            rm -f "$out_dir/${base}_asm.o"
        fi
    fi

    echo ""
done

# ── Verify correctness ─────────────────────────────────────────────────────

echo -e "${BOLD}${CYAN}--- Correctness Verification ---${RESET}"
echo ""

verify_count=0
verify_fail=0

for challenge_key in "${CHALLENGE_ORDER[@]}"; do
    IFS=':' read -r base expected <<< "${CHALLENGES[$challenge_key]}"
    out_dir="$BUILD_DIR/$challenge_key"

    for binary in "$out_dir"/${base}_*; do
        [ -x "$binary" ] || continue
        bname=$(basename "$binary")
        printf "    %-40s" "$bname"

        set +e
        "$binary" >/dev/null 2>&1
        rc=$?
        set -e

        if [ "$rc" -eq "$expected" ]; then
            printf "${GREEN}PASS${RESET} ${DIM}(exit=$rc)${RESET}\n"
            verify_count=$((verify_count + 1))
        else
            printf "${RED}FAIL${RESET} ${DIM}(expected=$expected, got=$rc)${RESET}\n"
            verify_fail=$((verify_fail + 1))
        fi
    done
done

echo ""
echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo -e "  Built:    ${GREEN}${build_count}${RESET} binaries"
echo -e "  Failed:   ${RED}${fail_count}${RESET} builds"
echo -e "  Verified: ${GREEN}${verify_count}${RESET} correct, ${RED}${verify_fail}${RESET} incorrect"
echo -e "${BOLD}${CYAN}============================================================${RESET}"
echo ""

[ "$fail_count" -eq 0 ] && [ "$verify_fail" -eq 0 ]
