#!/usr/bin/env python3
# Python - Challenge 07: Mersenne Prime Verification
# Verify M31 = 2^31 - 1 = 2,147,483,647 is prime
# Iterative trial division (Python uses bigints natively)

import sys

def is_prime(n):
    if n < 2:
        return False
    if n == 2:
        return True
    if n % 2 == 0:
        return False
    d = 3
    while d * d <= n:
        if n % d == 0:
            return False
        d += 2
    return True

sys.exit(1 if is_prime(2147483647) else 0)
