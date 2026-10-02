# The small-encoder assistants: architecture, training, accuracy, throughput, deployment

@author Olumuyiwa Oluwasanmi

Both production assistants are fine-tuned **Qwen3-0.6B** decoders served Q8_0 on
CPU. This document describes the replacement: a **~1 M parameter bidirectional
encoder with three heads**, which is roughly **550x smaller** and answers in **one
forward pass** instead of ~128 sequential ones.

Every number here was measured on `oluwasanmi-fedora-server` (16-core Ryzen 9
9955HX, AVX-512 + AVX512-BF16) and names its harness. Where something was not
measured it says so.

---

## 1. Why an encoder at all — the finding that makes this work

**Both assistants perform EXTRACTION while being served by an architecture that
GENERATES.** That is the whole result; model size is a consequence, not the point.

`agent/analysis/extractability.py` measured, on the mortgage corpus, that a gold
parameter value is almost always recoverable from a numeric literal in the
utterance under a transformation `mortgage_verification.cppm` **already
enumerates**:

| outcome | share | what serves it |
| --- | --- | --- |
| exact literal + map | **71.74%** | pointer head + map head |
| literal + map at the label's own precision | **14.68%** | same |
| small-cardinality convention constant | **9.19%** | a tiny classifier |
| unexplained | **2.23%** | the existing Horn-clause derivation layer |

On the strategy corpus the entire 22.76% residue is **one field** —
`expiration_days`, cardinality **6** (`{7,14,30,60,90,365}`). "next week" -> 7.
That is a six-way classifier, not generation.

**THE LATENCY ARGUMENT IS ABOUT ARCHITECTURE CLASS, NOT PARAMETER COUNT.**
Measured input/output token counts (`agent/analysis/lengths.py`):

| corpus | input tokens (encoder: ONE pass) | output tokens (decoder: one pass PER token) |
| --- | --- | --- |
| mortgage | mean 40.4, p95 95, max 133 | mean **128.2**, p95 291 |
| strategy | mean 10.2, p95 15, max 21 | mean **40.8**, p95 43 |

Decode on this stack is bandwidth-bound at ~60 tok/s over 604 MB of Q8_0 weights,
so one mortgage answer currently reads roughly `128 x 604 MB`. An encoder reads
`1 x weights`. A 135M *decoder* would still pay the 128x sequential factor — which
is why shrinking the decoder was not the answer.

**Two structural wins beyond speed:**

1. **The documented dangerous failure becomes impossible rather than refused.**
   `present_value = 304000.00` against a 495,000 utterance is the corrupted value
   this project caught in production. A model that emits *(literal index, map id)*
   **cannot name a value absent from the utterance** — the grounding gate stops
   being a filter and becomes a type.
2. **It is MORE precise than what ships.** The corpus rounds per-period rates to 6
   decimal places (7.21% -> `0.006008`). A pointer + map lets the serving layer
   compute the value in 256-bit `BigDecimal` at 38 places from the literal text, so
   that truncation disappears. A decoder cannot do this — it has to emit digits.

---

## 2. The model

A bidirectional (non-causal) transformer encoder. **It never emits a number.**

```
utterance ──> WordPiece (char offsets) ──> 3-layer encoder ──┬─ [CLS]      ──> op head          (29 / 48 classes)
                                                             ├─ [CLS]      ──> convention heads  (one per field)
                                                             └─ per-literal──> pair head (slot x map, multi-label)
```

### 2a. Exact configuration (`--servable`)

| | |
| --- | --- |
| d_model | **128** |
| layers | **3** |
| heads | **4** (head dim 32) |
| FFN | **SwiGLU, d_ffn 344** (gated, 8/3·d rounded to 8) |
| norm | **RMSNorm** |
| biases | **none** |
| positions | **RoPE**, base 10000, split-half rotation |
| max_len | **160** (measured p95 input is 95 tokens, max 133) |
| dropout | 0.1 |

**`--servable` is a compatibility constraint, not a tuning choice.** It switches
off LayerNorm, GeGLU, biases, segment embeddings, the pooler and the literal MLP
because **sensen's `TransformerBlock` is RMSNorm + SwiGLU with no biases, and its
GGUF loader refuses any other FFN activation**. RoPE is matched for the same
reason: sensen applies it unconditionally with no off switch, so learned absolute
positions would produce weights the serving path cannot run. The first trainer
used LayerNorm + GELU and produced a model the engine **could not load** — that is
why this flag exists.

