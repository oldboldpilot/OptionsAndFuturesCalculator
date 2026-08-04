# sensen CPU decode: memory-layout & bandwidth architecture

@author Olumuyiwa Oluwasanmi
Date: 2026-08-04 · sensen HEAD `f73c0633` · AMD Ryzen 9 9955HX (Zen 5, 16C/32T, 2×32 MB L3, DDR5-5600 ×2 = 89.6 GB/s theoretical)

## 0. Executive summary

**The headline "38% of peak" is an accounting artifact, and the GEMV kernels are not the problem.**
Measured on this box:

1. **Achievable read bandwidth is ~54 ± 3 GB/s, not 89.6.** A pinned, multi-threaded,
   64-byte-vector STREAM-style read sweep tops out at 53.8–57.3 GB/s at 16 threads
   (46.4 GB/s from a *single* Zen 5 core). 34 GB/s achieved is therefore **~60 % of
   achievable**, not 38 % of anything reachable.
2. **The Q8_0 weight sweep already runs at 90–100 % of that achievable ceiling when it
   is running.** A cycle-accurate replica of the production kernel + layout + FlatPool +
   partitioning sweeps the full 633 MB Qwen3-0.6B weight set at **49–52 GB/s (77–85
   tok/s equivalent) at 16 threads** — far above production's 51–57 tok/s. The layout
   (transposed block-column-major, 34-byte interleaved blocks) is *good*: its access
   pattern measures identical to pure sequential streaming (56.5 vs 53.8 GB/s).
3. **The real gap is duty cycle, not efficiency: ~4.3–6.1 ms of every ~18 ms token is
   serial-or-near-serial work during which DRAM sits idle.** Direct perf decomposition
   of the real engine (`decode_golden_probe`, 765 decoded tokens): decode attention is
   **single-threaded** (`compute_attention_paged_flat`, one `for (h)` loop on the
   calling thread — `src/multi_head_attention.cppm:3982`) and costs ~2.3 ms/token at
   ~170-token contexts, *growing linearly with context*; ~0.9 ms/token is spent in
   `sinf/cosf/precompute_freqs_cis` at decode time; ~0.6 ms in `memmove`; ~0.9 ms in
   `parallel_for` caller-side dispatch; plus sampler/norm/quantize residue.
4. Six of the seven candidate causes in the brief are **measured dead** (§5). The one
   confirmed cause is FlatPool-adjacent but not the barrier itself: the 21.8 %/25.1 %
   "thread proxy" time is workers *spinning while the caller runs serial sections* —
   the barrier round trip itself costs 2.1 µs × ~200 regions ≈ 0.4 ms/token (~2 %).
5. **Recommended plan** (§6, all changes hold the bit-exactness and thread-invariance
   contract): parallelize decode attention over heads (U1), kill the decode-time RoPE
   trig recompute (U2), trace and remove the memmove (U3), raise deployed threads
   4→8–12 (U5, config only, +10–14 % already measured by the coordinator). Projected:
   **~+20–25 % tokens/s at short context, and removal of an O(context) serial term
   that would otherwise dominate at 2048-ctx**. The glamorous options — planar/512-bit
   VNNI layout, int8 lm_head, NT hints, CCD-aware placement — were built, measured,
   and are reported here as **kills** with numbers, so nobody re-does them.

Everything below is anchored to file:line and to runnable benchmarks in the scratchpad
(`bench.cpp`, `exact.cpp`; provenance in §8).

---

## 1. The decode GEMV path, traced

Decode (batch 1) for the strategy assistant runs entirely on the CPU path:

