// Decimal Precision Sparring - Go (float64 / IEEE 754)
// Shows how Go's float64 fails at precise decimal arithmetic.
package main

import (
	"fmt"
	"math"
)

func fmtFloat(val float64) string {
	return fmt.Sprintf("%.20f", val)
}

func check(label string, got float64, expected string) int {
	gotStr := fmtFloat(got)
	pass := gotStr == expected
	tag := "FAIL"
	if pass {
		tag = "PASS"
	}
	fmt.Printf("  %s\n", label)
	fmt.Printf("    expected: %s\n", expected)
	fmt.Printf("    got:      %s  [%s]\n", gotStr, tag)
	if pass {
		return 1
	}
	return 0
}

func main() {
	passes := 0
	total := 7

	fmt.Println("=== Go float64 (IEEE 754) ===")
	fmt.Println()

	passes += check("0.1 + 0.2 = 0.3", 0.1+0.2, "0.30000000000000000000")
	passes += check("1.0 - 0.9 - 0.1 = 0.0", 1.0-0.9-0.1, "0.00000000000000000000")
	passes += check("0.1 * 0.1 = 0.01", 0.1*0.1, "0.01000000000000000000")
	passes += check("1.0 / 3.0 * 3.0 = 1.0", 1.0/3.0*3.0, "1.00000000000000000000")
	passes += check("0.3 - 0.2 - 0.1 = 0.0", 0.3-0.2-0.1, "0.00000000000000000000")
	passes += check("PI to 20 decimals", math.Pi, "3.14159265358979323846")
	passes += check("1.111...1 + 2.222...2 = 3.333...3",
		1.11111111111111111111+2.22222222222222222222,
		"3.33333333333333333333")

	fmt.Printf("\nResult: %d/%d PASS\n", passes, total)
}
