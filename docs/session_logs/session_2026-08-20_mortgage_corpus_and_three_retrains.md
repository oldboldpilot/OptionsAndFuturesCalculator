# The mortgage assistant was never broken — the corpus was

**Date:** 2026-08-20
**Author:** Olumuyiwa Oluwasanmi
**Outcome:** v6 promoted to model of record. Three dataset defects fixed, one
tokenizer defect, one proto-parser defect, one provenance gap.

## What was believed at the start

`CLAUDE.md` recorded the mortgage assistant at **27.8% params exact-match
(25/90)** through the real RPC and carried a section headed *"The mortgage
assistant is separately BROKEN"*. A retrain (v3) had been spent and had
regressed. The working theory was a weak model.

## What was actually true

The corpus contained labels that could not be derived from their own inputs.
Three instances, all the same shape.

### 1. `phrase_money` destroyed the cents (commit `c6bcc62`)

```python
def money_str(v): return f"{v:.2f}"    # LABEL:     1824.51
def phrase_money(v): return f"${round(v):,}"   # UTTERANCE: $1,825
```

The cents are not recoverable from the utterance. Worse, the serving-side
grounding gate in `mortgage_verification.cppm` REFUSES an emitted value that
"does not correspond to anything in the request" — so the model was squeezed
between the label and the verifier and learned to invent digits. That is the
documented "corrupted value" failure (`5379.00` for a stated `5378.63`), and it
is **trained behaviour, not decode corruption**.

Evidence it is the data and not either model:

| | |
| --- | --- |
| per-operation poisoning | bimodal — 8 operations at 99–100%, 19 at 0% |
| poisoned rows failing for BOTH v2 and v3 | 98/98 |
| v2 re-scored on the corrected holdout, **no retrain** | 255 → 325 (49.0% → **62.5%**) |
| poisoned operations, same weights | 0/98 → 49/98 |
| refusals | 214 → 135 |

**The 27.8% was a statement about the harness, not the model.** This is the
third time in this project a score has described the measurement rather than
the weights, after the strategy assistant's `llama-cli` phantom and the
`evaluate.py` bf16 gap.

### 2. `prepaid_interest_days` labelled a convention nothing stated

21 of 26 held-out closing-cost failures were this field alone — every gold value
15, every emission a number scraped from elsewhere in the request (180, 30, 36,
150, 250).

The first fix was to OMIT the key, which is exactly what `finance.proto`'s only
explicit-presence field is for: absent means the convention and
`value_or(15)` computes it. **It did not work.** Of 25 held-out rows whose gold
omitted the field, the model emitted a value on **25**, seventeen of them a
literal `0` — which is not the same computation. 48% of training rows omitted
the field; inference omitted it 0% of the time.

The model treats a sixteen-field answer as a fixed schema and fills every slot,
so the only slot it can fill correctly is one the utterance grounds. Grounding
it took closing costs **15/42 → 39/42**.

### 3. Six decimal places of precision the values did not have

`rate_str` defaults to six places; every closing-cost percent is generated with
`round(..., 4)`. All **3270 of 3270** labels ended in `"00"`. The model emitted
the four-place convention it sees everywhere else, so numerically exact rows
failed string equality: **0/42 string, 16/42 numeric**.

