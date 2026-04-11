// Rust - Challenge 14: Leibniz Pi, return floor(pi*50)=157
use std::process::ExitCode;
fn main() -> ExitCode {
    let (mut sum, mut sign, mut denom) = (0.0f64, 1.0f64, 1.0f64);
    for _ in 0..10000 {
        sum += sign / denom;
        sign = -sign;
        denom += 2.0;
    }
    ExitCode::from((sum * 4.0 * 50.0) as u8)
}