| Stage | Location |
|---|---|
| Per-layer projections (q/k/v/o, gate/up, down) | `multi_head_attention.cppm:3029–3049,3250` / `transformer_block.cppm:196–247` → `GEMM::matvecQuantizedBatch` (m=1) `gemm_kernels.cppm:3916` → `matvecQuantizedTiled` `:4003` |
| Activation quantization (once per matvec, on the dispatching thread) | `quantizeActivationQ8` `gemm_kernels.cppm:3726`, called at `:4017` |
| Row partitioning | `quantRowGrain(n, nt)` `gemm_kernels.cppm:3997` — one chunk per worker, rounded up to a multiple of 4 rows |
| Fork/join | `sensen::parallel::parallel_for` → FlatPool `src/parallel.cpp:94–297` (epoch/ticket packed in one 64-bit atomic, CAS chunk claim, ~350 µs spin then park, caller participates) |
| Hot kernel | `matvecQ8_0_Int8Slice` `gemm_kernels.cppm:4494` → `q8_0_Int8RowTile_VNNI<32/8/1>` `:4320–4370` (256-bit `vpdpbusd`, sign-trick, per-block fp32 scale FMA) |
| Runtime dispatch | `q8Int8Tier()` `gemm_kernels.cppm:3766–3772` over `detectCpuFeatures()` (`cpu_features.cppm:271`, `selectBestSimdLevel` `:280`); tiers VNNI (`:4320`) → AVX-512BW (`:4385`) → AVX2 (`:4439`) → scalar (`:4535`). Any change must slot into these tiers. |
| lm_head (165 MB of the 639 MB/token, 26 % of traffic) | `LlamaModel::try_load_quantized_lm_head` `llama_model.cpp:6894` keeps GGUF-native **row-major** blocks; `GEMM::matvecQuantized` `gemm_kernels.cppm:3447` → `matvecQuantizedSlice` `:5589` → `dotQ8_0_AVX512` `:5778` (fp32 dequant + FMA, 4 independent zmm accumulators) |
| Decode attention (not a GEMV) | `compute_attention_paged_flat` `multi_head_attention.cppm:3921`, called with `seq_len=1` from `:3144`; **serial** head loop at `:3982` (second variant `:4262`) |

Two weight layouts coexist, deliberately:

* **Layer weights — transposed block-column-major**, built once at load by
  `GEMM::transposeQuantizedWeights` (`gemm_kernels.cppm:3547–3568`, callers
  `multi_head_attention.cppm:3401`, `transformer_block.cppm:338`, `autograd.cppm:4484`):
  byte layout `b[bc][row][34]` — for block-column `bc` (32 k-elements), all `n` rows'
  34-byte blocks are adjacent. A 32-row tile reads **1088 contiguous bytes** (exactly 17
  cache lines, zero fill waste), then strides `n*34` to the next block column.
* **lm_head — GGUF-native row-major**: each output row is `k/32 × 34` contiguous bytes
  (1088 B at k=1024); the sweep over rows is fully sequential streaming.

Layout facts that answer the brief's questions directly:

* **Scales are interleaved with quants** (2-byte fp16 header + 32 int8 per 34-byte
  block) — but this is **one stream, not two**; the scale sits inside the same cache
  lines the quants occupy.
* **Repacking at load: yes** (transpose above). No further repack at inference.
* **Access stride per thread**: contiguous 1088 B per (tile, block-column), stride
  `n*34` B between block columns (34.8 KB at n=1024); threads own disjoint contiguous
  row ranges (`quantRowGrain`), so no weight cache line is read by two threads except
  at slice boundaries.
* **Alignment**: quant loads are 32-byte `loadu` at offset ≡2 (mod 34) — roughly half
  cross a 64-byte line. Zen 5's split-load penalty is ~1 cycle and invisible under the
  memory-bound regime (measured: §5.1).
* **Prefetching: present.** Software prefetch of the tile 4 block-columns ahead, hint
  T0/locality-3 (`gemm_kernels.cppm:4346–4353`); lm_head prefetches next row
  (`:5630–5633`, with a doc comment recording that `prefetchnta` measured *harmful*
  there: 4.9 vs 8.9 GB/s).

---

## 2. Baseline (coordinator-measured, HEAD f73c0633, deployed model)

Threads → tok/s (median of 3): 1→26.0, 2→40.2, 4→46.3, 8→50.8, 12→52.8, 16→50.8,
24→50.9, 32→42.5. The curve **flattens at 8 threads** — saturation, so thread tuning
beyond 8–12 is worthless and the only levers are bytes moved and duty cycle.
639 MB/token × 51 tok/s ≈ **32.6 GB/s** achieved.

