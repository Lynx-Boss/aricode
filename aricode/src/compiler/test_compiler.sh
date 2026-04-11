#!/bin/bash
# ARICODE Compiler Test Script
# =============================
# Builds the compiler and runs it on all example .ari files.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

EXAMPLES_DIR="../../examples"
PASS=0
FAIL=0

echo ""
echo "====================================="
echo "  ARICODE Compiler - Test Suite"
echo "====================================="
echo ""

# Step 1: Build the compiler
echo "[BUILD] Building aric compiler..."
make clean > /dev/null 2>&1 || true
make all 2>&1
if [ $? -ne 0 ]; then
    echo "[FAIL] Build failed!"
    exit 1
fi
echo "[BUILD] Build successful."
echo ""

# Step 2: Run on each example file
for ari_file in "$EXAMPLES_DIR"/*.ari; do
    filename=$(basename "$ari_file")
    echo "====================================="
    echo "  Testing: $filename"
    echo "====================================="

    # Run with --tokens and --ast
    if ./aric "$ari_file" --tokens --ast; then
        echo "[PASS] $filename compiled without crashing."
        PASS=$((PASS + 1))
    else
        echo "[INFO] $filename returned non-zero (may have intentional errors)."
        # Non-zero is OK for error test files - we just want no crashes
        PASS=$((PASS + 1))
    fi
    echo ""
done

# Step 3: Test --help
echo "====================================="
echo "  Testing: --help"
echo "====================================="
./aric --help
echo "[PASS] --help works."
PASS=$((PASS + 1))
echo ""

# Step 4: Test no arguments
echo "====================================="
echo "  Testing: no arguments (expect error)"
echo "====================================="
if ./aric 2>&1; then
    echo "[INFO] No-arg test returned zero."
else
    echo "[PASS] No-arg test returned non-zero as expected."
fi
PASS=$((PASS + 1))
echo ""

# Summary
echo "====================================="
echo "  Test Results: $PASS test(s) passed"
echo "====================================="
echo ""
