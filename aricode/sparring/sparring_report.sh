#!/bin/bash
# ============================================================================
#  ARICODE SPARRING - Podium Report Generator
# ============================================================================
#  Reads benchmark results and generates a visual podium report showing
#  rankings across all metrics and challenges.
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"

# ── ANSI Colors and Box Drawing ─────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
WHITE='\033[0;37m'
BOLD='\033[1m'
DIM='\033[2m'
RESET='\033[0m'

# Medal symbols
GOLD="\\xF0\\x9F\\xA5\\x87"     # Gold medal
SILVER="\\xF0\\x9F\\xA5\\x88"   # Silver medal
BRONZE="\\xF0\\x9F\\xA5\\x89"   # Bronze medal
CROSS="\\xE2\\x9D\\x8C"         # Red cross

# ── Challenge definitions ───────────────────────────────────────────────────

declare -A CHALLENGE_NAMES
CHALLENGE_NAMES["01_add"]="Simple Addition (37+5=42)"
CHALLENGE_NAMES["02_fib"]="Fibonacci (fib(10)=55)"
CHALLENGE_NAMES["03_factorial"]="Factorial (5!=120)"
CHALLENGE_ORDER=("01_add" "02_fib" "03_factorial")

# ── Formatting helpers ──────────────────────────────────────────────────────

format_bytes() {
    local bytes="$1"
    if [ "$bytes" = "N/A" ] || [ -z "$bytes" ]; then
        echo "N/A"
        return
    fi
    if [ "$bytes" -lt 1024 ]; then
        echo "${bytes} B"
    elif [ "$bytes" -lt 1048576 ]; then
        echo "$(echo "scale=1; $bytes/1024" | bc) KB"
    else
        echo "$(echo "scale=1; $bytes/1048576" | bc) MB"
    fi
}

format_time_ns() {
    local ns="$1"
    if [ "$ns" = "N/A" ] || [ -z "$ns" ]; then
        echo "N/A"
        return
    fi
    if [ "$ns" -lt 1000 ]; then
        echo "${ns} ns"
    elif [ "$ns" -lt 1000000 ]; then
        echo "$(echo "scale=2; $ns/1000" | bc) us"
    elif [ "$ns" -lt 1000000000 ]; then
        echo "$(echo "scale=2; $ns/1000000" | bc) ms"
    else
        echo "$(echo "scale=3; $ns/1000000000" | bc) s"
    fi
}

# ── Ranking helper ──────────────────────────────────────────────────────────

# Reads CSV, sorts by a numeric column, outputs ranked entries
# Args: csv_file, column_number (0-based), metric_name, format_func
show_podium() {
    local csv_file="$1"
    local sort_col="$2"  # 1-based for sort command
    local metric_name="$3"
    local format_type="$4"  # "bytes" or "time" or "count"

    echo -e "  ${BOLD}${metric_name}:${RESET}"

    # Filter out N/A entries, sort numerically
    local rank=0
    local entries=()
    local na_entries=()

    while IFS=',' read -r label bname bin_size text_size instr exec_time startup; do
        local val
        case "$sort_col" in
            3) val="$bin_size" ;;
            4) val="$text_size" ;;
            5) val="$instr" ;;
            6) val="$exec_time" ;;
            7) val="$startup" ;;
        esac

        if [ "$val" = "N/A" ] || [ -z "$val" ] || ! [[ "$val" =~ ^[0-9]+$ ]]; then
            na_entries+=("$label|$val")
        else
            entries+=("$val|$label")
        fi
    done < <(tail -n +2 "$csv_file")

    # Sort entries numerically
    IFS=$'\n' sorted=($(for e in "${entries[@]}"; do echo "$e"; done | sort -t'|' -k1 -n))
    unset IFS

    # Display ranked entries
    for entry in "${sorted[@]}"; do
        rank=$((rank + 1))
        local val="${entry%%|*}"
        local label="${entry#*|}"

        local formatted
        case "$format_type" in
            bytes) formatted=$(format_bytes "$val") ;;
            time)  formatted=$(format_time_ns "$val") ;;
            count) formatted="$val" ;;
        esac

        local medal=""
        local color=""
        case "$rank" in
            1) medal=$(echo -e "$GOLD")  ; color="${YELLOW}${BOLD}" ;;
            2) medal=$(echo -e "$SILVER"); color="${WHITE}${BOLD}" ;;
            3) medal=$(echo -e "$BRONZE"); color="${MAGENTA}" ;;
            *) medal="   "; color="${DIM}" ;;
        esac

        # Highlight aricode entries
        local label_fmt="$label"
        if [[ "$label" == "aricode" ]]; then
            label_fmt="${CYAN}${BOLD}${label}${RESET}"
        fi

        local ord
        case "$rank" in
            1) ord="1st" ;;
            2) ord="2nd" ;;
            3) ord="3rd" ;;
            *) ord="${rank}th" ;;
        esac

        if [ "$rank" -le 3 ]; then
            printf "    %s %-4s  ${color}%-22s${RESET} - %s\n" "$medal" "$ord" "$label" "$formatted"
        else
            printf "         %-4s  ${color}%-22s${RESET} - %s\n" "$ord" "$label" "$formatted"
        fi
    done

    # Show N/A entries
    for entry in "${na_entries[@]}"; do
        local label="${entry%%|*}"
        printf "    $(echo -e "$CROSS")       ${DIM}%-22s${RESET} - ${DIM}N/A (interpreted)${RESET}\n" "$label"
    done

    echo ""
}

