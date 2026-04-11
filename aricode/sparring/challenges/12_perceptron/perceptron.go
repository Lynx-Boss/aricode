// Go - Challenge 12: Perceptron Neural Network
// Train AND gate, 1000 epochs, fixed-point, return correct (4)
package main

import "os"

func train() int {
	w1, w2, bias, lr := 0, 0, 0, 100
	samples := [][3]int{{0,0,0},{0,1000,0},{1000,0,0},{1000,1000,1000}}
	for e := 0; e < 1000; e++ {
		for _, s := range samples {
			out := w1*s[0]/1000 + w2*s[1]/1000 + bias
			pred := 0; if out > 500 { pred = 1000 }
			err := s[2] - pred
			w1 += lr * err * s[0] / 1000 / 1000
			w2 += lr * err * s[1] / 1000 / 1000
			bias += lr * err / 1000
		}
	}
	c := 0
	if bias <= 500 { c++ }
	if w2+bias <= 500 { c++ }
	if w1+bias <= 500 { c++ }
	if w1+w2+bias > 500 { c++ }
	return c
}

func main() { os.Exit(train()) }
