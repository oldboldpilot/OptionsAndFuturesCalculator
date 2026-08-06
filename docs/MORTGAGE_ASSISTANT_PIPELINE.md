# Mortgage assistant: dataset-to-serving pipeline

`@author Olumuyiwa Oluwasanmi`

The end-to-end chain for `mortgage.assistant.MortgageAssistant`'s fine-tuned
Qwen3-0.6B — the model behind mortgagefvcalculator.com: how the training data
is built, how the model is trained, how it becomes the file the backend loads,
what guards its output on the way to a caller, and what it actually scores.

This is the **sibling** of `docs/STRATEGY_ASSISTANT_PIPELINE.md`, not a section
of it. That document is scoped end to end to a DIFFERENT model — the options
assistant's — and every unqualified "the model" in it means that one. Every
unqualified "the model" here means this one. Distribution mechanics common to
both live in `docs/MORTGAGE_MODEL_DISTRIBUTION.md`; the hosting decision behind
them is `docs/superpowers/specs/2026-08-05-container-model-hosting-design.md`.

**Read §7 before quoting an accuracy number from this page.** This model does
not meet the sibling's 95.0% bar and this document does not pretend otherwise.

---

## 1. Dataset

`agent/dataset/build_mortgage_dataset.py` (1,876 lines) generates a
ShareGPT-shaped JSONL set. Its output is committed as
`agent/dataset/data_mortgage/{train,val}.jsonl`.

### 1.1 The label space is parsed, never typed

The options dataset gets its label space from `strategies.json`, the same file
`strategy_catalogue.cppm` is generated from, so model and validator cannot
drift apart. `sensen.finance.Finance` has no `strategies.json` equivalent, so
this generator gets the same property a different way: it **parses
`backend/proto/finance.proto` directly** at build time.

| | |
| --- | --- |
| parser | `parse_finance_proto()` (line 133), `_PARSED` at line 211 |
| source | `PROTO_PATH` at **line 80** — `backend/proto/finance.proto` |
| scope selection | by the proto's own section banners (`IN_SCOPE_SECTIONS`), not a hand-listed RPC set |
| hand-curated exclusion | `EXCLUDE_RPCS` — `ConvertInterestRate`, `ComputeFisherRate` (rate-theory conversions nobody phrases as a chat message) |
| resulting label space | **26 operations, 160 fields** |

Verified by loading the module: `len(OPERATIONS) == 26`, and summing each
operation's field list gives exactly **160**. Every RPC name and every field of
its request message is read out of the `.proto` text. Adding a field to
`AmortizationRequest` widens this dataset the next time it runs; widening a
banner's section widens its scope. That is not an aspiration — it was observed:
six RPCs (`ComputeRefinance`, `ComputePayoffTiming`, `ComputeMortgageRecast`,
`ComputeHomeFutureValue`, `ComputeRentVsBuy`, `ComputeHomeNpv`) landed in the
proto mid-session and `build_operations()` picked them up with zero edits to
`IN_SCOPE_SECTIONS`, which is what let the speculative off-by-default registry
the original brief called for be deleted instead of maintained.

`main()` closes with a self-check that prints, verbatim:

```
  PASS: every emitted operation id exists in finance.proto
```

and reports any uncovered operation **by name** rather than failing silently.

### 1.2 What it emitted

| | value |
| --- | --- |
| train | **11,400** rows |
| val | **600** rows |
| refusal-family rows | **835** of 12,000 = **6.96%** |
| operations covered | **26/26** of the proto-derived label space |

The refusal family is `make_refusal` / `make_chitchat` /
`make_unknown_operation` — rows that end WITHOUT a `<params>` block. Its weight
(0.07 for `make_refusal` alone) is deliberately **above** the options
assistant's 0.05: advice, prediction, eligibility and legal questions are the
dominant real-world non-extraction class in this domain, and a model with
essentially zero refusal training mass on a page that computes somebody's
mortgage is the worst thing this could become.

### 1.3 Four hallucination families, found and fixed

Each of these taught the model that **asserting a value which appears nowhere
in the user's text is correct behaviour**. They are not cosmetic label noise;
they are the training-time form of the exact defect §5 exists to refuse at
serving time.

| family | rows | the text said | the label asserted | fixed to |
| --- | --- | --- | --- | --- |
| PMI | 637 | *"so PMI applies"* | `pmi_annual_rate: 0.0112` | the text now states it: *"PMI runs 1.12% a year"* |
| Refinance | 604 | *"20.8 years left"* | `250` months (20.8 × 12 = **249.6**) | the text now states months: *"250 months left"* |
| Depreciation | 220 | *"27.5-year life"* | `recovery_period: 27` (truncated) | emits the stated life — 27.5 stays 27.5 |
| XNPV/XIRR dates | 248 | *"after 14 months"* (days ÷ 30.44) | exact day counts | the text now states days: *"after 426 days"* |