# ── Scoring system ──────────────────────────────────────────────────────────

declare -A TOTAL_SCORES

add_scores() {
    local csv_file="$1"
    local sort_col="$2"

    local entries=()
    while IFS=',' read -r label bname bin_size text_size instr exec_time startup; do
        local val
        case "$sort_col" in
            3) val="$bin_size" ;;
            4) val="$text_size" ;;
            5) val="$instr" ;;
            6) val="$exec_time" ;;
            7) val="$startup" ;;
        esac

        if [ "$val" != "N/A" ] && [ -n "$val" ] && [[ "$val" =~ ^[0-9]+$ ]]; then
            entries+=("$val|$label")
        fi
    done < <(tail -n +2 "$csv_file")

    IFS=$'\n' sorted=($(for e in "${entries[@]}"; do echo "$e"; done | sort -t'|' -k1 -n))
    unset IFS

    local rank=0
    for entry in "${sorted[@]}"; do
        rank=$((rank + 1))
        local label="${entry#*|}"
        local points=$((${#sorted[@]} - rank + 1))
        TOTAL_SCORES["$label"]=$(( ${TOTAL_SCORES["$label"]:-0} + points ))
    done
}

# ══════════════════════════════════════════════════════════════════════════════
#  MAIN REPORT
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo -e "${BOLD}${CYAN}"
echo "  =================================================================="
echo "     _    ____  ___ ____ ___  ____  _____    "
echo "    / \\  |  _ \\|_ _/ ___/ _ \\|  _ \\| ____|   "
echo "   / _ \\ | |_) || | |  | | | | | | |  _|     "
echo "  / ___ \\|  _ < | | |__| |_| | |_| | |___    "
echo " /_/   \\_\\_| \\_\\___\\____\\___/|____/|_____|   "
echo "                                              "
echo "  ========= SPARRING PODIUM REPORT ==========="
echo -e "${RESET}"
echo -e "  ${DIM}Generated: $(date '+%Y-%m-%d %H:%M:%S')${RESET}"
echo -e "  ${DIM}Host: $(uname -n) ($(uname -m))${RESET}"
echo ""

# Check results exist
if [ ! -d "$RESULTS_DIR" ] || [ -z "$(ls "$RESULTS_DIR"/*.csv 2>/dev/null)" ]; then
    echo -e "${RED}ERROR: No benchmark results found.${RESET}"
    echo "Run ./benchmark.sh first."
    exit 1
fi

# ── Per-challenge podiums ───────────────────────────────────────────────────

for challenge_key in "${CHALLENGE_ORDER[@]}"; do
    desc="${CHALLENGE_NAMES[$challenge_key]}"
    result_file="$RESULTS_DIR/${challenge_key}.csv"

    if [ ! -f "$result_file" ]; then
        echo -e "  ${YELLOW}Skipping $challenge_key - no results${RESET}"
        continue
    fi

    echo -e "${BOLD}${CYAN}"
    printf "  %s SPARRING: Challenge %s - %s %s\n" \
        "$(echo -e '\\xe2\\x95\\x90\\xe2\\x95\\x90\\xe2\\x95\\x90')" \
        "${challenge_key%%_*}" "$desc" \
        "$(echo -e '\\xe2\\x95\\x90\\xe2\\x95\\x90\\xe2\\x95\\x90\\xe2\\x95\\x90\\xe2\\x95\\x90\\xe2\\x95\\x90\\xe2\\x95\\x90\\xe2\\x95\\x90\\xe2\\x95\\x90')"
    echo -e "${RESET}"

    show_podium "$result_file" 3 "Binary Size" "bytes"
    show_podium "$result_file" 5 "Instruction Count" "count"
    show_podium "$result_file" 6 "Execution Speed (avg per run)" "time"
    show_podium "$result_file" 7 "Startup Time" "time"

    # Accumulate scores
    add_scores "$result_file" 3
    add_scores "$result_file" 6
    add_scores "$result_file" 7
done

# ── Overall Champion ────────────────────────────────────────────────────────

echo -e "${BOLD}${CYAN}"
echo "  =================================================================="
echo "         OVERALL SPARRING CHAMPION"
echo "  =================================================================="
echo -e "${RESET}"

# Sort scores
sorted_scores=()
for label in "${!TOTAL_SCORES[@]}"; do
    sorted_scores+=("${TOTAL_SCORES[$label]}|$label")
done

IFS=$'\n' sorted_scores=($(for e in "${sorted_scores[@]}"; do echo "$e"; done | sort -t'|' -k1 -rn))
unset IFS

rank=0
for entry in "${sorted_scores[@]}"; do
    rank=$((rank + 1))
    local_score="${entry%%|*}"
    local_label="${entry#*|}"

    medal=""
    color=""
    case "$rank" in
        1) medal=$(echo -e "$GOLD")  ; color="${YELLOW}${BOLD}" ;;
        2) medal=$(echo -e "$SILVER"); color="${WHITE}${BOLD}" ;;
        3) medal=$(echo -e "$BRONZE"); color="${MAGENTA}" ;;
        *) medal="   "; color="${DIM}" ;;
    esac

    if [ "$rank" -le 3 ]; then
        ord=""
        case "$rank" in
            1) ord="1st" ;;
            2) ord="2nd" ;;
            3) ord="3rd" ;;
        esac
        printf "    %s %-4s  ${color}%-22s${RESET} - %s points\n" "$medal" "$ord" "$local_label" "$local_score"
    else
        printf "         %-4s  ${color}%-22s${RESET} - %s points\n" "${rank}th" "$local_label" "$local_score"
    fi
