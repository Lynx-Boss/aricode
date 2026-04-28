# Aricode — engineering status snapshot

Honest, number-backed picture of what the compiler can do, what's
fast, what's slow, and what's still on the backlog.  Updated after
each performance or feature push.

Last updated: 2026-04-28 (Phase A.8 closed: 17/17 f32 builtins + arr_f32_exp/softmax; futex barriers + atomic_add_f64 NaN guard; full f32 CNN trains end-to-end with AdamW; parallel f32 variant hits 98.65 % in 23 s; sparring suite re-verified)

---

## Performance — two regimes, both honest

Aricode's wall-clock performance splits cleanly into two categories
depending on the program's runtime profile.  Publish both; don't mix.

### Startup-dominated workloads

Small programs, thousands to ~10 K iterations inside the binary.
Binary load + dynamic linker setup dominate the clock, and aricode's
static-syscall, tiny-binary model wins there.

Sparring suite (5 000 iterations per binary, best-of-10), re-run
2026-04-28 as a regression sanity check after the Phase A.8 f32
work + the futex / NaN-guard / f32-exp / f32-softmax landings.
None of those code paths are exercised by these tiny startup-bound
programs, so the timings are expected to match prior runs within
system noise — and they do (aricode column ±5 % of the previous
session, all wins held):

| Challenge       | C -O2 (glibc) | aricode | aricode wins by |
|-----------------|--------------:|--------:|----------------:|
| 01_add          |   396 µs      | 279 µs  | **1.42×**       |
| 02_fib          |   396         | 277     |   1.43          |
| 03_factorial    |   394         | 276     |   1.43          |
| 05_ackermann    |   390         | 302     |   1.29          |
| 06_collatz      |   390         | 275     |   1.42          |
| 07_mersenne     |   426         | 352     |   1.21          |
| 08_gcd          |   386         | 271     |   1.42          |
| 09_primecount   |   390         | 278     |   1.40          |
| 10_powmod       |   383         | 275     |   1.39          |
| 11_isqrt        |   388         | 275     |   1.41          |
| 12_perceptron   |   379         | 300     |   1.26          |
| 13_minimax      |   408         | 334     |   1.22          |
| 14_leibniz      |   397         | 297     |   1.34          |
| 15_arraysum     |   387         | 283     | **1.37**        |

aricode wins on every challenge.  The biggest gaps still come from
the float-heavy / divsd-bound loops (`07_mersenne`, `12_perceptron`,
`13_minimax`) where the f64 hot-var allocator can't fully hide the
operand-shuffle cost; an instruction scheduler or auto-vectoriser
would close more of that.

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
| Leibniz (π, 100 M terms, divsd-bound)       | 100 ms | 166 ms | **1.66×** |
| varmul (k²-sum accumulator, f64-heavy loop) |  73 ms | 118 ms | **1.62×** |

Starting point was 4.2-4.8× slower; the chain of codegen
optimisations listed in `project_instruction_scheduling` (memory)
closed roughly 60-65 % of the gap.  Latest wins (2026-04-28):

- **Phase A.8 — full f32 CNN training stack** — closes the f32 ML
  front: `arr_f32_dot_range` (8-lane vfmadd231ps over a slice of two
  f32 arrays with separate offsets) lands as the 17th f32 builtin and
  cuts the f32 CNN backward by 2.2× when wired into
  `conv2d_backward_weights_f32`.  Single-threaded `mnist_cnn_f32` now
  trains to 98.60 % in 76 s, parallel `mnist_cnn_par2_f32` to 98.65 %
  in 23.2 s (3.27× over single-thread, 4.46-core occupancy).  The
  unblocking fix was a single-bit VEX-encoding bug in
  `arr_f32_adam_apply` (byte-1 `0x81` set X̃=0, silently aliasing
  RSI→R14 — every iter past lane 7 re-read m[0..7] and CNN AdamW
  diverged to NaN on the first batch).  Edge test #51
  (`f32_adam_sparse`) prevents regression; older test #33 missed it
  because every lane having non-zero (m, v) gave numerically-similar
  steps regardless of whether iter k advanced.

