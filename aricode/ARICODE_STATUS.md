# Aricode — engineering status snapshot

Honest, number-backed picture of what the compiler can do, what's
fast, what's slow, and what's still on the backlog.  Updated after
each performance or feature push.

Last updated: 2026-04-19 (AVX2 `arr_f64_adam_apply` shipped)

---

## Performance — two regimes, both honest

Aricode's wall-clock performance splits cleanly into two categories
depending on the program's runtime profile.  Publish both; don't mix.

### Startup-dominated workloads

Small programs, thousands to ~10 K iterations inside the binary.
Binary load + dynamic linker setup dominate the clock, and aricode's
static-syscall, tiny-binary model wins there.

Sparring suite (5 000 iterations per binary, best-of-10):

| Challenge       | C -O2 (glibc) | aricode | aricode wins by | gcc binary / aric binary |
|-----------------|--------------:|--------:|----------------:|-------------------------:|
| 02_fib          |   393 µs      | 283 µs  | **1.39×**       | 2377×                    |
| 05_ackermann    |   394         | 306     |   1.29          | 1959                     |
| 07_mersenne     |   425         | 357     |   1.19          | 1347                     |
| 09_primecount   |   394         | 278     | **1.42**        | 1067                     |
| 12_perceptron   |   391         | 302     |   1.30          | 174                      |
| 13_minimax      |   408         | 343     |   1.19          | 545                      |
| 14_leibniz      |   401         | 305     |   1.31          | 1111                     |
| 15_arraysum     |   395         | 287     | **1.38**        | 722                      |

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
| `arr_f64_new`, `get`, `set`          | `arr_f64_log`        |
| `arr_f64_matvec`, `matvec_T`         | `arr_f64_log1p`      |
| `arr_f64_outer_accum`                | `arr_f64_conv2d`     |
| `arr_f64_add_scaled`, `scale`        | `arr_f64_max_pool`   |
| `arr_f64_mul`, `sub`                 |                      |
| `arr_f64_dot`, `sum`, `sum_kahan`    |                      |
| `arr_f64_relu`, `sigmoid`, `tanh`    |                      |
| `arr_f64_exp`, `arr_f64_expm1`       |                      |
| `arr_f64_softmax`                    |                      |
| `arr_f64_adam_apply`                 |                      |

### MNIST demo — 97.15 % test accuracy

End-to-end digit classifier in `aricode-ml/examples/mnist/`:
784 → 128 → 10 MLP, He init, mini-batch SGD (batch 64, lr 0.1,
exponential 0.85 decay), 10 epochs on full 60 K / 10 K MNIST.

| Epoch | train NLL | test acc |
|-------|-----------|----------|
|   1   | 0.4139    | 92.23 %  |
|   5   | 0.1126    | 96.67 %  |
|  10   | 0.0885    | **97.15 %** |

33 s wall-clock, 21 KB binary, zero dynamic dependencies.

---

## Test infrastructure

Two complementary suites in `aricode/tests/`:

| Suite          | Tests | Runtime | Covers                                    |
|----------------|------:|--------:|-------------------------------------------|
| `run_all.sh`   |    39 |   ~90 ms | Arithmetic, strings, arrays, error handling, file I/O, SIMD basics, imports, memory |
| `run_edge.sh`  |    21 |   ~90 ms | Softmax scalar tails & underflow clamp, vec exp/expm1 range, f64 return-contract (xmm0 + rax), hot-var register allocation, branch peephole, short-circuit `&&`/`||` |

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
| Instruction scheduler         | **not shipped** |
| Auto-vectoriser pass          | **not shipped** |
| CNN builtins (conv2d, pool)   | **not shipped** |

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
- **Adam optimizer** ships (including the AVX2 `arr_f64_adam_apply`
  fused kernel and `adam_apply_fast` wrapper) but is not yet the
  default in MNIST.  A vanilla Adam drop-in (just swap the update
  rule) lands around 84–87 % where tuned SGD+decay hits 97.15 %.
  Closing that gap needs the full recipe — input standardization
  (mean 0.1307 / std 0.3081), label smoothing (~0.05), and AdamW-style
  decoupled weight decay (1e-4, weights only) — layered together.
  Builtin is ready; tuning is the pending piece.
- **No CNN builtins** — MNIST runs as an MLP.  Conv2d and max-pool
  are the next big additions when someone needs ~99 %.

---

## Quick start

```sh
# Build
cd aricode/src/compiler && make

# Test
cd aricode && make test-all

# Train MNIST
cd aricode-stdlib/aricode-ml/examples/mnist
./get_data.sh                           # one-time fetch
aric mnist.ari -o mnist
./mnist                                 # 33 s, 97.15 % accuracy
```
