#!/usr/bin/env bash
# analyze_binary.sh - Deep analysis of compiled binaries for the aricode sparring system
# Usage: ./analyze_binary.sh <binary> [function_name]
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

usage() {
    echo "Usage: $0 <binary> [function_name]"
    echo ""
    echo "Performs deep analysis of a compiled binary."
    echo ""
    echo "Arguments:"
    echo "  binary          Path to the ELF binary to analyze"
    echo "  function_name   Function to disassemble (default: main)"
    echo ""
    echo "Output:"
    echo "  - Binary and section sizes"
    echo "  - Instruction count and breakdown by type"
    echo "  - REX prefix usage"
    echo "  - Average instruction length"
    echo "  - NOP/padding byte count"
    echo "  - Full disassembly of the target function"
    exit 1
}

if [[ $# -lt 1 ]]; then
    usage
fi

BINARY="$1"
FUNC="${2:-main}"

if [[ ! -f "$BINARY" ]]; then
    echo "Error: binary '$BINARY' not found" >&2
    exit 1
fi

if ! file "$BINARY" | grep -q 'ELF'; then
    echo "Error: '$BINARY' is not an ELF binary" >&2
    exit 1
fi

# Ensure required tools exist
for tool in objdump size readelf file; do
    if ! command -v "$tool" &>/dev/null; then
        echo "Error: required tool '$tool' not found" >&2
        exit 1
    fi
done

echo -e "${BOLD}================================================================================${RESET}"
echo -e "${BOLD}  BINARY ANALYSIS: $(basename "$BINARY")${RESET}"
echo -e "${BOLD}================================================================================${RESET}"
echo ""

# --- Section 1: Size Information ---
echo -e "${CYAN}--- SIZE INFORMATION ---${RESET}"
TOTAL_SIZE=$(stat -c%s "$BINARY" 2>/dev/null || stat -f%z "$BINARY" 2>/dev/null)
echo -e "  Total binary size:     ${BOLD}${TOTAL_SIZE} bytes${RESET}"

TEXT_SIZE=$(size "$BINARY" 2>/dev/null | tail -1 | awk '{print $1}')
DATA_SIZE=$(size "$BINARY" 2>/dev/null | tail -1 | awk '{print $2}')
BSS_SIZE=$(size "$BINARY" 2>/dev/null | tail -1 | awk '{print $3}')
TOTAL_SEGMENTS=$(size "$BINARY" 2>/dev/null | tail -1 | awk '{print $4}')

echo -e "  .text (code):          ${BOLD}${TEXT_SIZE:-?} bytes${RESET}"
echo -e "  .data (initialized):   ${BOLD}${DATA_SIZE:-?} bytes${RESET}"
echo -e "  .bss (uninitialized):  ${BOLD}${BSS_SIZE:-?} bytes${RESET}"
echo -e "  Segments total:        ${BOLD}${TOTAL_SEGMENTS:-?} bytes${RESET}"

if [[ -n "$TEXT_SIZE" && -n "$TOTAL_SIZE" && "$TOTAL_SIZE" -gt 0 ]]; then
    CODE_RATIO=$(awk "BEGIN {printf \"%.1f\", ($TEXT_SIZE / $TOTAL_SIZE) * 100}")
    echo -e "  Code/binary ratio:     ${BOLD}${CODE_RATIO}%${RESET}"
fi
echo ""

# --- Section 2: Full disassembly into temp file ---
DISASM_FULL=$(mktemp)
DISASM_FUNC=$(mktemp)
trap "rm -f '$DISASM_FULL' '$DISASM_FUNC'" EXIT

objdump -d "$BINARY" > "$DISASM_FULL" 2>/dev/null

# Extract specific function disassembly
# Match "<func>:" or "<func>():" patterns
awk -v func="<${FUNC}>" -v func2="<${FUNC}()>" '
    $0 ~ func || $0 ~ func2 { found=1; print; next }
    found && /^$/ { exit }
    found { print }
' "$DISASM_FULL" > "$DISASM_FUNC"

if [[ ! -s "$DISASM_FUNC" ]]; then
    echo -e "${YELLOW}Warning: function '${FUNC}' not found in binary. Trying partial match...${RESET}"
    awk -v func="$FUNC" '
        $0 ~ "<" func { found=1; print; next }
        found && /^$/ { exit }
        found { print }
    ' "$DISASM_FULL" > "$DISASM_FUNC"
fi

# --- Section 3: Instruction Count and Analysis ---
echo -e "${CYAN}--- INSTRUCTION ANALYSIS ---${RESET}"

# Count total instructions in .text section (lines with hex bytes and mnemonics)
TOTAL_INSN=$(grep -cE '^\s+[0-9a-f]+:\s' "$DISASM_FULL" 2>/dev/null || echo 0)
echo -e "  Total instructions (all functions):  ${BOLD}${TOTAL_INSN}${RESET}"

FUNC_INSN=0
if [[ -s "$DISASM_FUNC" ]]; then
    FUNC_INSN=$(grep -cE '^\s+[0-9a-f]+:\s' "$DISASM_FUNC" 2>/dev/null || echo 0)
    echo -e "  Instructions in ${FUNC}():            ${BOLD}${FUNC_INSN}${RESET}"
fi
echo ""

# --- Section 4: Instruction Breakdown ---
echo -e "${CYAN}--- INSTRUCTION BREAKDOWN (${FUNC}) ---${RESET}"

# Extract just the mnemonics from the function disassembly
MNEMONICS=$(grep -oP '^\s+[0-9a-f]+:\s+([0-9a-f]{2}\s)+\s+\K\S+' "$DISASM_FUNC" 2>/dev/null || true)

if [[ -n "$MNEMONICS" ]]; then
    echo "$MNEMONICS" | sort | uniq -c | sort -rn | head -30 | while read count mnemonic; do
        if [[ "$FUNC_INSN" -gt 0 ]]; then
            pct=$(awk "BEGIN {printf \"%.1f\", ($count / $FUNC_INSN) * 100}")
            printf "  %-12s %4d  (%s%%)\n" "$mnemonic" "$count" "$pct"
        else
            printf "  %-12s %4d\n" "$mnemonic" "$count"
        fi
    done
else
    echo "  (no instructions found for function '${FUNC}')"
fi
echo ""

# --- Section 5: Instruction Category Summary ---
echo -e "${CYAN}--- INSTRUCTION CATEGORIES (${FUNC}) ---${RESET}"

if [[ -n "$MNEMONICS" ]]; then
    DATA_MOVE=$(echo "$MNEMONICS" | grep -ciE '^(mov|lea|push|pop|xchg|cmov)' || true)
    ARITHMETIC=$(echo "$MNEMONICS" | grep -ciE '^(add|sub|mul|imul|div|idiv|inc|dec|neg|adc|sbb)' || true)
    LOGICAL=$(echo "$MNEMONICS" | grep -ciE '^(and|or|xor|not|shl|shr|sar|sal|rol|ror|test)' || true)
    BRANCH=$(echo "$MNEMONICS" | grep -ciE '^(j[a-z]+|jmp|call|ret|loop|syscall|int)' || true)
    COMPARE=$(echo "$MNEMONICS" | grep -ciE '^(cmp|test)' || true)
    NOP_COUNT=$(echo "$MNEMONICS" | grep -ciE '^nop' || true)
    STRING_OPS=$(echo "$MNEMONICS" | grep -ciE '^(rep|movs|stos|lods|cmps|scas)' || true)
    SSE_AVX=$(echo "$MNEMONICS" | grep -ciE '^(v?)(add|sub|mul|div|mov|cmp|and|or|xor|sqrt|rsqrt|rcp|min|max|unpck|shuf|pack|punpck)(s|p)(s|d|b|w|dq|q)' || true)
    # Ensure all values are numeric (grep -c returns empty string on some systems when no match)
    DATA_MOVE=${DATA_MOVE:-0}; ARITHMETIC=${ARITHMETIC:-0}; LOGICAL=${LOGICAL:-0}
    BRANCH=${BRANCH:-0}; COMPARE=${COMPARE:-0}; NOP_COUNT=${NOP_COUNT:-0}
    STRING_OPS=${STRING_OPS:-0}; SSE_AVX=${SSE_AVX:-0}

    printf "  %-20s %4d\n" "Data movement:" "$DATA_MOVE"
    printf "  %-20s %4d\n" "Arithmetic:" "$ARITHMETIC"
    printf "  %-20s %4d\n" "Logic/Shift:" "$LOGICAL"
    printf "  %-20s %4d\n" "Branch/Control:" "$BRANCH"
    printf "  %-20s %4d\n" "Compare:" "$COMPARE"
    printf "  %-20s %4d\n" "NOP/Padding:" "$NOP_COUNT"
    printf "  %-20s %4d\n" "String ops:" "$STRING_OPS"
    printf "  %-20s %4d\n" "SSE/AVX:" "$SSE_AVX"
fi
echo ""

# --- Section 6: REX Prefix Analysis ---
echo -e "${CYAN}--- REX PREFIX ANALYSIS ---${RESET}"

# REX prefixes are 0x40-0x4f in x86_64; they show up in the hex bytes of the disassembly
# We look for instruction bytes that start with 48, 49, 4c, 4d (common REX.W patterns)
REX_LINES=0
if [[ -s "$DISASM_FUNC" ]]; then
    REX_LINES=$(grep -cP '^\s+[0-9a-f]+:\s+(48|49|4c|4d)\s' "$DISASM_FUNC" 2>/dev/null || echo 0)
fi
echo -e "  REX.W prefix instructions:  ${BOLD}${REX_LINES}${RESET}"
if [[ "$FUNC_INSN" -gt 0 ]]; then
    REX_PCT=$(awk "BEGIN {printf \"%.1f\", ($REX_LINES / $FUNC_INSN) * 100}")
    echo -e "  REX overhead ratio:         ${BOLD}${REX_PCT}%${RESET}"
fi
echo ""

# --- Section 7: Average Instruction Length ---
echo -e "${CYAN}--- INSTRUCTION LENGTH ---${RESET}"

if [[ -s "$DISASM_FUNC" ]]; then
    # Calculate total instruction bytes by counting hex byte pairs in each instruction line
    TOTAL_BYTES=0
    INSN_COUNT=0
    while IFS= read -r line; do
        # Extract the hex bytes between the address and the mnemonic
        hex_bytes=$(echo "$line" | grep -oP '^\s+[0-9a-f]+:\s+\K([0-9a-f]{2}\s)+' 2>/dev/null || true)
        if [[ -n "$hex_bytes" ]]; then
            nbytes=$(echo "$hex_bytes" | tr -s ' ' '\n' | grep -c '[0-9a-f]' 2>/dev/null || echo 0)
            TOTAL_BYTES=$((TOTAL_BYTES + nbytes))
            INSN_COUNT=$((INSN_COUNT + 1))
        fi
    done < "$DISASM_FUNC"

    if [[ "$INSN_COUNT" -gt 0 ]]; then
        AVG_LEN=$(awk "BEGIN {printf \"%.2f\", $TOTAL_BYTES / $INSN_COUNT}")
        echo -e "  Total instruction bytes:    ${BOLD}${TOTAL_BYTES}${RESET}"
        echo -e "  Instruction count:          ${BOLD}${INSN_COUNT}${RESET}"
        echo -e "  Average instruction length: ${BOLD}${AVG_LEN} bytes${RESET}"
    else
        echo "  (no instruction data available)"
    fi
else
    echo "  (function '${FUNC}' not found)"
fi
echo ""

# --- Section 8: NOP / Padding Analysis ---
echo -e "${CYAN}--- NOP / PADDING ANALYSIS ---${RESET}"

NOP_BYTES=$(grep -cP '^\s+[0-9a-f]+:.*\bnop\b' "$DISASM_FULL" || true)
NOP_BYTES=${NOP_BYTES:-0}
# Multi-byte NOPs (data16, cs, etc.)
MULTI_NOP=$(grep -cP '^\s+[0-9a-f]+:.*\b(data16|nopw|nopl)\b' "$DISASM_FULL" || true)
MULTI_NOP=${MULTI_NOP:-0}

echo -e "  NOP instructions (all):     ${BOLD}${NOP_BYTES}${RESET}"
echo -e "  Multi-byte NOPs:            ${BOLD}${MULTI_NOP}${RESET}"
echo ""

# --- Section 9: Disassembly of Target Function ---
echo -e "${CYAN}--- DISASSEMBLY: ${FUNC}() ---${RESET}"
if [[ -s "$DISASM_FUNC" ]]; then
    cat "$DISASM_FUNC"
else
    echo "  (function '${FUNC}' not found in binary)"
    echo ""
    echo "  Available functions:"
    grep -oP '<\K[^>]+' "$DISASM_FULL" | sort -u | head -20
fi
echo ""

echo -e "${BOLD}================================================================================${RESET}"
echo -e "${BOLD}  Analysis complete.${RESET}"
echo -e "${BOLD}================================================================================${RESET}"
