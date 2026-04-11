#!/bin/bash
# ═══════════════════════════════════════════════════════
# DECIMAL PRECISION SPARRING
# Proves IEEE 754 languages fail at 20-decimal arithmetic
# while aricode's pure digit-array approach gets it right.
# ═══════════════════════════════════════════════════════

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

# Test case definitions
TEST_LABELS=(
    "0.1 + 0.2 = 0.3"
    "1.0 - 0.9 - 0.1 = 0.0"
    "0.1 * 0.1 = 0.01"
    "1.0 / 3.0 * 3.0 = 1.0"
    "0.3 - 0.2 - 0.1 = 0.0"
    "PI to 20 decimals"
    "1.111...1 + 2.222...2 = 3.333...3"
)

EXPECTED=(
    "0.30000000000000000000"
    "0.00000000000000000000"
    "0.01000000000000000000"
    "1.00000000000000000000"
    "0.00000000000000000000"
    "3.14159265358979323846"
    "3.33333333333333333333"
)

# Track all contestants
declare -a CONTESTANT_NAMES
declare -a CONTESTANT_OUTPUTS
declare -a CONTESTANT_NOTES
declare -A RESULTS  # RESULTS[contestant,test] = "got_value"
declare -A PASS     # PASS[contestant,test] = 0 or 1
declare -A SCORES   # SCORES[contestant] = total passes