The fixes are all of one shape: **make the text state what the label asserts**,
never make the label guess what the text implied. In the generator today, PMI's
mention flag is sampled BEFORE `home_value` and the rate, not after
(`build_mortgage_dataset.py` ~line 478) — the reverse order is precisely the
bug; the refinance and XNPV generators phrase months and days directly
(lines ~776, ~836, ~614); and depreciation emits `life` as stated, with
`recovery_period` rounded only when the life is already an integer (line ~715).

**Verification after regeneration: 0 ungrounded rows across all four families,
of 11,400.** Before the fix, PMI alone contributed 641.

**Why this matters more than the row count suggests.** A model trained on
`"so PMI applies"` → `pmi_annual_rate: 0.0112` has been taught, thousands of
times, that inventing a plausible number is the correct completion. That is not
a different defect from `5378.63 → 5379.00` at serving time (§5) — it is the
same defect, installed upstream, where no gate can see it. Structurally the
output is flawless: real operation, real field, parses, in range. It simply
prices a loan the borrower never described. Teaching that at training time and
then blocking it at serving time means every request pays for the gate; not
teaching it is free.

Run it with:

```bash
cd agent/dataset && python3 build_mortgage_dataset.py --out data_mortgage/ --n 12000 --seed 0
```

---

## 2. Training

```bash
python3 train.py --method qlora \
  --data ../dataset/data_mortgage \
  --out <dir> \
  --epochs 4
```

Same `agent/train/train.py` as the options assistant, same recipe, different
data. Reused rather than redesigned; see that file's top-of-module comment.

| | value |
| --- | --- |
| base | `unsloth/Qwen3-0.6B`, `load_in_4bit` |
| method | QLoRA — 4-bit frozen base + LoRA adapters trained in bf16 |
| rank / alpha | **16 / 16** |
| learning rate | 2e-4 |

### `--epochs 4` is load-bearing and is NOT redundant

`train.py` line 90 declares `ap.add_argument("--epochs", type=float,
default=2.0)`. **The default is not the recipe.** Reading it as the recipe on
the sibling project produced a 2-epoch retrain that scored **5/16** against the
4-epoch model's **16/16** and was discarded. `run_qlora_mortgage.sh` passes
`--epochs 4` explicitly and says why on the line above it. Anyone invoking
`train.py` by hand must pass it too.

> The runner script itself lives on the training host, not in this repository
> — the same arrangement as the sibling's `/scratch/agents/run_qlora.sh`.
> `grep`ping this tree for it will come up empty; that is expected, not a
> missing file.

### 2b. Model of record — what the pinned GGUF was trained from

The v2 run. Recorded because "which model is this?" is the question that has
cost the most rework across both assistants.

| | value |
| --- | --- |
| data | `agent/dataset/data_mortgage` — 11,400 train / 600 val (the FIXED dataset, §1.3) |
| base | `unsloth/Qwen3-0.6B`, `load_in_4bit=True` |
| method | QLoRA, rank 16 / alpha 16, lr 2e-4 |
| epochs | **4** (explicit; the script default is 2) |
| steps / wall clock | **700 steps in 521.7 s** (0.745 s/step) |
| hardware | RTX PRO 6000 |
| trainable params | **10.09M / 398.5M (2.53%)** |
| sequence length | `token length p99=393` → `max_seq_length=512` |
| losses | `train_loss` **0.2534**, `eval_loss` **0.1538** |
| peak GPU memory | **3.26 GB** |

`max_seq_length=512` is derived, not guessed: the p99 token length of the
training rows is 393, and 512 is the next power of two above it. The mortgage
label space is wider than the options one (fourteen fields on
`HomeNpvRequest`/`RefinanceRequest` versus five), which is why this is 512 where
the sibling is 1024 on a much longer conversational shape.

**v1 — superseded.** Trained on the un-fixed dataset, before §1.3:

| | v1 | v2 |
| --- | --- | --- |
| steps | 696 | 700 |
| wall clock | 449.7 s | 521.7 s |
| `eval_loss` | 0.1586 | **0.1538** |

v1 is not the model of record and its checksum should not be pinned. It is
recorded here only so a stray GGUF on a training host can be identified rather
than guessed at (§3).

---

## 3. Merge, export and conversion

`train.py` saves the LoRA adapter on its own first (a few MB, always succeeds),
then folds it into the frozen base to produce a **bf16 merged checkpoint**
(`save_pretrained_merged`, `save_method="merged_16bit"`).

**That merged directory is an INTERMEDIATE artifact. It is not what ships and
it is not what any accuracy number on this page is measured on.** It is the
converter's input and nothing else. §7 returns to this, because quoting a
number measured on it is the single most expensive mistake available here.

