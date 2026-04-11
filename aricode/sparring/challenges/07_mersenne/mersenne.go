// Go - Challenge 07: Mersenne Prime Verification
// Verify M31 = 2^31 - 1 = 2,147,483,647 is prime
// Recursive trial division against ~23,170 odd divisors

package main

import "os"

func isPrimeRec(n, d int) int {
	if n/d < d {
		return 1
	}
	if n%d == 0 {
		return 0
	}
	return isPrimeRec(n, d+2)
}

func main() {
	os.Exit(isPrimeRec(2147483647, 3))
}
