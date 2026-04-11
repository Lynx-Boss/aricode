// Rust - Challenge 03: Factorial
// Compute 5! = 120, return as exit code

use std::process::ExitCode;

fn factorial(n: i32) -> i32 {
    if n < 2 { return 1; }
    n * factorial(n - 1)
}

fn main() -> ExitCode {
    ExitCode::from(factorial(5) as u8)
}
