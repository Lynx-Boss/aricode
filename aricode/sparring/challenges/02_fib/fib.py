#!/usr/bin/env python3
# Python - Challenge 02: Fibonacci
# Compute fib(10) = 55, return as exit code

import sys

def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

sys.exit(fib(10))
