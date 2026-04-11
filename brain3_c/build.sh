#!/bin/bash
# build.sh - Build all variants at -O0, -O1, -O2, -O3, -Os
set -e

cd "$(dirname "$0")"

SOURCES="sum_basic sum_inline sum_constexpr"
OPT_LEVELS="O0 O1 O2 O3 Os"

mkdir -p bin

for src in $SOURCES; do
    for opt in $OPT_LEVELS; do
        out="bin/${src}_${opt}"
        echo "Building: gcc -${opt} -o ${out} ${src}.c"
        gcc -${opt} -o "${out}" "${src}.c"
    done
done

echo ""
echo "All builds complete. Binary sizes:"
ls -la bin/ | grep -v "^total\|^d"
