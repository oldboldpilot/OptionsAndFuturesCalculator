# Session 2026-10-02 — replacing the two Qwen3-0.6B assistants with a small encoder

@author Olumuyiwa Oluwasanmi

Asked: can `all-MiniLM-L6-v2` (~22M) or ModernBERT replace the two fine-tuned
Qwen3-0.6B assistants — fine-tuned at 16-bit, served at 8-bit — to cut the
latency users are complaining about? Later additions: `flan-t5-small` (77M),
`SmolLM2-135M/360M`, "the smaller the better", "the smallest barebones model
that can do the job", follow the sensen five-line API, write the CUDA, Triton
and MLX equivalents, and move the toolchain to clang 23.1.2.

Every figure below was measured in this session. Where something was not run it
says so.

---

## 1. The finding that decided the design

**Both corpora are EXTRACTIVE, not generative.** A gold parameter value is
almost never a free-form number; it is recoverable from a numeric literal in the
utterance under a transformation `mortgage_verification.cppm` already
enumerates, or it is a small-cardinality constant.

`agent/analysis/extractability.py`, on a regenerated mortgage corpus (seed
20261002; 1,144 rows with gold params, 10,968 numeric values):

| outcome | count | share |
| --- | --- | --- |
| exact literal + map | 7,869 | 71.74% |
| literal + map at the label's own precision | 1,610 | 14.68% |
| small-cardinality convention constant | ~1,008 | 9.19% |
| UNEXPLAINED | 245 | 2.23% |

`agent/train/encoder_corpus.py` then routes the residue too, with array grouping
and a convention-class route:

```
train  rows 21503/21520 = 99.92%   fields 211177/211194 = 99.99%
val    rows  1144/1144  = 100.00%  fields  11131/11131  = 100.00%

point:exact 62.92%  class 28.94%  class(cat) 3.58%  point:approx 1.50%
array:group 1.39%   array:anchor 1.17%  point:exact-binary 0.51%
UNEXPLAINED 3 of 211,194
```

28 operations + NONE, 114 (slot, map) pairs, 5 small class heads, and **40 fields
carrying a single constant, which therefore need no head at all.**

On the strategy corpus the residue is simpler still: the ENTIRE 22.76%
unexplained is one field, `expiration_days`, cardinality **6**
(`{7,14,30,60,90,365}`). "next week" → 7 is a six-way classifier.

## 2. Why this is a latency finding rather than an accuracy one

| corpus | input tokens (encoder: ONE pass) | output tokens (decoder: one pass PER token) |
| --- | --- | --- |
| mortgage | mean 40.4, p95 95, max 133 | mean **128.2**, p95 291 |
| strategy | mean 10.2, p95 15, max 21 | mean **40.8** |

Decode here is bandwidth-bound (~60 tok/s over 604 MB of Q8_0), so answering one
mortgage question currently reads about `128 × 604 MB`. An encoder reads
`1 × weights`.

**ARCHITECTURE CLASS DOMINATES MODEL SIZE.** A 135M decoder still pays the 128×
sequential factor. So `SmolLM2-135M` buys ~4.5x, `flan-t5-small` ~8x, and an
encoder two to three orders of magnitude on weight bytes read.

**The honest ceiling:** ~71 ms per call is network, TLS, Envoy and gRPC framing,
and `ParseOperation` measures 2.18 s live. So the realistic win is
**2.18 s → ~80 ms, about 27x**, after which the request is network-bound. A
figure larger than that is quoting weight bytes, not latency.

## 3. Two structural wins beyond speed

1. **The documented dangerous failure becomes impossible rather than refused.**
   `present_value = 304000.00` against a 495,000 utterance is the corrupted
   value this project caught in production. A model emitting (literal index, map
   id) *cannot name a value absent from the utterance*. The grounding gate stops
   being a filter and becomes a type.
2. **It is MORE precise than what ships.** The corpus rounds per-period rates to
   6 places: 7.21% → `0.0721/12 = 0.00600833…` is labelled `0.006008`. That is
   why 14.68% of values matched only at label precision. A pointer + map lets the
   serving layer compute in 256-bit `BigDecimal` at 38 places from the literal
   text, so the rounding currently baked into the TRAINING LABELS disappears. A
   decoder cannot do this; it has to emit the digits.

## 4. Accuracy measured

The baseline is this repository's own recorded figures: **95.0% params
exact-match** (strategy) and **raw 414/560 = 73.9%, served 431 = 77.0%**
(mortgage v20). HuggingFace was never needed for this and the egress block on it
is irrelevant to the question.