### 2b. Parameter breakdown, per assistant

| | mortgage | strategy |
| --- | --- | --- |
| **total** | **1,041,687** | **692,175** |
| token embedding | 428,160 (vocab 3,345) | 88,192 (vocab 689) |
| 3 transformer blocks | 593,664 | 593,664 |
| final norm | 128 | 128 |
| op head | 3,741 (29 ops) | 6,192 (48 ops) |
| convention heads | 1,677 (5 fields) | 3,741 (3 fields) |
| literal MLP + pair head | 14,061 (**109** pairs) | 258 (**2** pairs) |
| label space | 29 ops, 109 pairs | 48 ops, 2 pairs |

The **593,664-parameter stack is identical** in both; all the difference is
vocabulary and head width. The **109-vs-2 pair count is why mortgage is the harder
task** — it needs 12 epochs and augmentation where strategy saturates at epoch 2.

`<NONE>` is **class 0 of the op head**, so "decline / out of scope" is an ordinary
class rather than a separate mechanism; the 40 no-params mortgage rows score 40/40.

### 2c. THERE IS NO PRETRAINED BASE MODEL

**These are trained from random initialisation.** No MiniLM, no ModernBERT, no
flan-t5, no SmolLM2, no BERT checkpoint of any kind. Two reasons, and both matter:

- **`huggingface.co` returned 403 on CONNECT** in the container where this work
  began — an organisation egress-policy denial — so no pretrained weights were
  reachable. **That block does NOT apply on `oluwasanmi-multigpu-server`, where
  `huggingface.co` returns 200**, so the comparison is now possible and has not
  been run.
- The brief asked for "the smallest barebones model that can do the job", and a
  from-scratch 1 M encoder is both smaller than MiniLM-22M and reachable.

**THE TRADEOFF, STATED PLAINLY BECAUSE IT IS THE MAIN RESIDUAL RISK:** a
pretrained encoder would generalise better to phrasings outside the corpus
distribution. From-scratch is excellent ON the distribution and measurably more
brittle off it — see §5. If MiniLM-L6 (22M) is used as a second arm, the heads,
the label builder and the serving seam are all unchanged.

Anything describing these models as "50 million parameters" is reading **`0.50M`**
— half a million — as 50M. The first trained arm was 0.50 M.

---

## 3. How it was trained

### 3a. Labels — the part that carries the accuracy

`agent/train/encoder_corpus.py` converts each gold params dict into
*(operation, {literal -> (slot, map)}, {convention field -> class})*, then
`reconstruct()` turns that back into params and `params_match()` compares against
gold. **The ceiling of that routing is reported on every run** as ORACLE COVERAGE,
and it is **100.00% on train and 100.00% on val for both corpora** — so the label
scheme contributes **0.00 points** of loss and every error is the model's.

**Map attribution must be exact-then-minimum-absolute-error, never first-match.**
Attributing an approximate match to the first map that fits produced **748 spurious
`M3 annual%->weekly` matches in a monthly-mortgage corpus**. Of 1,610 approximate
matches, **1,379 have between 2 and 11 candidate maps**. Min-error disambiguates
perfectly where it counts: `rate` goes **158/158 unanimously** to `M3
annual%->monthly`.

### 3b. Precision — the FULL bf16 path, not autocast

Training is **bf16 throughout the forward and backward**, with an **fp32 master
copy used only for the AdamW update**:

- the model's own weights are `torch.bfloat16`; every GEMM runs in bf16
- **no `torch.autocast`** anywhere in the step
- losses and softmax reduce in **fp32** (`compute_loss` casts every head; the
  attention softmax already specifies `dtype=torch.float32`)
- gradient clipping is computed on the **fp32 masters**, because a bf16 global norm
  over ~1 M values loses the small contributions that decide whether it fires