done

echo ""

# ── aricode Position Summary ───────────────────────────────────────────────

echo -e "${BOLD}${CYAN}"
echo "  =================================================================="
echo "         ARICODE POSITION ANALYSIS"
echo "  =================================================================="
echo -e "${RESET}"

aricode_score="${TOTAL_SCORES[aricode]:-0}"
if [ "$aricode_score" -gt 0 ]; then
    # Find aricode's rank
    ari_rank=0
    for entry in "${sorted_scores[@]}"; do
        ari_rank=$((ari_rank + 1))
        local_label="${entry#*|}"
        if [ "$local_label" = "aricode" ]; then
            break
        fi
    done

    total_entries=${#sorted_scores[@]}
    echo -e "  aricode overall rank: ${BOLD}#${ari_rank}${RESET} out of ${total_entries} implementations"
    echo -e "  Total points:         ${BOLD}${aricode_score}${RESET}"
    echo ""

    # Show per-challenge aricode stats
    for challenge_key in "${CHALLENGE_ORDER[@]}"; do
        desc="${CHALLENGE_NAMES[$challenge_key]}"
        result_file="$RESULTS_DIR/${challenge_key}.csv"
        [ -f "$result_file" ] || continue

        # Get aricode's binary size
        ari_line=$(grep "^aricode," "$result_file" 2>/dev/null || echo "")
        if [ -n "$ari_line" ]; then
            IFS=',' read -r _l _b bin_size text_size instr exec_time startup <<< "$ari_line"
            echo -e "  ${BOLD}${challenge_key}${RESET}: binary=$(format_bytes "$bin_size"), exec=$(format_time_ns "$exec_time"), startup=$(format_time_ns "$startup")"
        fi
    done
else
    echo -e "  ${YELLOW}aricode results not found in benchmarks.${RESET}"
fi

echo ""
echo -e "${BOLD}${CYAN}"
echo "  =================================================================="
echo "         KEY ADVANTAGES"
echo "  =================================================================="
echo -e "${RESET}"
echo ""
echo -e "  ${BOLD}aricode strengths:${RESET}"
echo -e "    - Direct x86_64 machine code generation (no linker, no libc)"
echo -e "    - Minimal ELF binary (no sections, no symbol table)"
echo -e "    - Zero runtime overhead (raw syscalls)"
echo -e "    - Sub-200 byte binaries for simple programs"
echo ""
echo -e "  ${BOLD}Comparison notes:${RESET}"
echo -e "    - NASM asm: hand-optimized assembly, the theoretical minimum"
echo -e "    - C (gcc/clang): mature optimizers, but carries CRT overhead"
echo -e "    - Rust: strong safety, but large runtime"
echo -e "    - Go: garbage collector and runtime inflate binary size"
echo -e "    - Python: interpreted, not directly comparable for exec speed"
echo ""

echo -e "${DIM}  Report generated by aricode sparring framework${RESET}"
echo -e "${DIM}  Results in: ${RESULTS_DIR}/${RESET}"
echo ""