The CPU decode path is **bit-identical across 1/4/8/16 threads** (verified by the
coordinator and independently re-verified here twice: a 16-thread `decode_golden_probe
--check` run against a 1-thread golden matched all 3 prompt digests on the stock
model, and a 16-thread run on the **deployed** model matched the coordinator's
4-thread golden `golden_f73c0633_t4.txt` bit-for-bit — 393 tokens in 8.1 s wall
≈ 48.5 tok/s on this session's slightly-loaded box, consistent with the 50.8 baseline). This holds
because every parallel split is over *independent output rows/heads*; each row's
reduction runs entirely inside one worker in a fixed order. **This is a hard
constraint on every proposal below**: no partition over the k/reduction dimension, no
dynamic re-chunking of a reduction. All proposed units partition over outputs only.

## 3. The true ceiling (measured, this box)

STREAM-like read benchmark (`bench.cpp stream`; 640 MB working set, write-initialized,
64-byte vector loads, threads pinned to physical cores; run twice, ±5–7 % run-to-run
with a light ~0.4-core background load present):

| pattern | 1T | 4T | 8T | 16T |
|---|---|---|---|---|
| sequential | 46.4 | 49.2 | 49.2 | **53.8** |
| tile (1088 B chunks @ 34 816 B stride — the exact kernel walk) | 43.5 | 47.8 | 48.6 | **56.5** |
| tile + prefetchnta | — | — | 48.5 | 55.7 |
| sequential + prefetchnta | — | — | — | 57.3 |
| sequential, 8T all on CCD0 | — | — | 48.9 | — |
| sequential, 16T unpinned | — | — | — | 50.5 |

**Achievable read bandwidth ≈ 54 ± 3 GB/s — 60 % of the 89.6 GB/s theoretical.** The
remaining 40 % is the platform (2-channel DDR5 efficiency, refresh, page management);
no software change recovers it.

Consequences:

* Absolute ceiling for this model: 639 MB ÷ 54 GB/s = **11.8 ms/token = ~85 tok/s** if
  every non-sweep cost were zero.
* Production's 32.6 GB/s is **~60 % of achievable** — that 40 % is the actual prize,
  worth up to ~+65 % tok/s if fully closed (it won't be; realistic target §6).

## 4. The sweep itself is nearly at the ceiling (replica evidence)

`bench.cpp decode` replicates, instruction-for-instruction: the
`q8_0_Int8RowTile_VNNI<32/8/1>` kernel, the transposed 34-byte layout, the FlatPool
(verbatim copy incl. `kSpinRounds = 1<<15`), `quantRowGrain` one-chunk-per-worker
partitioning, and the per-projection region structure (7 regions/layer × 28 + lm_head
≈ 197 regions/token) over the exact Qwen3-0.6B shapes (633 MB synthetic weight set).

| variant | 1T | 4T | 8T | 16T |
|---|---|---|---|---|
| v256 = production structure | 43.0 tok/s (27.2 GB/s) | 72.9 (46.1) | 75.0 (47.5) | 77.4–82.3 (49–52.1) |
| no software prefetch | — | — | — | 84.3 (53.4) |
| prefetchnta instead of T0 | — | — | — | 82.5 (52.2) |
| 4 chunks/worker instead of 1 | — | — | — | 78.1 (49.5) |
| **mono**: 1 region/token instead of 197 | — | — | — | 85.2 (53.9) |
| **v512**: planar scales+quants, 512-bit VNNI | 49.2 (31.1) | 67.6 (42.8) | 70.0 (44.3) | 81.0 (51.3) |

Readings:

* At ≥4 threads the production sweep machinery reaches **85–95 % of the achievable
  ceiling in isolation**. There is no big win inside the GEMV kernel or its layout.
* Region overhead (197 barriers vs 1) costs **~3.5 %** end to end. The FlatPool
  empty-region round trip measured directly: **2.08 µs at 16T, 0.30 µs at 4T**
  (`bench.cpp barrier`) → ~0.4 ms of an ~18 ms token.
* lm_head fp32-dequant path (`dotQ8_0_AVX512` replica): 56.0 GB/s @4T, 75.0 @16T
  (apparent >ceiling because 64 MB of L3 covers 39 % of the 165 MB matrix across
  repeated sweeps) — i.e. **bandwidth-saturated at production thread counts**. An int8
  sign-trick row-major variant measured *equal or slower* (54.7 @4T, 72.3 @16T, and
  at 1T fp32 22.9 vs int8 18.0 GB/s — the fp32 kernel's 4 independent accumulators
  out-MLP the single-accumulator int8 loop).

## 5. Hypothesis adjudication (ranked by measured evidence)

### Killed

1. **"34-byte stride causes cache-line splits / wasted fill" — DEAD.** The transposed
   layout makes reads contiguous 1088-B runs (17 exact lines per 32-row tile); the
   tile-stride pattern measures *equal to sequential* (56.5 vs 53.8 GB/s @16T). Only
   misaligned-load micro-penalties remain, invisible in a memory-bound loop.
2. **"Scale/quant interleaving forces two streams" — DEAD.** It is one stream (§1). The
   planar split was still built and measured (v512): **bit-exact** (see §6.U6 gate) but
   −7 % @4T, ~0 @16T, +14 % only at 1T. No production win.
3. **"No software prefetch" — DEAD (wrong premise).** Prefetch exists
   (`gemm_kernels.cppm:4346`), distance 4 block-columns, hint 3. Removing it at 16T
   *gained* ~2 % (within noise) — at saturation prefetch instructions are pure
   overhead, but not enough to act on. Do not add more prefetching.
4. **"Thread partition splits a matrix across both CCDs, doubling L3 fill" — DEAD.**
   Threads read disjoint row ranges — no shared weight lines to duplicate. Empirically
   8 threads confined to CCD0 = 48.9 GB/s vs 8 spread = 49.2. Reads don't pay a
   cross-CCD coherence tax; both CCDs share the same 2 DRAM channels through the IO
   die, so placement is bandwidth-neutral.
5. **"False sharing on output accumulators" — DEAD by arithmetic and by proxy
   measurement.** Output writes are ≤633 KB/token vs 639 MB read (<0.1 % of traffic);
   slice boundaries are 16-byte-aligned 4-row multiples, written once per 32-row tile.
   The chunks4 variant (4× more boundaries) moved −5 %, attributable to scheduling not
   sharing; kernels show no store-stall signature.
6. **"Non-temporal load opportunity" — DEAD.** x86 has no NT *load* semantics from WB
   memory; the realizable form (prefetchnta to bias eviction) measured neutral on the
   tile pattern (55.7 vs 56.5) and was previously measured actively harmful on the
   lm_head row sweep (comment at `gemm_kernels.cppm:5624–5633`). The theoretical
   benefit (protecting L3 for KV) is worthless today because the KV working set at
   short context is ≪ L3, and at long context the fix is Q8 KV (§6.U9), not hints.
7. **"lm_head fp32-dequant path is the laggard" — DEAD** at production thread counts
   (§4). Leave `dotQ8_0_AVX512` and the row-major lm_head exactly as they are.

### Confirmed (the actual gap)

8. **FlatPool per-op fork/join — PARTIALLY confirmed, reframed.** The barrier
   *mechanics* are cheap (2.1 µs/region; 197-region vs 1-region = 3.5 %). The 21.8 %
   (parent's measurement) / **25.1 %** (this session, §5.1) "thread proxy" CPU share is
   **workers spinning while the caller runs serial sections** — it is a *symptom* of
   the serial residual, not a scheduler defect. Fixing the pool does ~nothing; fixing
   the serial sections removes the spin automatically.
9. **Serial residual ≈ 4.3–6.1 ms of every ~18 ms token (24–35 % of wall) — THE
   finding.** Decomposition below.

### 5.1 Perf decomposition of the real engine

`decode_golden_probe` (built at `backend/build/decode_golden_probe`, source
`backend/src/decode_golden_probe.cpp`), stock `Qwen3-0.6B-Q8_0.gguf` (byte-layout
identical to the deployed fine-tune), 3-prompt battery, 765 decoded tokens.

**1 thread (CPU time = wall time; 31.6 ms/token):**

| symbol | share | ms/token |
|---|---|---|
| `q8_0_Int8RowTile_VNNI<32>` (layer sweep) | 54.9 % | 17.4 |
| `dotQ8_0_AVX512` (lm_head) | 21.8 % | 6.9 |
| `q8_0_Int8Batch_VNNI<8,8>` (prefill) | 5.0 % | 1.6 |
| attention: `compute_attention_paged_flat` + `attn_kernels::{dot,fma}_q8_avx512` + `kvq8::quantise_group_q8` | 7.6 % | 2.4 |
| RoPE trig: `__sinf_fma` + `__cosf_fma` + `RotaryEmbedding::precompute_freqs_cis` | 2.7 % | 0.86 |
| `__memmove_avx512_unaligned_erms` | 1.9 % | 0.59 |
| remainder (sampler, norms, softcap, dispatch, …) | ~5 % | ~1.6 |

**16 threads (wall 13.5 s, user 194.6 s → 17.6 ms/token, 57 tok/s on this battery):**
`RowTile<32>` 104 cpu-s, FlatPool thread proxy (spin) **48.8 cpu-s**, `dotQ8_0` 35
cpu-s, everything else ≈ 5.1 cpu-s. Wall model: GEMV ≈ (104+35+1.4)/16 ≈ 8.8 s fully
parallel (≈54 GB/s while active — at ceiling), leaving **4.7 s (35 %) of
low-parallelism wall**; the spin total independently brackets the fully-serial
equivalent at 48.8/15 ≈ 3.3 s. Attention cpu-seconds are the *same absolute* at 1T and
16T (≈1.8 cpu-s ≈ 2.3 ms/token) — direct proof it runs serial. Same for the RoPE trig
(~0.6 ms/token) and memmove (~0.5 ms/token). `parallel_for` caller-side dispatch
(std::function + chunk setup) shows 0.72 cpu-s ≈ 0.9 ms/token.

**The serial attention term grows linearly with context.** At the probe's ~170-token
average it is 2.3 ms; extrapolated to 2048-token contexts it is ~25–30 ms/token —
larger than the entire weight sweep. This is the single most future-proof item in the
plan.

---

## 6. Proposed architecture: work units

Ordering is (measured gain ÷ risk). Every unit's correctness gate is the existing
**golden probe**: `decode_golden_probe --check` against
`scratchpad/golden_f73c0633_t4.txt` (deployed model) must report all prompts
bit-identical, at threads ∈ {1, 4, 8, 16}, plus cross-thread-count digest equality
(the thread-invariance contract). Where a unit cannot meet byte-identity, that is
stated explicitly — nothing below stretches the gate.

### U0 — Lock the measurement harness (no production code) — first, blocking
Re-run and record: probe wall/tok/s at 1/4/8/16 threads + `perf record` symbol
decomposition (the §5.1 table), on the deployed model. This is the before/after
instrument for every other unit. Optionally add an env-gated
(`SENSEN_DECODE_PHASE_STATS=1`) per-phase wall-clock accumulator later; perf suffices
for now. *Effort: hours. Risk: none.*

### U1 — Parallelize decode attention over heads — the big one
`compute_attention_paged_flat` (`multi_head_attention.cppm:3921`; head loop `:3982`,
sibling variant `:4262`): wrap the `for (h)` loop in
`sensen::parallel::parallel_for(blocked_range(0, num_heads_, 1), …)`.
* **Thread-invariance/bit-exactness**: each head's online-softmax walks the same KV
  blocks in the same order inside one worker; heads write disjoint
  `o_flat[h*head_dim …]` ranges. Reduction order is untouched ⇒ byte-identical output
  regardless of thread count. Gate: golden probe, 4 thread counts.
* **Implementation hazards**: (a) the function's `thread_local` scratch
  (`m_buf/l_buf/tl_q8_*`, `:3958–3976`) must become per-head/per-worker locals or
  per-worker scratch — they are currently sized/reset per call on the calling thread;
  (b) nested-`parallel_for` safety is already guaranteed by FlatPool's inline-serial
  degradation (`parallel.cpp:120,392`) — if attention is ever called from inside a
  worker it silently serializes, which is correct; (c) per-head work at short context
  is a few µs — use grain 1–2 heads and keep the serial path for
  `total_seq_len < ~64` to avoid paying the 2 µs fork on tiny work.
* **Expected gain (measured basis)**: −1.5…−2.0 ms/token at ~170 ctx (2.3 ms serial →
  ~0.3–0.6 ms at 16-way over 16 heads); at 2048 ctx this is the difference between
  ~28 ms and ~2–4 ms of attention per token — it converts the term from
  O(context)·serial to O(context/threads). Also mechanically removes most of the
  FlatPool spin share.
* *Effort: 1–2 days. Risk: moderate (scratch-lifetime refactor); fully gated.*

### U2 — Kill the decode-time RoPE trig recompute
0.86 ms/token in `__sinf_fma`/`__cosf_fma`/`precompute_freqs_cis`
(`rotary_embedding.cppm:284`; constructors `:82,95,107`). Trig at decode time means
either a `RotaryEmbedding` is being re-constructed on the decode path or per-position
angles are computed outside the precomputed table. First step: `perf record -g` to
find the caller; then cache the full `max_seq_len × dim/2` cos/sin table once (2 MB at
4096×128 — trivial next to a 639 MB sweep).
* **Gate**: byte-identity requires the cached values be produced by *the same*
  `sinf/cosf` sequence currently executed (compute once with identical code, store,
  reuse). Then the golden probe applies unchanged. If the existing table already holds
  bit-identical values and the fix is just routing, this is free.
* **Expected**: −0.5…−0.9 ms/token. *Effort: ≤1 day. Risk: low.*

### U3 — Trace and remove the decode-path memmove
0.5–0.6 ms/token of `__memmove_avx512_unaligned_erms` (serial). Likely hidden-state
staging or KV append copies; `perf record -g` will name it in minutes. Removing a copy
is data-movement only ⇒ golden probe byte-identity applies.
* **Expected**: −0.3…−0.6 ms/token. *Effort: ≤1 day once traced. Risk: low.*
* **Conflict**: probably touches `multi_head_attention.cppm`/`kv_cache.cppm` — same
  files as U1. Do not run concurrently with U1.

### U4 — Deployment/config: threads 4 → 8–12; clamp default width to physical cores
The coordinator's own curve gives +10 % (46.3→50.8 @8T) to +14 % (@12T) for a config
change. Separately, the 32-thread cliff (42.5 tok/s) is SMT siblings spinning against
real work; clamp the *default* width derivation (`cached_default_concurrency`,
`parallel.cpp:64–89`) to physical core count while honoring explicit
`set_max_concurrency` values. Thread-count changes are numerics-safe (bit-identity
across thread counts is already verified fact, §2).
* **Gate**: golden probe at the new default; the 1/4/8/16 digest-equality check.
* *Effort: hours. Risk: minimal.*

### U5 — Region/dispatch trim (optional, small)
197 regions/token cost ~3.5 % vs a single region (§4) plus 0.9 ms/token of caller-side
`parallel_for` dispatch. Layer data dependencies cap how much can fuse: q/k/v are
already groupable through the existing same-qtype grouped-QKV dispatch
(`multi_head_attention.cppm:3829–3840` — verify it is active for this model's decode
path); gate+up are already fused (`matvecFusedGateUp`, `gemm_kernels.cppm:4060`).
Remaining upside ≈ 1–2 %. Do only if U1–U3 land early. *Gate: golden probe.*

### U6 — Planar scales/quants + 512-bit VNNI kernel — **do not build now (measured kill, kept on file)**
Built and measured in this session: separate `quants[nb][n][32]` + `scales[nb][n]`
planes, 64-byte-aligned row-pair loads, `_mm512_dpbusd_epi32` with the sign trick done
via a per-block-column hoisted `_mm512_mask_sub_epi8` (AVX-512 has **no `vpsignb`** —
this is the non-obvious part, and it works). Verified **bit-exact** against the
current kernel on randomized data including zero/negative activations
(`scratchpad/exact.cpp`, exit 0, all rows byte-identical — the strongest possible
gate would apply). Measured: **+14 % @1T, −7 % @4T, −6 % @8T, −2 % @16T** (§4). At
production thread counts the sweep is bandwidth-bound and wider vectors buy nothing,
while the second (scale-plane) stream slightly hurts. Blast radius would be large:
every consumer of the transposed layout (`matvecQ8_0_InlineSlice` `:4251`, the three
`Int8RowTile` tiers, both `Int8Batch` kernels `:4574+`, `matvecQ8_0_TiledVNNISlice`,
`transposeQuantizedWeights` and its three load-site callers). **Verdict: not worth
it** unless single-thread latency becomes a product goal; if it does, the kernel,
its exactness proof, and the mask-sub trick are ready in the scratchpad.

### U7 — lm_head int8/transposed conversion — **killed** (§5.7). No unit.

### U8 — NT hints / more prefetch / CCD placement — **killed** (§5.3, 5.4, 5.6). No units.

### U9 — Q8 KV cache attention wiring (separate, priced honestly, different gate)
Upstream sensen already has Q8 KV storage-complete but compute-incomplete
(`kv_q8.cppm`; fold helpers verified, `multi_head_attention.cppm` has no q8 read
path — see sensen CLAUDE.md 2026-08-02 entry). Wiring it halves attention K/V bytes
vs F16 at long context — it *multiplies* with U1 (which divides the same term by
thread count). **This is the one unit the golden probe cannot gate**: quantized KV is
*not* byte-identical; it needs the bounded-error instrument the upstream work already
defined (per-primitive relL2 vs the F16 baseline within the established ±20 % bands +
token-level rollout agreement + the 16-row defect holdout on the assistant). Do not
start it under this project's gate; schedule it behind the upstream contract.

### Explicit non-trade
No unit above buys bandwidth by breaking thread-count invariance, so there is no
priced invariance trade to surface. The one candidate that *would* have raised it —
dynamic work-stealing across the reduction dimension inside a single row-slice — was
never competitive: row-partitioned streaming already reaches the DRAM ceiling (§4),
so there is no prize behind that door. If a future >2-socket or >2-CCD-memory-domain
part changes this, re-measure `stream tile` first.

## 7. Sequencing & parallel-implementer matrix

Order: **U0 → {U1, U2, U4 in parallel} → U3 → U5**. U9 separately, after upstream gate.

| | U1 attn ∥ | U2 rope | U3 memmove | U4 config/clamp | U5 fuse |
|---|---|---|---|---|---|
| files | `multi_head_attention.cppm` | `rotary_embedding.cppm` (+one call site) | TBD, likely `multi_head_attention`/`kv_cache` | `parallel.cpp`, deploy config | `multi_head_attention.cppm`, `gemm_kernels.cppm` |
| U1 | — | OK | **CONFLICT** (same file) | OK | **CONFLICT** |
| U2 | OK | — | OK | OK | OK |
| U3 | **CONFLICT** | OK | — | OK | likely conflict |
| U4 | OK | OK | OK | — | OK |

Projected composite at 16T on the probe battery (17.6 ms/token today): U1 −1.8 ms,
U2 −0.6 ms, U3 −0.4 ms ⇒ ~14.8 ms ≈ **68–70 tok/s (+20–25 %)**; at the deployed
4-thread setting the same serial removals are worth ~+25–30 %, and U4 adds the
measured +10–14 % on top by moving to 8–12 threads. Hard ceiling remains ~85 tok/s
(639 MB @ 54 GB/s); the un-recoverable platform gap (89.6→54) should be retired from
all future roofline arguments.

## 8. Benchmark provenance

Scratchpad (`/tmp/claude-1000/…/scratchpad/`):
* `bench.cpp` — STREAM-style read sweeps, FlatPool-replica barrier cost, full-model
  decode replica (v256/v512/mono/chunks4/prefetch variants), lm_head fp32-vs-int8.
  Built `clang++ -O3 -std=c++23 -mavx2 -mfma -mf16c -mavx512f -mavx512bw -mavx512vl
  -mavx512dq -mavx512vnni -pthread`. Threads pinned to physical cores unless noted.
* `exact.cpp` — bit-exactness proof of the planar/512-bit kernel vs the current one.
* `probe_t1.data` / `probe_t16.data` — perf records of the real
  `decode_golden_probe` runs behind §5.1; `golden_probe_t1.txt` — 1-thread golden
  (stock Qwen3-0.6B-Q8_0) that the 16-thread run matched bit-for-bit.
* Known noise: ±5–7 % run-to-run (a ~0.4-core background process was present); every
  load-bearing comparison above exceeds that band or is called out as within it.
* Caveat honestly noted: the decode replica's 1T sweep (27.2 GB/s) runs ~20 % faster
  than the real engine's layer sweep at 1T (~22 GB/s implied by perf shares) —
  synthetic scales/pages differ; this does not affect any conclusion (both are far
  above production's effective 16.5 GB/s at 1T, and at ≥4T the replica matches the
  ceiling argument the stream benchmark independently establishes).
