# Session log — 2026-08-01

@author Olumuyiwa Oluwasanmi

Strategy assistant end to end, sensen correctness and performance, Pro tier
infrastructure. Everything below was measured rather than reasoned about; where
a prediction of mine turned out wrong, the wrong prediction is recorded next to
what replaced it, because in several cases the wrong model was the reason
something took hours instead of minutes.

---

## 1. The tokenizer was not doing byte-pair encoding

`Tokenizer::encodeBPE` walked a trie taking the longest vocabulary entry at
each position. That is greedy longest-match, not BPE. The fields for the real
algorithm — `bpe_merges`, `bpe_ranks` — were declared and never written by
anything, and `tokenizer.ggml.merges` was never read out of the GGUF at all.

**32.4% of this project's own prompts produced a different token-id sequence,
while the total token count differed by 0.02%.** That near-identical count is
why it survived: it round-trips through `decode()`, the count looks sane, the
model emits text. Only an id-level diff against a reference can see it.

The divergence concentrates on rare sequences, because common words exist as
single vocabulary entries where both algorithms agree — so it landed exactly on
tickers and domain jargon:

| text | real BPE | greedy |
| --- | --- | --- |
| `pltr` | `pl` + `tr` | `plt` + `r` |
| `backspread` | `back` + `spread` | `backs` + `pread` |
| `fop` | `f` + `op` | `fo` + `p` |

A model trained on real-BPE ids was being fed sequences it had never seen, on
precisely the tokens carrying the meaning.

Fixed by reading `tokenizer.ggml.merges`, populating `bpe_ranks`, and replacing
the greedy walk with rank-ordered merge application plus the pre-tokenizer
split that was also missing. **Qwen is not GPT-2 here**: the canonical GPT-2
pattern left 42 of 500 fixture rows wrong. Qwen2/Qwen3 matches numbers one
digit at a time (so `365` is never one token) and treats any non-newline
non-alphanumeric as a letter-run prefix (`e-mini` → `e` + `-mini`). Both
variants implemented, selected from `tokenizer.ggml.pre`.

Verified 500/500 exact. The old algorithm scores 323/500 on the same fixture.

A second tokenizer defect found later: `encodeInternal` prepended
`special_tokens.bos` whenever `add_bos` was set, without checking whether the
model has one. Qwen3 declares none, so an absent key read back as 0 — a real
token id — and every prompt silently gained a leading token. Cost was
measurable: correlation against llama.cpp 0.9995 → 0.9745, top-5 rank 5/5 →
4/5. Now gated on a `has_bos` fact read from the file.

---

## 2. The assistant generated fluent garbage over gRPC

The same model, same GGUF, same prompt produced correct JSON through
`sensen_serve` and 262 bytes of punctuation noise through the gRPC service.

Root cause, and it is a library defect rather than a caller mistake:
`GenerationConfig` defaults `compute_backend` to `AUTO`, and
`llm_pipeline.cppm` counted AUTO as a GPU request **unconditionally** — no
check for a GPU, no check for CUDA being compiled in. That enabled
`on_device_sampling`, a contract only the CUDA decode block honours, and that
block is inside `#ifdef SENSEN_HAS_CUDA`. On a CPU build nobody could keep the
promise, so the decode loop ran

```
next_token = static_cast<uint32_t>(logits[0]);
```

casting a raw float logit to a token id. Qwen vocabulary ids 5–13 are
`&'()*+,-`, which is exactly what came out, and it explains why token 1 was
correct (the first sample runs before the flag is consulted) and everything
after was noise.

Fixed at source in sensen — `is_gpu` is now false on a build without CUDA and
AUTO no longer implies a GPU — and independently in the service with
`n_gpu_layers = 0`, so the fix does not depend on which sensen commit is
pinned.

A second bug was masking it: the service refused on the mere presence of a
`<think>` block, on the theory that it meant the system prompt had not taken.
Qwen3 emits one on **every** response including its correct ones; the block
being *empty* is the signal the prompt took. That guard rejected every correct
answer the model produced.

