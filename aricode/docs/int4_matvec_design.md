# `arr_i4_matvec_f32_perrow` — design notes for the future SIMD builtin

## Why

Today `pack.py --int4-linear` emits an INLINE matmul:

```
while j < out_f:
    scale = scales[j]
    acc = 0.0
    while i < in_f / 2:
        raw = byte_at(W, j * (in_f/2) + i)
        lo  = raw % 16;       hi  = raw / 16
        los = ((lo+8) % 16) - 8
        his = ((hi+8) % 16) - 8
        acc += los * x[2i] + his * x[2i+1]
    y[j] = acc * scale + b[j]
```

That's correct but ~5× slower than the int8 builtin because:
- Pure scalar f32 ops (no AVX2)
- Per-element modulo / division for nibble extraction
- Per-element sign-extend formula evaluation

For the 32M aricode-tiny it costs 3.2 s per 64-token completion on
the LM head alone.  A proper builtin would push that under 1 s
(matching the int8 path's ~25 ms total).

## API

```
arr_i4_matvec_f32_perrow(W_i4, x_f32, y_f32, m, scales_f32)
```

Same signature as `arr_i8_matvec_f32_perrow` modulo the W buffer
format:
- W_i4 : packed-nibble byte buffer, length `m * (n/2)` bytes,
         where n = arr_len(x_f32).  Low nibble = even column,
         high nibble = odd column (matches the staging in
         pack.py main()).
- x, y, scales : identical to the int8 variant.

Returns 0.  Computes `y[j] = scales[j] · Σ_i W[j,i] · x[i]`
where W[j,i] is the sign-extended int4 nibble at byte offset
`j * (n/2) + (i/2)`, low nibble if i even, high if odd.

## Constraints

- `n` (= x length) MUST be even.  Caller is responsible.  Document
  in the SystemExit emitted by pack.py if input dim is odd (already
  done for --int4-embedding and --int4-linear).
- W length bounds check: `m * (n/2) <= arr_len(W_i4)`.  Mirror the
  int8 variant's runtime SystemExit message format
  ("arr_i4_matvec_f32_perrow W length < m * n/2").

## Algorithm — vectorised lane (AVX2, 16 nibbles per iteration)

Per row j:
1. Broadcast `scales[j]` to `xmm5` (already-saved register from int8
   kernel).
2. Zero `ymm0` (accumulator).
3. Loop over `i` in steps of 16 nibbles (= 8 bytes):
   a. `vmovq xmm1, [r13 + i/2]` — load 8 packed bytes (16 nibbles).
   b. Split into low + high nibbles in parallel:
      - `vpand xmm2, xmm1, 0x0F mask` → low nibbles (one per byte)
      - `vpsrlw xmm3, xmm1, 4`         → high nibbles in high half
      - `vpand xmm3, xmm3, 0x0F mask` → high nibbles aligned low
   c. Sign-extend nibbles to int8 (one of):
      - Subtract 0x08 then XOR with 0x08 (branchless)
      - OR: compare-greater-than 7 (`vpcmpgtb` against `0x07`),
        AND result with `0xF0`, OR into the nibble.  This sets the
        upper 4 bits to all-1 for values 8..15, mapping them to
        -8..-1 as int8.  This is the standard SIMD sign-extend
        trick — only 2 instructions, no subtraction needed.
   d. Interleave low + high back into the original order
      (`vpunpcklbw`) → 16 sequential int8 values.
   e. Promote int8 → int32 → f32 in 4 lanes of 4:
      `vpmovsxbd ymm4, xmm2_low4` → ymm4 has 8 int32 values
      `vpmovsxbd ymm6, xmm2_top4` (shuffle / VEX permute) → 8 more
      `vcvtdq2ps ymm4, ymm4` etc → f32
   f. Load corresponding f32 x lanes: `vmovups ymm7, [rsi + i*4]`
      and the next 8.
   g. `vfmadd231ps ymm0, ymm4, ymm7` (and same for the second
      8-lane block).
4. Horizontal-sum ymm0 → xmm0 → ss scalar
5. Multiply by xmm5 (scale).  Store to y[j].
6. Tail: handle leftover nibbles 0..15 with scalar loop
   (same shape as the int8 variant's tail).

The trick is step (c) using vpcmpgtb — that's the cheapest
parallel sign-extend from 4-bit-to-int8 on AVX2.

## Comparison to T-MAC

Microsoft Research's T-MAC (2024) goes further by also quantising
ACTIVATIONS to int8 and using a precomputed 4 KB LUT over the
(int4, int8) product space.  That eliminates the f32 multiply
entirely — the inner loop is just memory loads.  For pure-CPU
deploy this is the speed ceiling.  But it requires:
- An int8 quantisation pass on the activations on every layer.
- A `vpgatherdd`-friendly LUT layout.
- 4 KB of state per matmul block.

The builtin proposed above is simpler and gets within ~2× of
T-MAC at the cost of keeping activations in f32.  For aricode-tiny
that's the right trade — our activations aren't the bottleneck.

## Where to land it in the source tree

```
aricode/src/codegen/codegen_builtins.c  — emit the bytes
aricode/src/semantic/analyzer.c          — register builtin signature
aricode/src/codegen/hot_var.c             — mark args as hot (RAX/RDI)
aricode/tests/test_perrow.ari            — extend with int4 case
```

Pattern: copy/paste from the i8 perrow variant.  All four files
have a `arr_i8_matvec_f32_perrow` site that maps 1:1 to a new
`arr_i4_matvec_f32_perrow` site — the surrounding plumbing
(arg unpacking, stack layout, bounds check, register save/restore)
is identical.

## Test plan

1. Unit test (`tests/test_perrow.ari`): hand-construct a small
   matrix with known int4 packing, multiply by a known input,
   check that `arr_i4_matvec_f32_perrow(...)` produces the same
   result as the inline reference.
2. Smoke test on aricode-tiny: re-pack with `--int4-linear` (the
   existing flag), confirm pack.py's emit_linear has been updated
   to call the new builtin instead of emitting the inline loop.
3. Performance: 64-token completion should drop from ~4 s back to
   <1 s (close to the int8 baseline of 0.8 s).
4. Quality: identical bit-for-bit output as the inline path,
   since the math is the same; only the execution is faster.

## Plumbing in pack.py

After the builtin lands, replace the inline block in `emit_linear`
(bits=4 branch, line ~163 of aricode_pack.py) with:

```python
return [
    f"    arr_i4_matvec_f32_perrow({w_var}, {src_var}, "
    f"{dst_var}, {out_f}, {w_var}_scales);",
    f"    arr_f32_add_scaled({dst_var}, {b_var}, 1.0);",
]
```

Keep the inline path as a fallback emitted when an
`--inline-int4-matmul` debug flag is set — useful for differential
testing during the builtin implementation.

## Status (2026-05-15) — SHIPPED

- Inline path: shipped in `bc8b735` of aricode-stdlib/aricode-ml
- **SIMD builtin: SHIPPED in `aa8c738` of aricoderoot.**
  Implemented as the scratch-unpack + cloned-int8-AVX2-dotproduct
  variant described above (not the in-register vpcmpgtb unpack —
  that remains a possible future micro-opt but the scratch
  approach already delivers the target speedup with far less
  machine-code risk).
- pack.py `emit_linear` bits=4 path now emits the builtin call
  instead of the inline scalar loop.
- Verified: `test_i4_perrow.ari` exact; aricode_tiny_qat 64-token
  completion 4.02 s → 1.03 s (3.9x), bit-identical generation.
- Residual gap vs int8's 0.8 s = embedding + attention (f32/int8,
  out of scope for this builtin).

### Possible future micro-opt (not needed)

The in-register AVX2 unpack (vpand + vpcmpgtb sign-extend, no
scratch round-trip) would shave the unpack cost further, but the
unpack is already a small fraction of total time vs the FMA loop.
Not worth the machine-code risk unless profiling shows the
scratch store/load as a bottleneck (it isn't at our matvec sizes).
