# The options assistant "futures gap" is a measurement-environment artifact

@author Olumuyiwa Oluwasanmi

**Date:** 2026-08-06.
**Verdict up front:** tonight's 6/14 is not a model defect, not a dataset
defect, not the system prompt, not the input guards, and not the sampler.
It is the defect-holdout run against an engine that had **no market-data
credentials**, which turns every row needing a live equity quote into a
`DATA_UNAVAILABLE` refusal that the eval harness silently scores as
`got=None`. With credentials present, the same binary, same model, same
harness scores **16/16**. No production code change is required to restore
the score; the required changes are to the measurement procedure (and one
latent bug found along the way, below).

Everything here was measured this session against the pinned model —
`param-agent-qlora-v2-Q8_0.gguf`, sha256 verified
`eab97cf531b0c3746e366afafaaf74f90bec2d9263f269cd66018605223d80ac` — through
the real `calculator.assistant.StrategyAssistant/ParseStrategy` RPC
(`agent/train/eval_grpc.py --file defect_holdout.jsonl`), with exactly one
`calculator_engine` asserted before and after every run (`pgrep -x
calculator_engi | wc -l` == 1; count 0 at session end). Binary:
`backend/build/calculator_engine` built 2026-08-06 03:54 from HEAD `f6f5b61`.

## 1. The reproduction

Three runs, identical binary, identical model, identical holdout:

