# sensen GPU decode: launch collapse (CUDA graphs + kernel fusion)

@author Olumuyiwa Oluwasanmi

Status: architecture / analysis — no production code written. Grounded in source at
sensen `f73c0633` (`backend/sensen`) and the historical sm_120 nsys/ncu captures.
**No GPU was reachable while this document was written** (both remote hosts down,
local box has no CUDA device). Every claim below is tagged either
**[SOURCE]** (verified by reading the code at f73c0633, with file:line),
**[MEASURED]** (from the already-captured profiles), or
**[NEEDS-GPU]** (unverifiable until a device returns — the exact settling command is given).
No [NEEDS-GPU] claim is presented as measured.

## 0. Problem statement

Qwen3-0.6B Q8_0 autoregressive decode. sensen: 310–382 tok/s. llama.cpp (pinned
b6963, `6db3d1ff`) on the same card: 894 tok/s — 2.4×. [MEASURED]:

- `cudaLaunchKernel` = 70.1 % of host CUDA API time.
- 330,920 kernel launches / 512 tokens ≈ **646 launches per token** ≈ 1.74 ms of the
  2.66 ms per-token wall time. Kernel execution is the minority.
- With graph capture requested (all four switches, separately), launch count is
  **byte-identical** (330,920) and `cudaGraphLaunch` never appears.
- REFUTED and closed: `paged_attention` occupancy (2.88 %) is NOT the bottleneck —
  sequential-vs-flash A/B gave 377.4 vs 376.9 tok/s. Do not resurrect. The existing
  `SENSEN_BATCH_INVARIANT_ATTN` gate (added at f73c0633) is the residue of that
  experiment and stays.

Two attacks, complementary: **CUDA graph replay** removes the per-launch host cost;
**kernel fusion** lowers the launch floor (and the GPU-side inter-kernel gaps that
graphs do not remove).

---

## 1. Why capture never engages — the named root cause

### 1.1 What serving actually executes [SOURCE]

There are two distinct serving shapes, and both end in the **batched** decode
implementations even for a single sequence:

- **Single-request path** (calculator engine gRPC assistant, `assistant_throughput_probe`,
  `decode_golden_probe`): `LLMPipeline::generate()` calls
  `impl_->model->forward(next_token, agent, …)` per token
  (`src/llm_pipeline.cppm:2031`). With a paged KV cache (the serving default),
  `LlamaModel::forward()` enters the batched-N=1 block at
  `src/llama_model.cpp:1109–1163` and calls `forwardBatchDecodeGreedy_cuda[_graph]`
  (on-device-sampling greedy) or `forwardBatchDecode_cuda` (everything else).
- **Owner-thread scheduler path** (assistant_service.cpp:785 →
  `LLMPipeline::schedulerDecodeStep` → `LlamaModel::forwardBatch`,
  `src/llm_pipeline.cppm:2534–2541`, `src/llama_model.cpp:1560`): host-side sampling,
  decode via `forwardBatchDecode_cuda` / `_cuda_fp16`.

On sm_120 with `SENSEN_KV_DTYPE` unset, `resolveKvDtype()` auto-selects **BF16**
(`src/llama_model.cpp:1958–1961`), so the eager route is
`decodeBatchToDeviceLogits_cuda_fp16` (`src/llama_model.cpp:3089`). (If the profiled
run pinned `SENSEN_KV_DTYPE=fp32` it is `decodeBatchToDeviceLogits_cuda` — the
dispatch analysis below is identical either way.)

`forwardDecode_cuda` (single-sequence, non-paged `DeviceMultiLayerKVCache`) **does**
have working capture machinery (`forwardDecode_cuda_graph`,
`src/llama_model.cppm:1619–1622`), but it is only reachable when the agent's KV
cache is *not* paged (`src/llama_model.cpp:1109–1111` gates on
`dynamic_cast<const PagedKVCache*>`). Production serving is paged, so the
single-sequence graph path is dead in serving. This confirms the structural lead in
the brief.

### 1.2 The four switches, traced end to end [SOURCE]

