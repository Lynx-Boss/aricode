// Rust - Challenge 15: Array Sum, return sum%256=103
use std::process::ExitCode;
fn main() -> ExitCode {
    let data: Vec<i32> = (0..100).map(|i| i*i).collect();
    let sum: i32 = data.iter().sum();
    ExitCode::from((sum % 256) as u8)
}