---

## 3. Every layer verified, primitive for primitive

Not final logits — each primitive, all 28 layers, against llama.cpp b6963 on
bit-identical token ids.

- **F16 control: every primitive within 2.3e-03 relative L2**, cosine at or
  above 0.999997, embeddings bit-exact. F16 removes weight quantisation from
  both engines, so this establishes each primitive is *algorithmically*
  correct — Qwen3 QK-norm head scoping and NeoX RoPE with `head_dim` 128 from
  `attention.key_length` (not `hidden/heads` = 64) included.
- **Q8_0: every primitive within 6.0e-02**, cosine at or above 0.998.
- The Q8/F16 ratio is **uniform at 21.5–29.9× across all eighteen
  primitives**. That uniformity is the actual proof: a wrong kernel appears as
  one outlier, a flat ratio is quantisation noise.
- Q8_0 dequantisation is **bit-identical** to ggml across all 197 quantised
  tensors of the real model file.
- 38-token greedy rollout token-identical to end of sequence.

This was only possible because of the observer described next. The previous
audit had stopped at final logits and recorded per-layer comparison as the one
thing it could not check.

---

## 4. Nothing hidden — `sensen.tensor_observer`

A runtime hook, compiled into every build including release, publishing every
forward-pass primitive under llama.cpp's own names. Attaches by environment
variable to an already-built binary that knows nothing about it:

```
SENSEN_OBSERVE=Qcur_normed SENSEN_OBSERVE_LAYER=7 ./calculator_engine
```

Deliberately **not** a `SENSEN_DEBUG` macro. A debug facility you must rebuild
to use is one nobody uses, and that is precisely why the earlier audit could
not check per-layer agreement. Cost when unattached is one relaxed atomic load
per tensor, never per element — measured at **+0.3%** against a 17%
run-to-run spread, with the instrumented build faster in one round of three.

Two unconditional `std::cout` blocks were found and removed from decode hot
paths in the course of this work, one gated on a data condition (`pos == 6`)
rather than any debug flag.

---

## 5. Q8_0 computed in int8 end to end

`matvecQ8_0_InlineSlice` read int8 weights and immediately widened them to
fp32 — sixteen lanes per zmm where int8 gives sixty-four, on data that arrived
in exactly the layout integer SIMD wants. Q8_0 was also the only quantisation
without an inline AVX-512 slice; Q4_0, Q5_0, Q4_K and Q6_K all had one, while
the deployed format fell through to per-block function-pointer dispatch.

Now: activations quantised once per matvec, int32 accumulation via `vpdpbusd`
where VNNI exists and `vpmaddubsw`/`vpmaddwd` where it does not, scales applied
once per row. Runtime dispatch; `-march=x86-64-v3` stays the compiled baseline.

| threads | before | after | llama.cpp |
| --- | --- | --- | --- |
| 1 | 3.56 s | **2.38 s** | 3.53–3.59 s |
| 4 | 1.19 s | **0.83 s** | 1.06–1.13 s |
| 16 | 0.74 s | **0.61 s** | 0.60–0.63 s |
| 16 × 5 concurrent | 3.29 s | **2.06 s** | 2.19 s |

−35.7% instructions, −87.8% 512-bit fp arithmetic, +11% IPC. All four dispatch
tiers — VNNI, AVX-512BW, AVX2, scalar — produce **byte-identical output**,
which is what makes it safe on a Railway CPU nobody chose.

**A roofline argument said this could not work, and the argument was wrong.**
Arithmetic intensity is 1.88 FLOP/byte, which reads as hopelessly
bandwidth-bound. Two errors: the ceiling was taken from an old microbenchmark
at 16 GB/s when the platform does 90 (achieved was 32.5, VTune reporting DRAM
Bandwidth Bound at 0.0% of elapsed time), and a whole-model average was applied
to per-matvec behaviour — the qkv/gate/down projections are 3.3 MB and sit in
cache. Only lm_head at 165 MB behaved as predicted. The real limit was
per-core memory-level parallelism, with the fp32 path executing twice the
instructions per byte and throttling each core's own streaming.

