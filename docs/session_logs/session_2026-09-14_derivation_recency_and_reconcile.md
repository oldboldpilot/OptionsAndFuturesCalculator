# Recency in the dependency graph, and two logical defects in `reconcile`

@author Olumuyiwa Oluwasanmi

Date: 2026-09-14

## What was asked

Make the derivation layer resolve the ambiguity a clarifying exchange creates,
using the Prolog dependency graph and constraint programming rather than a
post-filter — and then fix the solver failure, which was a logical
inconsistency rather than an arithmetic one.

## The measured chain, in order

Every number below is `served_exact` on the same 600-row holdout
(`val-v15.jsonl`, 563 scorable), the same v15 GGUF, one engine per port, and
`raw_exact` stated alongside because it is what proves the model did not move.

| arm | harness | raw_exact | served_exact |
| --- | --- | --- | --- |
| v15 alone | original | — | 397 |
| + Graph B1/C, precision bugs present | original | 449 | 351 |
| + Graph B1/C, precision fixed (shipped `5a1a024`) | original | 449 | 414 |
| + recency, ungated | original | 449 | 395 |
| + recency, gated on `prior_question` | original | 449 | 395 |
| shipped `5a1a024` | **fixed** | 443 | **410** |
| + recency, gated | **fixed** | 443 | 394 |

**The harness changed mid-investigation and that invalidates cross-comparison.**
`eval_grpc_mortgage.py` computed `question1` on every path, carried a comment
saying to echo it back, and then did not pass it — so `prior_question` was `""`
on every scored call. Echoing it changes the prompt, so it changes what the
model emits: `raw_exact` moved 449 -> 443 on identical weights. Any number from
before the fix is incomparable with any number after it.

It also contradicted its own docstring, which predicted the echo would recover
17 ComputeRentalRoi rows. It cost 6 raw rows instead. `asked_ok` could not move
either way: it is scored on call 1, which never carried the field.

## Four defects, and how each was found

### 1. Recency belongs in the graph, not around it

Each literal is emitted with the turn that stated it and `kRules` carries
`superseded_*` clauses. The comparison is strictly greater, so two literals in
one turn do not supersede each other and every single-turn row is unchanged by
construction. The operands of a rule are dated INDEPENDENTLY, so a revised
price combines with the ORIGINAL turn's down payment — a pairing present in
neither turn alone, and one no filter over the latest turn could produce.

### 2. "more" is a role, not a magnitude

While `$750 more a month` was an ordinary `money` fact it paired with the
opening turn's `10% down` and derived a **$675 mortgage** — two real literals,
exact arithmetic, and a rule that could not see what "more" meant.
`names_increment` routes it to `extra_money`, a kind no loan or price rule
mentions. Mutation-checked: removing the split reproduces the $675.

### 3. `prior_question` is not the discriminator it looks like

Gating recency on "did this service ask a question?" is right semantically and
wrong operationally: the field reflects WHAT THE MODEL DID. On a clarification
row where the model answers instead of asking, `prior_question` is empty and
the gate reads a clarification as a revision. Twelve HELOC rows answering
"what is your maximum LTV?" with `75%` then let that 75 supersede a stated
8.33% and came back `annual_rate = 0.75 is outside this assistant's
interest-rate range` — every one of them a row the model had served perfectly.

The offline sweep missed it because the sweep derived the same signal from the
CORPUS, where those rows correctly carry a question. **The gate and the service
disagreed about when the rule applied**, so the gate tested something
production never did.

### 4. The two real fixes, neither of which is about recency

- **The solver must not emit what the verifier will refuse.** A replacement
  re-enters validation, so an out-of-band value does not merely fail to help —
  it converts a correct answer into a refusal. `reconcile` now asks
  `mv::slot_bound_violation`, the same `detail::bound_violation` G5 itself
  calls, rather than copying the band.

- **`claimed` counted fields that were going to be skipped anyway.** The tally
  was taken over ALL candidates while the skips are decided per field
  afterwards. On a rate revision both `annual_rate` and `pmi_annual_rate`
  derive the one new value; the model emitted `pmi_annual_rate` as the
  convention `0.0000`, which the zero rule skips regardless, yet it counted as
  a second claimant and vetoed the correct replacement. **This is why recency
  measured as doing nothing at all** — 0 of 145 rows differed, the guard
  cancelling the rule exactly. The inconsistency is latent in the shipped code
  and only became visible when recency made two fields collide on one value.

## A failure class no accuracy metric can see

The layer can take an answer the model got RIGHT and make it wrong or refused.
Such a row still scores `raw_exact`, so `raw_exact` was byte-identical across a
19-row served regression. `DerivationCorpusSweepTest` feeds the layer GOLD as
though the model had emitted it perfectly and reports every field the layer
then rewrites; it needs no model and no engine, runs in 0.08 s, generates its
corpus from `CORPUS_MIX` so it re-derives itself when the corpus changes, and
is mutation-proven in both directions.

Its limit, stated because it was reached: it models the SERVICE's decisions
itself, so a divergence between the sweep's model and the service's is exactly
what it cannot see. Defect 3 above is that divergence.

## Also fixed, found by the sweep, live in production before this change

`ComputeAmortizationBatch.term_months` is the array `[360,360]`. The rule base
derives the scalar `360`, `same_value` cannot parse the array and answers
false, and the field fell through to the replace path — a correct two-element
array replaced by a scalar. `reconcile` now leaves any non-scalar emitted value
alone.

## Left undone, deliberately

`ComputePayment` loan revisions (`redo it for $817,400` where no down payment
was ever stated) derive nothing: no rule maps a lone money literal to a loan
slot, and adding one would turn rows the model serves correctly today into
refusals. It needs an explicit revision-marker role, not a looser rule.
