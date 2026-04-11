// Rust - Challenge 11: Integer Square Root (Newton's Method)
// Compute isqrt(16129) = 127
use std::process::ExitCode;

fn isqrt(n: i32) -> i32 {
    if n < 2 { return n; }
    let mut x = n;
    let mut y = (x + 1) / 2;
    while y < x {
        x = y;
        y = (x + n / x) / 2;
    }
    x
}

fn main() -> ExitCode {
    ExitCode::from(isqrt(16129) as u8)
}
