#!/usr/bin/env python3
"""
Decimal Precision Sparring - Python (decimal.Decimal library)
Shows that Python CAN do it, but only by importing a heavy library.
This is NOT the default behavior.
"""
from decimal import Decimal, getcontext

getcontext().prec = 50  # plenty of precision

def fmt(val, decimals=20):
    # Format Decimal to fixed 20 decimal places
    fmt_str = f"{{:.{decimals}f}}"
    return fmt_str.format(val)

def check(label, got, expected_str):
    got_str = fmt(got)
    passed = got_str == expected_str
    tag = "PASS" if passed else "FAIL"
    print(f"  {label}")
    print(f"    expected: {expected_str}")
    print(f"    got:      {got_str}  [{tag}]")
    return passed

passes = 0
total = 7

print("=== Python decimal.Decimal (heavy library) ===\n")

if check("0.1 + 0.2 = 0.3",
         Decimal("0.1") + Decimal("0.2"), fmt(Decimal("0.3"))):
    passes += 1

if check("1.0 - 0.9 - 0.1 = 0.0",
         Decimal("1.0") - Decimal("0.9") - Decimal("0.1"), fmt(Decimal("0.0"))):
    passes += 1

if check("0.1 * 0.1 = 0.01",
         Decimal("0.1") * Decimal("0.1"), fmt(Decimal("0.01"))):
    passes += 1

if check("1.0 / 3.0 * 3.0 = 1.0",
         Decimal("1.0") / Decimal("3.0") * Decimal("3.0"), fmt(Decimal("1.0"))):
    passes += 1

if check("0.3 - 0.2 - 0.1 = 0.0",
         Decimal("0.3") - Decimal("0.2") - Decimal("0.1"), fmt(Decimal("0.0"))):
    passes += 1

# Pi - Decimal doesn't have a built-in pi, so we define it
pi = Decimal("3.14159265358979323846")
if check("PI to 20 decimals", pi, "3.14159265358979323846"):
    passes += 1

a = Decimal("1.11111111111111111111")
b = Decimal("2.22222222222222222222")
if check("1.111...1 + 2.222...2 = 3.333...3", a + b, "3.33333333333333333333"):
    passes += 1

print(f"\nResult: {passes}/{total} PASS  (requires 'from decimal import Decimal')")
