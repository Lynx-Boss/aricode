#!/bin/bash
# test.sh - Test all variants, expect exit code 42
set -u

cd "$(dirname "$0")"

PASS=0
FAIL=0

for bin in bin/*; do
    "$bin"
    code=$?
    name=$(basename "$bin")
    if [ "$code" -eq 42 ]; then
        echo "PASS: $name (exit=$code)"
        ((PASS++))
    else
        echo "FAIL: $name (exit=$code, expected 42)"
        ((FAIL++))
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
