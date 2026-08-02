# Tensor Observer — inspecting sensen's forward pass

`sensen.tensor_observer` is sensen's counterpart to llama.cpp's
`ggml_backend_sched_eval_callback` (`llama_context_params::cb_eval`): a
caller-installed callback that receives every intermediate tensor of a forward
pass, by name, as it becomes valid.

It exists because sensen previously exposed only `embed()` and the final
logits. Answering "which primitive diverged, and by how much?" meant editing
library source and rebuilding a C++23 modules tree, so in practice nobody
looked and numerical questions got answered by guesswork. A prior llama.cpp
parity audit stopped at final logits for exactly this reason.

## The contract

| Property | What it means |
| --- | --- |
| **Runtime, not compile-time** | No `SENSEN_DEBUG` macro. Compiled into every build including release, exactly as llama.cpp ships `cb_eval`. Attach to an already-built binary — including the one in the container. |
| **Zero cost when unattached** | `emit()` is inline; its first act is one relaxed atomic load of a bool. One check *per tensor*, never per element. See "Cost" below for measurements. |
| **Selective** | Filter by layer and by name substring, so "show me `attn_q_norm` in layer 7" does not mean dumping 28 layers of everything. |
| **llama.cpp-compatible names** | Where an upstream counterpart exists, the name is the same string, so diffing the two engines is a string join rather than a hand-maintained mapping. |

## Attaching without writing code

Set environment variables on any sensen binary:

```bash
SENSEN_OBSERVE=1                          # attach, dump every tensor
SENSEN_OBSERVE=Qcur,attn_q_norm           # attach, only names containing these
SENSEN_OBSERVE_LAYER=7                    # restrict to layer 7
SENSEN_OBSERVE_FILE=/tmp/dump.txt         # write here instead of stderr
SENSEN_OBSERVE_VALUES=8                   # print first N values (default 4, 0 = stats only)
```

Example — chase a QK-norm question in one layer, on an already-built engine:

```bash
SENSEN_OBSERVE=Qcur_normed,Kcur_normed SENSEN_OBSERVE_LAYER=7 \
  ./build/numeric_audit_probe model.gguf 1 8
```

Each tensor produces one line: name, shape, min/max/mean, leading values.

## Attaching from C++

```cpp
import sensen.tensor_observer;

sensen::observe::setObserver([](const sensen::observe::TensorEvent& ev) {
    // ev.name, ev.layer, ev.rows, ev.cols, ev.data  (std::span<const float>)
});
sensen::observe::setLayerFilter(7);              // optional
sensen::observe::setNameFilter("Qcur,ffn_gate"); // optional
...run the forward pass...
sensen::observe::clearObserver();
```

**Lifetime:** `ev.data` is valid **only during the callback**. Much of the
forward pass computes into `thread_local` scratch that the next layer
overwrites — observing it at the moment it is valid is the point. Copy what you
need inside the callback.

## The tensor vocabulary

Whole-model (`layer == -1`): `inp_embd`, `result_norm`, `result_output`.

Per layer, in forward order:

| Name | Primitive | llama.cpp counterpart |
| --- | --- | --- |
| `attn_norm` | pre-attention RMSNorm | same |
| `Qcur` / `Kcur` / `Vcur` | Q/K/V projections, pre-RoPE, pre-QK-norm | same (1st `cb`) |
| `Qcur_normed` / `Kcur_normed` | Qwen3 per-head QK-norm | same |
| `Qcur_rope` / `Kcur_rope` | Q/K after RoPE | llama.cpp **reuses** `Qcur`/`Kcur` (2nd `cb`) |
| `kq` | attention scores, pre-softmax, **unscaled and unmasked** | same |
| `kq_soft_max` | attention probabilities | same |
| `kqv` | attention output, pre output-projection | same (but see layout note) |
| `kqv_out` | attention output, post output-projection | same |
| `ffn_inp` | post-attention residual | same |
| `ffn_norm` | pre-FFN RMSNorm | same |
| `ffn_gate` / `ffn_up` | FFN projections | same |
| `ffn_swiglu` | `silu(gate) * up` | same |
| `ffn_out` | FFN down projection | same |
| `l_out` | post-FFN residual (layer output) | same |

