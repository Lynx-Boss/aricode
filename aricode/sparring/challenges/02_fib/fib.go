// Go - Challenge 02: Fibonacci
// Compute fib(10) = 55, return as exit code

package main

import "os"

func fib(n int) int {
	if n < 2 {
		return n
	}
	return fib(n-1) + fib(n-2)
}

func main() {
	os.Exit(fib(10))
}