- **f32 AVX2 dense kernels** (Phase A.3) — three more builtins close
  the dense-layer side of the f32 stack: arr_f32_matvec /
  matvec_T / outer_accum.  8-lane vfmadd231ps inner, vbroadcastss
  for the row scalar, scale=4 indexing throughout.  Microbench
  (200 × forward through 1024 → 512 → 128 MLP): f64 36 ms vs f32
  23 ms — **1.57×** speedup (below the pure-dot 1.96× because the
  workload is memory-bandwidth-bound at W1 size).  Output agrees
  with f64 to 5 significant digits.

- **f32 AVX2 foundation + element-wise** (Phase A.1 + A.2) — nine
  earlier builtins exploit the doubled SIMD throughput of single
  precision: arr_f32_new / get / set / dot / sum / relu / scale /
  fill / add_scaled.  Inner loops use 8-lane vfmadd231ps / vaddps /
  vmaxps / vmulps; the boundary (get/set/scalar args) goes through
  cvtss2sd / cvtsd2ss so the aricode language stays f64-only at the
  user level.  Microbench (10 000 × 100 k dot): f64 269 ms, f32
  137 ms — **1.96×** speedup.  All twelve f32 builtins empirically
  xmm-safe (zero ymm8..15 references in the disassembly probe).
  Remaining for end-to-end f32 MNIST: softmax, adam_apply,
  conv2d_3x3_p1, copy_at/slice — Phase A.4.
- **ymm8-15 save/restore wrapper for unsafe builtins** — nine
  builtins (softmax, sigmoid, tanh, adam_apply, conv2d_3x3_p1 and
  variants, log1p/exp/expm1) genuinely clobber ymm8..ymm13.  Instead
  of refactoring each, emit_call_expr now wraps inline emissions with
  a save/restore of the LIVE subset of ymm8..ymm15 around each call
  when the caller is xmm-safe.  Count is trimmed to
  `next_hot_xmm - 8` so a function with 2 f64 locals pays just 2
  vmovupd per side.  The xmm-safe whitelist was extended to accept
  these builtins; 8-hot-XMM probe sums 1..8 to 36.00 bit-exact across
  a softmax call, MNIST binary grew 46 B total, accuracy 98.65 %.
- **Widened xmm-safe builtin whitelist** — `call_is_xmm_safe` grew
  from 3 names to 30 after a per-builtin audit for xmm8..15 /
  r12..15 clobbers.  Scalar hot-loops that previously stayed on
  the cold stack-based path because they called `arr_f64_get`,
  `math_log` etc. now enter hot-var mode.  Microbench dot_loop
  dropped 420 ms → 312 ms (1.35×); MNIST binary shrank 2.4 %.
- **Hot-GP binop fast path** — when the right operand of an integer
  binop is a callee-saved hot-GP identifier (r12-r15), skip the
  stack stash and read it directly as the RCX source.  `while (i < n)`
  with both in hot-GP goes from 7 insns down to 3.
- **Hot-XMM binop fast path** — when the right operand is a hot-XMM
  identifier (xmm8-15), use its home register directly as the
  addsd/subsd/mulsd/divsd source.  Saves 3-4 movapd per float binop
  in register-pinned inner loops.
- Latent bug unmasked by the hot-XMM path: emit_var_decl /
  emit_assignment inferred "rhs is float?" from emit_expression's
  top-level return, which was wrong for nested `(a*b)+c` and
  triggered a stale-rax reload.  Now typed with the recursive
  `expr_is_float` walker.

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

**`mnist_cnn.ari` / `mnist_cnn_f32.ari` / `mnist_cnn_par2_f32.ari`** —
one-conv CNN with the same AdamW + smoothing + cosine recipe across
three precision/parallelism variants.  Architecture: Conv 1→8ch
(3×3, pad 1) + ReLU + MaxPool 2×2 + FC 1568→64 + ReLU + FC 64→10.
Conv forward uses the AVX2 `arr_f64_conv2d_3x3_p1` (or `_f32`)
builtin; im2col + backward_weights use the `fill` / `copy_slice` /
`copy_at` bulk-memory builtins for what used to be scalar inner
loops.  The f32 variant uses the full f32 stack (matvec, matvec_T,
outer_accum, conv2d_3x3_p1, adam_apply, dot_range).

