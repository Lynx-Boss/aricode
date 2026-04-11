// Rust - Challenge 08: Euclidean GCD
// Compute GCD(462, 1071) = 21
use std::process::ExitCode;

fn gcd(mut a: i32, mut b: i32) -> i32 {
    while b > 0 {
        let t = b;
        b = a % b;
        a = t;
    }
    a
}

fn main() -> ExitCode {
    ExitCode::from(gcd(462, 1071) as u8)
}
