// Go - Challenge 10: Modular Exponentiation
// Compute 7^19 mod 211 = 85
package main

import "os"

func powmod(base, exp, m int) int {
	result := 1
	b := base % m
	for exp > 0 {
		if exp&1 == 1 { result = result * b % m }
		exp >>= 1
		b = b * b % m
	}
	return result
}

func main() {
	os.Exit(powmod(7, 19, 211))
}