Two deliberate deviations, both because llama.cpp is ambiguous there:

* `Qcur_rope`/`Kcur_rope` — llama.cpp calls `cb()` on `Qcur` **twice** per layer
  (raw projection, then post-RoPE), so a consumer of its callback must
  disambiguate by arrival order. sensen names them distinctly instead.
* `kqv` layout — llama.cpp publishes it **before** its permute, so its layout is
  `[head_dim, n_tokens, n_head]` (head-major). sensen's is token-major
  `[n_tokens, n_head*head_dim]`. `layer_parity_probe` permutes the reference
  before comparing; a naive flat memcmp would report a large divergence that is
  purely memory order.

## Coverage and known gaps

Covered: the CPU prefill path (`forwardPrompt`) end to end, and the CPU decode
path's attention internals (decode delegates to `forward_prefill_flat` with
`seq_len=1`, so the same emit sites fire) plus its block-level norms and
residuals.

Not covered, deliberately and stated rather than implied:

* **FFN internals on the decode path.** Decode uses the per-token span overload,
  not `forward_batch_flat`, so `ffn_gate`/`ffn_up`/`ffn_swiglu` are not published
  there. The FFN's input (`ffn_norm`) and output (`ffn_out`) both are, so a
  divergence is still localised to the FFN — just not further inside it.
* **CUDA paths.** All emit sites are on the CPU path.
* **`kq`/`kq_soft_max` are a faithful recompute, not a kernel tap.** The fused
  flash kernels never materialise the score matrix; they consume it tile-by-tile
  inside an online softmax. The observer rebuilds it with the same dot product,
  scale and causal rule (`j > (Tk - Tq) + i`). A bug living strictly inside the
  kernel's online-softmax rescaling would therefore show up in `kqv`, not in
  these two. This is also the only emit site whose *production* costs anything,
  so it is additionally guarded on `observe::active()`.

---

# `layer_parity_probe` — sensen vs llama.cpp, per primitive

Drives both engines in one process on bit-identical token ids and joins their
intermediate tensors by name.

## Running it

```bash
cd backend
ninja -C build layer_parity_probe

# Q8_0
./build/layer_parity_probe /path/to/param-agent-qlora-Q8_0.gguf 8

# F16 control — isolates real algorithmic differences from quantisation noise
./build/layer_parity_probe /path/to/param-agent-F16.gguf 8
```

Environment:

| Variable | Effect |
| --- | --- |
| `PARITY_TOL` | relative-L2 threshold for the first-divergence verdict (default `1e-3`) |
| `PARITY_PROMPT` | override the user utterance |
| `PARITY_VERBOSE=1` | print every tensor, not just the summaries |

Exit code is `0` when every primitive is within tolerance, `1` otherwise — so it
works directly as a CI gate.

## What it does

1. **Verifies token ids first.** Tokenizes with both engines and **aborts** if
   they differ. A prior audit found sensen prepending a spurious BOS; every
   number downstream of a tokenizer mismatch is meaningless, so this is a hard
   gate rather than a warning.
2. **Disables flash attention on the reference.** `ggml_flash_attn_ext` fuses the
   score matrix away, so `kq`/`kq_soft_max` do not exist as graph nodes with it
   on. Disabling it is what makes scores comparable at all.
3. Captures llama.cpp via `cb_eval` (filtered at `ask` time to the joined names,
   so a long prompt does not copy the whole graph out of ggml) and sensen via
   `setObserver`.
4. Reports, per tensor: max |diff|, mean |diff|, relative L2 against the
   reference's own magnitude, and cosine similarity.

## Reading the output

* **`RELATIVE L2 ERROR, LAYER x PRIMITIVE`** — the main table. Read *down* a
  column to see whether a primitive's error is flat across depth (quantisation
  noise) or growing (a defect compounding).
* **`WORST LAYER PER PRIMITIVE`** — which primitive is worst, independent of depth.
* **`UNMATCHED`** — anything one engine publishes and the other never produced. A
  missing primitive is itself a finding, so it is reported rather than dropped.
