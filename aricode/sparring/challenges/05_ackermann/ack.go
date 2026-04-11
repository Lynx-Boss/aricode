// Go - Challenge 05: Ackermann Function
// Compute A(3,4) = 125
// The Ackermann function grows faster than any primitive recursive function.

package main

import "os"

func ack(m, n int) int {
	if m == 0 {
		return n + 1
	}
	if n == 0 {
		return ack(m-1, 1)
	}
	return ack(m-1, ack(m, n-1))
}

func main() {
	os.Exit(ack(3, 4))
}
