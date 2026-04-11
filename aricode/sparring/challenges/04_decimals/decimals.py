#!/usr/bin/env python3
"""
Decimal Precision Sparring - Python (built-in float / IEEE 754)
Shows how Python's native float fails at precise decimal arithmetic.
"""

def fmt(val, decimals=20):
    return f"{val:.{decimals}f}"

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

print("=== Python float (IEEE 754 double) ===\n")

# Expected values are the EXACT mathematical results
if check("0.1 + 0.2 = 0.3", 0.1 + 0.2, "0.30000000000000000000"):
    passes += 1

if check("1.0 - 0.9 - 0.1 = 0.0", 1.0 - 0.9 - 0.1, "0.00000000000000000000"):
    passes += 1

if check("0.1 * 0.1 = 0.01", 0.1 * 0.1, "0.01000000000000000000"):
    passes += 1

if check("1.0 / 3.0 * 3.0 = 1.0", 1.0 / 3.0 * 3.0, "1.00000000000000000000"):
    passes += 1

if check("0.3 - 0.2 - 0.1 = 0.0", 0.3 - 0.2 - 0.1, "0.00000000000000000000"):
    passes += 1

import math
if check("PI to 20 decimals", math.pi, "3.14159265358979323846"):
    passes += 1

a = 1.11111111111111111111
b = 2.22222222222222222222
if check("1.111...1 + 2.222...2 = 3.333...3", a + b, "3.33333333333333333333"):
    passes += 1

print(f"\nResult: {passes}/{total} PASS")