| Switch | Read at | Acted on at | Fate on the serving decode path |
| --- | --- | --- | --- |
| `SENSEN_DECODE_BACKEND=graph` | `src/llama_model.cpp:1091–1099` (into local `backend`) | `src/llama_model.cpp:1164` only | **DEAD.** Line 1164 sits *after* the paged-KV block (1109–1163), every branch of which `return`s. For any paged agent the value is computed and discarded. Neither `forwardBatch` (1560) nor `forwardBatchGreedy` (1765) reads this variable at all. |
| `SENSEN_DECODE_GRAPH=1` | `src/llama_model.cpp:1101–1102` (legacy alias → `backend=3`) | same line 1164 | **DEAD**, same mechanism. |
| `SENSEN_BATCH_DECODE_GRAPH=1` | `forward()`: `src/llama_model.cpp:1115–1118`; `forwardBatch`: 1616–1619; `forwardBatchGreedy`: 1794–1797 | 1124→1125, 1639→1640, 1813→1819 | **PARTIALLY DEAD.** In single-token `forward()` it is consulted *only inside the `agent.on_device_sampling` branch* (1114). The non-greedy paged route (1143–1162) never reads it — it consults only `agent.compute_backend == CUDA_GRAPH` (1155). Any request with sampling temperature, penalties (frequency/presence), grammar, or logprobs has `on_device_sampling == false` (`src/llm_pipeline.cppm:1880–1884`) and therefore **no configuration whatsoever** can route it to `forwardBatchDecode_cuda_graph` through `generate()` unless the caller explicitly sets `compute_backend=CUDA_GRAPH` — which `generate()` never resolves from AUTO (`src/llm_pipeline.cppm:1836` stamps the raw request value; the engine always passes AUTO). |
| `--gpu-backend cuda-graph` | `server/sensen_serve.cpp:1079` → 1321 | exports `SENSEN_SERVE_DEVICE=cuda-graph`, `SENSEN_DECODE_BACKEND=graph`, `SENSEN_DECODE_GRAPH=1`, `SENSEN_BATCH_DECODE_GRAPH=1` (1428–1438) | Correctly plumbed **for sensen_serve HTTP traffic only** (per-request backend comes from `serve_compute_backend()` reading `SENSEN_SERVE_DEVICE`, `server/serve_http.cppm:598–620`). It does not exist for the calculator engine / `generate()`-based serving, and the two env vars it forwards are the DEAD ones above; the one live var it forwards (`SENSEN_BATCH_DECODE_GRAPH`) lands in the partially-dead gate just described on the `generate()` path. |

**Named root cause (dispatch severing):** the graph switches were wired into
`LlamaModel::forward()`'s *pre-paged* dispatch (line 1164) and into the
on-device-greedy sub-branch (1116), while production decode moved to the paged
batched block (1109–1163) whose non-greedy route has no env-driven graph gate at
all. `BatchDecodeGraphState` and all three captured bodies are complete and
allocated (`src/cuda_backend.cppm:2448`, `src/llama_model.cpp:4624/5069/5549`) but
the serving dispatcher cannot reach them under any environment variable on the
`generate()` path — "the flag is read but never reaches the batched path", exactly
the first bug shape predicted by the brief.

### 1.3 The silent sticky fallback [SOURCE — defect proven; whether it fired NEEDS-GPU]

For routes that *can* reach the graph function (sensen_serve with
`--gpu-backend cuda-graph`, or the on-device-greedy branch with
`SENSEN_BATCH_DECODE_GRAPH=1`), there is a second, independent defect:

- `forwardBatchDecode_cuda_graph`, begin-capture failure:
  `src/llama_model.cpp:5015–5018` sets the **sticky** `g.capture_failed = true` and
  falls back to the per-launch path **with no log line of any kind**. (The
  end-capture failure at 5022–5028 does log; begin does not.) The greedy path logs
  begin failure only at `debug`/`error` level (5504–5507).
- A failed `cudaStreamBeginCapture` performs **zero** kernel launches, so a run that
  attempts capture once on token 1 and sticky-fails produces a launch count
  **byte-identical** to a run with graphs disabled, and `cudaGraphLaunch` never
  appears — precisely the measured signature. A failed *end*-capture, by contrast,
  would have added the ~646 captured `cudaLaunchKernel` API calls of one body and
  broken byte-identity; the measurement therefore excludes end-capture failure.

So the measured facts are consistent with exactly two hypotheses, both rooted in the
defects named above:

- **H1** — the profiled serving went through `generate()`/`forward()` where the
  switches are severed (§1.2): the graph functions were never entered.
- **H2** — the profiled serving went through a reachable route and
  `sensen_cuda_graph_begin_capture()` (`src/cuda/cuda_graph.cu:47–55`,
  `cudaStreamBeginCapture(get_default_stream(), cudaStreamCaptureModeThreadLocal)`)
  returned nonzero on the first token and the failure was swallowed silently.

