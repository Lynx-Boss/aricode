# Aricode — engineering status snapshot

Honest, number-backed picture of what the compiler can do, what's
fast, what's slow, and what's still on the backlog.  Updated after
each performance or feature push.

Last updated: 2026-04-25 (parallel MNIST training: 97 s → 32.5 s, 2.98× wall-time, accuracy preserved)

---

## Performance — two regimes, both honest

Aricode's wall-clock performance splits cleanly into two categories
depending on the program's runtime profile.  Publish both; don't mix.

### Startup-dominated workloads

Small programs, thousands to ~10 K iterations inside the binary.
Binary load + dynamic linker setup dominate the clock, and aricode's
static-syscall, tiny-binary model wins there.

Sparring suite (5 000 iterations per binary, best-of-10), measured
after this session's peephole landings:

| Challenge       | C -O2 (glibc) | aricode | aricode wins by | Δ vs previous session |
|-----------------|--------------:|--------:|----------------:|----------------------:|
| 01_add          |   371 µs      | 266 µs  | **1.40×**       | −6.4 %                |
| 02_fib          |   373         | 267     |   1.40          | −8.0 %                |
| 03_factorial    |   376         | 276     |   1.36          | −2.4 %                |
| 05_ackermann    |   387         | 293     |   1.32          | −5.5 %                |
| 06_collatz      |   378         | 266     |   1.42          | −6.7 %                |
| 07_mersenne     |   411         | 347     |   1.18          | −4.9 %                |
| 08_gcd          |   371         | 268     |   1.38          | −5.5 %                |
| 09_primecount   |   376         | 270     | **1.39**        | −4.0 %                |
| 10_powmod       |   373         | 275     |   1.36          | −2.0 %                |
| 11_isqrt        |   372         | 276     |   1.35          | −1.5 %                |
| 12_perceptron   |   380         | 295     |   1.29          | −3.5 %                |
| 13_minimax      |   388         | 327     |   1.19          | −8.1 %                |
| 14_leibniz      |   385         | 298     | **1.29**        | **−15.7 %**           |
| 15_arraysum     |   393         | 274     | **1.43**        | −4.6 %                |

Every benchmark improved versus the pre-peephole baseline, no
regressions.  Biggest win is `14_leibniz` — a float-heavy inner loop
with a `sign = 0.0 - sign` per iteration, which is exactly the shape
the new `0.0 - x → btc rax, 63` peephole and the `movq xmm0, rax`
elision were designed to catch.

