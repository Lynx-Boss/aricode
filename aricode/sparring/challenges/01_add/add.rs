// Rust - Challenge 01: Simple Addition
// Return 37 + 5 = 42 as exit code

use std::process::ExitCode;

fn main() -> ExitCode {
    ExitCode::from((37 + 5) as u8)
}