**[NEEDS-GPU] — settle H1 vs H2 the moment hardware returns (5 minutes):**

```bash
# 1) Count capture attempts in the API trace:
SENSEN_BATCH_DECODE_GRAPH=1 nsys profile -o /tmp/cap --stats=true \
    ./backend/build/assistant_throughput_probe   # or the serving binary used before
nsys stats --report cuda_api_sum /tmp/cap.nsys-rep | grep -E "BeginCapture|EndCapture|GraphLaunch|GraphInstantiate"
# 0 cudaStreamBeginCapture rows  -> H1 (dispatch severing; fix = WU-1 wiring)
# 1 BeginCapture, 0 GraphLaunch  -> H2 (begin failed silently; the rc/errstring is the next question)

# 2) Cross-check via the one unconditional print in the capture bridge:
SENSEN_BATCH_DECODE_GRAPH=1 <serving binary> 2>&1 | grep "\[CUDA GRAPH\] Captured"
# cuda_graph.cu:80 prints "Captured N nodes" on every successful end-capture.
```

Both hypotheses are fixed by the same work unit (WU-1): unify the gate so the
switches reach the batched path, and make every capture failure loud.

---

## 2. Capture safety: what varies per decode step, and how each is handled

The crux. A CUDA graph replays the exact kernel launches (grids, params, pointers)
recorded at capture. Anything that changes per token must be routed through device
memory at a stable address, updated via `cudaGraphExecKernelNodeSetParams`/
`cudaGraphExecUpdate`, or made capture-invariant — or the graph replays stale shapes
and produces *fluent wrong tokens*, which is strictly worse than no graph.

Inventory for the batched decode step (all [SOURCE]; the design in
`BatchDecodeGraphState` already addresses each — the point of this table is that an
implementer must not break any row, and the golden gate in §5 is what proves none
was broken):

| # | Varies per step | Handling at f73c0633 | Mechanism |
| --- | --- | --- | --- |
| 1 | Token id(s) | `g.token_ids` device buffer at a stable address, `upload_async` before replay; embedding gather is a captured node reading the device pointer (`llama_model.cpp:5354–5365`) | device-side pointer read |
| 2 | Position (RoPE angle, penalty window) | `g.positions` stable device buffer, re-uploaded per step (single-step) / advanced on-device by `sensen_paged_kv_advance_positions` (rollout graph) | device-side pointer read; **never** a host-baked int |
| 3 | KV length (`seq_len`) | `g.seq_lens` stable device buffer; the paged-attention kernels read the per-row length from memory inside a fixed grid (grid dims depend only on bucket/heads, never on seq_len) | device-side pointer read |
| 4 | KV write slot | `slot_mapping` **rebuilt on-device inside the graph** by `sensen_paged_kv_build_slot_mapping_devpos` from block_tables + positions (`llama_model.cpp:4160`) | captured compute node |
| 5 | Block table (grows when a sequence crosses a 16-token block boundary) | new block allocated **host-side before replay** (`allocateBlock`, 5178+); the table re-uploaded into stable `g.block_tables` every step | capture-invariant input buffer |
| 6 | KV pool base pointers | pool preallocated at first use, per-layer bases never move | capture-invariant by construction |
| 7 | Split-K partition count `v2p` (function of max seq len; changes at 1024 then every 512 tokens) | baked into `grid.z` → `ensureV2()` **drops and recaptures** the graph on change (`cuda_backend.cppm:2587–2600`, `llama_model.cpp:4832–4838`) | controlled recapture (bounded: ≤1 per 512 tokens) |
| 8 | Batch width | 9 fixed bucket sizes, one `BatchDecodeGraphState` per bucket (`llama_model.cppm:1375`); padded rows carry position −1 → slot −1 → skipped by `reshape_and_cache` | shape quantization |
| 9 | `deterministic` flag, rep penalty, softcap, rep window | baked into captured kernels; guarded recapture on change (`cap_det`/`cap_rep_penalty`/`cap_greedy_softcap`/`cap_rep_window`, `llama_model.cpp:4846–4851`, 5308–5317) | controlled recapture |
| 10 | Rep-penalty context window contents | `g.ctx`/`ctx_off`/`ctx_len` stable buffers, uploaded per step (5374–5423) | capture-invariant input buffers |
| 11 | Scratch shapes | `ensure()` drops the graph whenever any dimension or address changes (`cuda_backend.cppm:2532–2581`) | controlled recapture |
| 12 | DCA rotation positions | **cannot** be made device-side today → graph path refuses DCA models loudly (`llama_model.cpp:1634–1638`, 4631–4641) | correct decline |

