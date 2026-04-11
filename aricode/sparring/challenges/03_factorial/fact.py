#!/usr/bin/env python3
# Python - Challenge 03: Factorial
# Compute 5! = 120, return as exit code

import sys

def factorial(n):
    if n < 2:
        return 1
    return n * factorial(n - 1)

sys.exit(factorial(5))