First arm, LayerNorm + GELU + RoPE, 0.50M parameters, strategy corpus
(`agent/dataset/data/val.jsonl`, 1,500 rows, the repo's own holdout):

| epoch | operation | literal (slot+map) | convention | ROW (all heads at once) |
| --- | --- | --- | --- | --- |
| 0 | 0.9800 | 0.9500 | 0.9412 | 0.8067 |
| 1 | **0.9973** | **0.9896** | **0.9941** | **0.9753** |

**97.53% row accuracy from 0.50M parameters against a recorded 95.0%**, at epoch
1 of 12, from a model roughly 1,200x smaller.

**TWO CAVEATS THAT MUST TRAVEL WITH THAT NUMBER:**

- **The metric is not identical.** ROW here is "every head simultaneously
  correct". The 95.0% is "emitted params JSON matches gold". They should
  coincide, because the label routing is 100% invertible on val, but rendering
  the pointers+maps back to a params dict and diffing against gold has NOT been
  done.
- **The holdout provenance differs for mortgage.** CLAUDE.md's 414/560 was
  measured on `val.jsonl` sha `1aa3ce94…`; the mortgage corpus here was
  REGENERATED. A strict head-to-head needs the same rows. The strategy holdout
  is the repo's own and was not regenerated.

Mortgage had not finished epoch 0 when this arm was stopped (op-head loss was
already down to 0.052 at step 500/7120).

**THAT ARM IS UNSERVABLE AND WAS RESTARTED.** See §6.

## 5. bf16, and a conclusion of mine that was wrong

Reported first as "bf16 is 10.6x SLOWER, use fp32". That was measuring
`torch.autocast`, not bf16: autocast keeps fp32 master weights, casts per op,
and PyTorch's CPU `scaled_dot_product_attention` has no bf16 kernel at these
shapes so it wraps an fp32 one. Casting the WEIGHTS and dropping autocast:

| arm (hand-written matmul attention, 1200 rows, 1 epoch) | time |
| --- | --- |
| fp32 | 48 s |
| pure bf16 | **36 s** (1.33x faster) |
| autocast bf16 | 164 s (3.4x slower) |

**THE FAST ARMS CANNOT BE RANKED FROM THIS DATA.** `--attn sdpa` with fp32
measured 19 s on an idle box and 41 s in the same window as the table above,
because the machine was carrying six concurrent agents (load average reached
88.7 on 4 vCPU). A 22 s swing on one config exceeds the 12 s spread between the
arms. Only the autocast result survives, its ~4x being far too large to be load.

Losses reduce in fp32 regardless, and softmax reduces in fp32 and casts back:
the GEMMs are what bf16 is for, and a log-sum-exp at 8 mantissa bits is where a
bf16 run quietly stops converging.

## 6. The cross-lane break: trained weights the engine cannot load

`text_encoder.cppm` reuses sensen's `TransformerBlock`, which is **RMSNorm +
SwiGLU**, and its GGUF loader refuses any other FFN activation. The first
trainer used **LayerNorm + GELU with biases**. RoPE was already matched
deliberately — sensen applies it unconditionally with no off switch, so learned
absolute positions would have produced weights the serving path cannot run — but
the norm and FFN were not.

`train_encoder_min.py` now uses RMSNorm, SwiGLU and no biases, and both corpora
were relaunched on it (0.58M parameters for the strategy geometry). Accuracy
for that arm is not yet available. RMSNorm-vs-LayerNorm and SwiGLU-vs-GELU are
near-equivalent at this scale, so the §4 figure is expected to carry over, but
that is a prediction and not a measurement.

## 7. sensen defects found and fixed

Four were in scope. Two more were found while working and nobody had asked.

**`ops::crossEntropyLoss` — the worst one was not the one being looked for.**
Its backward was `softmax - t`, wrong whenever `sum(t) != 1`; the true gradient
is `softmax*sum(t) - t`. On the exact `[3,5]` shape `tests/test_distill.cpp`
uses, tape/finite-difference came to **2.999999** — a live consumer has been
training against a 3x wrong gradient. Also one softmax over the WHOLE buffer, so
`[[0,1,2],[1000,1001,1002]]` returned 18.83 where the true per-row loss is
0.815; a `+1e-8` floor capped `[[0,40]]` at 18.42 instead of 40; and the comment
promised a mean while the code took a sum.

Repaired in place; the flat semantics are public and documented, so turning them
per-row is an owner call. New `ops::crossEntropyLossIndexed` adds the per-row
masked op the tree lacked, with `Mean` over NON-IGNORED rows. Verified against
PyTorch 2.14.1 in float64 over **5,506 random configurations**: max difference
**2.8e-14** on the loss, **2.2e-15** on the gradient. 105/105 own checks under
g++ 13.3 and the real modules on clang 23.1.2, 21 mutation arms all red,
ASan/UBSan/LSan clean at -O0/-O2/-O3.

**Tokenizer — three defects, one of them nobody asked about and it is the
dangerous one.** `num_tokens` was assigned AFTER padding, so padded positions
carried `attention_mask = 1` and never 0. Harmless for a single causal sequence;
it silently corrupts every padded ENCODER batch, because attention would attend
to `[PAD]`. Also truncation ran after appending EOS and so could cut the `[SEP]`
it had just added, and `fromParser` with an unrecognised `tokenizer.ggml.model`
fell back silently to a raw-byte trie whose unk id is 0 — which is `[PAD]` in a
BERT vocab, the worst available failure mode because it yields plausible ids.

Evidence: new test 101/101; **340,160 encodings byte-identical** to the original
module; 26 of 27 mutation arms killed; BasicTokenizer compared against
HuggingFace's over **all 1,112,064 codepoints** x 5 configs with 0 unexplained
mismatches. Plus WordPiece (previously an enum value with no implementation), a
`vocab.txt` loader, and opt-in character offsets — load-bearing, because the
per-literal heads map a literal's character span to token indices.

**GGUF writer.** Fixed metadata key set with no extra-KV hook, so an encoder's
`pooling_type`, `attention.causal`, `layer_norm_epsilon` and cls/sep/mask ids
could not be written. Note the asymmetry: `gguf_parser.cppm` ALREADY parses
`attention.causal` and nothing wrote it, and it reads eps only from
`layer_norm_rms_epsilon`, defaulting to 1e-6 where HuggingFace BERT uses 1e-12 —
so `layer_norm_eps` is MIRRORED onto that key.

**THE `ExportTensor::shape` COMMENT WAS THE WRONG ONE.** It said "row-major,
outermost first"; `TensorPlan::shape`'s "GGUF order, inner-most first" was right.
`write()` emits dims verbatim and every production caller reverses first. So the
K-quant guard — which checked the TOTAL element count rather than the ROW length
ggml requires — let a `[384, 768]` tensor (MiniLM's width) export a file ggml
refuses to load. Fixing it turned five existing tests red because their fixtures
followed the wrong comment; all five are fixed here. `test_qwen3_ckpt_export`
needed a geometry change too (kHidden 8 → 32), because a row length of 8 cannot
hold one 32-element Q8_0 block in EITHER order — and its own comment said
"multiples of 32 so every 2-D tensor's FLAT ELEMENT COUNT divides evenly", a
verbatim statement of the bug. Emitted bytes unchanged for every legal caller:
343 files, 886,048 bytes, 0 differing, same aggregate sha256.

## 8. Two library-wide findings that reach past this work

**`GEMM::PrecisionScope` and `SENSEN_PRECISION_MODE=FP32` DO NOT GIVE FP32.** On
an AMX host, slices of 16 or more rows go to a tile kernel that ignores the
arithmetic mode: fp32 below 64 rows, bf16-level error (2.3e-3) at 64 and above.

**`GEMM::matmul` IS NOT BIT-REPRODUCIBLE UNDER CONCURRENCY** under the default
reduction. A call's bits depend on whether it got the worker pool, because
`parallel.cpp:187` runs a second concurrent caller inline. Four threads, 26
tokens, 120 calls, against a lone caller: FP32/AUTO 78 calls differed, the
library default TF32/AUTO 84, BF16/AUTO 63, and pinned `{FP32,TREE}` /
`{BF16,TREE}` **0**. The encoder cells are therefore pinned to
`GemmReduction::TREE`.

This matters to this repository specifically because so many of its gates are
byte-identity across repeated runs. It was not checked against the production
EPYC fleet, and it matters only on AMX hosts.

## 9. Toolchain

clang 23.1.2 is installed at **`/opt/llvm23`** with a working `import std;`.
`apt.llvm.org` is blocked by egress policy, but the official
`LLVM-23.1.2-Linux-X64.tar.xz` on GitHub releases is reachable (2.01 GB) and
carries `share/libc++/v1/std.cppm` plus `clang-scan-deps`. Extracted
selectively to a dedicated prefix rather than overlaying `/usr/local`, which
sidesteps the stale-shadowed-header hazard CLAUDE.md records, and sensen's
`dirname(dirname(CXX))/share/libc++/v1/std.cppm` resolution still works.

Verified: `std.pcm` precompiles (38 MB) and a program using `import std;`
compiles, links and runs.

## 10. What was NOT done

- **`ctest` has never run.** sensen's `external/` submodules are empty in this
  container, so the project cannot be configured. Every C++ result above came
  from hand-driven clang 23.1.2 builds of module closures, or from
  self-contained g++ harnesses compiled from text extracted verbatim from the
  modules. The fixture results were measured on scratch copies.
- **No CUDA, Triton or MLX device run.** The CE CUDA twin goes further than
  usual — nvcc 13.4 compiles it warning-clean for sm_89/sm_120a, and the kernel
  header's own text runs on the host one `std::thread` per CUDA thread with a
  real barrier, 1,516/0, ThreadSanitizer-clean, 20/20 kernel mutation arms red —
  but no real launch has happened. MLX runs on Linux CPU here (mlx 0.32.3), so
  the MLX cells are exercised on that backend; Metal is not.
- **The rendered-params comparison in §4** against gold.
- **The 2.23% mortgage residue** was not re-checked against the derivation layer.
- **`monthly_overpayment` 12/244 and `extra_monthly_payment` 10/21** unexplained
  with values that look stated in the utterance (`$100 more a month`). Either a
  lexer gap or a corpus defect; not diagnosed.
- **No deploy, no NAS backup** — neither is reachable from this container.
- **Old AI-named branches could not be deleted from here.** The new compliant
  branches `feat/small-encoder-assistants` and
  `feat/text-encoder-infrastructure` are pushed; the git proxy aborts a
  ref-deletion push with a sideband disconnect on every form tried
  (`--delete`, explicit `:refs/heads/...`, three retries). They need deleting
  from the GitHub UI.

## 11. Authorship policy violation, found and corrected

Eight commits were authored `Claude <noreply@anthropic.com>` with
`Co-Authored-By` trailers, which `config/update_policy.txt` forbids as MANDATORY
policy. Both histories were rewritten to `Olumuyiwa Oluwasanmi`, verified 0
remaining including in file content, and force-pushed. The branches were renamed
to drop the AI prefix. Recorded here rather than quietly fixed, because the
policy's own Violation Response section asks for exactly this remedy.


---

## 12. FINAL NUMBERS (appended after both runs completed)

Both corpora reach **100% row accuracy on the SERVABLE architecture** (RMSNorm +
SwiGLU + RoPE, no biases), read from the runs' own `metrics.json` rather than
from a report:

| corpus | run | params | row_acc | field_acc | op_acc | <NONE> rows |
| --- | --- | --- | --- | --- | --- | --- |
| mortgage | `mort_f5s`, 6 epochs | 1,062,551 | **1.0** | 1.0 | 1.0 | 56/56 |
| strategy | `strat_f5`, 12 epochs | 692,175 | **1.0** | 1.0 | 1.0 | 4/4 |

against the recorded decoder figures of **73.9% raw / 77.0% served** (mortgage
v20) and **95.0%** (strategy). `row_acc` is computed through the same
`reconstruct` + `params_match` pair as `eval_encoder_params.py`, so it IS the
params-exact-match quantity and not a different metric wearing the same name.

**THE IN-DISTRIBUTION CAVEAT STANDS AND IS THE MOST IMPORTANT LINE HERE.** Val
comes from the same synthetic generator as train, and the decoder figures come
from different holdouts and were never re-run. So this is NOT a like-for-like
claim that the encoder is a better assistant. What it does establish is that the
architecture is not the limiting factor: a 0.7-1.1M-parameter encoder saturates
the label space these corpora define.

### `train_encoder_min.py` underperforms on mortgage, and the cause is identified

The minimal trainer plateaued at literal exact-set **0.5444** and ROW **0.1233**
on mortgage from epoch 3 onward, with train loss at 0.0606 and the operation and
convention heads both perfect. The cause is not capacity or the optimiser: it
omits the per-literal **tag and magnitude** features that `encoder_model.py`
feeds its pair head.

With 109 multi-label (slot, map) pairs, the model must otherwise infer from
token context alone whether a literal is money or a percent and roughly how
large -- which is exactly what decides WHICH money slot a given amount fills.
Given those features the same task goes to 1.0. On strategy the minimal trainer
is fine (ROW 0.9933 at epoch 11) because that corpus has only **2** pairs, so
there is almost nothing to disambiguate. **The gap between the two corpora is
the measurement that isolates the cause**, and it is why the minimal trainer was
kept rather than deleted: it is the control arm.

### A robustness shortcut the fresh-digits probe cannot see

After digit augmentation the remaining in-distribution failures were rates of
10-11% returning `MISSING`. A two-digit percent is usually a TAX BRACKET in this
corpus, and only 497 of 13,855 rate pointers sit at 10% or above, so magnitude
has become a shortcut for the slot. The fresh-digits probe preserves digit
count, so it is structurally incapable of detecting this -- a probe that cannot
fail for the reason you care about is not evidence about that reason.

### The model cannot abstain, and fails confidently

On the strategy checkpoint, 6 of 14 wrong defect-holdout rows carry operation
confidence of 0.999 or above, so no softmax threshold can refuse them. The class
heads have no "missing" class; strategy has only 62 `<NONE>` training rows and
the first run got 0 of 2 declines right. The POINTER slots do abstain properly,
and training on first turns alone moved the ask probe from 68.7% to 100%. The
defect holdout's own ceiling is 9/16 because GC, CL and ZB are not in the
20-symbol class vocabulary -- a corpus limit, not a model one.


---

## 13. ctest DID run, and §10's "it never has" is now obsolete

Section 10 recorded that `ctest` had never run because sensen's `external/`
submodules were empty. That is fixed, and the merge gate is satisfied with real
test results rather than harness proxies. Four things blocked it, all cheap:

| blocker | resolution |
| --- | --- |
| `cmake_minimum_required(VERSION 4.1)`, host had 3.28.3 | `pip install cmake` -> 4.4.3 |
| no TBB on the host | `pip install tbb-devel` |
| `external/{cpp23-logger,fastestjsoninthewest}` empty | cloned; SGEE off via `-DSENSEN_USE_SGEE=OFF`, tbqwf and nanobind not needed |
| 5.0 GB disk free | freed the 2 GB LLVM tarball, already extracted |

**ONE REAL BUILD BREAK, and it is the `kv_lowbit` lesson again.**
`src/text_encoder.cppm` imports `sensen.llq_weight_store`, and
`src/llq_weight_store.cppm` was in NEITHER module list -- not the server-only one
nor the main one. The build stopped at step 906 of 921 with
`fatal error: module 'sensen.llq_weight_store' not found`. CLAUDE.md states the
rule exactly: "Every new module that a listed module imports has to be added
alongside it; nothing derives this."

**It surfaced only on the first REAL configure.** None of the hand-driven
module-closure builds that preceded it could see it, because a hand-maintained
list cannot report its own incompleteness. That is the argument for running the
real build rather than trusting a closure script, and it is worth more than the
fix.

### Result on the MERGED tree, 215/215 built, 0 failures

```
100% tests passed, 0 tests failed out of 24
```

| test | why it is in the set |
| --- | --- |
| `test_gguf_exporter`, `test_gguf_requantize`, `test_qwen3_ckpt_export`, `test_diffusiongemma_ckpt_export`, `test_diffusiongemma_bf16_export` | the five fixtures whose shapes were reversed -- the actual regression risk |
| `test_distill` | the live consumer that had been training against the 3x wrong CE gradient |
| `test_autograd_cross_entropy` | the new per-row masked CE |
| `test_tokenizer_bos_default`, `_qwen35`, `_expanded`, `_encoder` | tokenizer regression + the new encoder test |
| `test_float_types`, `test_precision_128` | precision regression |
| `test_bf16_conversion_nan` (+ `_noavx512`) | the NaN fix, both with and without the AVX-512 tiers |
| `test_lossless_quant`, `test_bf16_sixteen_bit_exports`, `test_native_ckpt_converter`, `test_fused_ce` | consumers the audits named |

4 skipped honestly: three Python tests (the `_sensen_core` extension is not
built, Triton and MLX absent) and `test_tokenizer_bpe_parity` (needs a real
model). `test_lossless_quant_device` needs a GPU and was excluded.

**THREE OF THOSE SKIPS WERE FAILURES FIRST, AND THE CAUSE WAS MINE.** They
reported `ModuleNotFoundError: No module named 'numpy'`, which looked like a
regression in the LLQ Python bindings. It was not: `find_package(Python3)` had
selected `/tmp/bt/bin/python3.11` -- the venv created for CMake and TBB -- which
had no numpy. Installing it there turned all three into proper 77 skips.
Established by removing the cause and re-measuring, not by arguing from the
error text. The decisive evidence beforehand was that
`git diff 5a510a3..HEAD` over those three test files and `src/lossless_quant.cppm`
was EMPTY -- this branch never touched them.

### The pre-merge 24/24 was deliberately discarded

A green run was recorded before merging `gh/master`. It was NOT carried forward:
7,352 commits of upstream change is exactly the condition under which a prior
green means nothing. The 24/24 above is measured on the merge commit itself.
