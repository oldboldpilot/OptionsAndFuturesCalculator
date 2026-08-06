# The mortgage assistant's 27% → 0% RPC accuracy gap: root cause and fix

@author Olumuyiwa Oluwasanmi

**Status: diagnosed, fix specified, nothing deployed.** The one-line cause was
proven by A/B measurement on 2026-08-06; the production tree is unchanged (the
instrumentation used below was reverted and the engine rebuilt from pristine
source).

## 0. The gap being explained

One model file, `backend/models/mortgagefv-assistant-v2-q8_0.gguf`
(sha256 `269efd32a5533ff94fc31975f0cbee2c46ba47863a924a0745886fdbc3b413fe`,
verified 2026-08-06), measured two ways:

| measurement | path | score |
| --- | --- | --- |
| `numeric_audit_probe` greedy rollout, `LlamaModel` direct, raw argmax | no `LLMPipeline`, no sampler, no RPC | **8/30 = 26.7%** params exact-match (llama.cpp on the identical rollouts: also 8/30 — the number is the model, not an engine artifact) |
| `agent/train/eval_grpc_mortgage.py` against the real `ParseOperation` RPC | full serving path | **0/279 = 0.0%** raw params exact-match; 0 served |

The 0/279 is the **raw model output** number — read from the
`[mortgage-assistant] raw model output` stderr line that
`interpret_model_output()` prints *before* any validation or verification runs.
So the loss was already known to sit **upstream of the verification gate**, in
the stretch between `LlamaModel` and the text the service logs.

## 1. Root cause (proven by A/B on identical rows)

**`sensen::Sampler::sample` applies a repetition penalty before the GREEDY
argmax, and the service leaves that penalty at its default of 1.1.**

- `backend/src/modules/mortgage_assistant_service.cpp`, `generation_config()`
  (lines 520–566): sets `strategy = GREEDY`, `max_new_tokens`,
  `deterministic = true`, `n_gpu_layers = 0` — and nothing else. A
  default-constructed `sensen::GenerationConfig`
  (`backend/sensen/src/llm_interfaces.cppm:99–120`) carries
  `repetition_penalty = 1.1F` and `repetition_window = 64`.
- `backend/sensen/src/llm_pipeline.cppm:925–971` (`Sampler::sample`): the
  penalty is applied to the logits **before** the strategy switch, so
  `GREEDY` argmaxes over *penalized* logits. `applyRepetitionPenalty`
  (llm_pipeline.cppm:1139) divides a positive logit by the penalty **once per
  occurrence** of the token in the window: a token seen *n* times in the last
  64 generated tokens has its logit divided by 1.1ⁿ.
- The probe (`backend/src/numeric_audit_probe.cpp`) never touches
  `Sampler` — it argmaxes the raw logit vector. That is the entire difference
  between the two measurements.

Why this specific defect produces this specific damage: the label space is
digit-dense JSON, and Qwen3 tokenizes digits one per token. By mid-object the
64-token window holds `'0'` eight-to-twelve times; 1.1⁸–1.1¹² divides its
logit by 2.1–3.1, a 14–18 point collapse on logits in the high twenties. The
model then cannot re-emit the user's own digits, quotes, or braces, so output
degrades **progressively** — correct opening, drifting digits, dropped fields,
truncated JSON — which is exactly the observed failure shape, and exactly why
the corruption never shows at the probe (raw argmax) on the same weights.

### The A/B that establishes it

Method: one `calculator_engine` at a time (asserted `pgrep -x calculator_engi
| wc -l` == 1 before each run, PID-tracked, killed by PID after),
`MORTGAGE_MODEL_PATH` pointed at the pinned GGUF,
`eval_grpc_mortgage.py --val agent/dataset/data_mortgage/val.jsonl --n 100`
— the identical first 100 rows every run. The only code change between run 1
and run 2 was one temporarily added line in `generation_config()`:
`config.repetition_penalty = 1.0F;` (reverted afterwards; `git diff` of the
file is back to its pre-experiment state).

| run | config | raw exact | served params | served exact-gold | bad-JSON blocks | homoglyph rows |
| --- | --- | --- | --- | --- | --- | --- |
| 1 (production config) | penalty 1.1, KV q8 | **0/90 = 0.0%** | 0/100 | 0 | 12 | 2 |
| 2 | penalty **1.0**, KV q8 | **25/90 = 27.8%** | 33/100 | 25 | 3 | 0 |
| 3 (control) | penalty 1.0, KV **fp32** | 24/90 = 26.7% | 32/100 | 24 | 0 | 0 |

Artifacts: `eval_base100.json`, `eval_nopen100.json`,
`eval_nopen_fp32_100.json` and the three engine logs, in this session's
scratchpad. 25/90 = 27.8% and 24/90 = 26.7% bracket the probe's 8/30 = 26.7%:
with the penalty off, the RPC serves **exactly the accuracy the model has**.

