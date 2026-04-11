#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Brain 1: Raw ELF Binary Test ==="
echo ""

# Generate the binary
echo "[1] Generating binary..."
python3 generate.py
echo ""

# Show file info
echo "[2] File info:"
ls -la sum.bin
file sum.bin
echo ""

# Hex dump of the entire binary
echo "[3] Complete hex dump:"
od -A x -t x1z sum.bin
echo ""

# Hex dump of just the machine code (last 11 bytes)
echo "[4] Machine code bytes only (offset 120, 11 bytes):"
od -A x -t x1z -j 120 -N 11 sum.bin
echo ""

# Run and check exit code
echo "[5] Executing sum.bin (expecting exit code 42)..."
set +e
./sum.bin
EXIT_CODE=$?
set -e

echo "Exit code: $EXIT_CODE"
echo ""

if [ "$EXIT_CODE" -eq 42 ]; then
    echo "PASS: 37 + 5 = 42"
    echo ""
    echo "=== RESULTS ==="
    echo "Binary size:       $(wc -c < sum.bin) bytes"
    echo "Code size:         11 bytes"
    echo "Instructions:      6"
    echo "ADD instruction:   1 (3 bytes: 83 C7 05)"
    echo "Status:            SUCCESS"
else
    echo "FAIL: Expected 42, got $EXIT_CODE"
    exit 1
fi
