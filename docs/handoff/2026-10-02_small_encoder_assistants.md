# Handoff — replacing the two Qwen3-0.6B assistants with a small model

@author Olumuyiwa Oluwasanmi
Session date: 2026-10-02
Branch: `claude/finetune-bert-models-quantization-ngwmnd`

**Written early and deliberately, as a credit-exhaustion insurance policy.** Every
number below is measured in this session unless the line says otherwise. Where
something is unverified it says UNVERIFIED in capitals, because this repository
has already paid three times for a figure that described a harness rather than
the thing it named.

---

## 1. The question, and the answer

Asked: can `all-MiniLM-L6-v2` (~22M) or ModernBERT replace the two fine-tuned
Qwen3-0.6B assistants, fine-tuned at 16-bit and served at 8-bit, to cut the
latency users are complaining about? Later additions to the ask:
`flan-t5-small` (77M), `SmolLM2-135M/360M`, "the smaller the better", "the
smallest barebones model that can do the job", and follow the sensen five-line
API convention.

**The answer is yes, and the reason is not model size.** It is that both
assistants are doing EXTRACTION while being served by an architecture that
GENERATES. The decisive measurement is below.

### 1a. MiniLM and ModernBERT are encoder-only

They cannot autoregressively generate, so they are not a drop-in base for
either assistant as the contract stands today. That is not fatal, because the
task does not need generation — but it does mean the output contract changes
from "emit a JSON params block" to "emit a classification plus a set of
pointers". `flan-t5-small` and SmolLM2 ARE generative and would be drop-ins;
they are also the slower options, for the reason in 1c.

### 1b. The corpora are extractive — measured

`agent/analysis/extractability.py`, run against
`agent/dataset/data_mortgage/val.jsonl` (generated in this session with
`build_mortgage_dataset.py --n 24000 --seed 20261002`; 1,200 rows, 1,144 with
gold params, 10,968 numeric values):

| outcome | count | share | what serves it |
| --- | --- | --- | --- |
| EXACT literal + map | 7,869 | **71.74%** | pointer head + map head |
| literal + map at the label's own precision | 1,610 | **14.68%** | same |
| small-cardinality convention constant | ~1,008 | **9.19%** | tiny classifier / serving default |
| UNEXPLAINED | 245 | **2.23%** | the existing Horn-clause derivation layer |

A "map" is one of the transformations `mortgage_verification.cppm` ALREADY
enumerates (M1 identity, M2 percent/100, M3 annual→per-period, M5 years→months,
M6, M7, M8 negate, M9 the binary a−b / a×(1−p), M10 complement, …). The script
reuses that set deliberately rather than inventing one.

On `agent/dataset/data/val.jsonl` (strategy, 1,496 rows with gold, 2,992
values) the result is stronger and simpler: **the entire 22.76% residue is ONE
field** — `expiration_days`, cardinality **6** (`{7,14,30,60,90,365}`). "next
week"→7, "at a month"→30. That is a six-way classifier, not generation.
Categorical fields: `strategy` card 47, `symbol` card 20, `asset_class` card 3.

### 1c. Why this is a latency finding, not an accuracy one

Measured input vs output token counts (`agent/analysis/lengths.py`):

| corpus | input tokens (encoder: ONE pass) | output tokens (decoder: one pass PER token) |
| --- | --- | --- |
| mortgage | mean 40.4, p50 30, p95 95, max 133 | mean **128.2**, p95 291 |
| strategy | mean 10.2, p50 10, p95 15, max 21 | mean **40.8**, p95 43 |

CLAUDE.md establishes decode on this stack is bandwidth-bound: ~60 tok/s over
604 MB of Q8_0 weights. So answering one mortgage question currently reads
roughly `128 × 604 MB`. An encoder reads `1 × weights`.

**ARCHITECTURE CLASS DOMINATES MODEL SIZE, and this is the single most useful
sentence in this document.** A 135M decoder still pays the 128× sequential
factor; it only shrinks the per-pass cost. So:

- SmolLM2-135M int8: ~128 passes × ~135 MB → ~4.5x better than today. Still generative.
- flan-t5-small int8: ~128 decoder passes over a smaller decoder → ~8x, order of magnitude.
- a small ENCODER + heads: **1 pass**, over single-digit MB → two to three orders of magnitude on weight bytes read.

