#!/usr/bin/env python3
import sys
data = [i*i for i in range(100)]
sys.exit(sum(data) % 256)
