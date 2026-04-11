#!/usr/bin/env node
/*
 * Decimal Precision Sparring - JavaScript/Node.js (Number / IEEE 754)
 * Shows how JS Number fails at precise decimal arithmetic.
 */

function fmt(val) {
    return val.toFixed(20);
}

function check(label, got, expected) {
    const gotStr = fmt(got);
    const pass = gotStr === expected;
    console.log(`  ${label}`);
    console.log(`    expected: ${expected}`);
    console.log(`    got:      ${gotStr}  [${pass ? "PASS" : "FAIL"}]`);
    return pass ? 1 : 0;
}

let passes = 0;
const total = 7;

console.log("=== JavaScript Number (IEEE 754 double) ===\n");

passes += check("0.1 + 0.2 = 0.3", 0.1 + 0.2, "0.30000000000000000000");
passes += check("1.0 - 0.9 - 0.1 = 0.0", 1.0 - 0.9 - 0.1, "0.00000000000000000000");
passes += check("0.1 * 0.1 = 0.01", 0.1 * 0.1, "0.01000000000000000000");
passes += check("1.0 / 3.0 * 3.0 = 1.0", 1.0 / 3.0 * 3.0, "1.00000000000000000000");
passes += check("0.3 - 0.2 - 0.1 = 0.0", 0.3 - 0.2 - 0.1, "0.00000000000000000000");
passes += check("PI to 20 decimals", Math.PI, "3.14159265358979323846");
passes += check("1.111...1 + 2.222...2 = 3.333...3",
    1.11111111111111111111 + 2.22222222222222222222,
    "3.33333333333333333333");

console.log(`\nResult: ${passes}/${total} PASS`);
