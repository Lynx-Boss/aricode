// Go - Challenge 14: Leibniz Pi, return floor(pi*50)=157
package main
import "os"
func main() {
	sum, sign, denom := 0.0, 1.0, 1.0
	for i := 0; i < 10000; i++ {
		sum += sign / denom
		sign = -sign
		denom += 2.0
	}
	os.Exit(int(sum * 4.0 * 50.0))
}
