#!/usr/bin/env python3
# Python - Challenge 12: Perceptron Neural Network
# Train AND gate, 1000 epochs, fixed-point, return correct (4)
import sys

def train():
    w1 = w2 = bias = 0
    lr = 100
    for _ in range(1000):
        samples = [(0,0,0), (0,1000,0), (1000,0,0), (1000,1000,1000)]
        for x1, x2, target in samples:
            out = w1*x1//1000 + w2*x2//1000 + bias
            pred = 1000 if out > 500 else 0
            err = target - pred
            w1 += lr * err * x1 // 1000 // 1000
            w2 += lr * err * x2 // 1000 // 1000
            bias += lr * err // 1000

    correct = 0
    if bias <= 500: correct += 1
    if w2 + bias <= 500: correct += 1
    if w1 + bias <= 500: correct += 1
    if w1 + w2 + bias > 500: correct += 1
    return correct

sys.exit(train())