Per-row confirmation: all 25 rows that became exact in run 2 were failures in
run 1, on the same utterances, with visibly penalty-shaped raw output. One of
them, val row 8 (`Show me the full payment schedule on $756,000, 6.12%,
30-year.`):

- raw at penalty 1.1: `"loan_amount":"756000.0","annual_rate":"0.1249","term_months":368,"monthly_overpayment":"9547.12"` — rate and term hallucinated after the digits of the loan amount consumed the window, two fields missing.
- raw at penalty 1.0: byte-exact match of the gold label.

### The homoglyph finding, closed

The U+FF10 fullwidth zero inside numeric literals (e.g. `"term_months":18０`)
is the same defect at its most visible. `０` (token 26022) and `₀` are
*different token ids* from `'0'`, never previously emitted, therefore never
penalized — while `'0'`'s logit is divided by 1.1 per prior occurrence. The
probe measured `０` sitting 19–25 logits below top under **raw** argmax
(unreachable); an 8–12-occurrence penalty collapse of 14–18 points, on top of
whatever context drift has already accumulated, closes that gap. Empirically:
2 of 100 rows contained fullwidth/subscript digits at penalty 1.1
(`eval_base100.json` rows 63, 66; 7 of 279 in the prior full run,
`v2_final.json`); **0 of 100** at penalty 1.0. Neither engine reproduces it at
the `LlamaModel` level because neither applies the penalty there.

## 2. The measured decomposition of 27% → 0%

On the fixed 100-row set (90 params-gold rows), out of the 27.8 points
available:

| candidate surface | points of the gap | evidence |
| --- | --- | --- |
| **`Sampler::sample` repetition penalty 1.1 under GREEDY** | **27.8 of 27.8 — the entire gap** | run 1 vs run 2 above: 0/90 → 25/90 with only `repetition_penalty` changed |
| Prompt construction (`build_prompt`, turn shape, chat template) | 0 | service and probe build byte-identical ChatML for a first-turn call (`mortgage_assistant_service.cpp:254–271` vs `numeric_audit_probe.cpp:55–65`, same 322-char system prompt); with the sampler fixed the RPC reaches the probe's exact score through the service's own prompt builder |
| BOS / tokenizer divergence | 0 | the GGUF declares **no** `tokenizer.ggml.bos_token_id` and no `add_bos_token` (metadata dumped 2026-08-06: 25 KV pairs, neither key present); `Tokenizer::fromParser` sets `has_bos = declares_bos_id && wants_bos.value_or(true)` (`backend/sensen/src/tokenizer.cppm:1202–1204`), so `addBos(true)` prepends nothing for this model |
| Other `GenerationConfig` fields | 0 | `n_gpu_layers = 0` already set (the documented AUTO trap is handled); `temperature = 0.7` is argmax-invariant (positive scalar before softmax); `frequency_penalty`/`presence_penalty` default 0.0 and are gated on `!= 0.0F`; grammar/logprobs off. The penalty was the sole active distortion, which is why toggling it alone recovers everything |
| KV cache Q8 (production default, `parseKvDtypeEnv` → Q8 when `SENSEN_KV_DTYPE` unset, `backend/sensen/src/kv_half.cppm:290`) vs the probe's FULL cache | ≤ 1.1 (one row: 25/90 q8 vs 24/90 fp32 — the *q8* run scored higher) | run 2 vs run 3 |
| Scoring asymmetry (metric, not model) | 0 | both numbers are the same metric — parsed-JSON dict equality of the `<params>` block against gold (`eval_grpc_mortgage.py` `raw_exact` is computed from the pre-verification stderr line, not from `gold_as_served`); with the sampler fixed the two harnesses agree (27.8% vs 26.7%) |
| Verification layer (`mortgage_verification.cppm`) refusing correct params | **0 correct answers refused** | in runs 2 and 3, `served_exact == raw_exact` (25 == 25, 24 == 24): every raw output that matched gold was Proven and served. The baseline's 97% refusal rate was the gate doing its job against genuinely corrupted params — `raw_exact` was already 0 *before* the gate |

Notes on the verification gate, for completeness: it is a grounding prover,
not a gold-equality oracle — in run 2 it also served 8 params objects that
were grounded in the utterance but not byte-equal to gold. That is its
documented contract, not a defect. And its refusal-shape distribution at
baseline (`unknown-field` 40, `missing-field` 16, `unparseable-json` 12 of 97)
is a faithful description of penalty damage, which is corroborating evidence,
not a second cause.

## 3. Exact fix procedure, in order