**Why the master copy is not optional.** bf16 has **8 mantissa bits**. A weight
update smaller than one ulp is discarded, and training silently stops moving
rather than failing. Measured: an all-bf16 run with bf16 optimizer state reached
**ROW 0.055** where this path reaches 1.00 — the pair-head loss plateaued at ~0.17
and never moved. That is the log-sum-exp-at-8-mantissa-bits failure, and it is
quiet, which is what makes it dangerous.

**Autocast is NOT this path and was measured worse on both axes:** it keeps fp32
master weights and casts per operation, so the weights are never actually 16-bit,
and it ran **46,016 padded tok/s against the full path's 64,424** — a separate
measurement put autocast at ~3.4x slower than pure bf16 on a hand-written matmul
attention. `--pure-bf16` implies `--no-bf16` so autocast cannot be layered on top.

### 3c. Augmentation — required for mortgage, free for strategy

`--augment-digits 0.5 --augment-format 0.5`. Each epoch, half the rows are retyped
into another numeric format and half receive fresh digits, with the labels
unchanged and the expected params **recomputed** from the new literals.

This is not cosmetic. Without it the mortgage model collapses on numeric
formatting it has never seen; with it the same probes recover almost completely.
§5 has the before/after.

### 3d. Hardware and cost

| | |
| --- | --- |
| host | 16-core Ryzen 9 9955HX, CPU only |
| **GPU** | **not used, and not usable** — both trainers hardcode `device = "cpu"` and contain **zero** CUDA references |
| threads | 12 |
| wall clock | **mortgage ~2.5 min (12 epochs), strategy ~2.4 min (12 epochs)** |
| throughput | 44,031–64,424 padded tokens/s |

**A GPU is unnecessary at this scale** — the whole run is minutes on CPU. This is
the one place the project's "train on the GPU server" convention does not apply,
and the reason is the model, not the toolchain.

---

## 4. Accuracy

The metric is **params exact-match**: `reconstruct()` the predicted
(op, pointers, maps, conventions) into a params dict and require every gold field
to match at gold's own precision. `train_encoder.py` scores through
`C.reconstruct()` + `C.params_match()`, so its `row_acc` **is** that quantity.

Holdouts: mortgage `agent/dataset/data_mortgage/val.jsonl` sha
**`1aa3ce94c344217e12f7`** — the repository's own recorded holdout, the same file
the deployed figures were measured on. Strategy `agent/dataset/data/val.jsonl`.

### 4a. Against the deployed decoders

| assistant | deployed Qwen3-0.6B | encoder, FULL bf16 | encoder, int8 served |
| --- | --- | --- | --- |
| mortgage | **73.9% raw / 77.0% served** | **100.00%** (600 rows) | **100.00%** |
| strategy | **95.0%** | **100.00%** (1500 rows) | **100.00%** |

Both at `op_acc` 100.00% and `pair-F1` 99.96% / 99.90%. The autocast arm reached
100.00% on mortgage and **99.93%** on strategy, so the full bf16 path is the better
of the two on accuracy as well as speed.

### 4b. Contamination — measured, not assumed

This repository has already paid for a holdout that was partly its model's own
training set (**304 of 600 rows**), so the overlap is measured on every run:

| corpus | val inputs byte-identical to a train input | accuracy on the DISJOINT rows |
| --- | --- | --- |
| mortgage | 14 / 600 | **100.00%** (586 rows) |
| strategy | **0 / 1500** | n/a — nothing to exclude |

### 4c. The ask probe

The first turn of 86 validation clarification dialogues, with the reply withheld:
**operation right 100.00%, the set of EMPTY fields is exactly the one the reply
fills 100.00%, both 100.00%.** Asking is therefore not lost — which matters,
because restoring asking in the serving layer was worth **+17 raw** and
**0/90 -> 49/90** on the decoder.

---

## 5. Error analysis — where it fails, and why

Everything above is on-distribution. The harness's own probes measure off it, and
**this is the real risk surface**.

### 5a. Unseen numeric formatting (mortgage)

| rewrite | no augmentation | augmented (full bf16) |
| --- | --- | --- |
| drop the `$` sign | 95.35% | trained |
| drop thousands commas | **59.38%** | trained |
| `6.5%` -> `6.5 percent` | **27.18%** | trained |
| `$250,000` -> `$250k` | 82.49% | trained |
| `$495,000` -> `495,000 dollars` *(held out)* | 93.46% | **99.14%** |
| `$495,000` -> `USD 495,000` *(held out)* | 85.69% | **88.45%** |

