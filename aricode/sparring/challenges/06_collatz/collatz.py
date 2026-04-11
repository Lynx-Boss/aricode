#!/usr/bin/env python3
# Python - Challenge 06: Collatz Conjecture
# Compute the number of steps for 871 to reach 1 = 178 steps
# The Collatz conjecture (1937) remains UNSOLVED.

import sys
sys.setrecursionlimit(10000)

def collatz_steps(n):
    if n == 1:
        return 0
    if n % 2 == 0:
        return 1 + collatz_steps(n // 2)
    return 1 + collatz_steps(3 * n + 1)

sys.exit(collatz_steps(871))
