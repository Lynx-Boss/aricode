// Rust - Challenge 09: Prime Counting
// Count primes below 100 = 25
use std::process::ExitCode;

fn is_prime(n: i32) -> bool {
    if n < 2 { return false; }
    if n % 2 == 0 { return n == 2; }
    let mut d = 3;
    while d * d <= n {
        if n % d == 0 { return false; }
        d += 2;
    }
    true
}

fn main() -> ExitCode {
    let count = (2..100).filter(|&n| is_prime(n)).count();
    ExitCode::from(count as u8)
}