Conversion uses **sensen's own converter**, per CLAUDE.md's standard:

```bash
ninja -C backend/sensen/build convert_safetensors_to_gguf validate_gguf   # once
scripts/convert_to_gguf.sh <merged_dir> <out.gguf> q8_0
```

`sensen::convert_safetensors_to_gguf` writes Q8_0 **directly** from the merged
safetensors — one call, no f16 intermediate, no two-step llama.cpp pipeline.

| | value |
| --- | --- |
| tensors | **310** |
| size | **639,447,136 bytes** |
| v2 sha256 | `269efd32a5533ff94fc31975f0cbee2c46ba47863a924a0745886fdbc3b413fe` |
| v1 sha256 | `31ed8f45b3fca52cf46d99ccecc65e9a7210b736cbc39ae043270be40c5630a5` |

Three precisions serve three purposes and this is not a contradiction: 4-bit
frozen base at TRAIN time (cheap, never updated, so its precision does not bound
final quality); bf16 adapters merged at EXPORT time (full precision for the part
that actually learned something); Q8_0 at SERVE time (the CPU deployment
target's real runtime format).

### The converter was ruled out as a variable

When the served accuracy came in far below expectation, the shipped GGUF was
found to have been converted by a **Jul-24 `convert_safetensors_to_gguf` binary
from a different sensen checkout** than the repo's Aug-3 build. That is exactly
the kind of thing that becomes a multi-day theory. It was closed by measurement
instead: re-converting the same merged checkpoint with the repo's correct binary
produced a **byte-identical file — the same sha256 `269efd32…`**. Converter
version is not the cause of anything in §7. Do not re-open it.

---

## 4. Serving

| | |
| --- | --- |
| service | `mortgage.assistant.MortgageAssistant` |
| RPC | `ParseOperation` |
| contract | `backend/proto/mortgage_assistant.proto` |
| implementation | `backend/src/modules/mortgage_assistant_service.cpp` |
| model path | `MORTGAGE_MODEL_PATH` (`/app/model/mortgage-assistant.gguf` in the image) |

### 4.1 Three outcomes, two of which are successes

`ParseResponse` is a `oneof { FinanceParams params; Clarification; Refusal }`.

**A clarification and a refusal are both SUCCESSFUL outcomes and both return
gRPC OK.** A clarifying question is the model doing its job when it is missing a
fact it cannot honestly default; a refusal is the model doing its job when the
alternative is inventing an interest rate, a term or a principal nobody stated.
Encoding either as a gRPC error would make every well-behaved interaction
register as a failure in gRPC metrics, in Envoy's access log and in the browser
client's error handling — indistinguishable from the model being down. Only the
RPC itself failing (full admission queue, malformed request) is a gRPC error.

This matters more here than on the options side: a fabricated PARAMETER has
nothing downstream to catch it. `ComputeAmortization` will happily amortize a
principal the borrower never mentioned and return an exact, auditable,
completely wrong schedule.

### 4.2 The payload is a string map, on purpose

```proto
message FinanceParams {
  string operation = 1;
  map<string, string> params = 2;
}
```

**Why not 26 request messages.** A mirrored `oneof` of 26 message types here
would be a hand-maintained SECOND copy of `finance.proto` — 26 messages, 160
fields — that could drift from the first, in a system whose entire dataset
design (§1.1) exists to make drift impossible. And it would buy little: most of
those fields are already `string` in `finance.proto` itself, because they are
`BigDecimal` money and rate values that must not round-trip through a float
(CLAUDE.md's "gRPC Surface" section covers why). A
`map<string, google.protobuf.Value>` was the other candidate and was rejected:
it reintroduces float64 for exactly the fields that must not have it.

### 4.3 Four load-bearing serving constraints

Each of these cost a debugging cycle. All four are also stated as comments
beside the code they constrain.

**1. The model requires its exact training system prompt.**
`mortgage_assistant_service.cpp:130`, `kSystemPrompt`. It is **322 characters**
and must be byte-identical to the `system` turn of every row in
`agent/dataset/data_mortgage/train.jsonl` — itself emitted from the `SYSTEM`
constant in `build_mortgage_dataset.py`, whose own comment states the
requirement from the other side. Verified: the constant in the `.cpp` and
row 0's system turn are 322 characters and compare equal. Without it the model
reverts to stock Qwen3 — prose, a `<think>` block, and **never** a `<params>`
block. That is not a degradation this file could detect and report: every
request would simply fall through to the clarification/refusal path with no
indication of why. So it is injected on every call with no opt-out, and changing
so much as a comma silently converts a working extraction model back into an
uninstructed base model.

**2. Qwen3 emits a `<think>` block on every response, including correct ones.**
`strip_think_block()`, `mortgage_assistant_service.cpp:1281`. The block being
**empty** is the signal the system prompt took — the correct shape is
`<think>\n\n</think>\n\n<params>{...}</params>`. Treating its mere presence as
failure rejects every valid answer. The stripper is also deliberately tolerant
of an unterminated `<think>`: the model was measured emitting
`<think>\n\n<params>{...}</params>` — a complete answer simply missing its
closing tag — and dropping those turned correct parses into refusals.

**3. `n_gpu_layers = 0` on a CPU build.**
`mortgage_assistant_service.cpp:563`. A default-constructed
`sensen::GenerationConfig` arrives with `n_gpu_layers = SIZE_MAX` and
`compute_backend = AUTO`. Together those satisfy every conjunct of an
"on-device greedy sampling" fast path inside `LLMPipeline` — greedy strategy, no
penalties, no grammar, no logprobs, a backend that counts as GPU (AUTO did), all
layers on GPU (`min(SIZE_MAX, L) == L`). The pipeline then sets
`on_device_sampling` and, from the second generated token onward, reads the next
token id out of `logits[0]` — but the only code honouring that flag by returning
a one-element vector holding the sampled id lives inside `#ifdef
SENSEN_HAS_CUDA`, which this CPU-only build never defines. The CPU path returns
the full vocabulary-sized logit vector and never consults the flag, so
`logits[0]` is a raw float score; casting it to a token id lands in the low
single digits, which in Qwen's vocabulary are the punctuation characters
`&'()*+,-`. The symptom on the sibling service was 262 bytes of punctuation
noise, byte-identical across runs, with only the first token correct. Fixed
upstream in sensen; the line stays regardless, because it states what this
service actually wants and keeps it correct against any sensen build.

**4. `repetition_penalty = 1.0F`.**
`mortgage_assistant_service.cpp:586`. As load-bearing as constraint 3, and it
was the whole of this service's accuracy failure.

`sensen::Sampler::sample` (`llm_pipeline.cppm`) applies `repetition_penalty` to
the logits **BEFORE the greedy argmax** — so it changes which token "greedy"
picks, despite greedy decode having no business being penalised at all.
`GenerationConfig`'s default is **1.1 over a 64-token window**
(`llm_interfaces.cppm`), and the penalty divides a logit by 1.1 **once per
occurrence** in that window, i.e. exponentially in repeat count.

Structured JSON is digit-dense and Qwen3 tokenises one digit per token, so `'0'`
lands in the window 8–12 times in a normal params block and its logit collapses
by 14–18 points.

| A/B on 90 identical held-out rows | penalty 1.1 | penalty 1.0 |
| --- | --- | --- |
| params exact-match | **0/90** | **25/90 (27.8%)** |
| `<params>`-but-bad-JSON blocks | 12 | 3 |
| rows carrying a homoglyph digit | 2/100 | 0/100 |

The homoglyph result is the mechanism made visible: U+FF10 (fullwidth zero) is a
DIFFERENT token id, so it escapes the penalty entirely while ASCII `'0'` is
being crushed, and greedy then prefers it. Nothing was wrong with the decode
path or the checkpoint.

Full write-up, including the three-run experiment table, the control on
`SENSEN_KV_DTYPE`, and what was explicitly NOT determined:
`docs/session_logs/session_2026-08-05_penalty_hosting_and_harness.md` and
`docs/superpowers/specs/2026-08-06-mortgage-rpc-accuracy-gap.md`.

### 4.4 Concurrency

`generate()` **cannot be called concurrently.** sensen's `FeedForwardNetwork`
keeps per-call scratch as `mutable` MEMBERS, not `thread_local` — two threads
decoding different requests interleave writes into the same buffer and silently
corrupt each other's hidden state. A wrong-answer race, not a crash. Concurrency
therefore comes from the iteration-level scheduler: **one owner thread**, one
fused `forwardBatch` per step, batching every in-flight sequence.

`kMaxNewTokens = 384`, sized from the training set rather than guessed: the
longest assistant turn in `train.jsonl` is 473 characters (a `ComputeHomeNpv`
block, fourteen fields, mostly digits), which is the worst case this label space
can produce. It is larger than the strategy assistant's 256 because that model's
widest answer is six short fields. It is also the exact figure `cost_llm_generate`
charges against, so raising it moves the ceiling and the price together.

---

## 5. Verification — the GP-ARA gate

`backend/src/modules/mortgage_verification.cppm`, wired into `ParseOperation` by
commit **`c873f9e`** ("Make the mortgage verifier something other than a test
fixture"). Before that commit the module existed, passed its tests, and **had no
consumer on the serving path** — its only importer was its own test. A verifier
nobody knows is inert gets counted as coverage, which is worse than no verifier.

`verify_mortgage_output` is now the **only** route to a `FinanceParams`.

### 5.1 Shape

Five gates, a tri-state verdict, **default-deny**.

| gate | asks |
| --- | --- |
| G1 | is the operation id in the closed vocabulary? |
| G2 | is every field name declared by THAT operation? |
| G3 | **value grounding** — is every emitted number traceable to the user's own words? |
| G4 | absent-params handling |
| G5 | bounds |

`Outcome` is `{ Proven, Unsafe, Indeterminate }` and
`VerificationVerdict` **default-constructs to `Indeterminate`**, so a future
early return that forgets to set an outcome fails closed rather than open.
`Proven` is the only path to params; `Unsafe` and `Indeterminate` take the
identical refusal branch. Serving on `Indeterminate` would mean serving on the
catalogue of the rule table's own blind spots, which reduces an attacker's job
to finding one of them.

Nothing is repaired. `ComputeFutureValue` is not silently promoted to
`ComputeFutureValueDetailed`; `years` is not renamed to `target_years`;
`5379.00` is not snapped back to `5378.63`. The options module ships
`normalize_strategy_alias`; this one deliberately does not.

**Test: 72 checks, 0 failures** (`test_mortgage_verification`, run against the
current build).

### 5.2 The grounding gate

A literal `v` lexed from the user's text, carrying an adjacency tag, expands to
candidates for a slot of kind K under **eight** admissible maps and in no other
way:

| | map | value | applies when |
| --- | --- | --- | --- |
| M1 | identity | `v` | every kind, tag compatible with K |
| M2 | percent→decimal | `v / 100` | K ∈ {Rate, Ratio}, tag PERCENT — or UNTAGGED with 1 ≤ v < 30 |
| M3 | annual→per-period | `v / n`, `(v/100) / n`, n ∈ {1,2,4,12,26,52} | K == Rate **and** the field is named exactly `rate` or `guess` |
| M4 | magnitude | `v × 1e3`, `v × 1e6` | K == Money, literal carries a k/thousand or m/million suffix — and the UNSCALED value is then **not** a candidate |
| M5 | years→months | `v × 12` | K ∈ {MonthCount}, tag ∈ {YEARS, UNTAGGED} |
| M6 | months→years | `v / 12`, exact only | K == YearCount, tag MONTHS — 360 months becomes 30 years, 361 becomes nothing |
| M7 | years→days | `v × 365` | K == DayOffsets, tag YEARS |
| M8 | negation | `-v` | ONLY the repeated field `values`, a cash-flow series where an outlay is negative |

Thousands separators and currency symbols are **not** maps: `$1,356,200` lexes
as the literal `1356200`, so `1356200.00` matches under M1. Separators are
notation, not arithmetic.

M3 is the widest map and is therefore the most tightly fenced — it never applies
to a field whose own name says `annual_` (`annual_rate`, `current_annual_rate`,
`new_annual_rate`), so the per-period-versus-annual "12× error" is still refused
wherever the slot's name settles the question.

### 5.3 Tolerance is each slot's own serialization precision

| slot kind | places | tolerance |
| --- | --- | --- |
| Money | 2 | 0.005 |
| Rate, Ratio | 6 | 0.0000005 |
| every count / index / offset | exact | 0 |

These are not chosen by feel. They are the precisions the **training labels** are
serialized at: `money_str` is `f"{v:.2f}"` and `rate_str` defaults to
`f"{v:.6f}"` in `build_mortgage_dataset.py`. The rule is *"the model may round to
the precision it was taught to write, and not one digit further."*

That single rule is what separates the two cases:

```
  4.68%   -> 0.0468     M2 gives 0.0468 exactly.                       PASSES
  6.5%    -> 0.005417   M3 with n=12 gives 0.00541666...; the correct
                        6-place rounding is 0.005417. |diff| = 3.3e-7
                        < 5e-7.                                        PASSES
  5378.63 -> 5379.00    M1 gives 5378.63 exactly. |diff| = 0.37 —
                        74x the money tolerance.                       REFUSED
```

6.5% → 0.005417 **must** pass: every per-period TVM label in the training set is
produced that way, and `finance.proto` documents those slots as per-period.
5378.63 → 5379.00 **must** refuse: rounding a payment to the whole dollar is not
a serialization precision this contract has, and the loan it prices is a
different loan.

A small `kConventionValues` whitelist exempts fields emitted from a stated
convention rather than from the text — `payments_per_year: 12`,
`future_value: 0`, `monthly_overpayment: 0`, `guess`, `pmi_drop_off_ltv: 0.80`.
Each is exempt at **one exact value only**: `payments_per_year: 12` is exempt,
`payments_per_year: 26` must be grounded like anything else. It is a whitelist
of constants, not of fields, so it cannot be used to launder an arbitrary
invention.

### 5.4 The decisive test

The module's own test asserts **both halves** on one row:

- The structural gates alone (a plain schema validator) return **`Proven`** on
  `current_monthly_payment: "5379.00"`. That is exactly the evidence a schema
  validator would have served it on.
- The composed gate returns **`Unsafe` / `UngroundedValue`, naming `5378.63`**.

`5379.00` names a real operation, sits in a real field of it, parses as a
decimal, and satisfies every bound `ComputeRefinance` enforces. Everything
upstream inspects the model's output against ITSELF; none of it can see the
utterance, so none of it can ask the only question that matters. This is why the
utterance is threaded down through `interpret_model_output` into
`validate_and_populate_params` rather than the gate being called somewhere
convenient: **the params path is the one path that must never be walked without
the text the numbers were supposed to come from.**

### 5.5 Live proof from the wiring

For a request stating **$420,000 at 6.25% over 30 years**, the raw model output
was:

```json
{"operation":"ComputePayment","rate":"0.00519167","periods":368,
 "present_value":"429984.73", ...}
```

Structurally flawless. A different loan. **Served pre-`c873f9e`, refused
post-`c873f9e`.**

Two further decisions in that commit are load-bearing:

- The verifier's object is assembled in the **same pass** that builds the wire
  message, from the already-encoded value rather than by re-walking the JSON, so
  what the gate judges is byte-for-byte what the caller would have received.
  There is no second encoding path that could differ.
- A **clarifying question is deliberately not routed through the gate.** G4
  answers `Indeterminate` for an absent params block, which is right for a
  caller about to dispatch something — but there is nothing to dispatch when the
  model asked a question. Feeding the absent case to the gate would convert all
  824 clarification shapes in the training set into refusals.

---

## 6. Constrained decoding — built, NOT yet wired

`backend/src/modules/mortgage_grammar.cppm`.

**State it plainly: this is not in the decode loop.** Nothing described in this
section affects a served request today. It is complete, tested, and waiting on
one of the two integration routes below.

What it is: a `sensen::IGrammar` — not a parallel mechanism, the third
implementation of an interface sensen already has (`backend/sensen/src/grammar.cppm`;
`mask(logits)` → set forbidden logits to −inf → `accept(token)`). It derives its
label space from `mortgage_verification`'s **exported accessors** rather than
keeping its own table, so the grammar and the gate cannot disagree about what
exists.

It makes the following **unrepresentable** rather than merely detectable:

- `ComputePayoff` and other operation ids that do not exist at all
- `years` where the operation declares `target_years`
- `"timing"]=`, unquoted keys, unbalanced brackets — output that is not even
  valid JSON

**Test: 139 checks, 0 failures.** Including a non-vacuous control, which is the
part that makes the number mean something: it accepts **554/554** gold params
objects, across **26/26** operations. A grammar that refuses everything also
scores zero failures on the refusal cases; only the acceptance side proves it
did not over-constrain.

The gold set also pins the emitted shape: no whitespace anywhere (554/554),
`operation` first (554/554), keys in `finance.proto` declaration order (554/554).

### The two routes to wiring it

`sensen::GenerationConfig` has **no `IGrammar*` field**, so a custom grammar
cannot simply be handed to `generate()`.

1. **Code change:** construct the grammar and pass `&grammar` to
   `LlmPipeline::sampleGuided`, which does take a `const IGrammar*`.
2. **No code change:** use `params_regex()`, which emits a pattern for
   `GenerationConfig{ .grammar_kind = GrammarKind::Regex, .grammar_pattern = ... }`.
   `sensen::RegexGrammar::create` compiles it and it accepts the same 554/554
   gold objects and refuses the same negatives as the automaton — verified in
   the test. The cost: **8,881 characters** of pattern, and some thousands of
   NFA states.

`params_regex()` **refuses** to emit at all unless `require_declaration_order`
is on, rather than approximating the order-free language.

---

## 7. Honest limits — the section that matters most

**This model does not meet the 95% bar. It is at 27.8%.**

### 7.1 The measured number

| | |
| --- | --- |
| **params exact-match, real RPC, sensen, Q8_0** | **25/90 = 27.8%** |
| the bar | 95.0% |
| harness | `agent/train/eval_grpc_mortgage.py` |
| conditions | one verified engine (`pgrep -x calculator_engi \| wc -l` == 1), `MORTGAGE_MODEL_PATH` at the pinned GGUF, `--n 100`, identical rows every run |

That is the number. It is measured through the production serving path, on the
file that actually ships, with the §4.3 constraint-4 fix in place.

### 7.2 The ceiling is the model, not the engine

Independently confirmed: a `numeric_audit_probe` greedy rollout (`LlamaModel`
direct, raw argmax, no `LLMPipeline`, no sampler, no RPC) scores **8/30**, and
**llama.cpp on the identical rollouts also scores 8/30**. Two independent
implementations agreeing to the row means the number describes the checkpoint,
not a serving artifact. 25/90 = 27.8% and the fp32-KV control's 24/90 = 26.7%
bracket the probe's 8/30 = 26.7%: with the penalty off, the RPC serves **exactly
the accuracy the model has**.

This is also the one legitimate use of llama.cpp in this repository — an
INDEPENDENT implementation for cross-checking, per CLAUDE.md. It is not a
serving alternative and not a conversion step.

### 7.3 Do NOT quote `evaluate.py`'s numbers

**`agent/train/evaluate.py` reports 31.7% and 36.4%. Both are wrong for this
purpose and neither belongs in any summary of this model.**

It measures **transformers on the bf16 merged intermediate** (§3) — a format no
request is ever decoded by. Its own sibling `agent/train/eval_grpc.py` says so in
its docstring. Those two figures were quoted as this model's accuracy before
anyone noticed, while the real serving path was scoring **0/279**.

The correct harness is **`agent/train/eval_grpc_mortgage.py`**, which drives the
real `ParseOperation` RPC against the real engine holding the real GGUF.

Its per-field breakdown is worse than merely inapplicable — it is actively
misleading. `evaluate.py:45` hardcodes its field list:

```python
fields = {k: 0 for k in ["symbol", "asset_class", "strategy", "expiration_days", "quantity"]}
```

Those are the **OPTIONS** assistant's five fields. None of them exists in this
model's 160-field label space, so every row scores the same way on the same
absent keys — which is why every row reports an identical **77.4%**. A constant
is not a measurement.

### 7.4 What actually fails

The dominant remaining failure is **schema confusion across 26 operations ×
160 fields**:

| emitted | wanted |
| --- | --- |
| `property_price` | `current_property_value` |
| `years` | `target_years` |
| `extra_payment` | `lump_sum_payment` |
| `compounding_frequency` | `compound_frequency` |
| `ComputeFutureValue` | `ComputeFutureValueDetailed` |

In run 2's residuals: 35 `unknown-field` refusals on off-schema field names,
5 hallucinated operation names, and clarification rows asked 0/10.

**This is a task-difficulty problem for a 0.6B model, not a data problem.** The
comparison that settles it is internal: the options assistant reaches 16/16 on
**one operation with five fields**. This one is asked to pick one of 26
operations and then fill the right subset of 160 fields, most of which are
near-synonyms of each other across operations. §1.3 removed the ungrounded rows;
the residual errors are not ungrounded values, they are wrong slots.

This is also exactly the failure class §6's grammar makes unrepresentable, which
is why that module exists and why wiring it is the highest-value next step.

### 7.5 Served outcome distribution

At n = 100:

| outcome | share |
| --- | --- |
| params | 33% |
| clarification | 4% |
| refusal | 63% |

**The refusals are largely correct.** The validator is declining genuinely wrong
params — off-schema fields, hallucinated operations, ungrounded numbers. A 63%
refusal rate on a model with a 27.8% exact-match rate is the gate working, not
the gate misfiring. The honest reading is that this service currently answers
about a third of requests and correctly declines most of the rest, rather than
answering all of them wrongly. That was the state before `c873f9e` and it was
worse.

---

## 8. Swapping the served model

Mirrors `STRATEGY_ASSISTANT_PIPELINE.md` §4's procedure, with the transport
changed. Full distribution detail: `docs/MORTGAGE_MODEL_DISTRIBUTION.md`.

### Why a build-time fetch

`railway up` **cannot carry a 639 MB model** — measured, not assumed: it returns
**HTTP 413** from Cloudflare on the upload payload (321 MB passes, 619 MB
fails). The runtime container has neither `curl` nor `wget`, so nothing in it can
pull a model at boot even if a URL existed. The fetch therefore happens at
**Docker build time**, in `backend/Dockerfile`'s dedicated `model` stage.

### Where the weights live

| | |
| --- | --- |
| store | **private Railway bucket** — same project, environment and region as the service, so the bytes never leave Railway |
| bucket | **`model-artifacts`**, region **`sjc`** |
| endpoint | **`t3.storageapi.dev`** — *not* the `*.storage.railway.app` the docs imply; recorded verbatim because guessing it costs a build cycle |
| auth | SigV4-signed GET (`curl --aws-sigv4`); `wget` cannot sign an AWS-style request at all |
| build args | `MORTGAGE_MODEL_URL`, `MORTGAGE_MODEL_SHA256`, `BUCKET_ACCESS_KEY_ID`, `BUCKET_SECRET_ACCESS_KEY` |

Buckets are private by default and Railway does not support public ones, so
there is no way to accidentally expose them. The weights are proprietary and
deliberately not on any external model registry.

### The procedure

```bash
# 1. Upload the candidate to the bucket (railway bucket / rclone / aws s3, SigV4).

# 2. PROVE THE BYTES SURVIVED THE ROUND TRIP.
#    Re-download from the bucket and re-checksum. Do NOT assume the upload was
#    faithful, and do NOT pin the checksum you computed against your local copy:
#    the local checksum and the SERVED checksum are two different claims, and
#    only the second one is what the build will verify.
#    The file MEASURED and the file SERVED must be provably identical.
sha256sum /tmp/verify.gguf   # must equal what you are about to pin

# 3. Repoint and redeploy. URL and SHA move TOGETHER — changing the URL alone
#    fails the checksum, changing the checksum alone fetches the old file and
#    fails the same way.
railway variables --service options-calculator-backend \
  --set 'MORTGAGE_MODEL_URL=...' \
  --set 'MORTGAGE_MODEL_SHA256=<sha>' --skip-deploys
railway up --detach --service options-calculator-backend

# 4. Re-measure through the real RPC (§7.1), never evaluate.py (§7.3).
```

A checksum mismatch **fails the build outright**. It never falls back to "no
model": a truncated or substituted GGUF loads fine and then generates fluent,
confident, wrong text — and this one answers questions about somebody's
mortgage, where wrong-but-plausible is the entire hazard. An **empty**
`MORTGAGE_MODEL_URL` is a deliberately supported no-op build; the calculator and
the finance surface do not need the assistant, and failing the whole image over
an optional feature would take down working functionality for an unrelated
reason.

### Four traps in this path, each of which produced a red or silent build

1. **A global `ARG` not redeclared inside the `model` stage** makes the SigV4
   branch see empty credentials, fall through to unauthenticated `wget`, and get
   a 403. `backend/Dockerfile:150–151` exists for this. It is the same
   ARG-scope bug as the `MODEL_TOKEN` one the file already warns about.
2. **`--mount=type=secret` anywhere in the Dockerfile** is rejected by Railway's
   Metal builder — silently. The build never starts, the previous container
   keeps serving, and `railway up` still exits 0. A local `docker build` will not
   reproduce it. Pass credentials as plain `ARG`s confined to the `model` stage;
   the runtime stage only does `COPY --from=model`, so no ARG value reaches a
   published layer.
3. **An empty `backend/models/` directory is not carried in the upload tar**, so
   a `COPY` targeting it fails. A `.gitkeep` fixes it — git does not track empty
   directories and neither does the tar built from it.
4. **A model staged in `backend/models/` while the `.railwayignore` exception is
   present** makes every subsequent `railway up` 413. The staging path is a
   LOCAL-BUILD stopgap, not a Railway mechanism.

Also measured rather than assumed: the two models do **not** cache
independently. Declared build args reach every `RUN` in a stage as environment
whether or not that `RUN` mentions them, so changing `MORTGAGE_MODEL_URL`
re-executes the strategy-model fetch too (buildah 5.8.4). Swapping either model
re-downloads both. Do not restructure the Dockerfile chasing a cache split the
builder does not honour — and note it costs nothing on Railway, which builds
from a cold cache anyway.

---

## Appendix: discrepancies found while writing this page

Recorded rather than quietly smoothed over, because each is a place where a
reader following an existing document would be misled.

1. **`docs/MORTGAGE_MODEL_DISTRIBUTION.md` records the v1 checksum
   (`31ed8f45…`) under "What ships".** The model of record is **v2**
   (`269efd32…`, §2b/§3), trained on the dataset with the four hallucination
   families fixed. That page's hosting half also still describes the deleted
   private HuggingFace repo under a `SUPERSEDED` banner, whereas the Railway
   bucket described in §8 is live in `backend/Dockerfile` today. Both facts are
   in that document's own status banner; this note exists so the checksum
   mismatch is not read as a contradiction with this page.

2. **`docs/superpowers/specs/2026-08-05-container-model-hosting-design.md`
   proposes the bucket name `ofc-model-weights`.** The bucket actually created
   is **`model-artifacts`** in **`sjc`**, with endpoint `t3.storageapi.dev`. The
   spec is the design; the session log is the record of what was built.

3. **The grounding gate has EIGHT admissible maps, not seven.** M6
   (months→years, exact division only) is easy to miss because it is the only
   map that is a narrowing rather than a widening. Documented in full in §5.2.

4. **`build_mortgage_dataset.py` is 1,876 lines**, not the ~1,325 figure that
   has been quoted for it. Nothing else about the file's description changes.

5. **`run_qlora_mortgage.sh` is not in this repository.** It lives on the
   training host, mirroring the sibling's `/scratch/agents/run_qlora.sh`. §2
   says so; a `find` over this tree returning nothing is expected.