Capture-illegal-operation hygiene already present [SOURCE]: `cudaMalloc` is
pre-materialized outside capture (fp16 residents, gate/up packing, lm_head, GEMM
scratch warmups `sensen_cuda_gemm_fp16_warmup` / `quant_matvec_batch_warmup` /
`quant_fused_mlp_warmup`, `llama_model.cpp:4921–4998`); uploads are async on the
same stream the graph replays on; the one sync (`CudaBackend::synchronize()`) runs
*before* `begin_capture` (5014). Residual [NEEDS-GPU] risk: any bridge function in
the captured body that launches on a stream other than
`sensen_cuda_internal::get_default_stream()` or performs a hidden sync/alloc
invalidates capture in `ThreadLocal` mode — this is precisely what the H2 probe in
§1.3 detects, and the compute-sanitizer-style sweep for it is:

```bash
SENSEN_BATCH_DECODE_GRAPH=1 CUDA_LAUNCH_BLOCKING=0 \
  compute-sanitizer --tool memcheck ./backend/build/decode_golden_probe \
  models/qwen3-0.6b-q8_0.gguf --check golden_gpu_eager.txt --gpu-layers 28
```

**What is NOT yet handled and must be in any new work:** nothing new is required
for the current body — but every *fusion* kernel added in §4 becomes a new node in
the captured body and inherits rows 1–4 (all its per-step inputs must be device
pointer reads at stable addresses; no host-computed scalar that changes per token
may be a kernel argument).

---

## 3. How llama.cpp reaches 894 tok/s [SOURCE — vendored tree, pinned b6963 `6db3d1ff`]

Checkout referenced by the backend build:
`ggml/src/ggml-cuda/ggml-cuda.cu` (paths below relative to that file).

1. **CUDA graphs are the primary mechanism, with `cudaGraphExecUpdate` instead of
   re-instantiate.** `ggml_backend_cuda_graph_compute` captures the whole token in
   `cudaStreamCaptureModeRelaxed` (line 3572), instantiates once (3497), and when
   node properties change (KV write offsets change every token) it *re-captures and
   patches the existing executable* via `cudaGraphExecUpdate` (2968–2989) rather
   than paying instantiate cost. Per-node property tracking
   (`ggml_graph_node_properties`: data pointer, op, dims, strides, src pointers,
   op_params — 2890–2935) decides whether an update is needed; unchanged topology →
   straight `cudaGraphLaunch` (3504).
2. **Graphs are gated, and disabled loudly-ish** for split buffers, `MUL_MAT_ID`
   with ne[2]≠1 (MoE multi-token), and batch>1 (2828–2886) — i.e. llama.cpp only
   graphs the exact case sensen needs graphed: single-token decode.
3. **Aggressive decode-path fusion** (`ggml_cuda_can_fuse`, 2996–3071 and the
   dispatch at 3141–3330): fused `MUL_MAT + MUL_MAT + GLU` (gate & up mat-vecs plus
   SwiGLU in ONE kernel, `fused_mul_mat_vec` 3258–3317, also the bias variants),
   fused `RMS_NORM + MUL`, fused consecutive `ADD`s (`ggml_cuda_op_fused_add`,
   3251), fused top-k MoE selection. Flash-attention decode is a single
   `FLASH_ATTN_EXT` kernel (KV read + softmax + V accumulate).
4. Net effect: roughly 14–16 kernels per layer per token (norm, 3 QKV mat-vecs, 2
   RoPE, 2 KV-cache copies, 1 flash-attn, 1 O mat-vec, fused gate/up/GLU, down
   mat-vec, fused residual adds) — ~60 % of sensen's 23 — and, when the graph
   engages, **one `cudaGraphLaunch` per token** regardless of node count. The 894
   tok/s is graphs × fewer/larger kernels, not a faster attention kernel — which is
   consistent with sensen's own refuted-occupancy A/B.

---

## 4. Kernel fusion: the per-layer launch ledger and the collapse plan

### 4.1 The measured 646 launches/token, fully accounted for [SOURCE derivation, matches MEASURED]