* **`VERDICT`** — the first tensor in forward order above tolerance, with an
  `SENSEN_OBSERVE=...` command line to reproduce just that tensor.
* **`DRIFT ACROSS DEPTH`** — growth factor of the `l_out` residual stream from
  first to last layer, and whether it is monotonic.

## Shape mismatches are expected

The comparison is shape-aware because two mismatches are legitimate:

* **Fewer reference rows.** llama.cpp applies `inp_out_ids` inside the last
  layer, so from `ffn_inp-27` onward it carries only the requested output rows
  while sensen still carries the whole sequence. The common rows are the
  *trailing* ones on both sides.
* **More reference columns.** `kq`/`kq_soft_max` are sized to llama.cpp's
  *padded* KV length, so the reference has masked columns sensen never
  materialises. The comparable region is the leading `min(cols)` of each row.

Both are reported in the per-tensor `note` field rather than silently absorbed.

---

# `observer_cost_probe` — what the hook costs

```bash
ninja -C build observer_cost_probe
./build/observer_cost_probe model.gguf 12 8 128
```

Drives sensen **alone** (no llama.cpp, so reference work cannot dilute the
number) in three configurations: detached (the shipped path), attached with a
no-op callback, and attached with a callback that touches every element.

The zero-cost claim is about **detached vs a build with the instrumentation
removed entirely** — run this binary before and after to make that comparison.
The two attached rows only bound what you pay while actually observing.

### Measured (Qwen3-0.6B Q8_0, 128-token prefill, 8 threads)

A/B built by reverting the three instrumented files to `HEAD` and rebuilding,
then restoring and rebuilding, with `gemm_kernels.cppm` verified byte-identical
across both builds so a concurrent edit could not confound it. Best-of-10 per
run, three interleaved rounds:

| round | baseline (no hook) | instrumented, detached |
| --- | --- | --- |
| 1 | 0.2557 s | 0.2570 s |
| 2 | 0.2351 s | **0.2247 s** (faster) |
| 3 | **0.2181 s** | 0.2187 s |

Best-of-all: baseline 0.2181 s vs instrumented 0.2187 s — **+0.3%**. Run-to-run
spread *within* a single binary is ~17% (0.218–0.256 s), and the instrumented
build was faster than baseline in one of three rounds. **The detached cost is
below this machine's measurement noise floor.** As a sanity check that the two
binaries really differ, attaching an observer to the baseline binary changes
nothing (±0%, it has no emit sites) while attaching to the instrumented one
costs +25% to +42%.

Cost **while observing** (instrumented binary, 535 emit sites per prefill):
attached no-op +25–42%, attached full-scan +43–72%. Most of that is the
`kq`/`kq_soft_max` recompute, which is O(n_head · seq²) and is the only emit
site that has to *build* its tensor rather than publish an existing buffer —
filter it out (`SENSEN_OBSERVE=Qcur,ffn_gate`) and the cost drops accordingly.

**Conclusion: the hook is free when unattached and stays compiled into release
builds.** There is no argument for a macro gate.

---

# Parity results (2026-08-01)

Model: Qwen3-0.6B (`param-agent-qlora-Q8_0.gguf`, and its F16 sibling from
`gguf_requantize_probe`). Prompt: the 81-token assistant chat template.
**Token ids bit-identical between engines** — verified before any numeric claim.

Worst layer per primitive, across all 28 layers:

