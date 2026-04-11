// Go - Challenge 15: Array Sum, return sum%256=103
package main
import "os"
func main() {
	data := make([]int, 100)
	for i := 0; i < 100; i++ { data[i] = i * i }
	sum := 0
	for _, v := range data { sum += v }
	os.Exit(sum % 256)
}