The probe **deliberately holds back rewrites augmentation never trains on**, so the
last two rows are the honest out-of-distribution number: **88-99% on phrasings the
model has never seen**, against 100.00% on the distribution. `USD 495,000` is the
weakest at 88.45%, and its failures concentrate in
`ComputeFutureValueDetailed.compound_frequency` (28 rows) -- a convention head,
not a pointer, so the currency prefix is shifting an operation-level inference
rather than breaking literal alignment.

### 5b. Unseen digits

| | without augmentation | with augmentation |
| --- | --- | --- |
| ONE pointed-at literal gets fresh digits | 91.96% | **100.00%** |
| EVERY pointed-at literal gets fresh digits | **59.82%** | **99.64%** |
| every literal resampled in-distribution | 98.39% | 99.82% |

Augmentation turns the catastrophic case (59.82%) into a non-issue (99.82%). This
is the single highest-value flag in the recipe.

### 5c. Limits that no probe here can reach

- **Every corpus is synthetic, from one generator.** `encoder_corpus.py` says so:
  *"every number this file or its siblings report is on a held-out split of the
  SAME synthetic generator."* Nothing here describes real users.
- **A constant the corpus never varies cannot be learned to vary.**
  `pmi_drop_off_ltv` is 0.80 in 985 of 985 `ComputeRefinance` rows, including rows
  that say "once I'm at 80% LTV", so a user who says 78% gets 0.80 — exactly as
  from the decoder trained on the same data.
- **Clarification is modelled only as the final params turn.** The decision to ask
  comes from the serving layer's existing missing-field logic (§4c measures that
  the encoder supplies what that logic needs).

---

## 6. Throughput and size

`agent/train/quantize_encoder.py`, 300 benchmark requests, 10 threads, host busy
fraction asserted under 15% before timing.

| variant | mortgage size | row acc | median | strategy size | row acc | median |
| --- | --- | --- | --- | --- | --- | --- |
| fp32 | 4.18 MB | 100.00% | 0.71 ms | 2.78 MB | 100.00% | 0.54 ms |
| bf16 autocast | 4.18 MB | 100.00% | 0.92 ms | 2.78 MB | 100.00% | 0.77 ms |
| bf16 weights | 2.09 MB | 100.00% | 0.79 ms | 1.39 MB | 100.00% | 0.62 ms |
| **int8 Linear** | 2.36 MB | **100.00%** | 0.83 ms | 0.98 MB | **100.00%** | 0.66 ms |
| **int8 Linear+Emb** | **1.10 MB** | **100.00%** | 0.82 ms | **0.72 MB** | **100.00%** | 0.68 ms |

Note that **fp32 is the fastest variant at this size** on this host: the model is
small enough to be compute-bound rather than bandwidth-bound, so int8's narrower
weights buy nothing and its dequantisation costs a little. int8 is chosen for
**footprint**, not speed -- 1.10 MB against 604 MB is what lets both assistants sit
in cache beside everything else the engine holds.

**Quantisation to 8 bits costs nothing** on the augmented mortgage model and
nothing on strategy. The int8 artifact is verified by **reloading it and requiring
bit-identical logits** — max |logit diff| over 50 requests is **0**.

Whole request in Python (lex + tokenize + forward + decode), 10 threads:
**1.18 ms median** (mortgage int8), **0.95 ms** (strategy int8); 1.07 ms and
0.82 ms at fp32.

### The honest latency ceiling

Production `ParseOperation` measures **2.18 s** live, of which **~71 ms** is pure
network, TLS, Envoy and gRPC framing. So the realistic end-to-end win is
**2.18 s -> ~80 ms, about 27x**, after which the request is network-bound and no
further model work helps. **Anyone quoting 500x is quoting weight bytes, not
latency.**

Weights per assistant: **604 MB -> 1.10 MB**, a factor of **549**.

---

## 7. How it will be deployed

### 7a. The serving path exists and is gated

sensen master carries the C++ serving side, and it **compiles and passes here**:

| target | result |
| --- | --- |
| `test_text_encoder` | **259 passed, 0 failed**, 3 n/a |
| `test_encoder_dispatch` | **107 passed, 0 failed** |
| `test_text_encoder_device` | **12 passed, 0 failed**, 2 n/a |