**The honest ceiling, which must be quoted alongside the speedup:** CLAUDE.md
measures ~71 ms per call of pure network, TLS, Envoy and gRPC framing, and
`ParseOperation` measures 2.18 s live. So the realistic win is **2.18 s → ~80 ms,
about 27x, after which the request is network-bound and no further model work
helps.** Anyone quoting a 500x figure is quoting weight bytes, not latency.

### 1d. Two structural wins beyond speed

1. **The documented dangerous failure becomes impossible rather than refused.**
   `present_value = 304000.00` against a 495,000 utterance is the corrupted
   value this project caught in production. A model that emits (literal index,
   map id) *cannot name a value absent from the utterance* — the grounding gate
   stops being a filter and becomes a type.
2. **It is MORE precise than what ships.** The corpus rounds per-period rates to
   6 decimal places: 7.21% → `0.0721/12 = 0.00600833…` is labelled `0.006008`.
   That rounding is why 14.68% of values matched only at label precision. A
   pointer + map lets the serving layer compute the value in 256-bit
   `BigDecimal` at 38 places from the literal text, so the 6-dp truncation
   currently baked into the training labels disappears. A decoder cannot do
   this — it has to emit the digits.

### 1e. One measurement trap, recorded because it nearly produced a wrong number

The first version of `extractability.py` tested `isinstance(v, float)` and
measured **3,458** values. Most money and rate values arrive as JSON **strings**
(`'843200.00'`, `'0.0758'`) because the wire format is `BigDecimal` decimal
strings and `FinanceParams.params` is a `map<string,string>`. The real
denominator is **10,968**. `as_decimal()` is the fix: a value is numeric if it
PARSES as a number, not if JSON happened to type it as one.

A second trap, in the same script: attributing an approximate match to the
first map that fits produced **748 spurious `M3 annual%→weekly` matches in a
monthly-mortgage corpus**. `agent/analysis/ambiguity.py` measures why — of
1,610 approximate matches, **1,379 have between 2 and 11 candidate maps**.
Attribution must be by MINIMUM ABSOLUTE ERROR, which disambiguates perfectly
for the field that matters: `rate` goes **158/158 unanimously** to
`M3 annual%→monthly`. Any label builder MUST use exact-then-min-error, never
first-match.

Corollary worth acting on: if the corpus emitted rates at 38 places instead of
6, the map would be uniquely determined and this ambiguity would not exist.

---

## 2. Proposed architecture

Three heads on one small bidirectional encoder. The model NEVER emits a number.

```
utterance ──> WordPiece (char offsets) ──> tiny encoder (bidirectional)
                                              │
            ┌─────────────────────────────────┼──────────────────────────┐
            │                                 │                          │
      [CLS] → operation              per-literal → slot id        [CLS] → each
      (mortgage 28 classes;          per-literal → map id          convention field
       strategy 47+20+3)             (NONE is a class)             (small cardinality)
```

The serving layer then computes each value in `BigDecimal` from
`(literal text, map id)`. Residue that is genuine arithmetic
(`current_remaining_months`, `current_loan_balance`) goes to
`mortgage_derivation.cppm`'s existing Horn-clause solver, which exists for
exactly this.

Target size **1–5M parameters** (d_model 128, 3 layers, 4 heads, vocab ~4096,
max_len 160 — chosen because measured p95 input is 95 tokens and max is 133).

### Why from scratch rather than MiniLM

`huggingface.co` returns **403 on CONNECT** from this container — an
organization egress-policy denial, which the agent-proxy README says to report
rather than route around. So no pretrained MiniLM / ModernBERT / flan-t5 /
SmolLM2 weights are reachable here. Combined with "the smallest barebones model
that can do the job", a from-scratch tiny encoder is both what is reachable and
what is smallest.

**The tradeoff, stated plainly:** pretrained MiniLM would generalise better to
phrasings outside the corpus distribution. From-scratch is fine ON the corpus
distribution and more brittle off it. If `huggingface.co` is allowlisted,
re-run the comparison — MiniLM-L6 at 22M is still the better bet for
robustness, and the heads and label builder are unchanged.

---

## 3. What exists in sensen, and the three gaps

Surveyed at commit `5a510a3d` (the pinned submodule; its default-branch tip is
the same commit, so `git clone --depth 1` gives exactly the pin).

**Present and usable:**
- CPU reverse-mode autograd (`autograd.cppm`, 7095 lines), thread-local `Tape`,
  `TensorT<T>::backward()`. **Defaults to `float`, not `fp128_t`** — `fp128_t`
  is an opt-in explicit instantiation. This corrects an implication in CLAUDE.md.
