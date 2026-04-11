// Rust - Challenge 02: Fibonacci
// Compute fib(10) = 55, return as exit code

use std::process::ExitCode;

fn fib(n: i32) -> i32 {
    if n < 2 { return n; }
    fib(n - 1) + fib(n - 2)
}

fn main() -> ExitCode {
    ExitCode::from(fib(10) as u8)
}
