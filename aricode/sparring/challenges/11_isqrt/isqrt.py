#!/usr/bin/env python3
# Python - Challenge 11: Integer Square Root (Newton's Method)
# Compute isqrt(16129) = 127
import sys

def isqrt(n):
    if n < 2: return n
    x = n
    y = (x + 1) // 2
    while y < x:
        x = y
        y = (x + n // x) // 2
    return x

sys.exit(isqrt(16129))
