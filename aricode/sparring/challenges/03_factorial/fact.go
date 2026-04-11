// Go - Challenge 03: Factorial
// Compute 5! = 120, return as exit code

package main

import "os"

func factorial(n int) int {
	if n < 2 {
		return 1
	}
	return n * factorial(n-1)
}

func main() {
	os.Exit(factorial(5))
}
