// Go - Challenge 13: Minimax Game AI
// Nim(15): full tree search, return optimal move (3)
package main

import "os"

func minimax(stones int, maximizing bool) int {
	if stones == 0 {
		if maximizing { return -1 }
		return 1
	}
	if maximizing {
		best := -100
		for take := 1; take <= 3 && take <= stones; take++ {
			if s := minimax(stones-take, false); s > best { best = s }
		}
		return best
	}
	best := 100
	for take := 1; take <= 3 && take <= stones; take++ {
		if s := minimax(stones-take, true); s < best { best = s }
	}
	return best
}

func main() {
	bestMove, bestScore := 1, -100
	for take := 1; take <= 3; take++ {
		if s := minimax(15-take, false); s > bestScore {
			bestScore = s
			bestMove = take
		}
	}
	os.Exit(bestMove)
}