- CPU training scaffolding: `trainer::Trainer<T>` / `TrainerBuilder<T>` (grad
  accumulation, global-norm clipping, LR schedule, AMP, checkpoints),
  `AdamWOptimizer`, `sensen.optimizers` (Muon, Lion, Sophia, Shampoo, Adafactor,
  GaLore), `sensen.dataloader`, `sensen.lr_scheduler`.
- CPU LoRA that genuinely TRAINS: `autograd::LoRALinear`, `CpuLoRACacheLinear`,
  `peft::ICpuPeftLinear`, proven by `tests/test_cpu_lora_cache_trajectory.cpp`
  (forward → `mseLoss` → `backward()` → `AdamWOptimizer::step()`).
- PTQ with calibration: `quantization_advanced` (`QuantPlan{}.weight(...)
  .method(RTN|GPTQ|AWQ|SmoothQuant).calibration(...).build()`), `gptq.cppm`.
- GGUF writer: `GGUFExporter`, 16 encodable types including **Q8_0, BF16, F32, F16**.
- `scripts/build_cpu.sh` — a one-command CPU-only build.

**Gap 1 — there is no per-row masked cross-entropy on the tape.**
`ops::crossEntropyLoss` (`autograd.cppm:3935`) does ONE global softmax over the
whole flat buffer, takes targets as a same-shaped probability tensor rather than
class indices, and has no `ignore_index` and no per-row mean. A multi-head
classifier with masked per-literal heads cannot be trained with it.
`FusedLinearCrossEntropy` (`fused_ce.cppm:249`) IS a correct CPU CE with
`ignoreIndex` and label smoothing, but it is **not on the autograd tape** and
its only consumers are its own test and a Python binding. This is the one real
defect the survey found and it must be fixed for sensen-side training.

**Gap 2 — no bidirectional encoder.** Every model class is causal/decoder.
Attention masking, pooling (CLS or mean), and classification/span heads are all
absent. UNVERIFIED in one respect: the second survey agent covering attention
mask construction and tokenizer char-offset support had not reported when this
document was written — check its findings before writing the forward pass.

**Gap 3 — the GGUF writer has a fixed KV set.** `general.architecture` is a free
string, but there is no extra-KV hook and no field for `pooling_type`,
`attention.causal`, `layer_norm_epsilon`, or cls/sep/mask token ids. A
`bert`-arch file written by it would likely not load in llama.cpp. Needs a
generic extra-KV mechanism.

Also noted: `ExportTensor::shape` is documented "outermost first" while
`TensorPlan::shape` is documented "inner-most first", and `write()` emits dims
verbatim with no reversal. **Check dimension order against
`tests/test_gguf_exporter.cpp` before trusting either.**

### The five-line API convention, to follow exactly

A default-constructible selector class; fluent setters that are `noexcept`,
return `X&` via `return *this;`, and are NOT `[[nodiscard]]` or `&&`-qualified;
terminals `.resolve() const -> std::expected<Route, XDispatchError>`,
`.lookup() const` (total, always carries a reason), `.explain() -> string_view`.
Setter vocabulary: a verb per kind, `withKind/withPrecision/withBackend/withMode`,
`onCpu()/onCuda()`, `.bf16()/.int8()`, `.training()/.inference()`.

```cpp
auto route = QatSelector{}.wag()
                          .withPrecision(QuantPrecision::INT4)
                          .withGranularity(QatGranularity::PerGroup)
                          .withBackend(QatBackend::Cuda)
                          .resolve();
```

Module shape: one `src/<x>_dispatch.cppm`, `export module sensen.<x>_dispatch;`,
`export namespace sensen::<x>_dispatch { ... }` with an internal `detail`,
everything `inline`/`constexpr`. Global module fragment includes
`quant_lowbit_codebook.h` and, under `#if defined(SENSEN_NO_IMPORT_STD)`, the
specific `<std>` headers plus `#include <new>` as an ODR anchor; `import std;`
guarded by `#if !defined(SENSEN_NO_IMPORT_STD)`.

**Registration — the trap:** the module must be added to **BOTH**
`SENSEN_SERVER_ONLY_MODULE_FILES` and `SENSEN_MODULE_FILES` in
`src/CMakeLists.txt`, imports listed first, and a gate test added using
`sensen::dispatch::audit<Router>(AuditPolicy{...})`.

