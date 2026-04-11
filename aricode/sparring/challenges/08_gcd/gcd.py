#!/usr/bin/env python3
# Python - Challenge 08: Euclidean GCD
# Compute GCD(462, 1071) = 21
import sys

def gcd(a, b):
    while b > 0:
        a, b = b, a % b
    return a

sys.exit(gcd(462, 1071))
