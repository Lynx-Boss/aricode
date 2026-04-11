// Rust - Challenge 05: Ackermann Function
// Compute A(3,4) = 125
// The Ackermann function grows faster than any primitive recursive function.

use std::process::ExitCode;

fn ack(m: i32, n: i32) -> i32 {
    if m == 0 {
        return n + 1;
    }
    if n == 0 {
        return ack(m - 1, 1);
    }
    ack(m - 1, ack(m, n - 1))
}

fn main() -> ExitCode {
    ExitCode::from(ack(3, 4) as u8)
}
