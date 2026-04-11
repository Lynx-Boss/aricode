/* C - Challenge 12: Perceptron Neural Network */
/* Train AND gate, 1000 epochs, fixed-point (scale=1000) */
/* Return correct predictions (expect 4) */

int train_and_predict(void) {
    int w1 = 0, w2 = 0, bias = 0;
    int lr = 100;

    for (int epoch = 0; epoch < 1000; epoch++) {
        int out, pred, err;

        /* (0,0) -> 0 */
        out = bias; pred = out > 500 ? 1000 : 0;
        err = 0 - pred;
        bias += lr * err / 1000;

        /* (0,1) -> 0 */
        out = w2 + bias; pred = out > 500 ? 1000 : 0;
        err = 0 - pred;
        w2 += lr * err / 1000; bias += lr * err / 1000;

        /* (1,0) -> 0 */
        out = w1 + bias; pred = out > 500 ? 1000 : 0;
        err = 0 - pred;
        w1 += lr * err / 1000; bias += lr * err / 1000;

        /* (1,1) -> 1 */
        out = w1 + w2 + bias; pred = out > 500 ? 1000 : 0;
        err = 1000 - pred;
        w1 += lr * err / 1000; w2 += lr * err / 1000; bias += lr * err / 1000;
    }

    int correct = 0;
    if (bias <= 500) correct++;
    if (w2 + bias <= 500) correct++;
    if (w1 + bias <= 500) correct++;
    if (w1 + w2 + bias > 500) correct++;
    return correct;
}

int main(void) { return train_and_predict(); }
