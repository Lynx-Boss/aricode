#!/bin/bash
# Test script: verifies 37 + 5 = 42 via exit code
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# Build if needed
if [ ! -f sum ]; then
    bash build.sh
fi

echo "=== Testing all approaches ==="
PASS=0
FAIL=0

for bin in sum approach_*; do
    [[ "$bin" == *.asm ]] && continue
    [[ -f "$bin" ]] || continue

    set +e
    ./"$bin"
    code=$?
    set -e

    if [ "$code" -eq 42 ]; then
        echo "  PASS: $bin -> exit code $code (37 + 5 = 42)"
        PASS=$((PASS+1))
    else
        echo "  FAIL: $bin -> exit code $code (expected 42)"
        FAIL=$((FAIL+1))
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed"
echo ""

# Specific test for the winner
set +e
./sum
code=$?
set -e

if [ "$code" -eq 42 ]; then
    echo "WINNER VERIFIED: sum exits with code 42 (37 + 5 = 42)"
    exit 0
else
    echo "ERROR: sum exits with code $code, expected 42"
    exit 1
fi
