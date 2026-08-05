# mortgagefvcalculator.com assistant: Qwen3-0.6B pipeline + GP-ARA verification

Date: 2026-08-05. Status: SPEC ONLY — analysis complete; nothing below is implemented.

A second natural-language assistant, for https://mortgagefvcalculator.com/, mirroring
`calculator.assistant.StrategyAssistant`: plain English → calculator parameters,
fine-tuned Qwen3-0.6B (QLoRA rank 16), served **in-process on sensen, Q8_0 on CPU**,
with a sensen GP-ARA component that verifies the model's extracted parameters are
CORRECT (semantically consistent), not merely well-formed — CPU path in the serving
engine, GPU batch path offline.

Sources read for this spec: `docs/STRATEGY_ASSISTANT_PIPELINE.md`, CLAUDE.md
§"Strategy assistant", `docs/guides/ASSISTANT_EVALUATION.md`, `backend/sensen/CLAUDE.md`
§GP-ARA, `docs/superpowers/specs/2026-08-05-mortgagefv-grpc-integration.md`,
`docs/superpowers/specs/2026-08-05-finance-proto-extension.md`,
`agent/dataset/build_dataset.py` (all 783 lines), `agent/train/train.py` (argparse),
`agent/train/defect_holdout.jsonl`, `scripts/generate_strategy_catalogue.py`,
`scripts/eval_assistant_sensen.py`, `backend/proto/finance.proto`,
`backend/proto/assistant.proto`, `backend/src/modules/assistant_service.cpp`,
`backend/src/modules/assistant_verification.cppm` (full banner + structure),
`backend/sensen/src/gp_ara_interfaces.cppm`, `backend/sensen/src/gp_ara_agent.cppm`
(`proveSafetyBatch`), `backend/sensen/src/cuda_backend.cppm` (`GPULogicState`),
`backend/src/main.cpp`. Every claim is labelled **VERIFIED** (read from the tree /
documented in-repo) or **INFERRED**.

---

## 0. The single most important design decision: the source of truth

**The existing system's invariant** (VERIFIED, `build_dataset.py` docstring +
`scripts/generate_strategy_catalogue.py` docstring): the model's label space and the
backend validator both derive mechanically from `agent/dataset/strategies.json` — the
dataset generator reads it directly, and `strategy_catalogue.cppm` is generated from it
and checked in (`--check` mode exists for a drift gate). Neither side is retyped by
hand, so they cannot drift.

**The mortgage equivalent is `backend/proto/finance.proto`. Not `financial.cppm`.**

Reasoning:

1. The assistant's output is a request to a *service*, and `finance.proto` IS the
   service contract. `financial.cppm` proves too much: the six home-finance functions
   merged at sensen `299cc4fc` exist there **and are unreachable over the wire today**
   (VERIFIED — `2026-08-05-finance-proto-extension.md` exists precisely because they
   are not in the proto). A label space derived from `financial.cppm` could teach the
   model to emit an operation that the wire refuses with `UNIMPLEMENTED` — exactly the
   "plausible object the frontend silently drops" failure `strategies.json` exists to
   prevent.
2. The proto also carries the field names, the wire types (decimal-string vs double vs
   int32 — the deliberate numeric contract, VERIFIED `finance.proto:20–38`), the field
   comments stating conventions (per-period vs annual rate, P&I-only, sentinel
   meanings), and the enum values. All of that is parameter-space truth the model must
   emit and the validator must check. `financial.cppm` has none of the wire-level facts.
3. On the backend side, the compiled proto descriptor is *already in the binary*.
   The validator can resolve `sensen.finance.Finance` methods and request-message
   fields from `google::protobuf::DescriptorPool::generated_pool()` at runtime —
   membership checks against the descriptor **cannot** drift from the proto by
   construction, with zero codegen (INFERRED from standard gRPC C++ codegen; the
   generated `finance.pb.cc` registers descriptors in the generated pool — confirm at
   implementation time with a one-line lookup test).

**How the generator reads it so a new RPC widens the label space automatically:**