Serving config: Qwen3-0.6B (28 layers, Q8_0 weights, QK-norm, no DCA, no LoRA),
Ni=1, BF16 KV pool, TC compute on. Per layer,
`decodeBatchToDeviceLogits_cuda_fp16` (`src/llama_model.cpp:3346–3763`) issues —
counting the hidden second launch inside every `sensen_cuda_quant_matvec` call
(activation Q8_1 quantize kernel + matvec kernel,
`src/cuda/cuda_gemm.cu:1820/2118`):

| # | Launch | Site | Note |
| --- | --- | --- | --- |
| 1 | `rms_norm_batch_f32_to_half` (attn) | 3437 | **output unused at this config** — the Q8_0 matvec branch reads the f32 buffer |
| 2 | `rms_norm_batch` f32 (attn) | 3463 | the one actually consumed |
| 3–4 | Q: act-quant + matvec | 3471 | Q8_1 quantize of the SAME normed row |
| 5–6 | K: act-quant + matvec | 3481 | …re-quantized again |
| 7–8 | V: act-quant + matvec | 3491 | …and again (3 identical quantizations) |
| 9 | Q-norm (per-head RMS) | 3505 | Qwen3 QK-norm |
| 10 | K-norm | 3509 | |
| 11 | RoPE (Q+K one kernel) | 3534 | |
| 12 | `paged_kv_half_reshape_and_cache` | 3544 | |
| 13 | `paged_attention_v1_half` | 3636 | (+1 reduce kernel when split-K v2 engages past 1024 ctx) |
| 14–15 | O: act-quant + matvec | 3649 | |
| 16 | `residual_add` | 3659 | |
| 17 | `rms_norm_batch_f32_to_half` (ffn) | 3663 | **output unused at this config** |
| 18 | `rms_norm_batch` f32 (ffn) | 3684 | |
| 19–20 | fused-MLP: act-quant + fused gate/up/SiLU·mul kernel | 3715 | already a good fusion (one kernel for gate-matvec+up-matvec+activation) |
| 21–22 | down: act-quant + matvec | 3752 | |
| 23 | `residual_add` | 3762 | |

23 × 28 = 644, + final `rms_norm_batch` (3766) + lm_head act-quant + Q8 matvec
(3768–3771) = **647 ≈ the measured 646/token**. The ledger closes; there is no
mystery launch.

### 4.2 Fusion candidates, launches saved per layer

All candidates keep per-element FP operation order — this repo prices determinism
explicitly (CPU decode is bit-identical across 1/4/8/16 threads;
`SENSEN_BATCH_INVARIANT_ATTN` exists for exactly this reason). Each row states the
determinism impact honestly.

