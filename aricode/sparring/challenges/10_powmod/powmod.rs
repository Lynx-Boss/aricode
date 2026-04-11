// Rust - Challenge 10: Modular Exponentiation
// Compute 7^19 mod 211 = 85
use std::process::ExitCode;

fn powmod(base: i32, mut exp: i32, m: i32) -> i32 {
    let mut result = 1i64;
    let mut b = (base % m) as i64;
    let m = m as i64;
    while exp > 0 {
        if exp & 1 == 1 { result = result * b % m; }
        exp >>= 1;
        b = b * b % m;
    }
    result as i32
}

fn main() -> ExitCode {
    ExitCode::from(powmod(7, 19, 211) as u8)
}
