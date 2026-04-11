// Go - Challenge 08: Euclidean GCD
// Compute GCD(462, 1071) = 21
package main

import "os"

func gcd(a, b int) int {
	for b > 0 {
		a, b = b, a%b
	}
	return a
}

func main() {
	os.Exit(gcd(462, 1071))
}
