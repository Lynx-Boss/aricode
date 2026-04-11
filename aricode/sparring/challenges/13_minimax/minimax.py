#!/usr/bin/env python3
# Python - Challenge 13: Minimax Game AI
# Nim(15): full tree search, return optimal move (3)
import sys
sys.setrecursionlimit(1000000)

def minimax(stones, maximizing):
    if stones == 0:
        return -1 if maximizing else 1
    if maximizing:
        best = -100
        for take in range(1, min(4, stones+1)):
            best = max(best, minimax(stones-take, False))
        return best
    else:
        best = 100
        for take in range(1, min(4, stones+1)):
            best = min(best, minimax(stones-take, True))
        return best

best_move, best_score = 1, -100
for take in range(1, 4):
    score = minimax(15 - take, False)
    if score > best_score:
        best_score = score
        best_move = take

sys.exit(best_move)
