// Rust - Challenge 06: Collatz Conjecture
// Compute the number of steps for 871 to reach 1 = 178 steps
// The Collatz conjecture (1937) remains UNSOLVED.

use std::process::ExitCode;

fn collatz_steps(n: i32) -> i32 {
    if n == 1 {
        return 0;
    }
    if n % 2 == 0 {
        return 1 + collatz_steps(n / 2);
    }
    1 + collatz_steps(3 * n + 1)
}

fn main() -> ExitCode {
    ExitCode::from(collatz_steps(871) as u8)
}