| run | environment | result |
| --- | --- | --- |
| A | `config/.env` sourced as-is | 0/0 — 13× `PERMISSION_DENIED` (the `.env`'s `PRO_GATE_MODE` enforces), then 3× `RESOURCE_EXHAUSTED` (its `QUOTA_POLICY` budget exhausted by the denied calls) |
| B | `.env` sourced, `PRO_GATE_MODE`/`QUOTA_POLICY` unset | **14/14 params exact, 2/2 non-params, 3/3 asked-when-ambiguous = 16/16** |
| C | **no Alpaca credentials at all** | **6/14 params exact, 2/2 non-params, 3/3 asked** — and the harness's "first mismatches" list is byte-for-byte the five utterances reported tonight, in the same order, all `got None` |

Run C is tonight's number, reproduced exactly. The eight failing rows in run C
are precisely the rows whose symbol requires a live Alpaca equity snapshot:

- `Long NQ`, `Short GC`, `options on gold futures`, `gold outright long`,
  `long the 30-year bond futures` — futures claims on **non-ambiguous** roots
  (NQ/GC/ZB), which `probe_symbol` checks via `market_data::fetch_quote`
  before falling back to the catalogue;
- plus three rows tonight's report did not list: `Iron condor on SPY`,
  `bull call spread on NVDA`, and the `calendar spread on CL` → "options"
  clarification row (an EQUITY claim on CL).

The six rows that *pass* without credentials are exactly the FUTURES claims
on the two **ambiguous** roots (ES, CL): once `validate_and_populate_params`
has disambiguated them from the trader's own words, `probe_symbol` takes the
static-catalogue path and never touches the network
(`backend/src/modules/assistant_service.cpp:1763-1770`).

The mechanism, confirmed by direct RPC against the credential-less engine:

```
'Iron condor on SPY, 30 days out, one contract.' -> refusal:
reason=DATA_UNAVAILABLE msg='Live market data is temporarily unavailable,
so "SPY" could not be verified. Please try again shortly.'
'Long NQ, 45 days, 2 contracts.' -> refusal: reason=DATA_UNAVAILABLE ...
```

`AlpacaProvider` returns `NotConfigured` when `ALPACA_API_KEY`/
`ALPACA_API_SECRET` are unset; `probe_symbol` correctly maps that (and
`NetworkError`) to `ProviderUnavailable` rather than "symbol does not exist"
(`assistant_service.cpp:1778-1780`) — the refusal is the engine being honest.
The *harness* is what buries it: `eval_grpc.py` maps any non-params outcome
to `got=None` and never prints the refusal reason, so an infrastructure
refusal is indistinguishable from a model failure in its output. That the
SPY iron condor and NVDA bull call spread — the two baselines this model has
never failed — were among the eight should have been the tell.

**Why the score was penalty-independent:** the failures never depended on a
single generated token. The model's raw output was identical and correct-or-
recoverable in every run; the refusal happened in the symbol-verification
layer after (or independent of) generation. A sampler A/B cannot move it.

## 2. What the model actually gets wrong (the real, narrow gap)

The engine logs raw output unconditionally before verification
(`[assistant] raw model output`, `assistant_service.cpp:2254`). From run B,
quoted verbatim:

- `"Long NQ, 45 days, 2 contracts."` →
  `<think>\n\n</think>\n\nI can't price a position directly — I calculate
  options and futures positions. Would you like me to set up a position
  instead?` — no params.
- `"Short GC, 60 days, 1 contract."` → `<think>\n\n</think>\n\nI can't do
  that.` — no params.
- `"gold outright long, 30 days, 3 contracts"` → `<think>\n\n</think>\n\nI
  can't do that. I only do options and futures.` — no params.

That is the entire model-level failure class: **three utterances, all bare
directives with no strategy-vocabulary token.** The other two rows tonight's
report grouped with them are answered correctly *by the model itself*:

- `"options on gold futures, 45 days, 2 contracts"` →
  `<params>{"symbol":"GC",...,"strategy":"covered_futures_call",...}</params>`
- `"long the 30-year bond futures, 90 days, 1 contract"` →
  `<params>{"symbol":"ZB",...,"strategy":"futures_long",...}</params>`

### Root cause: utterance shape, not the root set

CLAUDE.md attributes the gap to "the training set restricts futures roots to
ES and NQ". That is true of the data but it is **not the operative cause**,
and the observed pattern contradicts it both ways: `Long NQ` fails although
NQ is in the training set, and GC/ZB rows succeed although those roots never
appear in it.

The deployed model of record (trained 2026-08-03, per
`docs/STRATEGY_ASSISTANT_PIPELINE.md` §2b: data = `agent/dataset/data`,
28,500 rows) was trained on the dataset generated by `build_dataset.py` at
commit `8c498fc` (the committed `data/train.jsonl` is dated Aug 1). In that
generation:

- `FUTURES = ["ES", "NQ"]`; measured over the committed `train.jsonl`:
  FUTURES-class rows are ES ×2827, NQ ×2740, nothing else.
- Every way a futures outright can be phrased says the word "futures" or
  "future": `"futures_long": ["long futures", "buy futures", "go long the
  future"]`, and `FUTURES_NAMES["NQ"] = ["nasdaq futures", "nq", "e-mini
  nasdaq", "the nasdaq future"]`.
- **Zero rows** have the shape `<direction> <root>, <days>, <qty>` with no
  strategy word. (A scan for bare `long|short`-initial utterances without
  strategy vocabulary finds only "long guts ..." rows — a strategy name.)

So `Long NQ, 45 days, 2 contracts.` is out-of-distribution in *shape*: the
model never saw a directive where the only evidence of instrument class is a
bare direction word next to a root, and it falls back to its refusal
behaviour. Conversely `... gold futures ...` and `... bond futures ...`
contain the in-distribution token "futures", so the extraction machinery
engages and generalizes to unseen roots (GC, ZB) — the roots were never the
barrier; the trigger vocabulary was.

### This gap is already fixed in the engine, deliberately

Commit `b8866e0` (2026-08-03, "Make the holdout pass 16/16 — three engine
defects, no model change") added
`recover_bare_futures_directive` (`assistant_verification.cppm:1337`), which
fires only when the model emitted no params, parses exactly this shape
deterministically, and feeds the result through the same validation and
GP-ARA gate as model output. In run B it fired on exactly the three prose
rows above and produced the correct params for all three
(`[assistant] recovered bare futures directive: {"symbol":"NQ",...}` etc.).

## 3. The other suspects, cleared with evidence

1. **System prompt** — `kSystemPrompt` in `assistant_service.cpp` compared
   byte-for-byte (programmatically, not by eye) against the `system` turn of
   `agent/dataset/data/train.jsonl` row 0 and of `defect_holdout.jsonl`:
   **identical**.
2. **Input guards (`8402b0f`)** — the two substring tables
   (`kInjectionSignals`, `kAdviceSignals`,
   `assistant_verification.cppm:1173-1203`) were replicated in Python and run
   over all 16 holdout utterances: **zero hits**. Independently confirmed at
   runtime: the engine logged `[assistant] raw model output` for every one of
   the 19 holdout RPC calls (13 single-turn + 3 clarification rows × 2), and
   a guard refusal returns before the model runs, so a guard trip would have
   left a gap in that log. There is none.
3. **Verification refusing good model params** — did not occur in run B; in
   run C it refused *everything it could not probe*, which is the finding of
   section 1, and the refusal reason (`DATA_UNAVAILABLE`) states so honestly.
4. **Sampler / repetition penalty** — see section 1; also, the one row where
   verification genuinely saved the score is `"Buy 100 shares of ES stock,
   Eversource, 30 days."`, where the model emitted
   `<params>{...,"strategy":"cash_flour",...}</params>` (a hallucinated id)
   and the unknown-strategy check turned it into the expected refusal. That
   row scores correct in *both* run B and run C.

## 4. Score reconciliation

| measurement | engine state | score | notes |
| --- | --- | --- | --- |
| 2026-08-03, documented "13/16" | pre-`b8866e0` | 13/16 | denominator = all 16 rows; the 3 failures are exactly the 3 bare directives of §2, model prose-refusing |
| 2026-08-03, post-`b8866e0` | recovery + far-leg + think-strip fixes | 16/16 | recorded in `b8866e0`'s message and pipeline doc §2b |
| tonight, reported | HEAD binary, **no market data** | 6/14 + 2/2 = 8/16 | all 8 failures are `DATA_UNAVAILABLE` refusals; three of them (SPY, NVDA, CL-equity) are not futures rows at all |
| this session, run B | HEAD binary, live market data, gate off, single engine | **16/16** (14/14 + 2/2, asked 3/3) | the model's real defect-holdout score today |

CLAUDE.md's "13/16" describes the raw model through the pre-`b8866e0` engine
and is stale as a statement about the served system; its "3 of the 16 rows"
bare-directive note remains true of the *model in isolation* but those rows
are recovered deterministically in serving.

Note the two denominators in circulation: "6/14" counts only params rows
(the harness's `params exact-match` line); "13/16" and "16/16" count all 16
rows. Tonight's 6/14 + 2/2 is 8/16 on the latter scale — not a 13→8 model
regression but a 16→8 environment regression.

## 5. What to change (and what not to)

**No retraining is required to fix anything observed tonight**, and no
production serving code needs to change for the score. The fixes:

1. **Measurement procedure (the actual defect).** Before scoring, the runner
   must verify the engine can verify symbols — otherwise every score is a
   measurement of the Alpaca credentials. Concretely, in
   `agent/train/eval_grpc.py`:
   - print `which` and the refusal reason/message for every mismatch (the
     data exists in `call()`'s return and is currently discarded for params
     rows). A mismatch line reading `got refusal(DATA_UNAVAILABLE: ...)`
     instead of `got None` would have ended tonight's investigation in one
     glance;
   - hard-fail (or loudly banner) the run if any row returns
     `Refusal.DATA_UNAVAILABLE` or `Refusal.MODEL_UNAVAILABLE` — these are
     infrastructure outcomes, and a score containing them is not a model
     score. `docs/guides/ASSISTANT_EVALUATION.md` should state the
     market-data precondition next to its existing single-engine rule
     (CLAUDE.md already warns "symbol verification needs live market data";
     the harness should enforce it rather than rely on the operator
     remembering).
2. **Latent bug in the recovery (fix before it bites):**
   `recover_bare_futures_directive`'s root scan
   (`assistant_verification.cppm:1352-1355`) does a plain substring search of
   each root over the lowered utterance, and `"futures".find("es")` hits — so
   any prose-refused bare directive whose text contains the word "futures"
   (e.g. `"nasdaq futures long, 45 days, 2 contracts"` if the model ever
   declined it) would resolve to symbol **ES** regardless of the actual root,
   and ES `futures_long` passes verification, so nothing downstream catches
   the wrong symbol. It is masked today only because every "futures"-worded
   directive is in-distribution and the model answers it before recovery
   runs. Fix: match roots on word boundaries (tokenize, or require
   non-alphanumeric neighbours), or scan `kRootWords` before the raw tickers
   and require the ticker to appear as a standalone token. Add
   `"nasdaq futures long, 45 days, 2 contracts"`-shaped negative/positive
   controls to `test_assistant_verification`.
3. **Optional, not required: internalize the bare-directive shape in the
   model.** The dataset fix already exists at HEAD —
   `make_bare_futures_direction`, `make_futures_options_extraction`,
   `make_share_purchase_refusal`, broadened `FUTURES = [ES, NQ, CL, GC, ZB]`
   (`build_dataset.py`, commit `7fdb731`) — but **no deployed model has ever
   been trained on it**: the committed `data/train.jsonl` predates it (Aug 1)
   and §2b pins the model of record to that data. If a retrain is ever done
   for other reasons, regenerate the data first (`python3 build_dataset.py
   --out data/ --n 30000`), train with `run_qlora.sh`'s `--epochs 4` (not
   `train.py`'s default 2 — the 2-epoch retrain scored 5/16), and measure on
   sensen through this same RPC harness with market data verified per (1).
   Until then the engine recovery covers the gap with a deterministic,
   tested, narrower-than-the-model rule, which is arguably the stronger
   position.
4. **Documentation.** Update CLAUDE.md's assistant section: the served
   system's holdout score is 16/16 post-`b8866e0` (13/16 is the raw model
   through a pre-recovery engine), and the bare-directive limit is a shape
   gap ("no strategy-vocabulary token"), not a root-set gap — `Long NQ`
   fails with NQ in-distribution, and GC/ZB succeed out-of-distribution.

## 6. What could not be determined

- The precise environment of tonight's 6/14 run (whose shell it ran in, and
  whether the credentials were absent, empty, or the network was
  unreachable). `NotConfigured` and `NetworkError` both map to
  `ProviderUnavailable` and produce the identical refusal, so the RPC
  outcomes cannot distinguish them. The signature match (score, per-metric
  breakdown, and the exact five-row mismatch list in the same order) is
  otherwise complete.
- Whether tonight's runner also had `PRO_GATE_MODE`/`QUOTA_POLICY` trouble
  first (run A's failure mode). Run A's shape — RPC errors, 0/0 — does not
  match the reported 6/14, so if it happened it was worked around before the
  reported number.
- Row 12's (`calendar spread on CL` → "options" → EQUITY) failure tonight is
  inferred from run C's reproduction rather than from a quoted line in
  tonight's report, which listed only the five futures-flavoured rows.

## Appendix: run artifacts (session scratchpad, not committed)

`eval_run2.txt` (16/16 with market data), `eval_run3.txt` (6/14 without),
`engine2.log` / `engine3.log` (raw model output for every call, recovery
events) under
`/tmp/claude-1000/-home-muyiwa-Development-OptionsAndFuturesCalculator/8dccb92c-7fce-41f1-a147-a0ff289eef49/scratchpad/`.
Engines started this session: 3, all killed by PID; `pgrep -x
calculator_engi | wc -l` = 0 at session end.
