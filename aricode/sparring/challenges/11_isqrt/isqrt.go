// Go - Challenge 11: Integer Square Root (Newton's Method)
// Compute isqrt(16129) = 127
package main

import "os"

func isqrt(n int) int {
	if n < 2 { return n }
	x := n
	y := (x + 1) / 2
	for y < x {
		x = y
		y = (x + n/x) / 2
	}
	return x
}

func main() {
	os.Exit(isqrt(16129))
}