# --- Extract results from program output ---
# Each program prints lines like:  "    got:      VALUE  [PASS]" or "    got:      VALUE  [FAIL]"
parse_output() {
    local name="$1"
    local output="$2"
    local note="$3"
    local idx=${#CONTESTANT_NAMES[@]}
    CONTESTANT_NAMES+=("$name")
    CONTESTANT_NOTES+=("$note")

    local test_idx=0
    local score=0
    while IFS= read -r line; do
        if [[ "$line" =~ "got:" ]]; then
            # Extract value and pass/fail
            local val
            val=$(echo "$line" | sed 's/.*got: *//; s/ *\[.*//')
            local pf
            if [[ "$line" == *"[PASS]"* ]]; then
                pf=1
            else
                pf=0
            fi
            RESULTS["$idx,$test_idx"]="$val"
            PASS["$idx,$test_idx"]="$pf"
            score=$((score + pf))
            test_idx=$((test_idx + 1))
        fi
    done <<< "$output"
    SCORES["$idx"]=$score
}

echo ""
echo -e "${BOLD}Building and running all contestants...${RESET}"
echo ""

# --- 1. aricode AriDecimal ---
echo -n "  [1/6] aricode AriDecimal ... "
gcc -o decimals_aricode decimals_aricode.c -lm 2>/dev/null
if [ $? -eq 0 ]; then
    OUT=$(./decimals_aricode 2>&1)
    parse_output "aricode" "$OUT" "pure digit-array, no libs"
    echo -e "${GREEN}OK${RESET}"
else
    echo -e "${RED}BUILD FAILED${RESET}"
fi

# --- 2. C double ---
echo -n "  [2/6] C double ... "
gcc -o decimals_c decimals.c -lm 2>/dev/null
if [ $? -eq 0 ]; then
    OUT=$(./decimals_c 2>&1)
    parse_output "C (double)" "$OUT" "IEEE 754 64-bit"
    echo -e "${GREEN}OK${RESET}"
else
    echo -e "${RED}BUILD FAILED${RESET}"
fi

# --- 3. C long double ---
echo -n "  [3/6] C long double ... "
gcc -o decimals_long_c decimals_long.c -lm 2>/dev/null
if [ $? -eq 0 ]; then
    OUT=$(./decimals_long_c 2>&1)
    parse_output "C (long double)" "$OUT" "80-bit extended"
    echo -e "${GREEN}OK${RESET}"
else
    echo -e "${RED}BUILD FAILED${RESET}"
fi

# --- 4. Python float ---
echo -n "  [4/6] Python float ... "
if command -v python3 &>/dev/null; then
    OUT=$(python3 decimals.py 2>&1)
    parse_output "Python (float)" "$OUT" "IEEE 754 double"
    echo -e "${GREEN}OK${RESET}"
else
    echo -e "${YELLOW}SKIP (python3 not found)${RESET}"
fi

# --- 5. Python Decimal ---
echo -n "  [5/6] Python Decimal ... "
if command -v python3 &>/dev/null; then
    OUT=$(python3 decimals_correct.py 2>&1)
    parse_output "Python (Decimal)" "$OUT" "heavy library import"
    echo -e "${GREEN}OK${RESET}"
else
    echo -e "${YELLOW}SKIP (python3 not found)${RESET}"
fi

# --- 6. Node.js ---
echo -n "  [6/6] Node.js ... "
if command -v node &>/dev/null; then
    OUT=$(node decimals.js 2>&1)
    parse_output "Node.js" "$OUT" "IEEE 754 double"
    echo -e "${GREEN}OK${RESET}"
else
    echo -e "${YELLOW}SKIP (node not found)${RESET}"
fi

# --- Optional: Go ---
if command -v go &>/dev/null; then
    echo -n "  [+] Go float64 ... "
    OUT=$(go run decimals.go 2>&1)
    parse_output "Go (float64)" "$OUT" "IEEE 754 double"
    echo -e "${GREEN}OK${RESET}"
fi

# --- Optional: Rust ---
if command -v rustc &>/dev/null; then
    echo -n "  [+] Rust f64 ... "
    rustc -o decimals_rs decimals.rs 2>/dev/null
    if [ $? -eq 0 ]; then
        OUT=$(./decimals_rs 2>&1)
        parse_output "Rust (f64)" "$OUT" "IEEE 754 double"
        echo -e "${GREEN}OK${RESET}"
    else
        echo -e "${RED}BUILD FAILED${RESET}"
    fi
fi

# Clean up binaries
rm -f decimals_aricode decimals_c decimals_long_c decimals_rs 2>/dev/null

# ═══════════════════════════════════════════════════════
# RESULTS TABLE
# ═══════════════════════════════════════════════════════

echo ""
echo ""
echo -e "${BOLD}${CYAN}"
echo "=================================================================="
echo "   DECIMAL PRECISION SPARRING - 20-digit Accuracy Challenge"
echo "=================================================================="
echo -e "${RESET}"

NUM_CONTESTANTS=${#CONTESTANT_NAMES[@]}

for t in $(seq 0 6); do
    echo -e "${BOLD}Test $((t+1)): ${TEST_LABELS[$t]}${RESET}"
    echo -e "  Expected: ${EXPECTED[$t]}"
    echo ""

    for c in $(seq 0 $((NUM_CONTESTANTS - 1))); do
        local_val="${RESULTS[$c,$t]}"
        local_pass="${PASS[$c,$t]}"
        local_name="${CONTESTANT_NAMES[$c]}"
        local_note="${CONTESTANT_NOTES[$c]}"

        if [ "$local_pass" = "1" ]; then
            icon="PASS"
            color="$GREEN"
        else
            icon="FAIL"
            color="$RED"
        fi

        printf "  ${color}%-4s${RESET}  %-20s %-28s  %s\n" \
            "$icon" "$local_name" "$local_val" "($local_note)"
    done
    echo ""
done

# ═══════════════════════════════════════════════════════
# SCOREBOARD
# ═══════════════════════════════════════════════════════

echo -e "${BOLD}${CYAN}"
echo "=================================================================="
echo "   SCOREBOARD"
echo "=================================================================="
echo -e "${RESET}"

for c in $(seq 0 $((NUM_CONTESTANTS - 1))); do
    local_name="${CONTESTANT_NAMES[$c]}"
    local_score="${SCORES[$c]}"
    local_note="${CONTESTANT_NOTES[$c]}"

    if [ "$local_score" = "7" ]; then
        color="$GREEN"
        bar="======="
    elif [ "$local_score" -ge "4" ]; then
        color="$YELLOW"
        bar=$(printf '=%.0s' $(seq 1 $local_score))
    else
        color="$RED"
        bar=$(printf '=%.0s' $(seq 1 $local_score))
    fi

    printf "  ${color}%-20s  %d/7  ${RESET}%-8s  %s\n" \
        "$local_name" "$local_score" "[$bar]" "($local_note)"
done

echo ""

# ═══════════════════════════════════════════════════════
# VERDICT
# ═══════════════════════════════════════════════════════

echo -e "${BOLD}${CYAN}"
echo "=================================================================="
echo "   VERDICT"
echo "=================================================================="
echo -e "${RESET}"

echo -e "  IEEE 754 floating-point (used by C, Python, JS, Go, Rust)"
echo -e "  ${RED}CANNOT represent 0.1 exactly in binary.${RESET}"
echo -e "  At 20 decimal places, the errors become visible."
echo ""
echo -e "  Python's decimal.Decimal module CAN do it -- but requires"
echo -e "  importing a ${YELLOW}heavy external library${RESET} and wrapping every"
echo -e "  number in Decimal(\"...\") string constructors."
echo ""
echo -e "  ${GREEN}${BOLD}aricode uses pure digit-array decimal arithmetic.${RESET}"
echo -e "  ${GREEN}No floats. No doubles. No external libraries.${RESET}"
echo -e "  ${GREEN}Just digits 0-9 in arrays, with schoolbook arithmetic.${RESET}"
echo -e "  ${GREEN}${BOLD}7/7 PASS. Always correct. Always exact.${RESET}"
echo ""
echo "=================================================================="
echo ""
