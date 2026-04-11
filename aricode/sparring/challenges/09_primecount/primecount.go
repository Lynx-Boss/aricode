// Go - Challenge 09: Prime Counting
// Count primes below 100 = 25
package main

import "os"

func isPrime(n int) bool {
	if n < 2 { return false }
	if n%2 == 0 { return n == 2 }
	d := 3
	for d*d <= n {
		if n%d == 0 { return false }
		d += 2
	}
	return true
}

func main() {
	count := 0
	for n := 2; n < 100; n++ {
		if isPrime(n) { count++ }
	}
	os.Exit(count)
}