| ID | Fusion | Saves/layer | Mechanism | Determinism |
| --- | --- | --- | --- | --- |
| F1 | **Delete the dead norms.** Skip `rms_norm_batch_f32_to_half` (rows 1, 17) when Ni==1 and Wq/Wk/Wv (resp. gate/up/down) are all matvec-covered quant types — its output (`pre_narrowed_normed_ptr`) is provably unconsumed on that branch. | **2** | dispatch condition only, no new kernel | bit-identical by construction (removed kernel's output unused) |
| F2 | **Norm+quant fusion + shared quantized activation.** One `rms_norm_batch_q8_1` kernel writes the normed row directly as Q8_1 (per-32-block scales, same block mapping as `quantize_activation_q8_1_kernel`); Q/K/V (and down/lm_head from their own producers) consume the pre-quantized activation, deleting rows 2? no — replacing rows 2+3+5+7 with one launch, and 18+19 with one. | **4** | new kernel: RMS reduction (unchanged shape) + per-block absmax/scale epilogue; `sensen_cuda_quant_matvec` grows a pre-quantized-A entry point | bit-identical iff the RMS reduction keeps its current block shape and the Q8_1 block mapping is unchanged — both are requirements, gated in §5 |
| F3 | **Packed QKV matvec.** Pack Wq/Wk/Wv rows into one resident `[q_out+2·kv_dim, H]` Q8_0 buffer at load (exact precedent: `ensure_layer_gate_up_resident`); one matvec launch produces Q‖K‖V. | **2** | load-time packing + one wider matvec | bit-identical: each output row's dot-product reduction is unchanged; packing only concatenates independent rows |
| F4 | **QK-norm + RoPE fused.** One per-head kernel: RMS-normalize the head (same reduction shape as `rms_norm_batch` at head_dim granularity), then rotate in registers. Replaces rows 9+10+11. | **2** | new kernel over (Ni × heads) grid, positions read from device buffer (graph row 2) | bit-identical iff the per-head RMS reduction shape is preserved; rotation is element-wise |
| F5 | **Residual add folded into matvec epilogue.** O-proj and down-proj matvecs write `hidden[i] += dot` instead of a separate buffer + `residual_add`. Replaces rows 16, 23. | **2** | epilogue add in the existing matvec kernels (new flag/entry point) | bit-identical: per element it is the same two-operand add, performed after the same complete dot; ordering per element unchanged. Flagged anyway per house rule; gate proves it |
| F6 | **KV-cache scatter into producer epilogues.** F4's kernel writes rotated K straight to the pool slot (slot from the device slot_map, graph row 4); V is scattered from F3's epilogue. Deletes row 12. | **1** | epilogue global store with slot indirection | bit-identical (pure data movement) |
| F7 | **Activation-quant epilogues for O and down inputs.** `paged_attention` writes its output row pre-quantized to Q8_1 (head_dim=128 is 4 aligned 32-blocks per head, so per-head epilogue quantization is exact); the fused-MLP kernel's SiLU·mul epilogue emits Q8_1. Deletes rows 14, 21 (and 19's quant half merges into F2's pattern). | **2** | epilogue absmax+round per 32-block over values already in registers/smem | bit-identical iff the 32-block mapping matches the standalone quantize kernel (requirement) |

Floor after F1–F7: 23 → **~10 launches/layer** (~290/token with lm_head), a ~55 %
launch cut. At the measured ≈2.7 µs/launch host cost this is worth ≈0.95 ms/token
*without* graphs (2.66 → ~1.7 ms ⇒ ~590 tok/s) — **[projection, NEEDS-GPU to
confirm]**. Under graphs, fusion's residual value is the GPU-timeline gaps between
small kernels and a smaller captured node count (cheaper `cudaGraphExecUpdate`,
faster replay validation); it also directly shrinks the eager fallback path that
non-graphable configs (DCA) keep using.

Graph capture alone (if H1/H2 fixes land and capture succeeds):
646 host launches/token collapse to ~1 `cudaGraphLaunch` + a handful of
`upload_async` + one sync + one D2H. Host-side cost drops from ~1.74 ms to
≲0.2 ms; per-token wall becomes kernel-execution bound (~0.92 ms + inter-kernel
gaps) ⇒ **~900–1000 tok/s ceiling, i.e. llama.cpp-class** — [projection,
NEEDS-GPU].

---

## 5. Correctness gates

### 5.1 The mandatory graph gate: GPU-vs-GPU golden token stream

`backend/src/decode_golden_probe.cpp` (registered in `backend/CMakeLists.txt`)
captures the exact greedy+deterministic token-id stream over a 3-prompt battery
(one prompt runs 320 tokens — satisfying the multi-hundred-token requirement) and
re-checks it, reporting the FIRST divergent index.

**GPU output is NOT expected to match the CPU baseline**
(`scratchpad/golden_f73c0633_t4.txt` is CPU-captured; reduction orders differ).
The gate for graph work is **GPU-vs-GPU**: capture on the device with graphs
DISABLED, then check with graphs ENABLED. That isolates exactly the failure mode a
graph introduces (stale-shape replay ⇒ fluent wrong tokens no smoke test catches):

```bash
# leg 1 — eager GPU baseline (graphs off):
SENSEN_BATCH_DECODE_GRAPH=0 ./backend/build/decode_golden_probe \
    models/qwen3-0.6b-q8_0.gguf --capture golden_gpu_eager.txt --tokens 320 --gpu-layers 28

# leg 2 — identical run, graphs on. MUST report zero divergent tokens:
SENSEN_BATCH_DECODE_GRAPH=1 ./backend/build/decode_golden_probe \
    models/qwen3-0.6b-q8_0.gguf --check golden_gpu_eager.txt --tokens 320 --gpu-layers 28
```

Two required amendments to the probe (part of WU-1, and why the gate is specified
here rather than assumed):

1. The probe drives `generate()` with GREEDY + no penalties + AUTO ⇒
   `on_device_sampling == true` ⇒ it exercises the **greedy** graph
   (`forwardBatchDecodeGreedy_cuda_graph`). The **logits** graph
   (`forwardBatchDecode_cuda_graph`) needs a second leg: add a
   `--host-sampling` flag that breaks the on-device conjunction (e.g. sets
   `config.logprobs = true`; greedy token choice is unchanged) so the same battery
   runs through `forwardBatchDecode_cuda[_graph]`. Both legs must pass.
2. Engagement must be *proven*, not assumed, or the gate can pass vacuously via the
   silent fallback (§1.3): after WU-1, a capture failure logs at error level and
   the probe run must show `[CUDA GRAPH] Captured N nodes` (cuda_graph.cu:80) on
   stderr, and an `nsys stats` pass must show `cudaGraphLaunch ≈ tokens` with
   `cudaLaunchKernel` collapsed. A gate run whose stderr lacks the capture line is
   a FAIL even if tokens match.

Additional graph-specific stress within the same battery (the 320-token prompt
already provides them, verify by inspection of the run): at least one 16-token
block boundary crossing (row 5 of §2) and — with `--ctx` ≥ 1024 and a long prompt —
one v2 partition-count recapture (row 7).

### 5.2 Fusion gates

Every fusion unit F1–F7 claims bit-identical output. The gate is therefore
**same-backend, before/after, bit-identical**, which is *stricter* than the graph
gate and uses the same tool:

```bash
# before the unit lands (eager, graphs off):
./backend/build/decode_golden_probe m.gguf --capture golden_prefusion.txt --tokens 320 --gpu-layers 28
# after: MUST be zero-divergence
./backend/build/decode_golden_probe m.gguf --check golden_prefusion.txt --tokens 320 --gpu-layers 28
```

plus `compute-sanitizer --tool memcheck` (0 errors) on the probe for any unit adding
or modifying a kernel (F2, F4–F7). If any fusion cannot in practice preserve the
reduction shape (e.g. a future variant that widens the RMS block), it must NOT be
silently accepted at a tolerance — it comes back to this document as an explicit
determinism change with measured cost and benefit, per the project's standing rule.

### 5.3 Throughput evidence

Per unit, on the historical harness:

```bash
./backend/build/assistant_throughput_probe    # tok/s table, median over runs
nsys profile -o /tmp/u --stats=true <same>    # cudaLaunchKernel count per 512 tokens
```

Expected launch counts are exact and stated per unit below; a unit that does not
hit its predicted launch delta has not landed what it claims.

---

## 6. Work units, ranked by expected gain / risk

All units must degrade gracefully: no CUDA, older arch, capture-unsupported, DCA ⇒
the existing eager per-launch path, loudly logged once — never a silent engine
substitution. New files carry `@author Olumuyiwa Oluwasanmi`. C++23/CUDA per
`config/cpp_details.txt` (sensen repo).

| # | Unit | Contents | Expected gain | Risk | Gate | Conflicts |
| --- | --- | --- | --- | --- | --- | --- |
| WU-1 | **Graph reachability + loud failure** (dispatch only) | (a) One `decode_graph_requested()` helper (reads `SENSEN_BATCH_DECODE_GRAPH`, `SENSEN_DECODE_BACKEND=graph`, `SENSEN_DECODE_GRAPH`, and `compute_backend==CUDA_GRAPH`) consulted by *all three* dispatchers: `forward()`'s paged non-greedy route (llama_model.cpp:1143–1162), `forwardBatch` (1616–1638), `forwardBatchGreedy` (1794–1812). Delete the dead post-paged consumer at 1164 or keep it for the non-paged path with a comment. (b) Log every begin-capture failure with `cudaGetErrorString` at 5015–5018 (mirror 5504–5507’s pattern) and log one `graph engaged (bucket N, M nodes)` line on first successful capture. (c) `decode_golden_probe --host-sampling` flag (§5.1). | Unlocks the entire graph win: projected 2.66→≲1.1 ms/token (**~2×+, NEEDS-GPU**) if capture succeeds; if capture fails, produces the exact error string that has been invisible until now | LOW — routing + logging; falls back to the byte-identical eager path on any failure | §5.1 both legs + engagement proof; plus H1/H2 disambiguation commands (§1.3) run and their outcome recorded | touches only dispatcher regions; safe alongside WU-2 |
| WU-2 | **Dead-norm elimination (F1)** | Gate the two `rms_norm_batch_f32_to_half` calls (3437, 3663) on the consumer actually existing; mirror in `emit_batch_decode_core` (4068+) so the captured body gets the same cut | −56 launches/token ≈ −0.15 ms eager (**~+6 %, NEEDS-GPU**) | MINIMAL — removes provably-unconsumed work | §5.2 bit-identical | edits the same layer-loop region as WU-3/4/5/6 — do not run in parallel with them |
| WU-3 | **Norm+quant fusion + shared Q8_1 activation (F2)** | New `rms_norm_batch_q8_1` kernel (cuda_gemm.cu or new `cuda_fused_norm_quant.cu`); `sensen_cuda_quant_matvec` pre-quantized-A entry point; wire attn + FFN sites (eager + captured body) | −112/token ≈ −0.30 ms (**NEEDS-GPU**) | MODERATE — new kernel; must preserve RMS reduction shape and Q8_1 block mapping exactly | §5.2 bit-identical + memcheck | serialize after WU-2 (same call sites) |
| WU-4 | **Packed QKV matvec (F3)** | Load-time `Wqkv` packing beside `ensure_layer_gate_up_resident`; one matvec call; VRAM note: replaces three residents with one same-size resident (no net growth) | −56/token ≈ −0.15 ms (**NEEDS-GPU**) | LOW-MODERATE — packing is mechanical; row-independent reductions unchanged | §5.2 bit-identical | serialize with WU-3/5 (same region) |
| WU-5 | **QK-norm+RoPE fusion (F4), optional KV-scatter epilogue (F6)** | New per-head kernel; positions from device buffer (graph-safe, §2 row 2); K written to pool slot via device slot_map | −56 to −84/token (**NEEDS-GPU**) | MODERATE — per-head reduction shape must match `rms_norm_batch` at head_dim granularity | §5.2 bit-identical + memcheck | serialize with WU-3/4 |
| WU-6 | **Epilogue folds (F5 residual, F7 act-quant)** | Epilogue flags on matvec/attention/fused-MLP kernels | −112/token ≈ −0.30 ms (**NEEDS-GPU**) | MODERATE — three kernels touched; per-element order preserved (argued §4.2; the gate decides) | §5.2 bit-identical + memcheck | serialize with WU-3/4/5 |
| WU-7 | **Graph update polish (llama.cpp-style)** | Replace §2-row-7/9 full recaptures with `cudaGraphExecUpdate`; consider relaxed-mode capture; per-node param patching for penalty changes | small, steady-state (recapture is already ≤1/512 tokens) | LOW but only meaningful once WU-1 is proven live on hardware | §5.1 + a forced-recapture case (ctx crossing 1024 inside the battery) | independent, but strictly after WU-1 |

**Parallelization guidance for concurrent implementers:** WU-1 ∥ WU-2 are safe
together (disjoint regions of `llama_model.cpp`). WU-3, WU-4, WU-5, WU-6 all edit
the same layer-loop body (`decodeBatchToDeviceLogits_cuda[_fp16]` +
`emit_batch_decode_core`) and the same kernel families in `cuda_gemm.cu` — one
implementer, or strict sequence WU-2 → WU-3 → WU-4 → WU-5 → WU-6, each landing its
own bit-identical gate before the next starts. WU-7 must not start until WU-1's
device verification (§1.3 + §5.1) has been run on real hardware and recorded.

**Every eager-path fusion must be mirrored in `emit_batch_decode_core`** (the
captured body shares the same emit helpers) in the *same* commit, or the graph and
eager paths diverge numerically and the §5.1 gate — which compares exactly those
two — will catch it. That is not incidental: it is the reason the gate is shaped
this way.

---

## 7. Open items blocked on hardware (explicit)

1. H1-vs-H2 disambiguation (§1.3) — which of the two named defects actually fired
   in the historical profile. Fixing both is correct regardless; knowing which
   fired tells us whether a capture-illegal op hunt inside the captured body is
   also needed.
2. Whether `sensen_cuda_graph_begin_capture` / end-capture succeed for the batched
   bodies on sm_120 at all buckets — §5.1 leg 2 + the `[CUDA GRAPH] Captured` line.
3. All throughput projections in §4 and §6 (marked NEEDS-GPU). The launch-count
   deltas, by contrast, are exact and verifiable from `nsys stats` the moment any
   device is available.
4. The GPU-vs-GPU golden baselines themselves (`golden_gpu_eager.txt`) do not
   exist yet and can only be captured on a device; the CPU baseline at
   `scratchpad/golden_f73c0633_t4.txt` is NOT a substitute for graph gating.
