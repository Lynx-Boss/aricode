// Rust - Challenge 12: Perceptron Neural Network
// Train AND gate, 1000 epochs, fixed-point, return correct (4)
use std::process::ExitCode;

fn train() -> i32 {
    let (mut w1, mut w2, mut bias) = (0i32, 0i32, 0i32);
    let lr = 100;
    let samples = [(0,0,0), (0,1000,0), (1000,0,0), (1000,1000,1000)];
    for _ in 0..1000 {
        for &(x1, x2, target) in &samples {
            let out = w1*x1/1000 + w2*x2/1000 + bias;
            let pred = if out > 500 { 1000 } else { 0 };
            let err = target - pred;
            w1 += lr * err * x1 / 1000 / 1000;
            w2 += lr * err * x2 / 1000 / 1000;
            bias += lr * err / 1000;
        }
    }
    let mut c = 0;
    if bias <= 500 { c += 1; }
    if w2+bias <= 500 { c += 1; }
    if w1+bias <= 500 { c += 1; }
    if w1+w2+bias > 500 { c += 1; }
    c
}

fn main() -> ExitCode { ExitCode::from(train() as u8) }
