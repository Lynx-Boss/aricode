// Decimal Precision Sparring - Rust (f64 / IEEE 754)
// Shows how Rust's f64 fails at precise decimal arithmetic.

use std::f64::consts::PI;

fn check(label: &str, got: f64, expected: &str) -> bool {
    let got_str = format!("{:.20}", got);
    let pass = got_str == expected;
    let tag = if pass { "PASS" } else { "FAIL" };
    println!("  {}", label);
    println!("    expected: {}", expected);
    println!("    got:      {}  [{}]", got_str, tag);
    pass
}

fn main() {
    let mut passes: i32 = 0;
    let total = 7;

    println!("=== Rust f64 (IEEE 754) ===\n");

    if check("0.1 + 0.2 = 0.3", 0.1_f64 + 0.2_f64, "0.30000000000000000000") { passes += 1; }
    if check("1.0 - 0.9 - 0.1 = 0.0", 1.0_f64 - 0.9_f64 - 0.1_f64, "0.00000000000000000000") { passes += 1; }
    if check("0.1 * 0.1 = 0.01", 0.1_f64 * 0.1_f64, "0.01000000000000000000") { passes += 1; }
    if check("1.0 / 3.0 * 3.0 = 1.0", 1.0_f64 / 3.0_f64 * 3.0_f64, "1.00000000000000000000") { passes += 1; }
    if check("0.3 - 0.2 - 0.1 = 0.0", 0.3_f64 - 0.2_f64 - 0.1_f64, "0.00000000000000000000") { passes += 1; }
    if check("PI to 20 decimals", PI, "3.14159265358979323846") { passes += 1; }
    if check("1.111...1 + 2.222...2 = 3.333...3",
             1.11111111111111111111_f64 + 2.22222222222222222222_f64,
             "3.33333333333333333333") { passes += 1; }

    println!("\nResult: {}/{} PASS", passes, total);
}
