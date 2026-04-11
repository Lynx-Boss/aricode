#!/usr/bin/env python3
# Python - Challenge 14: Leibniz Pi, return floor(pi*50)=157
import sys
s, sign, d = 0.0, 1.0, 1.0
for _ in range(10000):
    s += sign / d
    sign = -sign
    d += 2.0
sys.exit(int(s * 4.0 * 50.0))
