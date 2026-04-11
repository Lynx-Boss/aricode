// Rust - Challenge 07: Mersenne Prime Verification
// Verify M31 = 2^31 - 1 = 2,147,483,647 is prime
// Recursive trial division against ~23,170 odd divisors

use std::process::ExitCode;

fn is_prime_rec(n: i32, d: i32) -> i32 {
    if n / d < d {
        return 1;
    }
    if n % d == 0 {
        return 0;
    }
    is_prime_rec(n, d + 2)
}

fn main() -> ExitCode {
    ExitCode::from(is_prime_rec(2147483647, 3) as u8)
}
