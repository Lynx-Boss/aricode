// Go - Challenge 06: Collatz Conjecture
// Compute the number of steps for 871 to reach 1 = 178 steps
// The Collatz conjecture (1937) remains UNSOLVED.

package main

import "os"

func collatzSteps(n int) int {
	if n == 1 {
		return 0
	}
	if n%2 == 0 {
		return 1 + collatzSteps(n/2)
	}
	return 1 + collatzSteps(3*n+1)
}

func main() {
	os.Exit(collatzSteps(871))
}
