/* C - Challenge 13: Minimax Game AI */
/* Nim(15): full tree search, return optimal move (3) */

int minimax(int stones, int maximizing) {
    if (stones == 0) return maximizing ? -1 : 1;
    if (maximizing) {
        int best = -100;
        for (int take = 1; take <= 3 && take <= stones; take++) {
            int score = minimax(stones - take, 0);
            if (score > best) best = score;
        }
        return best;
    } else {
        int best = 100;
        for (int take = 1; take <= 3 && take <= stones; take++) {
            int score = minimax(stones - take, 1);
            if (score < best) best = score;
        }
        return best;
    }
}

int main(void) {
    int best_move = 1, best_score = -100;
    for (int take = 1; take <= 3; take++) {
        int score = minimax(15 - take, 0);
        if (score > best_score) { best_score = score; best_move = take; }
    }
    return best_move;
}
