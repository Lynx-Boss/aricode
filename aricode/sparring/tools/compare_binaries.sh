#!/usr/bin/env bash
# compare_binaries.sh - Side-by-side comparison of multiple binaries
# Usage: ./compare_binaries.sh binary1 binary2 [binary3 ...]
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
RESET='\033[0m'

usage() {
    echo "Usage: $0 binary1 binary2 [binary3 ...] [--func FUNCTION]"
    echo ""
    echo "Compares multiple compiled binaries side-by-side."
    echo ""
    echo "Options:"
    echo "  --func NAME    Function to compare (default: main)"
    echo ""
    echo "Examples:"
    echo "  $0 prog_O0 prog_O2 prog_Os"
    echo "  $0 prog_gcc prog_clang --func compute"
    exit 1
}

if [[ $# -lt 2 ]]; then
    usage
fi

# Parse arguments
BINARIES=()
FUNC="main"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --func)
            FUNC="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            BINARIES+=("$1")
            shift
            ;;
    esac
done

if [[ ${#BINARIES[@]} -lt 2 ]]; then
    echo "Error: at least 2 binaries required" >&2
    exit 1
fi

# Validate all binaries exist
for bin in "${BINARIES[@]}"; do
    if [[ ! -f "$bin" ]]; then
        echo "Error: binary '$bin' not found" >&2
        exit 1
    fi
done

TMPDIR=$(mktemp -d)
trap "rm -rf '$TMPDIR'" EXIT

# --- Gather metrics for each binary ---
declare -a NAMES SIZES TEXT_SIZES INSN_COUNTS AVG_LENS REX_COUNTS NOP_COUNTS

for i in "${!BINARIES[@]}"; do
    bin="${BINARIES[$i]}"
    name=$(basename "$bin")
    NAMES+=("$name")

    # Disassemble
    objdump -d "$bin" > "$TMPDIR/disasm_$i.full" 2>/dev/null

    # Extract function
    awk -v func="<${FUNC}>" -v func2="<${FUNC}()>" '
        $0 ~ func || $0 ~ func2 { found=1; print; next }
        found && /^$/ { exit }
        found { print }
    ' "$TMPDIR/disasm_$i.full" > "$TMPDIR/disasm_$i.func"

    # Sizes
    SIZES+=("$(stat -c%s "$bin" 2>/dev/null || stat -f%z "$bin" 2>/dev/null)")
    TEXT_SIZES+=("$(size "$bin" 2>/dev/null | tail -1 | awk '{print $1}')")

    # Instruction count in function
    ic=$(grep -cE '^\s+[0-9a-f]+:\s' "$TMPDIR/disasm_$i.func" 2>/dev/null || echo 0)
    INSN_COUNTS+=("$ic")

    # Average instruction length
    total_bytes=0
    count=0
    while IFS= read -r line; do
        hex_bytes=$(echo "$line" | grep -oP '^\s+[0-9a-f]+:\s+\K([0-9a-f]{2}\s)+' 2>/dev/null || true)
        if [[ -n "$hex_bytes" ]]; then
            nb=$(echo "$hex_bytes" | tr -s ' ' '\n' | grep -c '[0-9a-f]' 2>/dev/null || echo 0)
            total_bytes=$((total_bytes + nb))
            count=$((count + 1))
        fi
    done < "$TMPDIR/disasm_$i.func"
    if [[ "$count" -gt 0 ]]; then
        AVG_LENS+=("$(awk "BEGIN {printf \"%.2f\", $total_bytes / $count}")")
    else
        AVG_LENS+=("0.00")
    fi

    # REX count
    rex=$(grep -cP '^\s+[0-9a-f]+:\s+(48|49|4c|4d)\s' "$TMPDIR/disasm_$i.func" 2>/dev/null || echo 0)
    REX_COUNTS+=("$rex")

    # NOP count
    nops=$(grep -cP '\bnop\b' "$TMPDIR/disasm_$i.func" 2>/dev/null || echo 0)
    NOP_COUNTS+=("$nops")

    # Extract just mnemonics for later
    grep -oP '^\s+[0-9a-f]+:\s+([0-9a-f]{2}\s)+\s+\K\S+' "$TMPDIR/disasm_$i.func" > "$TMPDIR/mnemonics_$i" 2>/dev/null || true
done

# === Output ===

echo -e "${BOLD}================================================================================${RESET}"
echo -e "${BOLD}  BINARY COMPARISON: ${FUNC}()${RESET}"
echo -e "${BOLD}================================================================================${RESET}"
echo ""

# --- Metrics Table ---
echo -e "${CYAN}--- METRICS COMPARISON ---${RESET}"
echo ""

# Header
printf "  ${BOLD}%-24s" "Metric"
for name in "${NAMES[@]}"; do
    printf "%-16s" "$name"
done
printf "${RESET}\n"

printf "  %-24s" "------------------------"
for _ in "${NAMES[@]}"; do
    printf "%-16s" "----------------"
done
printf "\n"

# Rows
printf "  %-24s" "Binary size (bytes)"
for s in "${SIZES[@]}"; do printf "%-16s" "$s"; done; echo ""

printf "  %-24s" ".text size (bytes)"
for s in "${TEXT_SIZES[@]}"; do printf "%-16s" "$s"; done; echo ""

printf "  %-24s" "Instructions (${FUNC})"
for s in "${INSN_COUNTS[@]}"; do printf "%-16s" "$s"; done; echo ""

printf "  %-24s" "Avg insn length (B)"
for s in "${AVG_LENS[@]}"; do printf "%-16s" "$s"; done; echo ""

printf "  %-24s" "REX prefix count"
for s in "${REX_COUNTS[@]}"; do printf "%-16s" "$s"; done; echo ""

printf "  %-24s" "NOP instructions"
for s in "${NOP_COUNTS[@]}"; do printf "%-16s" "$s"; done; echo ""

echo ""

# --- Find best values ---
echo -e "${CYAN}--- WINNER ANALYSIS ---${RESET}"
echo ""

# Smallest binary
min_size=${SIZES[0]}; winner=0
for i in "${!SIZES[@]}"; do
    if [[ "${SIZES[$i]}" -lt "$min_size" ]]; then
        min_size="${SIZES[$i]}"
        winner=$i
    fi
done
echo -e "  Smallest binary:         ${GREEN}${NAMES[$winner]}${RESET} (${min_size} bytes)"

# Fewest instructions
min_insn=${INSN_COUNTS[0]}; winner=0
for i in "${!INSN_COUNTS[@]}"; do
    if [[ "${INSN_COUNTS[$i]}" -lt "$min_insn" && "${INSN_COUNTS[$i]}" -gt 0 ]]; then
        min_insn="${INSN_COUNTS[$i]}"
        winner=$i
    fi
done
echo -e "  Fewest instructions:     ${GREEN}${NAMES[$winner]}${RESET} (${min_insn})"

# Shortest avg instruction
min_avg="${AVG_LENS[0]}"; winner=0
for i in "${!AVG_LENS[@]}"; do
    if awk "BEGIN {exit !(${AVG_LENS[$i]} < $min_avg && ${AVG_LENS[$i]} > 0)}"; then
        min_avg="${AVG_LENS[$i]}"
        winner=$i
    fi
done
echo -e "  Shortest avg insn:       ${GREEN}${NAMES[$winner]}${RESET} (${min_avg} B)"

echo ""

# --- Instruction Breakdown Comparison ---
echo -e "${CYAN}--- INSTRUCTION BREAKDOWN COMPARISON ---${RESET}"
echo ""

# Collect all unique mnemonics
all_mnemonics=$(cat "$TMPDIR"/mnemonics_* 2>/dev/null | sort -u)

if [[ -n "$all_mnemonics" ]]; then
    printf "  ${BOLD}%-16s" "Instruction"
    for name in "${NAMES[@]}"; do
        printf "%-12s" "$name"
    done
    printf "${RESET}\n"

    printf "  %-16s" "----------------"
    for _ in "${NAMES[@]}"; do
        printf "%-12s" "------------"
    done
    printf "\n"

    while IFS= read -r mnem; do
        [[ -z "$mnem" ]] && continue
        printf "  %-16s" "$mnem"
        for i in "${!BINARIES[@]}"; do
            count=$(grep -cxF "$mnem" "$TMPDIR/mnemonics_$i" 2>/dev/null || echo 0)
            printf "%-12s" "$count"
        done
        echo ""
    done <<< "$all_mnemonics"
fi
echo ""

# --- Instruction Difference Highlights ---
echo -e "${CYAN}--- INSTRUCTION DIFFERENCES ---${RESET}"
echo ""

# Compare each pair of binaries for unique instructions
for i in "${!BINARIES[@]}"; do
    for j in "${!BINARIES[@]}"; do
        if [[ $j -le $i ]]; then continue; fi
        only_i=$(comm -23 <(sort -u "$TMPDIR/mnemonics_$i" 2>/dev/null) <(sort -u "$TMPDIR/mnemonics_$j" 2>/dev/null) | tr '\n' ' ')
        only_j=$(comm -13 <(sort -u "$TMPDIR/mnemonics_$i" 2>/dev/null) <(sort -u "$TMPDIR/mnemonics_$j" 2>/dev/null) | tr '\n' ' ')
        if [[ -n "$only_i" || -n "$only_j" ]]; then
            echo -e "  ${BOLD}${NAMES[$i]} vs ${NAMES[$j]}:${RESET}"
            [[ -n "$only_i" ]] && echo -e "    Only in ${NAMES[$i]}: ${YELLOW}${only_i}${RESET}"
            [[ -n "$only_j" ]] && echo -e "    Only in ${NAMES[$j]}: ${YELLOW}${only_j}${RESET}"
        fi
    done
done
echo ""

# --- Side-by-side disassembly (first two binaries) ---
echo -e "${CYAN}--- SIDE-BY-SIDE DISASSEMBLY (first 2 binaries) ---${RESET}"
echo ""

# Extract just mnemonic + operands lines for readability
for i in 0 1; do
    grep -P '^\s+[0-9a-f]+:' "$TMPDIR/disasm_$i.func" | \
        sed 's/^\s*[0-9a-f]*:\s*\([0-9a-f ]*\)\s*//' | \
        sed 's/^[0-9a-f ]*\t//' > "$TMPDIR/clean_$i" 2>/dev/null || true
done

if [[ -s "$TMPDIR/clean_0" && -s "$TMPDIR/clean_1" ]]; then
    printf "  ${BOLD}%-40s | %-40s${RESET}\n" "${NAMES[0]}" "${NAMES[1]}"
    printf "  %-40s | %-40s\n" "----------------------------------------" "----------------------------------------"

    max_lines=$(wc -l < "$TMPDIR/clean_0")
    max_lines2=$(wc -l < "$TMPDIR/clean_1")
    [[ "$max_lines2" -gt "$max_lines" ]] && max_lines=$max_lines2

    for n in $(seq 1 "$max_lines"); do
        line1=$(sed -n "${n}p" "$TMPDIR/clean_0" 2>/dev/null || true)
        line2=$(sed -n "${n}p" "$TMPDIR/clean_1" 2>/dev/null || true)
        marker=" "
        if [[ "$line1" != "$line2" ]]; then
            marker="*"
        fi
        printf " %s %-39s | %-39s\n" "$marker" "${line1:0:39}" "${line2:0:39}"
    done
else
    echo "  (insufficient disassembly data for side-by-side view)"
fi
echo ""

echo -e "${BOLD}================================================================================${RESET}"
echo -e "${BOLD}  Comparison complete.${RESET}"
echo -e "${BOLD}================================================================================${RESET}"