1. **`backend/src/modules/mortgage_assistant_service.cpp`**, in
   `SensenBackend::generation_config()` (immediately after
   `config.deterministic = true;`, before the `n_gpu_layers` block): add

   ```cpp
   // THESE LINES ARE LOAD-BEARING, exactly like n_gpu_layers below.
   //
   // A default-constructed GenerationConfig arrives with
   // repetition_penalty = 1.1 (window 64), and Sampler::sample applies it
   // BEFORE the greedy argmax -- greedy then argmaxes over penalized
   // logits. On digit-dense JSON, where Qwen3 tokenizes one digit per
   // token, the per-occurrence division (1.1^n) collapses the logits of
   // every repeated digit/quote/brace and the decode drifts progressively:
   // measured 0/90 exact through this RPC with the default vs 25/90 with
   // the penalty off, on identical weights and rows (see
   // docs/superpowers/specs/2026-08-06-mortgage-rpc-accuracy-gap.md).
   // It also makes unpenalized homoglyph digits (U+FF10 fullwidth zero)
   // reachable inside numeric literals. Structured extraction wants the
   // model's own distribution, unmodified.
   config.repetition_penalty = 1.0F;
   ```

   (Do not "fix" this by setting `repetition_window = 0` — the guard in
   `Sampler::sample` is on `penalty != 1.0F`, and window 0 still enters
   `applyRepetitionPenalty` with `start = context.size()`; 1.0 is the value
   with a defined meaning.)

2. **`backend/src/modules/assistant_service.cpp`**, same addition in *its*
   `generation_config()` (lines 592–641) — **the strategy assistant has the
   identical latent defect.** Its answers are short (~6 fields), so fewer
   repeats accumulate in the 64-token window and it still scores 13/16, but
   its digit fields run through the same penalized argmax. Per this repo's own
   policy, re-measure the 16-row defect holdout through the real RPC before
   and after; treat any change as a re-baselining event, since the deployed
   13/16 was measured *with* the penalty.

3. **Rebuild and re-measure the mortgage RPC on the full set**:
   `ninja calculator_engine`, one engine
   (`pgrep -x calculator_engi | wc -l` == 1), then
   `eval_grpc_mortgage.py --val agent/dataset/data_mortgage/val.jsonl`
   (all rows). Expected: raw exact ≈ 27–28% (± the KV row), served params
   ≈ 33%, `<params>`-but-bad-JSON ≈ 3%, zero fullwidth/subscript digits in
   any raw output. Anything materially below that reopens this
   investigation.

4. **Do not change** `SENSEN_KV_DTYPE` (Q8 default measured at ±1 row, and
   the q8 run was the higher of the two) and do not touch
   `backend/sensen/` for this fix. Separately worth filing upstream, not
   blocking: `Sampler::sample` applying repetition/frequency penalties under
   `GREEDY` is a footgun (greedy + default config silently isn't greedy), as
   is `repetition_penalty = 1.1` as a *default*.

5. **Guard against recurrence**: add one digit-dense amortization utterance
   (e.g. val row 8's) to whatever smoke gate fronts deployment of this
   service, asserted to round-trip to exact params through the real RPC. The
   defect class — serving-side sampler config diverging from what every probe
   measures — is invisible to `numeric_audit_probe` by construction, and this
   is the cheapest detector that runs on the serving path.

After step 3, the served accuracy ceiling is the model's own 27%, which is a
**model** problem (out of scope here, but quantified in run 2's residuals: 35
`unknown-field` refusals — off-schema field names like
`compounding_frequency` for `compound_frequency`; 5 hallucinated operation
names; clarification rows asked 0/10). Retraining decisions should start from
27%, not from 0%.

## 4. What was NOT determined

- **No captured logit trace of a homoglyph flip.** The mechanism (per-token
  per-occurrence divisive penalty vs an unpenalized homoglyph id) and the
  elimination experiment (2/100 homoglyph rows at 1.1, 0/100 at 1.0, probe
  rank 19–25-below-top under raw argmax) establish causation at the outcome
  level, but I did not instrument `Sampler::sample` to dump the penalized
  logit vector at an exact divergence step showing token 26022 crossing above
  token `'0'`. If byte-level proof is ever wanted, that is the instrumentation
  to add — temporarily.
- **The one-row q8-vs-fp32 KV difference (25 vs 24 of 90) was not
  root-caused** and its direction was not tested for stability across the full
  302-row set or across models. It is bounded (≤1.1 points here) and the
  production default scored higher, so it does not change any decision above.
- **Full-set post-fix numbers.** The A/B ran on val rows 0–100 (~5 min per
  run); the full 302-row confirmation is step 3 of the fix procedure, not done
  here (the tree had to go back to pristine). The prior full-set baseline
  0/279 is `v2_final.json` from 2026-08-06 00:20–01:07.
- **The strategy assistant's production exposure was not measured.** The
  shared defect is confirmed by code identity, and its historical 13/16 was
  measured with the penalty active; how many of its 3 known holdout failures
  (bare futures directives) or any digit-precision errors trace to the penalty
  is unknown until step 2's re-measurement.
- **Why the model itself asks 0/10 clarifying questions through this harness**
  (at either penalty setting) was not investigated; it is a model/dataset
  question, unaffected by this serving fix, and the harness's own docstring
  already flags the modification-row prompt-shape caveat.