Precision, re-measured rather than assumed: **int8 is closer to llama.cpp than
the "exact" path** — max|diff| 0.6297 against 0.6358 — because llama.cpp
quantises activations too. Being precise was diverging from the reference.

---

## 6. Threading: the cap did nothing

`GEMM::threadCount()` gated serial-versus-parallel and nothing else;
`LLMPipeline::numThreads` configured a different pool that never reached GEMM.
Asking for four inference threads consumed the whole machine — **35 CPU-seconds
per request** on a 36-thread box. On a small container that is not an
inefficiency, it is a knob that does nothing, and the assistant shares a
process with the calculator and finance services.

`parallel_for` also entered a TBB arena from a foreign arena roughly 200 times
per token, with futex parking and waking. Replaced with a persistent pool:
epoch and ticket in one 64-bit atomic with CAS-verified chunk claiming, caller
participation, spin then park, inline-serial degradation when nested.

**35.10 → 4.68 CPU-seconds** at four threads. Byte-identical outputs.

---

## 7. Whole binary on libc++

`backend/CMakeLists.txt` forced `SENSEN_USE_LIBCXX OFF`, dragging sensen onto
libstdc++ to match gRPC — contradicting sensen's own binding policy, and the
wrong side of the trade since gRPC, protobuf, abseil, re2 and upb are all
built from source here.

`-stdlib=libc++` added to `CANONICAL_FLAGS` before `FetchContent_MakeAvailable`,
so the whole dependency subtree inherits it. `_LIBCPP_NO_ABI_TAG` made global —
it was sensen-scope-only, inert under libstdc++ and an ODR split under libc++,
since it changes mangled names of abi-tagged inline functions.

Result: `libc++.so.1`, `libc++abi.so.1`, **zero libstdc++** on both binaries.
Finance smoke suite bit-identical to baseline.

Three container-only bugs were found by actually building the image, none
visible locally: the model fetch sent no credentials to a private repository; a
fix for that was doubly incomplete (`--mount=type=secret` missing from the RUN,
and `MODEL_TOKEN` never declared as an ARG, so it failed while *insisting* a
token was needed with one supplied); and TBB was absent in-container because
`ENABLE_PARALLEL_STL OFF` gated off the FetchContent path that the migration's
own comment claimed was the only TBB source. That last passed locally only
because this host carries a stray system oneTBB. `ENABLE_PARALLEL_STL` now
probes rather than hard-codes, because the container and a developer machine
need opposite answers.

---

## 8. The fine-tune

QLoRA on Qwen3-0.6B, rank 16 on all seven projections, 2056 steps in 21.7
minutes, peak 1.97 GB GPU.

| field | accuracy |
| --- | --- |
| params exact-match | **95.0%** |
| symbol | 98.7% |
| asset_class | 99.0% |
| strategy | 97.0% |
| expiration_days | 100.0% |
| quantity | 100.0% |

The full fine-tune with int8 QAT on the same clean data scored **49.8%**. On a
0.6B model, updating all 398M parameters damaged the pretrained representations
faster than it learned a five-field extraction task; frozen base plus small
adapters did not.

Converted to Q8_0 GGUF (639 MB, 310 tensors), hosted privately on the HF Hub
with the checksum pinned, and fetched at image build time — it cannot travel
through `railway up`, which enforces a ~30 s upload deadline that 62 MB already
failed.

**The model requires its training system prompt.** Without it, it reverts to
stock Qwen3 and emits no `<params>` block at all. This cost a debugging cycle
and is recorded in the model's own README on the NAS.

Known limits, measured: the dataset restricts futures roots to `ES` and `NQ`,
so commodity queries are out of distribution — asked for a crack spread on
crude, the model answered `CND`, which is not an instrument. Symbol validation
now refuses that rather than passing it to the calculator. `crack_321` is gated
out of the UI for the same reason: the calculator prices it correctly, but the
assistant was never taught it, so the two surfaces disagreed.