Note the banner style is informative: `/// The five-line API.` alone in
cnn/ssm/transformer/gnn, with the worked call chain in qat/quant/peft/rl.

---

## 4. Environment constraints, measured

| thing | state |
| --- | --- |
| CPU | 4 vCPU Intel Xeon @2.10GHz, **avx512f + avx512_bf16** present |
| RAM / disk | 15 GB / ~30 GB free (torch venv alone is 5.4 GB) |
| GPU | **none** — no `nvidia-smi`, no `/dev/nvidia*` |
| `pypi.org`, `files.pythonhosted.org` | direct-routed, 200 |
| `huggingface.co` | **403 CONNECT — org egress policy denial** |
| `apt.llvm.org`, `download.pytorch.org` | see §6 |
| clang | **18.1.3**; `clang++-23` ABSENT; **no libc++ `std.cppm` anywhere** |
| cmake / ninja | 3.28.3 / 1.11.1 |

**THEREFORE: sensen CANNOT BE COMPILED IN THIS CONTAINER.** `import std;`
requires clang 23 plus a libc++ that ships `share/libc++/v1/std.cppm`; neither
is present. Any sensen C++ written in this session is **UNCOMPILED AND
UNTESTED** and must be labelled so wherever it lands. This is consistent with
CLAUDE.md's own note that a C++23-modules + sensen + gRPC build does not fit a
hosted runner.

The verified deliverable from this session is therefore the PyTorch side plus
the measurements. Do not let the C++ ride on the Python's green.

Submodule checkout, for the next session:
```bash
git clone --depth 1 https://github.com/oldboldpilot/sensen /home/user/sensen   # HEAD == the pin
git -c protocol.file.allow=always \
    -c submodule."backend/sensen".url=/home/user/sensen \
    submodule update --init backend/sensen
```
`protocol.file.allow=always` is required — git blocks file-transport submodules
by default since CVE-2022-39253, and without it the clone fails with
`transport 'file' not allowed`.

---

## 5. Repository state at the time of writing

Committed on `claude/finetune-bert-models-quantization-ngwmnd`:
- `agent/analysis/extractability.py` — the measurement in §1b. Reusable: it
  exports the lexer, the map table, `at_label_precision`, `as_decimal`.
- `agent/analysis/ambiguity.py` — the min-error disambiguation evidence in §1e.
- `agent/analysis/lengths.py` — the input/output token counts in §1c.
- `agent/dataset/data_mortgage/` — the regenerated mortgage corpus (gitignored
  if large; regenerate with the seed above, which is recorded for exactly this reason).
- this document.

In flight when written, status UNKNOWN: a PyTorch harness under `agent/train/`
(`encoder_corpus.py`, `encoder_model.py`, `encoder_tokenizer.py`,
`train_encoder.py`, `quantize_encoder.py`). Check whether those files exist and
whether their reported numbers are real before quoting any of them.

---

## 6. What to do next, in order

1. **Get `huggingface.co` allowlisted** and re-run the comparison with
   pretrained MiniLM-L6 (22M) as a second arm against the from-scratch model.
   The from-scratch result is the floor, not the recommendation.
2. **Fix Gap 1** — a per-row masked cross-entropy on sensen's autograd tape.
   Nothing else on the sensen side can be trained without it.
3. **Baseline before anything else.** Measure the CURRENT `ParseOperation`
   end-to-end latency and `raw_exact`/`served_exact` on the pinned holdout
   BEFORE touching serving, so any change is attributable. This repository's own
   scale-widening cutover was caught only because a pre-deploy baseline existed.
4. Gate any new serving path OFF by default behind an env var that refuses an
   unrecognised value at boot, the way `MORTGAGE_WEIGHT_STORE` does.
5. Do NOT re-record any digest or holdout figure because a test went red. Verify
   against an independent identity first.

## 7. Open questions

- Does the 2.23% mortgage residue shrink to zero once the derivation layer runs,
  or are some rows genuinely unanswerable? Not measured.
- `monthly_overpayment` 12/244 and `extra_monthly_payment` 10/21 unexplained,
  values look stated in the utterance (`$100 more a month`). Either a lexer gap
  or a corpus defect. **Not diagnosed** — worth ten minutes.
- Row-level coverage is 82.08% (mortgage) / 54.48% (strategy) against 96–98%
  value-level coverage. Row coverage is the number that bounds end-to-end
  accuracy, and the gap is concentrated in the convention fields. Confirm the
  serving-layer defaults close it.
