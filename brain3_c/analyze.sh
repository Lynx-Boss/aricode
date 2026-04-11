#!/bin/bash
# analyze.sh - Extract main() machine code from each binary
set -e

cd "$(dirname "$0")"

echo "============================================"
echo "  Machine Code Analysis - main() function"
echo "============================================"
echo ""

for bin in bin/*; do
    name=$(basename "$bin")
    echo "--- $name ($(stat -c%s "$bin") bytes) ---"
    # Extract main function disassembly
    objdump -d "$bin" | awk '/<main>:/{found=1} found{print; if(/^$/ && found>1)exit} found{found++}'
    echo ""
done

echo "============================================"
echo "  Binary Size Comparison"
echo "============================================"
echo ""
printf "%-30s %s\n" "Binary" "Size (bytes)"
printf "%-30s %s\n" "------------------------------" "------------"
for bin in bin/*; do
    printf "%-30s %s\n" "$(basename "$bin")" "$(stat -c%s "$bin")"
done

echo ""
echo "============================================"
echo "  Raw Machine Code Bytes for main()"
echo "============================================"
echo ""
for bin in bin/*; do
    name=$(basename "$bin")
    echo "--- $name ---"
    # Extract just the hex bytes of main()
    objdump -d "$bin" | awk '/<main>:/{found=1; next} found && /^$/{exit} found{
        # extract hex bytes between address and instruction
        sub(/^ *[0-9a-f]+:\t/, "")
        sub(/\t.*$/, "")
        gsub(/ /, "")
        printf "%s ", $0
    }'
    echo ""
done