The other six-place operations were deliberately NOT touched — their rates are
per-period (`ComputePayment`'s `0.005625` is 6.75%/12) and four places prices a
different loan.

## Three retrains

All 2026-08-20, from `unsloth/Qwen3-0.6B`, QLoRA rank 16, 4 epochs, RTX 5090,
736 steps, measured against v2 on the same 520-row legacy holdout through the
real `ParseOperation` RPC on the Q8_0 GGUF, one engine on `:50051`.

| | v2 | v4 | v5 | **v6** |
| --- | --- | --- | --- | --- |
| vocabulary extension | none | 17 tokens, masked | 16 tokens, masked | **none** |
| trainable parameters | — | 165,675,008 | 165,675,008 | **10,092,544** |
| train_loss | — | 0.2476 | 0.2484 | 0.2460 |
| legacy raw exact / 520 | 325 | 320 | 317 | 313 |
| legacy served exact | 296 | 297 | 291 | 282 |
| clean family / 357 | 248 | 225 | 217 | 237 |
| formerly-poisoned / 98 | 49 | 68 | 69 | 46 |
| closing costs numeric / 42 | 0 | 16 | 15 | **39** |
| closing costs served as params | 0 | 11 | 20 | **28** |
| non-param withheld / 38 | — | 35 | 38 | 34 |

**v6 vs v2, paired McNemar on the row index:** 40 lost / 28 gained, net −12,
**p = 0.182 — not significant.** It gains an operation v2 cannot express at all.

### The vocabulary extension was the regression

`--extend-vocab` calls `requires_grad_(True)` on the whole embedding matrix,
making it **155,582,464 of 165,675,008 trainable parameters (93.9%)** and
leaving the LoRA adapters at 6.1%. Global gradient-norm clipping is dominated by
freshly-seeded rows and starves every adapter update. Removing it recovered the
clean family 217 → 237 and trainable parameters to exactly the 10,092,544 LoRA
weights.

**The gradient mask measurably held while this happened** — all 151,669
pre-existing rows moved `0.000e+00`. The mask stops the weights moving; it
cannot take them out of the optimizer. The extension was never needed: the model
spells `ComputeClosingCosts` as an ordinary BPE sequence, and v6's GGUF is
byte-for-byte the same size as v2's.

### `annual_rate` was a real defect that was not the cause

v4 derived its token list from ComputeClosingCosts' fields and so added
`annual_rate` — which is also a field of six other operations, **10,425
occurrences outside closing-cost rows**, including the system prompt's own
label-space listing. HuggingFace splits added tokens out with a trie BEFORE BPE,
so every one of those moved onto a single fresh, mean-seeded row.

`closing_cost_vocab` now excludes any candidate occurring outside a closing-cost
row, tested as a **substring of raw row text** rather than by JSON-key equality,
because the trie does not care about JSON structure. Exactly one of seventeen is
excluded.

**Removing it made the regression worse** (v4 −23 rows, v5 −31), and the broken
rows name `ComputeAmortization` — `ComputeDetailedAmortization`'s own sibling,
nothing to do with retokenisation. Finding *a* bug is not finding *the* bug.

## Two defects found on the way

**The proto parser discarded `optional`.** `_FIELD_RE` matched the keyword in a
non-capturing group, so `params_block` treated the one explicit-presence field
in the label space as required and rejected the only correct answer. Same class
of trap `CLAUDE.md` already records for the three text-level proto parsers that
strip `repeated ` and not `optional `. Now captured and recorded.

**The corpus seed was never recorded.** The argparse default is `3407`; the
corpus was built with `--seed 0`, the module's own documented invocation. A
rebuild meant to change only the closing-cost rows reseeded all 11,400 —
**11,198 of 11,400 rows changed, moving held-out rows into training** — caught
only by diffing against a kept copy. `main()` now writes `meta.json` with the
seed, the row counts and the sha256 of both generator and proto.

A related rule the same day: a change meant to affect one operation must consume
the same randomness as what it replaces, because every generator shares one
`Random`. The first grounding fix added an `rng.random()` call to a branch that
had none and reshuffled everything.

## Left open

- **v6's formerly-poisoned family is 46/98 against v5's 69/98.** The vocabulary
  extension was helping those eight operations by ~20 rows while costing ~25
  elsewhere. That interaction is not explained by the parameter-split mechanism
  and is the first thing to look at if this is picked up again.
- **The evaluation harness is still sequential.** `eval_grpc_mortgage.py`
  matches the engine's `raw model output` stderr line to a request
  POSITIONALLY. The serving stack is genuinely concurrent — `max_concurrent=4`
  per replica behind the SGEE queue, proven with 24 concurrent `ParseOperation`s
  — so the constraint is the harness, not sensen. The RPC response is correctly
  correlated under concurrency; only the raw pre-verification decode is not.
  Adding a request id to that one log line makes evaluation 4× faster.

## Gates that now exist because they were missing

- `closing_cost_vocab` excludes colliding candidates and prints what it dropped.
- `gguf_vocab.py` asserts each expected token sits ABOVE the base-vocabulary
  boundary and that `annual_rate` does not. Mutation-checked: run against v4's
  GGUF it fails by name with `annual_rate ADDED AT id=151670`.
- `check_frozen_rows.py` compares the masked and trainable bands against the
  base checkpoint and refuses if they moved by similar amounts.
- `build_mortgage_dataset.py` writes `meta.json`.
- Every closing-cost label is asserted grounded in its own utterance (545/545).
