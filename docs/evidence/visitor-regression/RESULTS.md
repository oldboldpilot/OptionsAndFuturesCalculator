# The deal assistant returns what the visitor STATED -- results, 2026-10-07

Lane: `fix/mortgage-assistant-stated-fields`, base `ebf68e2`. Nothing here is deployed.
The "after" column is a LOCAL engine built from this tree; the "before" column is PRODUCTION,
measured on 2026-10-06 against the live partner key (`prod_baseline_2026-10-06.{txt,jsonl}`).

## Diagnosis, from the raw model output of the pre-change model and engine

Run locally on the deployed model (`mortgage-encoder.gguf`, sha256 `2d5beb8c197c9b37...`) with the pre-change
engine, so the raw line is what the model emitted before any serving code touched it:

| utterance | raw model output | what the service did with it |
| --- | --- | --- |
| `Closing costs on 450k` | the PROSE line `The assistant did not identify a calculation for this request.` (the model's `<NONE>`) | shown to the visitor as a clarifying question (production asks "What's the interest rate?" / "Over how many years?" -- a question table naming a field no closing-cost calculation takes) |
| `Refinance 320000 at 7% to 6%` | `ComputePayment{rate 0.005, future_value 0, timing END_OF_PERIOD}` | refused: "left out periods, which ComputePayment needs" -- the model named a different calculation and the layer above could only complain about its missing field |
| `rent vs buy: 600k home, 20% down, 6.5% for 30 years, rent 3000 a month` | `ComputeRentVsBuy{down_payment 600000, loan_annual_rate 0, loan_term_years 0, years 30, monthly_taxes_ins_maintenance 3000, annual_rent_increase 0.2, property_price (absent)}` | refused: "left out property_price" |
| `Evaluate rent vs. buy ... Renting costs $3,400 a month. Buying a $971,500 home ...` | every field but `current_monthly_rent` | refused: "left out current_monthly_rent" |

Row 1 is a SERVING defect: the model said it had no calculation and the service displayed that sentence as a
question. Row 2 is the MODEL naming the wrong calculation for refinance wording it was never taught. Rows 3 and 4
are both: a sixteen-field rent-versus-buy answer asks the model to place every figure of a sparse sentence into one
of sixteen slots, and a model taught to fill all of them scrambles the figures it has (the price into the down
payment, the rent into the tax slot, the 20% down into the rent increase) and zero-fills or drops the rest -- while
the contract above it treated a missing optional field as grounds for refusal. No serving rule can repair a scrambled
value, and a verifier cannot refuse it, because each value IS a literal of the utterance. So the fix has three parts that
only work together: the SERVICE stops requiring or defaulting fields, the CORPUS labels only stated ones (and teaches the
sparse shapes people type), and the MODEL is retrained on it.

## What the gate is

`backend/tests/data/visitor_regression.jsonl` -- 272 rows, each an utterance a visitor of
mortgagefvcalculator.com could type, with the outcome and the EXACT fields it should produce:

| kind | rows | what it is |
| --- | --- | --- |
| sparse | 97 | one figure per field, nothing extra -- how people actually type |
| should | 57 | advice-worded ("should I refinance...?", "is it worth...?") |
| full | 48 | every figure of a calculation stated |
| suffix | 30 | `450k`, `1.2 million`, `-100k` |
| oos | 16 | out of scope: must refuse, never ask for a rate |
| twoturn | 14 | ask, answer, complete (the contract's three-field protocol) |
| typo | 7 | `HAO duse`, `extar`, `mortage` |
| range | 3 | "25 to 30 years": a range is not a stated term |

Rows cover all 28 reachable operations, the site's own example prompts (`site-01..03`) and the
usability audit's live observations (`live-01..09`). The scorer fails any visitor-facing text that
matches `/Compute[A-Z]|_[a-z]+_|"[a-z]+_[a-z_]+"/`, never scores a 429, paces at <= 2 req/s and
never prints the key. `--self-test` proves each failure class is detectable.

## Before and after

| | production, before | local engine, after |
| --- | --- | --- |
| rows passing | **19 / 272** (7.0%) | **265 / 272** (97.4%) |
| params rows served correctly | 8 / 248 | 241 / 248 |
| out-of-scope rows refused | 9 / 16 | 16 / 16 |
| clarification rows asked correctly | 0 / 6 | 6 / 6 |
| two-turn exchanges completed | 0 / 14 | 14 / 14 |
| typo rows | 0 / 7 | 7 / 7 |

By failure class (rows):

| class | before | after |
| --- | --- | --- |
| refused "left out X" (an optional or defaulted field named as missing) | 81 | 0 |
| spurious field the visitor never stated (zero-filled / defaulted) | 63 | 1 |
| asked a question where params (or a refusal) were expected -- including ones the visitor had already answered | 52 | 0 |
| refused something computable | 33 | 1 |
| wrong operation named | 13 | 1 |
| clarifying question about the wrong field | 8 | 0 |
| internal text (an operation or field name) shown to the visitor | 2 | 0 |
| answered something out of scope | 1 | 0 |
| a stated field omitted | -- | 3 |
| a wrong value | -- | 1 |
| **total failing** | **253** | **7** |

The site's own example prompts and the usability audit rows:

| id | utterance | before | after |
| --- | --- | --- | --- |
| site-01 | `550k home, 10% down, 6.75% 30-year, 1.2% taxes, $200 HOA` (textarea placeholder) | FAIL, internal text leaked | PASS |
| site-02 | `$550k house, 10% down, 6.75% for 30 years, taxes 1.2%, $200 HOA` (guest description) | FAIL, "left out" | PASS |
| site-03 | `... insurance $1,800, $200 HOA, paying $300 extra a month` (empty-state example) | FAIL, "left out" | PASS |
| live-01..03 | `Closing costs on 450k` / `...with 10% down` / `...house at 6.5%` | FAIL, asked "Over how many years?" | PASS |
| live-04, 05 | `Refinance 320000 at 7% to 6%` / `...closing costs 5000` | FAIL | PASS |
| live-06, 07 | `can I afford a house` / `...on 90k a year?` | PASS (refused as advice) | PASS |
| live-08, 09 | `payment on a 400000 loan at 6.5% for 30 years` / `...$455,000 house with $55,000 down...` | FAIL, zero-filled fields | PASS |

`taxes 1.2%` in site-01..03 is not a field any calculation here takes (it is a percentage of
value, the calculations take a dollar amount), so it is neither served nor invented; the
calculation proceeds on the figures that are representable. That is a product decision to
confirm, not a defect found by this work.

## The seven rows still failing, each from the model's raw output

All seven are EXTRACTION errors of a 1.14M-parameter encoder, not contract or serving defects.
Raw output is what the engine logs before any shaping.

| id | what happens | raw evidence |
| --- | --- | --- |
| ip-03 | refused "couldn't tell which calculation": `Is the first payment mostly interest? $350,000 at 7% ...` | the model answers `<NONE>`; no wrong value, an honest refusal |
| rb-04 | serves `annual_rent_increase = 0.0675` beside the correct `loan_annual_rate` | the one stated `6.75%` is selected for BOTH rate slots |
| hn-01 | `annual_discount_rate` (stated "discount rate 5%") omitted | the pair head gives the `5%` literal to no slot |
| sc8-01 | `annual_rent_increase` (stated "rising 3%") omitted | same shape, other literal |
| cc-09 | `inspection_fee = 300` (stated 500) and `recording_fees` (stated 300) omitted | the `$300 recording` literal is attached to the inspection slot; `$500` to none |
| npv-04 | `values` ends at 50,000: the 60k flow is dropped (`-100k now then 30k, 40k, 50k, 60k over four years`) | the last literal is not selected |
| am-14 | names `ComputeAmortizationBatch` for `should I get a 15 year or 30 year? amortize $300,000 at 6.5% over 15 years first` | operation head prefers the comparison reading |

Three of the seven put a wrong or unstated VALUE in front of the visitor (rb-04, cc-09, npv-04),
about 1.1% of the corpus; the rest omit or refuse. Two of those three are the "per-field
blindness" this repository already records: the value IS a literal of the utterance, grounding is
per field, and the verifier has no way to know which of two fields a literal belongs to.

**A serving-layer remedy for rb-04 was built, measured and REVERTED.** "A literal fills at most one
rate slot, the most confident slot wins" is consistent with the whole training corpus (2,403 of
41,220 rows have a literal filling two slots and not one of them is two rates). Over the 272 rows it
fired exactly once -- on rb-04 -- and kept the WRONG slot, because this model ranks rent growth above
the loan rate there. It turned "correct field plus a spurious one" into "a spurious field alone", so
it was removed rather than shipped on a sample of one.

## Model: which one, and why not the others

| candidate | train rows | epochs | val ROW (own val) | visitor regression | existing holdout, stated-only |
| --- | --- | --- | --- | --- | --- |
| v5d | 37,421 | 12 | -- | 262 / 272 | not run |
| **v5e** | 41,220 | 12 | 97.93% | **265 / 272** | **559 / 559** (447 exact + 112 deviating only by `original_home_value`) |
| v5f | 41,220 | 20 | 98.43% | 264 / 272 | 558 / 559 + 1 refusal (row 522) |

v5f has the higher in-distribution score and the lower held-out one: its eight failures are
a DIFFERENT set (three wrong operations, two spurious fields), which is what run-to-run variance at
this size looks like. v5e was kept because it is better on the older holdout and fails in fewer
dangerous ways, not because 265 beats 264 -- that difference is noise.

**ADOPTED GGUF: `mortgage-encoder-v5e.gguf`**
- sha256 `b03dcdeff5da7c18d0e0871457d54ef1a9e41b674fe9e3af496f2bb2af3434db`, 4,640,544 bytes
- checkpoint `encoder_fp32.pt` sha256 `74ba8cef9062b5e75cd347b3d94014480668a2c193991394eea9f293cedc0c9d`
- 129 (slot, map) pairs, 29 operations, 5 convention heads, vocabulary 4,096 (the v4 GGUF had 2,694);
  from scratch, `--servable`, d=128, 3 layers, 4 heads, 12 epochs, seed 0
- corpus `agent/dataset/data_mortgage_v5/`: train 41,220 rows sha256 `c451e5d5...5207`, val 2,169 rows
  sha256 `073b2b37...4531`, generator sha256 `bef9f345...8d81` (`meta.json`)
- disjointness, measured: 0 of the 600 existing-holdout rows have their user turns in train; 1 of the
  272 visitor rows (`oos-10`, "what credit score do I need for a mortgage?") has its text in the
  legacy out-of-scope bank, so that one row is in-distribution
- NOT staged anywhere the engine reads it. Deploying it is a separate decision; see "What mortgage-nest-egg
  must change" in the hand-off.

## The existing 600-row holdout

`agent/dataset/data_mortgage/val.jsonl`, sha256 `062614ce7bb18abd055e28a1e0ef9b62b714437f2583e5575e784e8626b23c06`.
This file carries 559 rows with params and 41 prose rows, not the 560/40 this repository's notes quote
for an earlier regeneration, so the baseline was re-measured on THIS file.

| arm | result |
| --- | --- |
| production model + pre-change engine (`existing-holdout/baseline_production_model_and_engine.txt`) | 557 / 559 (rows 264 and 555 serve `pmi_annual_rate = 0` for a stated 1.31% / 0.31%) |
| v5e + this tree, the old complete-answer comparator (`after_v5e_strict_...`) | 61 / 559 -- the old comparator counts every deliberately omitted field as a miss; kept only so the number is not hidden |
| **v5e + this tree, stated-only comparator** (`after_v5e_stated_only.txt`) | **447 exact + 112 deviating ONLY by `original_home_value` = 559 / 559**, 0 wrong, 0 invented, 0 refused |
| same, two-call contract (`after_v5e_stated_only_two_call.txt`) | the same; 82 clarification rows, 63 asked on turn one and all 63 completed on turn two |

The stated-only comparator is `score_encoder_served_rpc.py --stated-only`: a gold field is STATED when
the oracle labelled a literal of the utterance for it, and must then be served and equal; any other gold
field is a convention value and may be absent; nothing may be invented. Every non-clean row is printed.

**Every one of the 112 changed rows is one field and one documented decision.** The old corpus
labelled `original_home_value` from the loan literal ("amortize $480,000..." gold `original_home_value =
480000`). That is the loan counted twice -- it prices PMI and equity against a house worth exactly the
loan, which is how a $480,000 loan met a "$275 house" in production on 2026-10-05. It is no longer
labelled, no longer served, and the comparator is told so explicitly with `--accept-missing
original_home_value`, which keeps those rows OUT of the "exact" column. The two production errors
(`pmi_annual_rate` zeroed) are served correctly now.

## The strategy assistant is unchanged, byte for byte

The strategy path shares the encoder chain (`encoder_assistant.cpp`, `encoder_reconstruct.cppm`) and
none of the verifier or service. Its market-data verification needs credentials this lane may not
read, so the comparison is taken BEFORE verification: both engines (pre-change binary, this tree) loaded
the same `strategy-encoder.gguf` (sha256 `abd40b21e4971c2d27700cf1fd468c4c4934c2b2fdc32edf7b8b5685f74c2f84`) and answered the same 2,013 `ParseStrategy`
requests (the 1,500-row strategy holdout, clarification turns included); the engine's `raw model output` lines are byte-identical
(`cmp` clean, both files sha256 `450a6376ee21...`). `assistant_service.cpp` is untouched.

## Parity gates, all on the v5e artefacts

| gate | result |
| --- | --- |
| `reconstruct()` C++ vs Python (`parity/reconstruct.txt`) | **2,169 / 2,169 rows**, 17,067 decimal values (16,779 exact, 288 within one BigDecimal ulp of 1e-38). Roles exercised include `M9 a*p` (8), `M3 annual%->quarterly` (2), `M5 years->quarters` (2) and repeated identities `#rep2..#rep20` (69) |
| WordPiece tokenizer (`parity/tokenizer.txt`) | **2,472 / 2,472 utterances**, ids and spans, on BOTH routes (vocab.txt and the GGUF's own keys), byte-identical to each other |
| deployed lexer vs trainer `lex()` (`parity/lexer.txt`) | **2,472 / 2,472 utterances, 10,687 literals**: order, span and value agree; 284 tag divergences (`bare`->`money`), counted, unused at serving |

The reconstruct oracle had to change once: Python's `Missing` was written as the TEXT `"MISSING"`, which no C++
output can equal, and the stated-only contract makes most fields missing on most rows -- the gate read 687
disagreements on arithmetic that agreed everywhere. It now writes `null`.

## Mutation arms (each with `CCACHE_DISABLE=1`, RED shown, source restored byte-identical)

`mutation/arms_all_final.txt` is the run of all 27 arms against the final tree, `mutation/roles.txt` the three role
allow-list arms of the encoder parity gate; runnable as `scripts/mutation_arms_stated_only.py` (`A1 A2 S1` selects
the three the brief names). Every arm was RED, and after every arm the restored source compared equal to the
baseline (`restored == baseline: True` in the log). "RED" is the number of failing checks in the owning test
binary, whose total is in the header (398 / 27 / 351 / 84 checks).

| arm | mutation | RED | symptom reproduced |
| --- | --- | --- | --- |
| A1 | `ComputePayment` also REQUIRES `future_value` | 8 of 398 | `present_value = 304000` against a 495,000 utterance is kept again; a payment needs principal, rate and term and nothing else |
| A2 | a Defaulted field is kept | 17 of 398 | zero-filled HOA, repairs, insurance, growth, overpayment and PMI come back; a home value invented from the loan |
| A3 | range masking removed | 4 of 398 | a ranged rate or a "25 to 30 years" term is not asked about, and the end the model took is served |
| A4 | a convention constant claimed by another field still counts as stated | 1 of 398 | `factor = 2` taken from the "year 2" that is the period |
| A5 | scaled literal loses the cash-flow negation | 2 of 398 | `-100k now then 30k...` ungrounded |
| A6 | `plain_decimal_text` -> default `to_chars` | 9 of 398 | `-1e+05`, `5e+05` refused |
| A7 | a bare condo is dues again | 1 of 398 | `a 300k condo` read as HOA |
| A8 | the spelled percent word no longer advances the scan | 1 of 398 | `30 percent down` unrecognised as a deposit |
| A9 | a deposit as a percent of the price no longer grounds | 1 of 398 | `631500, 20% down` refused |
| A10 | `yearly` is not a cadence word | 1 of 398 | `compounded yearly` ungrounded |
| A11 | no cadence is ever unsupported | 6 of 398 | bi-weekly, fortnightly, twice a month, semi-monthly silently priced monthly |
| A12 | a declared field without a plain-language label | 1 of 398 | a question or refusal that would show the visitor a field name |
| A13 | a spaced hyphen is a minus again | 4 of 398 | `Atlanta - 479k loan` lexes -479k and the principal is ungrounded |
| A14 | `down to` is a deposit again | 1 of 398 | `5.375% down to 4.375%` read as a 5.375% deposit |
| A15 | a HOA word followed by its own figure also claims the figure before it | 3 of 398 | the loan or the price taken as the dues (`$339,600 borrowed HOA $275/month`, `a $387,000 home HOA 100/month`): a HOA word with its own figure after it also claimed the one before |
| A16 | a HOA word already claimed by a figure before it claims the next one too | 3 of 398 | the figure after claimed dues taken as dues (`$300 hoa dues, 286400`) |
| A17 | an increment word before a figure beats a HOA word right after it | 2 of 398 | `+1000 extra $300 HOA`: the 300 read as an overpayment |
| A18 | vacancy only recognised AFTER the figure | 3 of 398 | `vacancy 10%` not read as a vacancy, occupancy not 0.90 |
| A19 | a no-deposit purchase no longer states the home value | 1 of 398 | `$900,000, no down payment` loses its home value (a home worth the loan) |
| S1 | the advice carve-out removed | 5 of 27 | `Should I refinance my $300,000 loan at 7.5% to 6.25%?` refused as advice |
| S2 | the one-figure-and-a-named-calculation clause removed | 2 of 27 | `should I rent or buy a $450,000 house?` refused as advice |
| F1 | lump-sum `ComputeFutureValue` requires a payment again | 2 of 351 | `what will $10,000 grow to at 5%?` refused for a payment nobody makes |
| F2 | a batch with omitted optional arrays is ragged again | 2 of 351 | a loan comparison refused for extra payments nobody stated |
| D1 | a bare figure of money size is no longer a money fact | 3 of 84 | the rent taken for the price |
| D2 | a stated non-monthly compounding no longer stops the x12 rules | 3 of 84 | `40 years compounded yearly` rewritten to 480 periods |
| D3 | an annual rate is rewritten to a monthly one | 1 of 84 | an annual rate is rewritten to a monthly one when the horizon was misspelt |
| D4 | periods stay in months beside an annual rate | 2 of 84 | an annual rate with its 15 years is not left alone, and 180 periods beside it is not repaired to 15 years |

Role allow-list arms (parity gate, `mutation/roles.txt`): `M9 a*p` removed fails 8 rows, a repeated identity bounded
fails 54, quarterly removed fails 2.

An earlier version of this table listed 21 arms and counts taken before the lexer and derivation fixes (A2 was 15,
A1 7). A13-A19 and D3/D4 are the arms for those fixes; the counts above are from the final tree.

## ctest, final tree

`ctest --test-dir backend/build-lane -j4 --timeout 900`, serialised on the shared lock, after a `CCACHE_DISABLE=1` rebuild
of `build_tests calculator_engine dbg_grounding dbg_derivation` (rc=0; the build dir had held mutated test binaries from the
last mutation arm, so nothing here was measured on them). Summary: `ctest_final_summary.txt`.

| | baseline (the tree before this lane) | final tree |
| --- | --- | --- |
| tests | 218 | 218 |
| passed | 208 | **208** |
| skipped | 8 (absent brokers, no GPU, sanitizer overlay) | 8, the same eight |
| not run | 2 (`CausalityBenchGate`, `CausalityBenchGateCanFail`) | 2, the same two |
| failed | `GroundingCorpusSweepTest`, `MortgageGrammarTest` | **0** |

The two baseline failures now pass, and `DerivationCorpusSweepTest`, which failed in the earlier full run (before the lexer
and derivation fixes of this lane), passes too. The two "Not Run" are the same two the baseline carries; ctest reports
them in its failed list and they are not tests that ran and failed. Counts of the four new or extended test binaries are in
the mutation table above.

## Open: what the larger corpus sweeps still find (NOT fixed, deliberately)

The gate runs the grounding and derivation sweeps at n=500 (466 rows): 0 refused, 0 asked wrongly, 0 stated figures
dropped, 0 rows corrupted. The defects fixed in this lane (spaced hyphen, `down to`, HOA claims, vacancy before its
figure, annual-cadence periods) were found at n=6,000 and n=25,000, and those two samples still find a residue.
Full output, regenerated against the final tree, is `corpus_sweeps.txt`. Honest account, in the order of the sweeps:

| sample | finding | what it is |
| --- | --- | --- |
| n=6,000 (5,589 rows) | **1 of 17,245 ask probes asked wrongly** | `depreciation: cost 96,500 year 2 macrs 15-year`: with the cost mutated off-corpus the layer asks `What did the asset cost?` instead of refusing the mutated value. The utterance states the cost, so this is the direction `refine_unstated` promises never to go (a question where a corrupted value should be refused). One probe in 17,245; not diagnosed past the row |
| n=25,000 (23,270 rows) | **1 refused** | `... rent is $5,600 7% vaancy at 6.13% APR`: a typo'd `vacancy` is not a role word, so `occupancy_rate = 0.93` is ungrounded and refused (the refusal direction: a wrong answer was not served) |
| n=25,000 | **3 asked wrongly, 6 stated figures dropped** | the sweep prints one example, `in paymentts 1 to 13?`: a typo'd role word that is an INSERTION away from the real one (an earlier n=25,000 run also recorded a `hoouse` row; it is not in the printed example of this one). The transposition rule of 2026-10-05 reaches `hoa`/`hao`, not an insertion, which the project guide records as the known gap. `ComputeCumulative.start_period = 1` is asked about and `start_period`/`end_period` (1 and 13) are dropped though the words state them |
| n=25,000 | **3 derivation corruptions of 23,270 rows** | `ComputePayoffTiming.extra_monthly_payment` 250 -> 1,809.29; `ComputeAmortization.monthly_overpayment` 750 -> 886,600; `ComputeRentalCashFlow.property_price` 300,000 -> 1,400. Each is a correct emitted value overwritten by a derived one. The utterances are not printed by the sweep and these were not diagnosed |

Why these are written down and not fixed: the n=500 gate that guards the tree is green, none of the four is a
regression against production (production serves the zero-filled complete answer for all of them), each is a rate
of roughly one row in several thousand, and the two typo findings are the documented insertion gap rather than a new
defect. The three derivation corruptions are the one item that serves a wrong value rather than refusing or asking,
which is why they lead the owner decisions in the report. The smallest next step is to print the utterance beside each
`CORRUPT` line in `dbg_derivation --sweep` and bisect the rule; none of it needs a retrain.

## Reproduce

```
# the gate itself, against any engine
python3 scripts/score_visitor_regression.py --self-test
python3 scripts/score_visitor_regression.py --target local:<port> --json out.jsonl
python3 scripts/score_visitor_regression.py --target local:<port> --two-turn-protocol site   # the site's broken second turn
python3 scripts/score_visitor_regression.py --target local:<port> --dispatch                 # answered params -> Finance
python3 scripts/score_visitor_regression.py --target prod --max-rps 1 ...                    # ~190 parses/hour budget, shared with the live site

# a local engine on the new model (never source config/.env)
scripts/run_with_env.py --env-file <empty.env> --set MORTGAGE_ASSISTANT_BACKEND=encoder \
  --set MORTGAGE_ENCODER_PATH=<mortgage-encoder-v5e.gguf> --set ENGINE_GRPC_PORT=<port> \
  --set PRO_GATE_MODE=off --set INFERENCE_QUEUE=local --set DATABASE_URL= --set QUOTA_POLICY= -- \
  stdbuf -o0 backend/build-lane/calculator_engine

# the existing holdout
ENCODER_RPC_TARGET=localhost:<port> python3 scripts/score_encoder_served_rpc.py --stated-only \
  --accept-missing original_home_value --model <gguf> --val agent/dataset/data_mortgage/val.jsonl

# the corpus and the model
python3 agent/dataset/build_mortgage_dataset.py --out agent/dataset/data_mortgage_v5 --n 44000 --seed 0 \
  --exclude-holdout agent/dataset/data_mortgage/val.jsonl     # reproduces train.jsonl and val.jsonl byte for byte
python3 agent/train/train_encoder.py --servable --train agent/dataset/data_mortgage_v5/train.jsonl \
  --val agent/dataset/data_mortgage_v5/val.jsonl --out-dir <run> --epochs 12 --extra-val <old holdout> \
  --augment-digits 0.25 --augment-format 0.4 --threads 8 --seed 0
python3 agent/train/export_encoder_gguf.py --checkpoint <run>/encoder_fp32.pt --out <gguf>

# parity
python3 agent/train/make_reconstruct_parity_fixture.py --ckpt <run>/encoder_fp32.pt \
  --corpus agent/dataset/data_mortgage_v5/val.jsonl --out <dir>
backend/build-lane/test_encoder_reconstruct_parity <dir>/schema.json <dir>/fixture.json > <dir>/cpp.ndjson
python3 scripts/check_encoder_reconstruct_parity.py <dir>/python.ndjson <dir>/cpp.ndjson --expect-rows 2169
python3 agent/train/export_encoder_tokenizer.py --checkpoint <run>/encoder_fp32.pt --out <dir>/vocab.txt \
  --val agent/dataset/data_mortgage_v5/val.jsonl --fixture-out <dir>/fixture.json --source user_turns --turns all
backend/build-lane/test_encoder_tokenizer_parity <dir>/vocab.txt (or the .gguf) <dir>/fixture.json > cpp.ndjson
python3 scripts/check_encoder_tokenizer_parity.py <dir>/fixture.json cpp.ndjson <dir>/vocab.txt
backend/build-lane/test_encoder_lexer_parity <dir>/fixture.json > lex.ndjson
python3 scripts/check_encoder_lexer_parity.py <dir>/fixture.json lex.ndjson

# mutation arms (one ninja per build dir)
python3 scripts/mutation_arms_stated_only.py            # all
python3 scripts/mutation_arms_stated_only.py A1 A2 S1   # the three named in the brief

# full suite, serialised on the shared lock, output to a file
CCACHE_DISABLE=1 ninja -C backend/build-lane -j5 -l 28 build_tests calculator_engine dbg_grounding dbg_derivation
flock /home/muyiwa/.cache/lanes/test.lock ctest --test-dir backend/build-lane -j4 > ctest.log 2>&1
```

## What was NOT measured

- Production "after": nothing was deployed, so the after column is a local engine. The model, the engine
  binary and the service code are exactly the ones that would ship; the Railway edge, Envoy and the Pro gate are
  not in that path.
- The site's own client was not run. The protocol break (`--two-turn-protocol site`: 2 of 14 exchanges complete,
  against 14 of 14 under the contract) and the Finance dispatch (`--dispatch`: 137 of 247 stated-only answers accepted
  by Finance as they are, the rest rejected by name for an input the client's own form holds) are measured at the
  RPC boundary.
- The visitor corpus was written by the same lane that built the generator and has not been seen by anyone
  else; the 97.4% is a regression gate, not an estimate of production accuracy.
- The sweeps at n=6,000 and n=25,000 still find a residue (above, "Open"), including three derivation rewrites that
  replace a correct value with a wrong one. The gate size n=500 is green.