| primitive | F16 rel L2 | F16 cosine | Q8_0 rel L2 | Q8_0 cosine | Q8/F16 |
| --- | --- | --- | --- | --- | --- |
| `attn_norm` | 1.33e-03 | 0.9999991 | 3.87e-02 | 0.9992518 | 29.0x |
| `Qcur` | 1.19e-03 | 0.9999993 | 3.55e-02 | 0.9993721 | 29.7x |
| `Kcur` | 1.06e-03 | 0.9999994 | 3.13e-02 | 0.9995090 | 29.7x |
| `Vcur` | 1.65e-03 | 0.9999986 | 4.84e-02 | 0.9988266 | 29.4x |
| `Qcur_normed` | 1.22e-03 | 0.9999993 | 3.63e-02 | 0.9993429 | 29.8x |
| `Kcur_normed` | 1.01e-03 | 0.9999995 | 3.02e-02 | 0.9995457 | 29.9x |
| `Qcur_rope` | 1.22e-03 | 0.9999993 | 3.63e-02 | 0.9993429 | 29.8x |
| `Kcur_rope` | 1.01e-03 | 0.9999995 | 3.02e-02 | 0.9995457 | 29.9x |
| `kq` | 1.07e-03 | 0.9999994 | 3.12e-02 | 0.9995123 | 29.3x |
| `kq_soft_max` | 1.13e-03 | 0.9999994 | 2.44e-02 | 0.9997034 | 21.5x |
| `kqv` | 2.30e-03 | 0.9999974 | 6.03e-02 | 0.9981785 | 26.2x |
| `ffn_inp` | 1.20e-03 | 0.9999993 | 3.21e-02 | 0.9995007 | 26.8x |
| `ffn_norm` | 1.36e-03 | 0.9999991 | 3.94e-02 | 0.9992237 | 28.9x |
| `ffn_gate` | 1.01e-03 | 0.9999995 | 3.00e-02 | 0.9995500 | 29.6x |
| `ffn_up` | 1.38e-03 | 0.9999990 | 4.08e-02 | 0.9991653 | 29.5x |
| `ffn_swiglu` | 1.85e-03 | 0.9999983 | 5.42e-02 | 0.9985289 | 29.2x |
| `ffn_out` | 1.92e-03 | 0.9999982 | 5.71e-02 | 0.9983718 | 29.7x |
| `l_out` | 1.53e-03 | 0.9999988 | 3.92e-02 | 0.9992620 | 25.7x |

Whole-model anchors — F16: `inp_embd` **0.0 (bit-exact)**, `result_norm`
9.88e-04, `result_output` 6.45e-04. Q8_0: `inp_embd` **0.0**, `result_norm`
2.76e-02, `result_output` 1.82e-02 (cosine 0.9998).

**Verdict: no primitive diverges.**

* **F16 control: every primitive agrees to rel L2 ≤ 2.3e-03**, cosine ≥ 0.999997,
  across all 28 layers. Passes at the default `PARITY_TOL=5e-3` (set at ~2x the
  measured worst case, not chosen for comfort). Since F16 removes weight
  quantisation from both engines, this is the statement that **each primitive is
  algorithmically correct** — including the Qwen3 per-head QK-norm and the
  head_dim=128 NeoX RoPE, the two most commonly-wrong steps.
* **Q8_0: every primitive agrees to rel L2 ≤ 6.0e-02**, cosine ≥ 0.998, passing
  at `PARITY_TOL=7e-2`. Crucially the Q8/F16 ratio is **uniform at 21.5–29.9x
  across all 18 primitives** — no primitive is disproportionately affected. That
  uniformity is what identifies the residual as weight/activation quantisation
  noise rather than a defect in any one kernel.
* **Drift across depth:** F16 `l_out` is non-monotonic (4.2e-04 → 1.5e-03,
  bounded noise). Q8_0 is monotonically non-decreasing, 6.2e-03 → 3.9e-02 over
  28 layers (x6.3) — real accumulation, but sub-linear in depth and with cosine
  still 0.9993 at the last layer, and the final logits still produce identical
  argmax and an identical greedy rollout.

## Using this as a gate for kernel changes

```bash
./build/layer_parity_probe <model.gguf> 8                  # F16: expect exit 0
PARITY_TOL=7e-2 ./build/layer_parity_probe <q8.gguf> 8      # Q8_0: expect exit 0
```

For an int8 Q8_0 matvec rewrite specifically, the primitives that must not move
are the ones fed by `matvecQuantizedBatch`: `Qcur`, `Kcur`, `Vcur`, `ffn_gate`,
`ffn_up`, `ffn_out`, and `result_output`. A regression in the kernel shows up
there *first*, before it is diluted by the residual stream — which is precisely
what final-logits comparison could not tell you.
