// Rust - Challenge 13: Minimax Game AI
// Nim(15): full tree search, return optimal move (3)
use std::process::ExitCode;

fn minimax(stones: i32, maximizing: bool) -> i32 {
    if stones == 0 { return if maximizing { -1 } else { 1 }; }
    if maximizing {
        let mut best = -100;
        for take in 1..=3.min(stones) {
            best = best.max(minimax(stones - take, false));
        }
        best
    } else {
        let mut best = 100;
        for take in 1..=3.min(stones) {
            best = best.min(minimax(stones - take, true));
        }
        best
    }
}

fn main() -> ExitCode {
    let mut best_move = 1;
    let mut best_score = -100;
    for take in 1..=3 {
        let score = minimax(15 - take, false);
        if score > best_score { best_score = score; best_move = take; }
    }
    ExitCode::from(best_move as u8)
}