---

## 9. Assistant deployment

Third gRPC service, `calculator.assistant.StrategyAssistant`. `ParseResponse`
is a `oneof` over params / clarification / refusal, so "ask a follow-up" and
"that is invalid" are distinguishable outcomes rather than both arriving as
errors.

Concurrency came from sensen's iteration-level scheduler rather than threads:
`generate()` cannot be called concurrently because `FeedForwardNetwork` holds
`mutable` scratch buffers per instance rather than `thread_local`, and layer
objects are shared, so parallel calls corrupt hidden state **silently**. One
owner thread, one fused `forwardBatch` per step, requests admitted into free
slots as earlier ones retire — 1.50× at 8 concurrent, all generations correct,
about 20 MiB marginal per user with paged KV. The previous design refused four
of eight users outright.

llama.cpp is available as a second backend behind `ASSISTANT_BACKEND`, and the
honest finding is that it does **not** amortise batching in-service — 1.01×
aggregate at 8 concurrent. It is the better single-user and cold-start choice;
sensen is the better multi-user one.

Envoy's route had no `timeout:` and inherited its 15 s default, which would
have killed generation mid-flight for any browser client sending no
`grpc-timeout`. Now 120 s.

`DATA_UNAVAILABLE` was added because the symbol validator reused
`MODEL_UNAVAILABLE` for market-data outages, and the smoke gate reads that code
as "this environment ships no model" — so with no Alpaca credentials a fully
working assistant reported its own model missing and the gate agreed.

---

## 10. Pro tier

Nothing was broken; it was unwired. Stripe already had the product
(`prod_UzPggKhfTSR8kO`) and live prices at $9.99/month and $99/year — only the
`.env` ids were blank. The restricted `rk_live` key creates checkout sessions
successfully, verified against the live monthly price.

Set in production: `LICENCE_SIGNING_KEY` (256-bit; nothing had ever been
issued, so it invalidated nothing), `SUPABASE_JWT_SECRET`, the Stripe price
ids, `PRO_GATE_MODE=warn` — the gate live in observe-only mode, logging
would-denies without denying anyone — and `QUOTA_POLICY`.

The quota policy needed a correction before it could ship. `quota.cpp:266`
collapses every unkeyed caller into one shared `~anonymous` bucket, so a
per-user-looking 30 req/min would have been 30 across the entire public site
and would have started refusing real traffic on deploy. Anonymous is now sized
as what it is — a site-wide DoS backstop at 6000/min — while the authenticated
tiers are per caller.

| tier | req/min | compute-units/hr | scope |
| --- | --- | --- | --- |
| anonymous | 6000 | 120,000 | shared site-wide |
| free | 120 | 3,600 | per caller |
| pro | 600 | 240,000 | per caller |
| partner | 2400 | 1,200,000 | per caller |

The compute-unit axis is what contains the assistant: a pricing call costs 1
unit, an LLM call costs hundreds.

Still open: the billing worker deployment, and `STRIPE_WEBHOOK_SECRET`, which
cannot exist until the worker has a URL — Stripe mints the `whsec_` when the
endpoint is created against it.

---

## Still open

- Billing worker not deployed; no `STRIPE_WEBHOOK_SECRET`; `PRO_GATE_MODE`
  stays at `warn` until the checkout round trip is proven.
- KV cache is F32 at 224 KiB/token against llama.cpp's F16 at 112. Worth ~2% at
  short prompts and roughly 27% of total traffic at 2048-token context. Work in
  flight.
- The assistant's params-to-response path has never executed past symbol
  verification, because Alpaca credentials exist only in Railway.
- RSS is 2.87 GB against llama.cpp's 0.72 GB, from a genuine 622 MB fp32
  embedding expansion. Fixing it means untangling `TokenEmbedding::lookup_ptr`
  across 15 call sites shared with CosyVoice2.
- `FINANCE_API_KEYS` unset — no partner keys issued yet, by decision.