`test_text_encoder` proves `encode()` is **bit-identical under concurrency at f32,
bf16 AND int8** (4 threads x 36 calls x 3 trials over 6 input lengths), and that
INT8 is served when `d_model` and `d_ffn` are both multiples of 32. The 5 n/a are
the CUDA and Triton numerical-agreement cells, which **need the GPU server**.

Built with `scripts/build_cpu.sh --fresh --dir build-enc`, `CXX` set to the real
compiler. **Two traps cost a build each and are worth knowing:** sensen's configure
runs a clang-tidy gate that `FATAL_ERROR`s (bypass with `SKIP_CLANG_TIDY=1`), and
`build_cpu.sh`'s `resolve_cxx` falls through to **`/usr/lib64/ccache/clang++`** on
this host because its probe list covers only the apt.llvm.org layout — the shim
then sends sensen's `dirname(dirname(CXX))` include walk to `/usr/lib64/...` and
the build dies on `'__config_site' file not found`, which reads as a broken libc++
and is not one.

### 7b. What is still missing

1. **A GGUF writer path for this architecture.** The exporter's metadata key set is
   fixed, with no extra-KV hook for `pooling_type`, `attention.causal`,
   `layer_norm_epsilon` or the cls/sep/mask token ids. A `bert`-arch file would
   likely not load elsewhere.
2. **The contract change.** The wire contract goes from "emit a JSON params block"
   to "classification plus pointers". The operation label space has **five copies,
   one of them in another repository** (`nest-egg-loan`'s derived set), and the
   verifier's role changes from filtering values to typing them.
3. **No live traffic has touched it.**

### 7c. The staged plan

1. **Ship the augmentation flags in the recipe.** Free, and it is what turns 27%
   into trained behaviour.
2. **Gate it behind an env selector that refuses an unrecognised value at boot**,
   the way `MORTGAGE_WEIGHT_STORE` does — asked for the encoder and not getting all
   of it must make the assistant unavailable, never silently serve the decoder.
3. **Run it in SHADOW against live traffic**, comparing against the decoder per
   request. This is the only step that produces the number none of the above has:
   what real users actually type. The format probes say that is exactly where the
   risk is.
4. **Promote on the shadow comparison**, not on the holdout — and keep the decoder
   as the rollback target for a full deploy cycle.
5. **Re-run the comparison with pretrained MiniLM-L6 (22M)** as a second arm now
   that `huggingface.co` is reachable from the GPU server. The from-scratch result
   is the floor, not the recommendation.

---

## 8. Reproducing every number here

```bash
cd agent/train

# mortgage: full bf16, augmented, 12 epochs
python3 train_encoder.py --train ../dataset/data_mortgage/train.jsonl \
    --val ../dataset/data_mortgage/val.jsonl --op-key operation \
    --servable --pure-bf16 --augment-digits 0.5 --augment-format 0.5 \
    --epochs 12 --threads 12 --out-dir OUT_MORT

# strategy: full bf16, 12 epochs
python3 train_encoder.py --train ../dataset/data/train.jsonl \
    --val ../dataset/data/val.jsonl --op-key strategy \
    --servable --pure-bf16 --epochs 12 --threads 12 --out-dir OUT_STRAT

# 8-bit, accuracy at every precision, and the latency table
python3 quantize_encoder.py --checkpoint OUT_MORT/encoder_fp32.pt \
    --val ../dataset/data_mortgage/val.jsonl --out-dir OUT_MORT_INT8

# the C++ serving side
cd ../../backend/sensen
CXX=/usr/local/bin/clang++ SKIP_CLANG_TIDY=1 bash scripts/build_cpu.sh \
    --fresh --dir build-enc --target test_text_encoder \
    --target test_encoder_dispatch --target test_text_encoder_device
./build-enc/bin/test_text_encoder && ./build-enc/bin/test_encoder_dispatch
```

Each run writes `metrics.json` (every figure in §4 and §5), `schema.json`,
`tokenizer.json`, `failures.jsonl` and `encoder_fp32.pt`; `quantize_encoder.py`
writes `bench.json` and `encoder_int8.pt`.