- `scripts/generate_finance_operations.py` (new) runs
  `protoc --descriptor_set_out=/tmp/finance.desc --include_source_info backend/proto/finance.proto`
  and walks the `FileDescriptorSet` with Python `google.protobuf.descriptor_pb2`:
  for each RPC of `service Finance` it emits one operation record — RPC name, request
  message name, and per-field `{name, wire_type (string-decimal | double | int32 |
  bool | enum{values}), repeated?, leading comment text}`. Output:
  `agent/mortgage/dataset/operations.json`, checked in (the same generate-then-check-in
  pattern as `generate_strategy_catalogue.py`, for the same Docker reason: the
  container build context only COPYs `backend/`, and the training host consumes the
  JSON without protoc — VERIFIED rationale in that script's docstring).
- **Scope is derived, not retyped.** `finance.proto` is organised under section banner
  comments (VERIFIED: `-- Time value of money --`, `-- Mortgages, HELOC --`,
  `-- Cash-flow analysis --`, plus `-- Real estate --` per the proto-extension spec §3;
  bonds/T-bills/futures/options/portfolio have their own banners). Source-info retains
  these as detached/leading comments; the generator assigns each RPC to its section.
  Sections {Time value of money, Mortgages HELOC, Cash-flow analysis, Real estate} are
  **in-scope** (extraction targets); every other section is emitted into the catalogue
  as **out-of-scope**, which feeds the *redirect-refusal* generator (§2) — so the
  model is taught, from the same file, both what it can extract and what it must
  decline by name. A new RPC appended to an in-scope section widens the label space on
  the next regeneration with **no hand edit**. A new *section* fails `--check` loudly
  until classified (one line) — a forced decision, never a silent drop.
- Two mechanical sub-rules derived per-operation from the descriptor:
  - any request containing a `repeated` field (NpvRequest, IrrRequest,
    DatedCashFlowRequest, PaybackRequest, AmortizationBatchRequest — VERIFIED shapes
    from the mortgagefv spec §1a) is **not an extraction target**: a chat utterance
    cannot state a cash-flow vector. These become "use the form" redirects (§2).
  - every non-repeated in-scope request is an extraction target with a slot schema
    taken verbatim from its fields.
- The backend validator (§5) checks the model's emitted `op` against the descriptor
  pool and each emitted key against that request message's fields — same file, other
  end of the wire, no third copy anywhere.

**Sequencing consequence:** the finance-proto extension (its WU-1/WU-2/WU-3) must land
**before** the dataset is generated and the model trained. The label space is about to
grow from 20-ish to 26-ish operations within one spec cycle; training a model days
before its label space widens buys a guaranteed immediate retrain. Land the proto
first, then train once on the post-extension surface. (If the extension slips
indefinitely, the generator's section mechanism still works — the six simply don't
exist in the proto yet and are neither taught nor emittable.)

---

## 1. Label space

Grounded in `2026-08-05-mortgagefv-grpc-integration.md` §1a (VERIFIED): ~20 of the 36
live RPCs serve this site. Post-extension (VERIFIED §3 of the extension spec), six
more: `ComputeRefinance`, `ComputePayoffTiming`, `ComputeMortgageRecast`,
`ComputeHomeFutureValue`, `ComputeRentVsBuy`, `ComputeHomeNpv`. (`get_pmi_drop` is a
lambda inside `calculate_refinance_metrics`, not a seventh RPC — VERIFIED
`financial.cppm:1906` per both specs; its outputs ride `RefinanceResponse` fields.)

Extraction targets (single-utterance, scalar-slot operations):

| tier | operations |
| --- | --- |
| Core mortgage | ComputeAmortization, ComputeDetailedAmortization, ComputeHeloc, ComputePayment, ComputeInterestPayment, ComputePrincipalPayment, ComputeCumulative |
| Solvers | ComputeRate ("what rate makes this payment work"), ComputePeriods ("how long to pay off") |
| FV/PV | ComputeFutureValue, ComputeFutureValueDetailed, ComputePresentValue, ConvertInterestRate, ComputeFisherRate |
| Rental | ComputeRentalRoi, ComputeDepreciation |
| New six (post-extension) | ComputeRefinance, ComputePayoffTiming, ComputeMortgageRecast, ComputeHomeFutureValue, ComputeRentVsBuy, ComputeHomeNpv |

Not extraction targets, taught as **redirects** (mechanically derived, §0):
ComputeNpv/Irr/Xnpv/Xirr/PaybackPeriod (repeated cash-flow inputs),
ComputeAmortizationBatch (repeated inputs; scenario grids are a UI feature).
Out-of-scope sections (bonds, T-bills, futures, options, portfolio) are taught as
refusals naming the sibling product (the options calculator serves them).

**Two label-space rules that differ from the options assistant, deliberately:**

1. **The model transcribes; it never computes.** `ComputePayment.rate` is a
   *per-period* rate (VERIFIED comment, mortgagefv spec §4c example `rate 0.005`).
   Teaching a 0.6B model to divide an annual rate by 12 is a hallucination generator.
   Monthly-payment utterances are therefore labelled as `ComputeAmortization`
   (annual_rate + term_months; its `MortgageSummary` carries the payment — VERIFIED),
   and `ComputePayment` is taught only for utterances that natively state a per-period
   rate. The only arithmetic-like mapping permitted is the same class the options
   model already does via lookup tables (`WEEK_DTE`, VERIFIED): years→months over the
   closed vocabulary {5,10,15,20,25,30}→{60,…,360}, and spoken-percent→decimal over
   common rate phrasings — finite memorizable maps, not arithmetic.
2. **Defaults are the service's job, not the model's.** The extension spec's handlers
   refuse `payments_per_year <= 0` (VERIFIED §4), and proto3 unset = 0. The model does
   NOT emit `payments_per_year`; the assistant service fills the documented default
   (12) deterministically before validation. Same for `AnnuityTiming` (END_OF_PERIOD
   is the zero value, VERIFIED). This mirrors the `far_expiration_days` lesson
   (ASSISTANT_EVALUATION.md, engine fix 3): accept what the utterance states, complete
   the rest deterministically downstream — never refuse for a slot the UI/service can
   fill, and never make the model invent one.

**Designed for growth:** the label space is whatever `operations.json` says at
generation time (§0). The retrain trigger is `generate_finance_operations.py --check`
failing in CI after a proto change — the same trip-wire the strategy catalogue has.

The `crack_321` analogue (VERIFIED lesson, CLAUDE.md): ARM loans, interest-only, and
balloon mortgages have **no fields in any request message** — no rate-schedule, no IO
period (VERIFIED absence in `finance.proto` and in the extension spec's messages). They
are the "plausible instrument this deployment cannot price" class and are taught as
refusals-by-name (§2), never coerced onto the fixed-rate ops.

---

## 2. Dataset

New `agent/mortgage/dataset/build_dataset.py`, mirroring `agent/dataset/
build_dataset.py`'s structure exactly: seeded `random.Random`, template-based
generators, whole-conversation dedup, 5% val split, per-operation coverage report with
a LOUD warning for uncovered ops (VERIFIED all of these in the options builder).
Grounded in `operations.json` (§0) the way the original is grounded in
`strategies.json`.

`<params>` format (the single serialization the model learns — compact, key-ordered,
one convention, per `params_block()`'s rationale, VERIFIED):

```
<params>{"op":"ComputeAmortization","loan_amount":"300000","annual_rate":"0.06","term_months":360}</params>
```

`op` = RPC name verbatim; keys = proto field names verbatim; decimal-string fields as
JSON strings, int32 as JSON numbers — mirroring the wire contract so the service layer
is a transcription, not a translation.

### Generators and mix

The options mix is 50% extraction / 14% clarification / 12% modification / 5% refusal
/ ~10% assorted defect+refusal generators (VERIFIED `mix` list). A mortgage user's
question distribution differs: affordability, refinance break-even, extra payments,
PMI, rent-vs-buy dominate, and the ambiguity classes are about *units*, not ticker
collisions. Proposed mix:

| weight | generator | notes |
| --- | --- | --- |
| 0.46 | `make_extraction` | complete request → op + slots. Money phrased naturally ("$300k", "300,000", "1.2 million" via a closed lookup map), rates ("6%", "six and a quarter" → "0.0625" via lookup), terms in years from the closed set. Slot count varies 2–10 by op — templates per op family, driven by `operations.json` field lists |
| 0.17 | `make_clarification` | four-turn (below). Missing-slot axis: amount / rate / term / (refinance) current-payment. Slightly above the options 14% because mortgage requests omit slots more often than trader requests omit expiry — a refinance question needs 5+ slots and users state 3 |
| 0.12 | `make_modification` | "same but at 5.5%", "make it 15 years", "add $200 a month", "what if I put 25% down" — one field changes, rest carried. Four-turn, params-first-turn shape identical to the options `make_modification` (VERIFIED shape) |
| 0.06 | `make_unit_ambiguity_clarification` | **the mortgage analogue of AMBIGUOUS_ROOTS.** "20 down on a 400k house" — 20% or $20,000? "rate of 6" on its own is NOT ambiguous (no mortgage is 600%; taught as 0.06 when context is a rate slot), but bare "put 30 down", "15 down" genuinely are. Ask one question ("Percent or dollars?"), resolve on the reply. Decisive phrasings ("20% down", "$20k down") must produce params with NO question — the same teach-decisiveness rule as the contract-code entries in `FUTURES_NAMES` (VERIFIED comment) |
| 0.07 | `make_refusal` | advice/predictions/eligibility: "should I refinance?", "will rates drop?", "will I be approved?", "how much house can I afford on my salary?" (affordability-as-advice vs affordability-as-arithmetic: "payment on 400k at 7%" is extraction; "can I afford it" is refusal-with-offer), tax/legal advice. Weighted ABOVE the options 5%: a consumer-mortgage assistant improvising lending advice is a worse liability than a trading one, and refusal must be trained, not hoped for (VERIFIED rationale in the options `make_refusal`) |
| 0.03 | `make_chitchat` | greeting/capabilities, kept short and on-task |
| 0.03 | `make_unknown_product` | ARM / interest-only / balloon / reverse mortgage / DTI / credit score → decline by name, offer the nearest fixed-rate op ("I can model a fixed-rate amortization…"). The `crack_321`/unknown-strategy analogue |
| 0.03 | `make_vector_redirect` | NPV/IRR/payback over custom cash flows, scenario grids → "use the cash-flow form", derived from the repeated-field rule (§0) |
| 0.03 | `make_out_of_scope_redirect` | bonds/options/futures questions → name the sibling calculator. Derived from out-of-scope sections (§0) |

**Lessons carried over verbatim (do copy):**

- **Never label a value the utterance does not state.** The options builder's
  quantity bug — 25% of rows asking the model to recover an unshown digit — cost
  measured accuracy (74.7% quantity) and taught guessing that leaked into other fields
  (VERIFIED, `make_extraction`'s long comment). Mortgage templates must pick the
  template FIRST and only sample optional slots (PMI, overpayment, tax rate) when the
  template can say them.
- **Cleaning matters more than volume.** The 28.5K→11.4K history: the model of record
  trained on `agent/dataset/data/` at 28,500 rows / 18.5% clarification; the
  regenerated `out/` set is 11,400 at 16.5% — and ASSISTANT_EVALUATION.md (VERIFIED)
  records that coverage "was never the problem", and that an earlier 31.1%
  clarification figure was a measurement error (counting modification rows as
  clarification, against the wrong directory). Read: the clarification share belongs in
  the mid-teens measured by *whether the first assistant turn contains `<params>`*, and
  the dataset gate must report exactly that statistic. Target `--n 30000`, expect
  ~25–28K after dedup, val 5% — matching the recipe the 16/16 model actually consumed.
- **Two directories will exist; train on `data/`, and measure the one the run
  consumed** (VERIFIED trap, ASSISTANT_EVALUATION.md "Dataset facts").

**The four-turn clarification shape** (load-bearing, VERIFIED constraint #5 in
STRATEGY_ASSISTANT_PIPELINE.md §5): every clarification row is exactly
`[system, user(request), assistant(one short question), user(answer),
assistant(params)]`. The serving `build_prompt()` reconstructs this same shape — the
options implementation (VERIFIED `assistant_service.cpp:244–256`) replays the trader's
`utterance` as the FIRST user turn, inserts a placeholder assistant question
(`"Which did you mean?"`), and puts `prior_clarification` — the user's ANSWER, never
the question — as the SECOND user turn. The mortgage service must reproduce this
byte-convention exactly, and the dataset's turn shape must match it, because a shape
the model never trained on produces a degraded near-miss, which "broke once in
production this way" (VERIFIED). The proto comment trap is also documented: do not
describe `prior_clarification` as "the question this service asked" (the options
proto's stale comment, VERIFIED in ASSISTANT_EVALUATION.md).

**Dataset gate (new, enabled by GP-ARA — §5):** before training, every generated
`<params>` label is run through the SAME verification the serving path applies
(offline batch, GPU pre-filter + full check). A label the verifier would refuse is a
taught defect; the build fails listing the offending generator. The options pipeline
has no equivalent (its labels are structurally simple); the mortgage slot space is
wide enough that a generator bug (swapped rate/term, percent-as-whole-number) would
otherwise train silently.

---

## 3. System prompt

Load-bearing to the byte (VERIFIED: without its training system prompt the model
reverts to stock Qwen3 and emits no `<params>` at all; a comma-level change silently
converts a working model into an uninstructed one — STRATEGY_ASSISTANT_PIPELINE.md §5
constraint 1). Proposed prompt, same register and length class as the options one:

```
You turn a homeowner's request into parameters for the Mortgage & Future Value
Calculator. Reply with a single JSON object inside <params></params> when you have
enough to act, or ask exactly one short question when you do not. You do not give
financial, lending, tax or investment advice.
```

**The exact two files that must hold byte-identical copies:**

1. `agent/mortgage/dataset/build_dataset.py` — the `SYSTEM` constant.
2. `backend/src/modules/mortgage_assistant_service.cpp` — the `kSystemPrompt`
   constant (mirroring `assistant_service.cpp:76–80`, VERIFIED).

**Drift gate — make it mechanical this time.** The options pair is held identical by
convention and documentation only (VERIFIED: no automated check exists in the tree).
Add `scripts/check_assistant_prompts.py`: extracts the string literal from both files
(and from the options pair while at it), normalises C++ string-literal concatenation,
and fails non-zero on any byte difference; wire it into
`scripts/code_policy_check.sh` (VERIFIED file exists) so it runs where policy checks
already run. Additionally, the eval harness asserts every holdout row's `system`
content equals the serving constant before scoring — a wrong-prompt holdout otherwise
measures an uninstructed model and reads as a model regression.

Two serving-side prompt facts carried over (VERIFIED, CLAUDE.md): Qwen3 emits a
`<think>` block on every response — the block being *empty* is the signal the prompt
took; treating its presence as failure rejects every valid answer. And an unclosed
`<think>` with a balanced `<params>` inside is a COMPLETE answer
(`strip_think_block` fix, ASSISTANT_EVALUATION.md engine fix 2) — port that parsing
behaviour, do not rediscover it.

---

## 4. Training recipe

Reuse `agent/train/train.py` unchanged — it is dataset-agnostic (ShareGPT JSONL in,
QLoRA out; VERIFIED argparse: `--data`, `--out`, `--method`, `--epochs` default
**2.0**, `--lora-r 16`, `--seed 3407`). Do not fork it.

**The documented trap, restated so it cannot be re-tripped:** `--epochs` defaults to
2.0 but **the recipe is 4 epochs** — `run_qlora.sh` on the training host overrides it
(VERIFIED, STRATEGY_ASSISTANT_PIPELINE.md §2b: "--epochs 4 is the recipe; the
script's default is 2"). Reading the default as the recipe produced a 2-epoch retrain
scoring 5/16 against the 4-epoch model's 16/16. The command below states `--epochs 4`
explicitly; any run without it is not the recipe.

Exact commands, on `oluwasanmi-multigpu-server` (GPU0 RTX PRO 6000 96GB, GPU1 RTX
5090 32GB, both idle; 9.3TB free on /scratch — per task brief). The run needs ~2GB
GPU peak (VERIFIED options figure), so pin it to GPU1 and leave GPU0 free:

```bash
# 1. Generate the dataset (repo checkout on the training host)
cd agent/mortgage/dataset
python3 build_dataset.py --out data/ --n 30000 --seed 3407

# 2. Train — QLoRA rank 16, FOUR epochs (the default 2.0 is NOT the recipe)
cd ../../train
CUDA_VISIBLE_DEVICES=1 python3 train.py \
  --method qlora \
  --data ../mortgage/dataset/data \
  --out /scratch/agents/mortgage-param-agent-v1 \
  --epochs 4 --batch-size 8 --grad-accum 4 --max-seq-length 1024 \
  --lora-r 16 --lora-alpha 16 --seed 3407

# 3. Convert with sensen's converter — NOT llama.cpp (0.765s vs minutes, VERIFIED)
ninja -C backend/sensen/build convert_safetensors_to_gguf validate_gguf   # once
scripts/convert_to_gguf.sh /scratch/agents/mortgage-param-agent-v1/merged \
  /scratch/agents/mortgage-param-agent-v1/mortgage-param-agent-v1-Q8_0.gguf q8_0
sha256sum /scratch/agents/mortgage-param-agent-v1/mortgage-param-agent-v1-Q8_0.gguf
```

Budget: the whole chain — train, merge, quantize — well under 30 minutes at this
dataset size (VERIFIED §2; the options run was ~23 min wall for 28.5K rows ×4
epochs). A materially longer run means the recipe changed, not the data volume — stop
and explain rather than ship quietly.

Notes:
- `run_qlora.sh` itself lives at `/scratch/agents/run_qlora.sh` on the training host,
  not in the repo (VERIFIED reference in §2b; contents INFERRED from the documented
  hyperparameter table). The command above reproduces its documented parameters;
  create `/scratch/agents/run_qlora_mortgage.sh` capturing it verbatim so the next
  person reads a script, not a doc.
- `--method full` is known-refuted at this model size (49.8% vs QLoRA's 95.0%,
  VERIFIED §2). Do not re-litigate.
- **Separate model, not a merged two-domain model.** The options model of record is
  shipped, gated, and 16/16; folding a second domain into one checkpoint puts a
  passing production model's behaviour at risk on every mortgage retrain, and the two
  products' load-bearing system prompts differ. Two ~640MB Q8_0 models, two
  provenance records. (Cost: resident memory, §7.)
- Record a §2b-style "model of record" table (sha256, epochs, losses, wall clock,
  holdout score) in the mortgage pipeline doc at first deploy — "which model is
  this?" was answered wrong three times from memory once already (VERIFIED §2b).

The acceptance bar mirrors the options bar: **≥95.0% params exact-match on the
mortgage `val.jsonl`**, re-measured on the Q8_0 GGUF through the production inference
path (bf16 merged weights are an intermediate no request is ever decoded by —
VERIFIED §5 constraint 6), plus the holdout (§6).

---

## 5. GP-ARA verification — design

### 5.0 What exists to build on (all VERIFIED)

- `sensen::gp_ara` concepts: `DomainPolicy` (types `InputDataType`,
  `LogicalConstraintType`, `translate()`) and `ReasonerPolicy` (`ContextType`,
  `prove_safety`, `prove_goal` returning `std::expected<bool, ReasonerError>`) —
  `gp_ara_interfaces.cppm:148–172`.
- Tri-state: `Proven / Indeterminate / Unsafe`, with `ReasonerErrorCode::Indeterminate`
  kept distinct from both definitive-false and hard errors end-to-end
  (`gp_ara_interfaces.cppm:80–101`).
- The options assistant already ships a GP-ARA-conformant verifier **without Z3**:
  `assistant_verification.cppm` defines `AssistantParamsDomain` + `RuleBasedReasoner`
  satisfying both concepts (static_asserts at lines 828–831), because the engine links
  `sensen_slim` which excludes Z3 (`ldd` confirms no libz3 — file banner). Z3Reasoner
  is a drop-in replacement if a solver ever becomes available. Fail-closed:
  default-constructed verdict is Indeterminate; Indeterminate is always a refusal.
- GPU batch path: `GeneralizedProblemSolver::proveSafetyBatch` runs a **sound,
  default-deny, reject-only** pre-filter (`CudaBackend::check_safety`, ~1M states in
  0.6ms, NaN/Inf default-denied) when the domain provides
  `toGPULogicState(input) -> GPULogicState`, then routes every survivor through the
  full scalar proof path — the GPU can mark UNSAFE cheaply but can never certify SAFE
  (`gp_ara_agent.cppm:353–420`). `GPULogicState` is `float variables[8]` +
  per-variable bounds + `is_safe` (`cuda_backend.cppm:1632–1637`).

### 5.1 Architecture: one domain, two reasoners, two deployments

New module `backend/src/modules/mortgage_assistant_verification.cppm`, mirroring the
options module's shape:

- `MortgageParamsInput` (plain struct, no protobuf dependency — the service maps
  to/from the wire one layer up, same as `AssistantParamsInput`): `std::string op;`
  plus the decoded slots — raw decimal strings AND their double decodes (the reasoner
  compares in double; the service parses with the same strict decimal grammar the
  finance handlers use, so a malformed number never reaches the reasoner).
- `MortgageParamsDomain::translate(input) -> VerificationFacts` evaluates the rule
  table below; `VerificationFacts{violated, incomplete, reason, detail}` exactly as
  the options module (VERIFIED shape).
- **CPU path (serving, mandatory, every request):** `RuleBasedReasoner` — direct
  evaluation of closed-form comparisons over ≤14 scalars. This is the gate inside
  `calculator_engine`; Z3 is genuinely unavailable there (sensen_slim). Latency is
  microseconds; it runs after the model, before a single field reaches the response.
- **GPU batch path (offline, oluwasanmi-multigpu-server):** a small tool
  (`agent/mortgage/verify/batch_verify.cpp`, linking full sensen with CUDA + Z3)
  instantiates `GeneralizedProblemSolver<MortgageParamsDomain, Z3Reasoner>` and calls
  `proveSafetyBatch` over (a) every label in the generated training set — the §2
  dataset gate — and (b) every model output captured during an eval sweep. The GPU
  pre-filter rejects bounds violations at ~1M states/0.6ms; survivors get the full
  scalar proof. **Purpose split, stated plainly: the CPU path protects users; the GPU
  path protects the dataset and the evaluation at scale.** The GPU path is NOT a
  serving component — production is a CPU container (VERIFIED: engine built without
  CUDA; `ENABLE_LLAMACPP_BACKEND=OFF` CPU image).
- **Z3 as the independent cross-check.** On the offline host, the same
  `MortgageParamsDomain` runs under BOTH `RuleBasedReasoner` (what production uses)
  and `Z3Reasoner` (SMT-LIB over the reals, discharging the same obligations
  independently). Disagreement on any input is a bug in one of them — this is the
  same "independent implementation or the check proves nothing" philosophy the parity
  probes apply to llama.cpp (VERIFIED, CLAUDE.md). Note the documented Z3 scope fix
  (constraint+goal parsed in ONE `from_string`, sensen CLAUDE.md 2026-07-09) — the
  obligations below must be emitted as combined sources.

### 5.2 Proof obligations — concrete

Layer A: **input obligations** (gate the model's extracted params, pre-call):

| id | obligation | outcome on failure |
| --- | --- | --- |
| A1 | `op` ∈ descriptor pool, every key ∈ request fields, no repeated-input op | Unsafe → UNSUPPORTED_OPERATION (this is the well-formedness floor; A2+ are the semantic point) |
| A2 | every money slot > 0 where the op requires it (loan_amount, property_value, current_loan_balance…); property_value > 0 strictly | Unsafe, detail names field |
| A3 | rate slots: `0 ≤ annual_rate < 0.30`. The band [1, 30] is the *percent-stated-as-whole-number* signature ("6" for 6%) — NOT a refusal: pre-GP-ARA deterministic normalisation (the mortgage analogue of `detect_asset_class_signal`, which also runs BEFORE the verifier — VERIFIED banner) rewrites it or asks. What reaches GP-ARA must already be a decimal; ≥ 0.30 there is Unsafe | Unsafe / handled pre-verifier |
| A4 | term slots: `0 < term_months ≤ 1200`; `new_term_years ≤ 100` — the same DoS-dressed-as-a-mortgage ceiling `check_term` enforces (VERIFIED, extension spec §4) | Unsafe |
| A5 | LTV consistency: where both appear, `loan / property_value ∈ (0, 1.5]` (allows underwater refis, refuses transcription absurdities); `pmi_drop_off_ltv ∈ (0, 1]` | Unsafe |
| A6 | **payment covers interest**: `current_monthly_payment × 12 > current_loan_balance × annual_rate` for ComputePayoffTiming / ComputeRefinance / ComputeHomeFutureValue. This is exactly the case where sensen's `nper_fn` fails and `calculate_payoff_timing` returns silent zeros that look like answers (VERIFIED hazard, extension spec §1b). The handler refuses it too (§4) — the verifier catches it BEFORE a wire call, with a better message ("the payment you stated does not cover the interest; the loan never retires — check the payment amount") | Unsafe |
| A7 | recast: `0 ≤ lump_sum_payment ≤ current_loan_balance × 1.0` (a lump above balance is a payoff, not a recast; clamp behaviour exists engine-side but the utterance more likely mis-stated) | Unsafe |
| A8 | enum slots: closing_cost_type ∈ {0,1}; sign conventions (cash_out ≥ 0, extra_monthly ≥ 0) | Unsafe |
| A9 | cross-slot sanity for FV: compound_frequency > 0 (the service refuses 0 — VERIFIED mortgagefv spec §1a); holding/target years ∈ (0, 100] | Unsafe |

Layer B: **conditional/result obligations** (verify the *answer* is consistent —
"correct, not merely well-formed"). Evaluated where the response is available (RPC
layer of the eval harness always; serving optionally behind an env flag, since it
doubles compute):

| id | obligation |
| --- | --- |
| B1 | refinance break-even: if test-side `savings = (old P&I + old PMI) − (new P&I + new PMI) > 0` (new payment recomputed from the closed-form annuity — independent arithmetic, not the engine's own figure), then `simple_break_even_months == ceil(closing_costs / savings)`, finite and positive; if savings ≤ 0, the field MUST be the −1 sentinel (VERIFIED sentinel semantics, extension spec §1a). A positive break-even reported for a savings-free refi is a wrong answer the user would act on |
| B2 | PMI drop-off ↔ LTV consistency: if starting `balance ≤ pmi_drop_off_ltv × property_value` (or PMI = 0), drop-off months == 0; else drop-off month m satisfies `balance_at(m) ≤ threshold < balance_at(m−1)` with `balance_at` the closed-form remaining balance (independent) |
| B3 | payoff timing: `new_months_remaining ≤ original_months_remaining`; `months_saved` = difference; zeros with a nonzero extra payment = the silent-zero hazard leaked through — hard failure |
| B4 | recast: `new_monthly_payment` equals closed-form pmt on `(balance − lump, rate, remaining_months)` to 1e-6; `monthly_savings` = difference (pmt linearity — the smoke-gate identity, extension spec §5d) |
| B5 | amortization: schedule closes (final balance 0), `actual_term_months ≤ term_months` |

Layer B deliberately reuses the identities the finance smoke gate already specifies
(extension spec §5) — the assistant's verifier and the service's smoke test assert the
same mathematics from two different positions, sharing no code with the engine.

### 5.3 GPU mapping

`MortgageParamsDomain::toGPULogicState`: per-op table mapping numeric slots to
`variables[0..7]` with `[lower, upper]` from the Layer-A bounds (A2–A5, A7–A9). Ops
with ≤8 numeric slots map to one state. `ComputeRefinance` (10 numeric slots,
VERIFIED field count) and `ComputeHomeNpv` (12) split into TWO states per row
(current-loan state + new-loan state; purchase state + holding state) — sound because
the pre-filter is per-variable bounds rejection and reject-only: a reject in either
state rejects the row; survival certifies nothing (the scalar path still runs —
VERIFIED contract, `gp_ara_agent.cppm:360–365`). A6 (payment-covers-interest) is a
cross-variable product constraint the bounds kernel cannot express — it is CPU/Z3-only,
which is fine: the GPU stage is a throughput pre-filter, not the proof.

### 5.4 Indeterminate — is default-deny right here?

**Yes, unchanged, for serving.** The tri-state maps exactly as the options module
does (VERIFIED banner): Proven → populate params; Unsafe → Refusal with the named
rule; **Indeterminate → Refusal, never a clarification** — an Indeterminate here
means the verifier was never taught this op/rule combination (the gap is between the
verifier and `operations.json`, not something a user's answer can fix), and a
default-allow on "the checker doesn't know" would wave through precisely the cases
the checker exists for. A wrong mortgage figure looks exactly as authoritative as a
right one; refusing is the honest output. What must NOT be routed through
Indeterminate: genuine input ambiguity (percent-vs-dollars) — that is resolved BEFORE
the verifier by the deterministic signal/clarification layer (§5.2 A3, mirroring
`detect_asset_class_signal`'s placement, VERIFIED), because ambiguity is a
conversation problem, not a proof problem.

One deployment-context difference: in the **offline dataset gate**, Indeterminate is
promoted from "refuse the row" to **build failure** — a training label the verifier
cannot decide means either the generator or the rule table is wrong, and both are
fixable at build time. Default-deny at serving; default-fix at build.

---

## 6. Evaluation

The absolute rule, carried over verbatim (VERIFIED, ASSISTANT_EVALUATION.md +
CLAUDE.md): **measure candidates on sensen, never llama.cpp.** `ASSISTANT_BACKEND`
defaults to `sensen` and production runs it; a `llama-cli` score describes an engine
that never handles a request. The precedent is concrete: a llama-cli holdout scored
the deployed options model 7/16, triggered a retrain for a phantom regression, and the
retrain scored 5/16; through the real RPC the "regressed" model scored 13/16 → 16/16
after engine fixes. Clone `scripts/eval_assistant_sensen.py` →
`scripts/eval_assistant_mortgage.py` (same two-layer design) rather than writing a new
harness.

**Holdout:** `agent/mortgage/train/defect_holdout.jsonl`, 16 rows to start (mirroring
`agent/train/defect_holdout.jsonl`'s size and ShareGPT shape, VERIFIED), authored
from the anticipated defect classes rather than random sampling:

1–3. Unit slips: "6 percent" vs "6" vs "0.06"; "$300k"; "1.2 million".
4–5. Years-vs-months: "15 year mortgage" → term_months 180; "180 months".
6–7. **Ambiguity rows where asking IS the pass condition**: "putting 20 down on a
     500k place" (percent or dollars); bare "help me refinance" (no slots). Note the
     options scoring lesson: 2 of its 16 rows correctly emit NO `<params>` — the
     arithmetic of "emitted params" vs "correct" columns is not complementary
     (VERIFIED explanation in ASSISTANT_EVALUATION.md).
8. Payment-doesn't-cover-interest extraction ("$200/mo on a 300k loan at 7%, when am
   I done?") — **the raw layer expects faithful params; the RPC layer expects a
   verification refusal.** The two layers legitimately disagree on this row; score
   each against its own key.
9–10. Refusals: "should I refinance now?", "will I qualify?".
11. ARM refusal ("5/1 ARM at 6 then 8").
12. Vector redirect ("NPV of these cash flows: −2000, 500, 600…").
13. Out-of-scope redirect ("price a call on NVDA" → sibling product).
14–16. Multi-slot refinance/rent-vs-buy/HELOC extractions with all slots stated.

Grow it with real production defects exactly as the options holdout did — it is a
defect ledger, not a benchmark.

**Pass bar:** ≥95.0% params exact-match on `val.jsonl` measured on the Q8_0 GGUF
through the sensen path (the options bar, VERIFIED §2 — below it is a regression
regardless of what else improved); holdout scored at BOTH layers with per-row keys as
above; and any candidate compared against the currently-deployed mortgage model
**measured the same way in the same session** — never against a remembered number
(VERIFIED checklist item 7). Before calling anything "the deployed model", compare
`sha256sum` against the pinned `MORTGAGE_MODEL_SHA256` (§7) — the options project
spent a session measuring the wrong file (VERIFIED).

**Trap 1 — SO_REUSEPORT model roulette (unchanged, VERIFIED):** engines bind `:50051`
with SO_REUSEPORT; several stale engines each holding a DIFFERENT model all listen at
once and the kernel splits requests across them — producing cross-domain answers that
read exactly like KV-cache bleed. Before trusting any number:

```bash
pgrep -x calculator_engi | wc -l   # must be 1 — comm is truncated to 15 chars,
                                   # so `pkill -x calculator_engine` matches NOTHING
ss -ltn | grep -c ':50051'         # must be 1
```

Never `pkill -f` on the binary path — the launching shell's command line contains it
and the shell kills itself (exit 144, VERIFIED). With two assistants in one engine
this trap gets worse, not better: a stale engine may hold a different *mortgage* model
AND a different *options* model simultaneously.

**Trap 2 — the verification layer scoring instead of the model.** The options form:
without live Alpaca data, symbol verification refuses everything and every model
scores identically 3/16 (VERIFIED). The mortgage verifier needs **no live data** — a
genuine domain difference: GP-ARA's obligations are pure arithmetic over the request,
so the RPC layer is always scoreable without credentials. **Do not copy the
market-data machinery.** But the trap generalises and still applies: any bug or
over-tight bound in the GP-ARA gate (e.g. a wrong A3 rate band) refuses a whole class
regardless of model quality, and every candidate scores identically on those rows —
a measurement of the verifier, not the model. So the harness scores
`[assistant] raw model output` (logged before verification — keep that log line in
the mortgage service) as the model measurement, and the RPC result as the
user-experience measurement, and reports BOTH, exactly as `eval_assistant_sensen.py`
does (VERIFIED `--layer raw|rpc`).

---

## 7. Serving

Mirror `assistant_service.cpp`'s architecture; the constraints below are each
VERIFIED as documented lessons.

- **Contract:** new `backend/proto/mortgage_assistant.proto`, service
  `mortgage.assistant.MortgageAssistant`, rpc `ParseQuery(ParseRequest) returns
  (ParseResponse)` with `ParseRequest{utterance, prior_clarification}` and
  `ParseResponse{ MortgageParams | Clarification | Refusal }` mirroring
  `assistant.proto`'s shapes (VERIFIED). `MortgageParams`: `string op = 1;` plus
  `map<string, string> fields = 2;` — decimal values as strings per the numeric
  contract (money never rides a double to a browser — VERIFIED proto header rule);
  the frontend passes them through `decimal.js` untouched. Register in `main.cpp`
  beside the existing three services (VERIFIED registration pattern,
  `main.cpp:39–55`); Envoy's catch-all route needs nothing (VERIFIED, extension spec
  §7).
- **In-process, second pipeline instance.** New
  `backend/src/modules/mortgage_assistant_service.cpp` with its own
  `MortgageAssistantWorker` singleton (mirroring `AssistantWorker`,
  VERIFIED :1316–1350): its own `sensen::LLMPipeline::fromGGUF`, its own **single
  owner thread** — `generate()` cannot be called concurrently because
  `FeedForwardNetwork` holds `mutable` scratch per instance, not `thread_local`
  (VERIFIED); two *instances* on two owner threads are safe precisely because the
  scratch is per-instance. Concurrency within the instance comes from sensen's
  iteration-level scheduler (~20 MiB marginal per user, VERIFIED).
- **CPU pinning:** `n_gpu_layers = 0` explicitly — `GenerationConfig` defaults
  `compute_backend` to AUTO, which sensen counted as a GPU request, enabling
  `on_device_sampling` whose contract only the CUDA decode block honours; the CPU
  path then casts a raw float logit to a token id (262 bytes of deterministic
  punctuation noise). Fixed upstream AND pinned independently here regardless
  (VERIFIED). Env: `MORTGAGE_MODEL_PATH`, `MORTGAGE_ASSISTANT_CONTEXT_TOKENS=4096`,
  same knob family as the options service; `ASSISTANT_BACKEND` stays global —
  production is sensen, and the image is built `-DENABLE_LLAMACPP_BACKEND=OFF`
  (VERIFIED) so no second engine can be quietly substituted.
- **Distribution:** the GGUF goes in the same private HF repo family, fetched at
  **Docker build time** in the `model` stage with `MORTGAGE_MODEL_URL` /
  `MORTGAGE_MODEL_SHA256` / `MODEL_TOKEN` as Railway **build ARGs — never
  `--mount=type=secret`**: Railway's Metal builder rejects the entire Dockerfile if a
  secret mount appears anywhere, silently — the build never starts, the old container
  keeps serving, `railway up` exits 0; five consecutive deployments failed exactly
  this way, and local docker builds cannot reproduce it (VERIFIED). The ARG is safe
  because the fetch is isolated in the `model` stage and only `COPY --from=model`
  reaches the published layers (VERIFIED). It cannot travel through `railway up` —
  the CLI's upload deadline already failed at 62 MB and the GGUF is ~640 MB
  (VERIFIED). Checksum mismatch fails the build (a truncated GGUF loads and generates
  fluent wrong text — worse than a red build); empty `MORTGAGE_MODEL_URL` is a
  supported no-op so the finance surface never goes down over an optional feature
  (VERIFIED pattern). URL and SHA256 move together on every swap; re-download and
  re-checksum after upload (VERIFIED procedure, §4 of the pipeline doc).
- **Conversion:** `sensen::convert_safetensors_to_gguf` via
  `scripts/convert_to_gguf.sh` — one call, Q8_0 direct, 0.765s, deterministic
  sha256 across runs; llama.cpp's two-step is not the standard and its output is not
  byte-comparable anyway (~480 bytes of metadata differ — compare behaviour, not
  checksums; VERIFIED).
- **Verification order in the handler:** deterministic unit-normalisation /
  ambiguity signal → (clarification short-circuit) → GP-ARA CPU gate
  (`verify_mortgage_params`, mandatory, fail-closed) → fill service defaults
  (payments_per_year=12) → populate `MortgageParams`. Log
  `[mortgage-assistant] raw model output` BEFORE verification (the eval harness
  depends on it, §6).
- **Memory (open risk):** a second Q8_0 model adds ~640 MB resident plus KV
  (~4096-token cache per slot). I could not determine the Railway container's current
  RAM headroom from the tree — **measure RSS with both pipelines loaded before
  deploying**, and note the mitigation if tight: lazy-load the mortgage pipeline on
  first request (the no-op `MODEL_URL` machinery already tolerates absence).
- **Gating and quota (open decision):** the options assistant is Pro-gated
  (VERIFIED, CLAUDE.md §4 note: unauthenticated probes return `grpc-status 7` and
  look like a broken model — remember this when verifying in production). Whether the
  mortgage assistant is Pro-gated, key-gated via the site's `mortgagefv-web`
  publishable key (the mortgagefv spec's W1–W2), or open, is a product decision; the
  mechanism exists either way. Every new RPC needs a `CHARGE` line — an
  assistant call is expensive (a full LLM decode); price it well above
  `cost_default()` (INFERRED sizing; the assistant service's existing cost handling
  was not read in this analysis — check `assistant_service.cpp`'s quota wiring at
  implementation time).
- **Production verification goes through gRPC-Web** — native gRPC does not survive
  the Railway ingress ("Stream removed", no request reaches the container; VERIFIED).
  Verify via a browser call or `curl` with `application/grpc-web-text`, confirmed in
  `railway logs`.

---

## Work units

**WU-0 — Prerequisite: land the finance proto extension** (its WU-1..WU-3: proto,
handlers, smoke gate). Not this spec's work; this spec's dataset generation is blocked
on it (§0 sequencing). Gate: `ComputeRefinance` et al. answer over gRPC-Web locally.

**WU-1 — Operations catalogue generator.**
Files: `scripts/generate_finance_operations.py` (new),
`agent/mortgage/dataset/operations.json` (generated, checked in).
Commands: `python3 scripts/generate_finance_operations.py` and `--check`.
Risks: proto section comments not surviving descriptor parsing as expected
(mitigate: assert every RPC gets a section; fail loudly on orphans); the repeated-field
rule silently reclassifying a future op (the `--check` diff makes it visible in
review). Acceptance: `--check` passes; JSON lists 26 in-scope ops post-extension with
per-field types/comments; out-of-scope sections enumerated; a deliberate dummy RPC
added locally to the proto appears in the regenerated JSON without editing the script.
Estimate: **0.5 day**.

**WU-2 — Dataset builder.**
Files: `agent/mortgage/dataset/build_dataset.py` (new; mirrors the options builder's
structure §2), `agent/mortgage/train/defect_holdout.jsonl` (new, §6).
Commands: `python3 build_dataset.py --out data/ --n 30000 --seed 3407`.
Risks: labelling values the utterance doesn't state (the 25%-quantity bug class —
templates picked first, optional slots sampled only when sayable); clarification
share drifting (report the first-assistant-turn-has-`<params>` statistic in the build
output); per-op coverage holes (loud warning, as the options builder does).
Acceptance: ~25–28K unique rows; clarification share 15–19% by the correct metric;
zero uncovered in-scope ops; **every label passes the WU-4 batch verifier** (the
dataset gate); holdout rows' system prompt byte-equals the constant (WU-6's gate
script). Estimate: **1.5 days**.

**WU-3 — GP-ARA CPU verification module.**
Files: `backend/src/modules/mortgage_assistant_verification.cppm` (new).
Content: `MortgageParamsInput`, `MortgageParamsDomain` (Layer-A rules A1–A9, Layer-B
B1–B5 behind a results-provided flag), `RuleBasedReasoner` sibling, static_asserts
against `sensen::gp_ara::DomainPolicy`/`ReasonerPolicy`, tri-state mapping with
Indeterminate → refusal (§5.4).
Risks: routing genuine ambiguity through the verifier instead of the pre-verifier
signal layer (keep A3's percent-band OUT of the reasoner); descriptor-pool lookup
assumption (verify `generated_pool()` finds `sensen.finance.Finance` in a unit test
first — INFERRED, §0.3).
Acceptance: unit tests per rule with a violating and a passing case each; the
options module's convention checks (fail-closed default construction; no error path
returns Proven). Estimate: **1 day**.

**WU-4 — Offline batch verifier (GPU + Z3 cross-check).**
Files: `agent/mortgage/verify/batch_verify.cpp` (new; full-sensen build on the
training host), `toGPULogicState` mapping in the WU-3 module (guarded so sensen_slim
builds don't need CUDA types — mirror how the domain stays header-clean).
Commands: `./batch_verify data/train.jsonl` (dataset gate), `./batch_verify
--from-eval sweep.jsonl` (eval sweep).
Risks: the 8-variable `GPULogicState` limit vs 10–12-slot requests (two-state split,
§5.3 — sound because reject-only); Z3/RuleBased disagreement triage time (that
disagreement is the tool working); the documented Z3 single-`from_string` scope rule.
Acceptance: pre-filter rejects a seeded bad-bounds row; Z3 and RuleBased agree on the
full training set; wall clock for 28.5K rows under a minute (the GPU stage is
microseconds; Z3 dominates — if Z3 is slow, sample it, the RuleBased pass stays
exhaustive). Estimate: **1 day**.

**WU-5 — Train, convert, evaluate.**
Files: none in-repo (artifacts on /scratch); `/scratch/agents/run_qlora_mortgage.sh`
on the training host.
Commands: §4 verbatim — `--method qlora --epochs 4` (**the default 2.0 is not the
recipe**), then `convert_to_gguf.sh … q8_0`, then `eval_assistant_mortgage.py`
against ONE verified engine (`pgrep -x calculator_engi | wc -l` == 1).
Risks: the epochs trap; measuring bf16 merged weights instead of the Q8_0 through
sensen; stale-engine roulette; scoring the wrong dataset directory.
Acceptance: ≥95.0% params exact-match on val (Q8_0, sensen path); holdout ≥13/16 raw
at first train with every miss triaged (model vs harness vs verifier) before any
retrain decision — the options history says most "model failures" were engine
defects (VERIFIED 13→16 with zero retrains). Estimate: **0.5 day** (train ~25 min;
triage dominates).

**WU-6 — Serving.**
Files: `backend/proto/mortgage_assistant.proto` (new),
`backend/src/modules/mortgage_assistant_service.cpp` (new), `backend/src/main.cpp`
(one registration line), `backend/Dockerfile` (`MORTGAGE_MODEL_URL`/`_SHA256` ARGs in
the `model` stage), `scripts/check_assistant_prompts.py` (new) +
`scripts/code_policy_check.sh` (one line), CMake source additions.
Risks: the secret-mount Railway trap (ARG only); prompt drift (the new gate);
`build_prompt` shape divergence (copy the options function's turn construction
verbatim, placeholder question included); memory headroom (measure RSS before
deploy); concurrent `generate()` (one owner thread per instance).
Acceptance: startup line `Mortgage assistant ready: backend=sensen model=…`; smoke
ParseQuery locally over native gRPC and via gRPC-Web through Envoy; unset
`MORTGAGE_MODEL_URL` build still serves finance + options; prompt gate red when a
byte is changed in either file. Estimate: **1.5–2 days**.

**WU-7 — Deploy + docs.**
Files: `docs/MORTGAGE_ASSISTANT_PIPELINE.md` (the §2b-style model-of-record table,
the swap procedure), CLAUDE.md short section.
Commands: HF upload → re-download → `sha256sum` → set Railway vars together →
`railway up --detach` → verify over gRPC-Web with the appropriate credential,
confirmed in `railway logs`.
Risks: verifying through native gRPC against the custom domain (proves nothing —
VERIFIED); calling the wrong file "deployed" (checksum first).
Acceptance: production ParseQuery round trip visible in logs; model-of-record table
committed with sha256 + holdout score. Estimate: **0.5 day**.

**Total: ~6.5–7 working days** after WU-0, dominated by dataset iteration and serving
wiring, not training (minutes) or GPU time (seconds).

---

## Where the mortgage domain genuinely differs — patterns NOT to copy

1. **No live-data verification layer.** The options verifier needs Alpaca quotes to
   resolve symbols; mortgage verification is closed-form arithmetic over the request.
   Do not port `probe_symbol`/market-data plumbing. The *measurement* lesson survives
   in generalised form (§6 trap 2): any always-on gate can make all models score
   identically — score raw output separately.
2. **Ambiguity is about units, not tickers.** `AMBIGUOUS_ROOTS`/Eversource-vs-E-mini
   has no analogue; the mortgage ambiguity class is percent-vs-dollars and
   years-vs-months. Same four-turn resolution machinery, different detector.
3. **The model must not do arithmetic.** Options labels are transcriptions
   (symbol/strategy/days/qty). Mortgage tempts per-period-rate division and
   percent-of-price computation — keep every mapping a finite lookup (§1) and keep
   `ComputePayment`'s per-period rate away from NL extraction.
4. **The label space is a service contract, not a product catalogue.** strategies.json
   is app-owned and hand-curated; finance.proto is a general-purpose library surface
   shared with other consumers. Hence section-scoped inclusion (§0) instead of
   teaching every RPC — the options design never needed a scope concept.
5. **Sentinels are part of correctness.** Options params have no −1/-0 sentinel
   semantics; the mortgage responses do (break-even −1, PMI 0/−1 — VERIFIED). The
   verifier and the holdout must treat "sentinel expected" as a pass condition, not a
   failure — a new failure mode class the options harness never needed.

## What I could not determine

- Railway container RAM headroom for a second resident Q8_0 model (§7) — measure
  before deploy; lazy-load is the fallback.
- Whether the mortgage assistant should be Pro-gated, key-gated, or open (§7) —
  product decision; mechanisms exist for all three.
- The exact contents of `/scratch/agents/run_qlora.sh` (training host, not in repo) —
  its parameters are INFERRED from the documented model-of-record table (§2b), which
  is authoritative enough to reproduce.
- Whether `google::protobuf::DescriptorPool::generated_pool()` lookup works as
  assumed in this binary (INFERRED from standard codegen) — verify with a one-line
  test before building WU-3 on it.
- How the existing assistant service charges quota (whether `CHARGE` applies to
  `ParseStrategy`) — not read during this analysis; mirror whatever it does (WU-6).
- mortgagefvcalculator.com frontend integration (a chat surface does not exist on a
  static calculator site today) — out of scope here; the site consumes the new RPC
  the same gRPC-Web way the mortgagefv integration spec already establishes.
