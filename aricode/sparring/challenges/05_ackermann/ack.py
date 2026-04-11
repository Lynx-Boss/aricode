#!/usr/bin/env python3
# Python - Challenge 05: Ackermann Function
# Compute A(3,4) = 125
# The Ackermann function grows faster than any primitive recursive function.

import sys
sys.setrecursionlimit(100000)

def ack(m, n):
    if m == 0:
        return n + 1
    if n == 0:
        return ack(m - 1, 1)
    return ack(m - 1, ack(m, n - 1))

sys.exit(ack(3, 4))
