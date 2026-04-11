#!/usr/bin/env python3
# Python - Challenge 09: Prime Counting
# Count primes below 100 = 25
import sys

def is_prime(n):
    if n < 2: return False
    if n % 2 == 0: return n == 2
    d = 3
    while d * d <= n:
        if n % d == 0: return False
        d += 2
    return True

sys.exit(sum(1 for n in range(2, 100) if is_prime(n)))