| Epoch | train NLL | test acc |
|-------|-----------|----------|
|   1   | 0.2998    | 96.73 %  |
|   5   | 0.1122    | 98.18 %  |
|  10   | 0.0925    | **98.66 %** |

**97 s wall-clock**, ~65 KB binary.  Beats the MLP at half the epoch
budget with ~2× fewer parameters (101 K vs 203 K).

f32 + parallel variants:

| Demo                     | Wall  | Test acc | Notes                                |
|--------------------------|------:|---------:|--------------------------------------|
| `mnist_cnn.ari` (f64, AdamW)         |  97 s | 98.66 %  | baseline above                        |
| `mnist_cnn_f32_sgd.ari` (f32, SGD)   |  76 s | 98.24 %  | full f32 stack, plain SGD             |
| `mnist_cnn_f32.ari` (f32, AdamW)     |  76 s | 98.60 %  | unblocked by `adam_apply` VEX fix     |
| `mnist_cnn_par2_f32.ari` (f32+4-thread) | **23.2 s** | **98.65 %** | 3.27× over single-thread, 4.46-core occupancy |

The 76 s f32 vs 97 s f64 single-thread gap is bandwidth-bound — the
W_fc1 weight tensor (64 × 1568 floats) halves in size, so each Adam
step touches 401 KB instead of 802 KB and stays closer to the L2
working set.

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
  children — the supported pattern is "workers `atomic_add_i64` a
  shared done counter, parent `futex_wait`s on it" (Phase A.8).  In
  `mnist_cnn_par2_f32` this drops the parent from a 4.50 user/real
  ratio to 3.57 — same wall time, 20 s of CPU recovered per training
  run because the parent now actually sleeps during barriers.
- `atomic_add_f64` uses a `lock cmpxchg` loop.  Contention makes it
  noticeably slower than `atomic_add_i64`; prefer per-thread gradient
  buffers + a serial reduction by the parent.  As of Phase A.8 a NaN
  delta is silently dropped (a single bad gradient would otherwise
  poison the slot for the rest of the run — `mem + delta = NaN +
  finite = NaN`, and cmpxchg compares NaN-bits to NaN-bits and
  succeeds, so the slot would be permanently stuck).

---

## Test infrastructure

Two complementary suites in `aricode/tests/`:

| Suite          | Tests | Runtime | Covers                                    |
|----------------|------:|--------:|-------------------------------------------|
| `run_all.sh`   |    39 |   ~90 ms | Arithmetic, strings, arrays, error handling, file I/O, SIMD basics, imports, memory |
| `run_edge.sh`  |    55 |  ~2.5 s | Softmax scalar tails & underflow clamp, vec exp/expm1 range, f64 return-contract (xmm0 + rax), hot-var register allocation, branch peephole, short-circuit `&&`/`||`, CNN forward/backward spot checks, threading (spawn, arg-passing, i64 + f64 atomic contention, futex barrier, NaN-delta guard), f32 builtin parity (matvec, conv2d, adam_apply, mul/dot_range, exp, softmax, sparse-moment chunk advance) |

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
| `atomic_add_f64`              | shipped (`lock cmpxchg` loop, NaN-delta dropped to keep counter usable) |
| `futex_wait` / `futex_wake`   | shipped (`__NR_futex` 202 with FUTEX_PRIVATE) |
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
- **f32 `arr_f32_softmax` / `arr_f32_exp`** shipped (Phase A.8).
  Both reuse the f64 `vec_exp_body` via promote/narrow, so the
  per-iter throughput is 4 elements (not 8 like a true f32 polynomial
  body would give).  Both require `n` mod 4 = 0 — fine for power-of-
  two attention heads / hidden sizes; not yet drop-in for the
  10-class MNIST output head, which still runs scalar.

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