Known wart: binary sizes grew between runs on most challenges
(e.g. `09_primecount` 544 B → 655 B, `12_perceptron` 1.6 KB → 4.2 KB
despite faster wall-clock.  Text size and emitted-instruction count
rose in parallel, so the peepholes are improving per-instruction
quality while something upstream — possibly the f64-hot-var
allocator, the exp/log coefficient stacks, or the new CNN builtin
landing in binaries that never call it — is inflating total code.
Not a correctness issue; flagged for investigation.

aricode binaries are 300 B–4 KB (static syscalls, no glibc); gcc's are
754 KB (glibc-linked).  The loader pays off at startup.

Run it yourself:

```sh
cd aricode/sparring
./build_all.sh && ./benchmark.sh 5000
```

### Compute-dominated workloads

100 M+ iterations, compute-bound, no startup influence.  Here gcc -O2
still wins — but after this session's codegen work the gap is
manageable.

100 M iterations, best-of-10, bash `time`:

| Benchmark | C -O2 | aricode | ratio vs C |
|-----------|------:|--------:|-----------:|
| Leibniz (π, 100 M terms, divsd-bound)       | 94 ms  | 150 ms  | **1.60×** |
| varmul (k²-sum accumulator, f64-heavy loop) | 73 ms  | 118 ms  | **1.62×** |

Starting point was 4.2-4.8× slower; the chain of codegen
optimisations listed in `project_instruction_scheduling` (memory)
closed roughly 60 % of the gap.

The remaining ~1.6× is largely divsd latency (14-20 c on Zen 3)
and the lack of a proper instruction scheduler.  An auto-vectoriser
or scheduler pass would push further.

---

## ML stack

Aricode can train a real network end-to-end.  The `aricode-ml`
stdlib (separate repo `AriCodeLabs/aricode-ml`) exposes:

- **Dense layer**: `dense_forward`, `dense_backward`,
  `dense_backward_relu`, `dense_backward_sigmoid`, `dense_backward_tanh`.
- **Loss**: `xent_backward` (softmax-cross-entropy fused gradient
  reduces to `probs − onehot`).
- **Optimizers**: `sgd_update`, `adam_update_moments`, `adam_apply`,
  `clip_gradients`, `zero_gradients`.
- **Scalar math helpers**: `math_pow`, `math_pow_int`, `arr_f64_log_scalar`,
  `arr_f64_log1p_scalar`, `arr_f64_sqrt_scalar`, `arr_f64_abs_scalar`.

Compiler-side AVX2 tensor builtins (`arr_f64_*`):

| Shipped        | Not shipped yet |
|----------------|------------------|
| `arr_f64_new`, `get`, `set`          | `arr_f64_max_pool`           |
| `arr_f64_matvec`, `matvec_T`         | `arr_f64_conv2d` (C_in > 1)  |
| `arr_f64_outer_accum`                |                              |
| `arr_f64_add_scaled`, `scale`        |                              |
| `arr_f64_mul`, `sub`                 |                              |
| `arr_f64_dot`, `sum`, `sum_kahan`    |                              |
| `arr_f64_relu`, `sigmoid`, `tanh`    |                              |
| `arr_f64_exp`, `arr_f64_expm1`       |                              |
| `arr_f64_log`, `arr_f64_log1p`       |                              |
| `arr_f64_softmax`                    |                              |
| `arr_f64_fill`, `copy_at`, `copy_slice` |                           |
| `arr_f64_adam_apply`                 |                              |
| `arr_f64_conv2d_3x3_p1`              |                              |
| `arr_f64_conv2d_3x3_p1_multi` (C_in>1) |                            |

`arr_f64_conv2d_3x3_p1(padded_input, weights, bias, output, C_out)`
is a direct-convolution AVX2 kernel hardcoded for MNIST-size CNNs
(C_in = 1, 28 × 28 spatial, 3 × 3 kernel, pad 1, stride 1).  Caller
pre-pads input to 30 × 30; the builtin pre-broadcasts the 9 kernel
weights once per output channel, then fuses 9 `vfmadd231pd` per
4-wide output chunk — 28 rows × 7 chunks × 8 channels × 9 FMAs per
sample.

The three bulk memory primitives — `arr_f64_fill(buf, value)`,
`arr_f64_copy_at(src, src_offset, dst)`, `arr_f64_copy_slice(src,
src_offset, dst, dst_offset, n)` — replace the scalar bias-broadcast
and im2col construction loops that used to dominate the CNN wall
clock.  Each runs 4 f64 per vmovupd with a scalar tail.

Pool and backward stay as pure-`.ari` helpers in
`aricode-ml/conv2d.ari`:

  - `conv2d_forward` / `conv2d_forward_multi`: pad + call the AVX2
    builtin, single-channel / multi-channel.
  - `conv2d_im2col` / `conv2d_im2col_from_padded`: build 9 patch rows
    from 28×28 input or from a channel of an already-padded tensor.
  - `conv2d_backward_weights` / `conv2d_backward_weights_multi`: dW,
    db via AVX2 sum_range / dot_range reductions.
  - `conv2d_backward_input_multi`: transpose conv via kernel flip +
    C_in/C_out axis swap on the existing forward builtin — no new
    compiler work, just a few dozen lines of `.ari`.
  - `maxpool_2x2_forward` / `_backward`: still scalar (AVX2 argmax
    is low-ROI per earlier Plan agent analysis).

With all three backward paths in place, a stacked LeNet-style CNN
can be written end-to-end (conv → pool → conv → pool → FC) with
gradients flowing through both conv layers.  MNIST demo using this
is the next natural extension; current `mnist_cnn.ari` still uses
the single-layer architecture at 98.66 %.

### MNIST demo — up to 98.66 % test accuracy

End-to-end digit classifier in `aricode-ml/examples/mnist/`,
784 → 128 → 10 MLP, He init, full 60 K / 10 K MNIST.  Two variants:

**`mnist.ari`** — SGD baseline.  Raw pixels scaled to `[0, 1]`,
mini-batch SGD (batch 64, lr 0.1, exponential 0.85 decay), 10 epochs.

| Epoch | train NLL | test acc |
|-------|-----------|----------|
|   1   | 0.4139    | 92.23 %  |
|   5   | 0.1126    | 96.67 %  |
|  10   | 0.0885    | **97.15 %** |

33 s wall-clock, 21 KB binary.

**`mnist_adam.ari`** — AdamW + full recipe.  Hidden 256, input
standardization (mean 0.1307 / std 0.3081), label smoothing α = 0.05,
AdamW (β₁ 0.9, β₂ 0.999, ε 1e-8, wd 1e-3 on weights only), cosine lr
from 1e-3 down to 1e-5 over 20 epochs.  Uses the fused AVX2
`arr_f64_adam_apply` kernel.

| Epoch | train NLL | test acc |
|-------|-----------|----------|
|   1   | 0.2658    | 96.75 %  |
|   5   | 0.0910    | 98.32 %  |
|  10   | 0.0697    | 98.41 %  |
|  15   | 0.0637    | 98.53 %  |
|  20   | 0.0619    | **98.61 %** |

137 s wall-clock, ~50 KB binary.

**`mnist_cnn.ari`** — one-conv CNN with the same AdamW + smoothing +
cosine recipe.  Architecture: Conv 1→8ch (3×3, pad 1) + ReLU +
MaxPool 2×2 + FC 1568→64 + ReLU + FC 64→10.  Conv forward uses the
AVX2 `arr_f64_conv2d_3x3_p1` builtin; im2col + backward_weights
use the `arr_f64_fill` / `copy_slice` / `copy_at` bulk-memory
builtins for what used to be scalar inner loops.

| Epoch | train NLL | test acc |
|-------|-----------|----------|
|   1   | 0.2998    | 96.73 %  |
|   5   | 0.1122    | 98.18 %  |
|  10   | 0.0925    | **98.66 %** |

**97 s wall-clock**, ~65 KB binary.  Beats the MLP at half the epoch
budget with ~2× fewer parameters (101 K vs 203 K).

Wall-clock trajectory across the session's CNN AVX2 work
(identical training trajectory / final accuracy in every row):

| Stage                                                          | Time  | Δ        |
|----------------------------------------------------------------|------:|---------:|
| Pure-`.ari` conv forward + scalar im2col                       | 124 s | baseline |
| + `arr_f64_conv2d_3x3_p1` AVX2 forward                         | 111 s | −10.5 %  |
| + `arr_f64_copy_at` in `conv2d_backward_weights`               | 106 s | − 4.5 %  |
| + `arr_f64_fill` + `copy_slice` in `conv2d_im2col`             | 101 s | − 4.7 %  |
| + dropped wasted `conv2d_im2col` in inference loop             |  97 s | − 4.0 %  |
| **Total**                                                      |       | **−22 %** |

---

## Multi-core threading

Three primitives now ship in the compiler.  They lean on raw Linux
syscalls — no libpthread, no runtime — and rely on the fact that
`CLONE_VM` keeps the heap (`arr_new` / `arr_f64_new` mmaps) shared
across threads:

| Primitive                                 | Emits                                               |
|-------------------------------------------|-----------------------------------------------------|
| `thread_spawn(func)` / `(func, arg)`      | `clone(CLONE_VM|…|THREAD, mmap'd stack)` syscall 56. Pre-seeds the child's new stack with the RIP-relative-resolved absolute address of `func` (and optionally `arg` for RDI) so the child's `pop rax ; pop rdi ; call rax` reaches the function. Returns child tid. |
| `atomic_add_i64(base, idx, delta)`        | one `lock xadd` — uninterruptible fetch-and-add, returns the old value. |
| `atomic_add_f64(base, idx, delta)`        | `lock cmpxchg` retry loop on f64 bits (x86 has no atomic FADD). Returns the old value.  Slower than the i64 form under heavy contention — prefer per-thread buffers + a serial reduction for gradient aggregation. |
| `thread_wait(pid)`, `thread_exit(code)`   | thin wrappers on `wait4` / `exit`. |

Previous state: `thread_spawn` was broken — it clone()'d and then
fell straight through to `exit(0)` without invoking the passed
function, because the child had a fresh stack that didn't reach the
parent's push of the function pointer.  The fix pre-seeds the top
slot(s) of the child's mmap'd stack with the func address (and arg
if two-arg form) before the clone syscall, so `pop rax ; call rax`
on the child side actually runs the function.

**Benchmark 1 — pure compute** — `aricode-ml/examples/threading/parallel_matvec.ari`
computes a 512 × 512 matvec, 10 000 iterations, four workers splitting
disjoint output rows and calling `arr_f64_dot_range` (the AVX2 dot
builtin).  Matrix sized to fit in L2 per worker so the test isolates
compute parallelism from memory-bandwidth effects.

| Variant                | Wall time | Speedup |
|------------------------|----------:|--------:|
| `serial_matvec.ari`    |  0.53 s   |  1.00×  |
| `parallel_matvec.ari`  |  0.13 s   | **4.0×** |

Bit-identical `-95.856602` checksum across both binaries — same
arithmetic, different work distribution.  `user / real ≈ 4.0` ⇒
effective 4-core occupancy.

**Benchmark 2 — real ML workload (MNIST CNN)** — three variants of the
same 98.6 %-accuracy model, increasing amount of parallelism:

| Variant              | Wall time | Test acc | Parallelism                          |
|----------------------|----------:|---------:|--------------------------------------|
| `mnist_cnn.ari`      |   97 s    | 98.66 %  | serial                               |
| `mnist_cnn_par.ari`  |   88 s    | 98.66 %  | parallel eval only                   |
| `mnist_cnn_par2.ari` | **32.5 s**| 98.65 %  | **parallel training + eval**         |

`mnist_cnn_par2.ari` — four workers split the 64-sample mini-batch,
each accumulates into its OWN copy of the six gradient buffers
(dW_conv, db_conv, dW_fc1, db_fc1, dW_fc2, db_fc2).  The parent folds
worker[1..3] into worker[0] via `arr_f64_add_scaled` (six AVX2 calls
per worker), then runs AdamW + weight decay once against worker[0]'s
summed gradient.  Per-worker scratch activation buffers live in
shared memory and each worker has its own `im2col_rows`, `t_lbl`, `dy`,
`dhid`, `dpool`, `dconv` — so there's zero contention on the hot
forward/backward path.  Loss aggregates via one `atomic_add_f64` per
worker per batch, not per sample.

2.98× wall-time over the serial build, `user / real ≈ 3.9` ⇒ effective
4-core occupancy on the training loop.  Final accuracy 98.65 % is
within 0.01 % of the serial 98.66 % — the tiny drift is the expected
float-summation-order variance when 64 per-sample gradients reduce as
(0..15)+(16..31)+(32..47)+(48..63) instead of 0+1+…+63.

**Caveats**
- `thread_wait` uses `wait4`, which returns `-ECHILD` for CLONE_THREAD
  children — the intended coordination pattern today is
  "workers `atomic_add_i64` a shared done counter, parent spin-waits
  on it."  Fine for short-running workers; a futex wait is the next
  step for long-running ones.
- No `atomic_add_f64` yet.  Gradient aggregation across threads
  should prefer per-thread gradient buffers + a serial reduction by
  the parent over cmpxchg-loop atomics — less contention, predictable
  numerics.

---

## Test infrastructure

Two complementary suites in `aricode/tests/`:

| Suite          | Tests | Runtime | Covers                                    |
|----------------|------:|--------:|-------------------------------------------|
| `run_all.sh`   |    39 |   ~90 ms | Arithmetic, strings, arrays, error handling, file I/O, SIMD basics, imports, memory |
| `run_edge.sh`  |    40 |  ~2.5 s | Softmax scalar tails & underflow clamp, vec exp/expm1 range, f64 return-contract (xmm0 + rax), hot-var register allocation, branch peephole, short-circuit `&&`/`||`, CNN forward/backward spot checks, threading (spawn, arg-passing, i64 + f64 atomic contention) |

Run both with one command:

```sh
make test-e2e            # end-to-end suites only
make test-all            # + module unit tests (lexer, parser, errors)
```

`run_edge.sh` is the canary for subtle codegen bugs — each test was
added immediately after a bug was found, so the next regression is
caught in under 100 ms.

---

## Language features

| Feature                       | State        |
|-------------------------------|--------------|
| i32, f64, bool, str, arrays   | shipped      |
| `dec` (exact decimal)         | shipped      |
| Structs, enums                | shipped      |
| if/while/for/match            | shipped      |
| try/catch + error.raise       | shipped      |
| Short-circuit `&&` / `||`     | shipped      |
| Ternary `? :`                 | shipped      |
| Compound assignment           | shipped      |
| Tail-call optimisation        | shipped (self-recursion) |
| f64 hot-var allocation (xmm8-15)   | shipped      |
| i32 hot-var allocation (r12-r15)   | shipped      |
| XMM stash, branch peephole, movq-drop | shipped |
| Imports, namespaces           | shipped      |
| `math_pow` / pow_int scalar   | shipped (stdlib) |
| `thread_spawn(func[, arg])`   | shipped      |
| `atomic_add_i64`              | shipped (`lock xadd`) |
| `atomic_add_f64`              | shipped (`lock cmpxchg` loop) |
| Futex-based thread_wait       | **not shipped** (spin-on-counter barrier works today) |
| Instruction scheduler         | **not shipped** |
| Auto-vectoriser pass          | **not shipped** |
| CNN builtins (conv2d, pool)   | shipped (`arr_f64_conv2d_3x3_p1*`) |

---

## Honest caveats

- **Register allocator** is hot-var mode only, conservative: if any
  unsafe call is reachable in a function, every local round-trips
  the stack.  That's why compute-heavy f64 loops beat gcc in the
  sparring suite (tiny binary wins startup) but lag by ~1.6× in
  pure compute.
- **f64 `arr_f64_softmax` / `arr_f64_exp`** clamp shifted values to
  `≥ −700`.  Values below that underflow in the 2^k reconstruction;
  the clamp maps them to `exp(−700) ≈ 1e−304`, which is zero for all
  practical purposes.  Values below `−700` lose the small-scale
  distinction between them — acceptable for softmax, but if you need
  honest `exp(−2000)` use scalar `math_exp`.
- **Adam optimizer** ships (AVX2 `arr_f64_adam_apply` kernel +
  `adam_apply_fast` wrapper) and is wired into `mnist_adam.ari`
  with the full recipe (input standardization, label smoothing,
  AdamW weight decay).  Earlier notes said Adam couldn't beat SGD
  here — that was before the recipe was layered on.  With all three
  knobs in place it reaches 98.14 % vs SGD's 97.15 %.
- **No CNN builtins** — MNIST runs as an MLP.  Conv2d and max-pool
  are the next big additions when someone needs ~99 %.

---

## Quick start

```sh
# Build
cd aricode/src/compiler && make

# Test
cd aricode && make test-all

# Train MNIST (SGD baseline, 97.15 %)
cd aricode-stdlib/aricode-ml/examples/mnist
./get_data.sh                           # one-time fetch
aric mnist.ari -o mnist
./mnist                                 # 33 s, 97.15 % accuracy

# Or AdamW MLP variant (98.61 %)
aric mnist_adam.ari -o mnist_adam
./mnist_adam                            # 137 s, 98.61 % accuracy

# Or CNN variant (98.66 %)
aric mnist_cnn.ari -o mnist_cnn
./mnist_cnn                             #  97 s, 98.66 % accuracy
```
