#!/bin/bash
# Build script for brain2_asm - all approaches
# Target: x86_64 Linux, static ELF binary via nasm + ld

set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# Ensure nasm is available
if ! command -v nasm &>/dev/null; then
    echo "[*] Installing nasm..."
    sudo apt-get install -y nasm
fi

echo "=== Building all approaches ==="

# Build each .asm file into a minimal static ELF
for src in sum.asm approach_*.asm; do
    name="${src%.asm}"
    echo -n "  $src -> $name ... "
    nasm -f elf64 -o "${name}.o" "$src"
    ld -s -n -o "$name" "${name}.o"
    size=$(stat -c%s "$name" 2>/dev/null || stat -f%z "$name")
    echo "OK (binary: ${size} bytes)"
    rm -f "${name}.o"
done

echo ""
echo "=== Build complete ==="
echo ""

# Show disassembly of the winner
echo "=== Winner (sum) disassembly ==="
objdump -d -M intel sum
echo ""
echo "=== Binary size comparison ==="
ls -la sum approach_* | grep -v '\.asm' | awk '{printf "  %-25s %s bytes\n", $NF, $5}'

echo ""
echo "=== All disassemblies ==="
for bin in sum approach_*; do
    [[ "$bin" == *.asm ]] && continue
    [[ -x "$bin" || -f "$bin" ]] || continue
    echo ""
    echo "--- $bin ---"
    objdump -d -M intel "$bin"
done
