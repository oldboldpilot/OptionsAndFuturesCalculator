# Session log — 2026-08-27 — the rent-vs-buy dispatch, and what v8 actually bought

@author Olumuyiwa Oluwasanmi

## The headline

`ComputeRentVsBuy` refused **100% of assistant traffic** — both label shapes,
every request a model can emit — while `ctest` was 100/100. Found by sending
the bytes a model actually produces to a live engine, not by reading code.

```
assistant v8 AMORTISING -> INVALID_ARGUMENT: ...cannot be combined...
assistant v8 LEGACY     -> INVALID_ARGUMENT: ...cannot be combined...
```

Fixed, and after the fix:

```
assistant v8 AMORTISING -> OK / AMORTISING  adv=70,977.18
assistant v8 LEGACY     -> OK / LEGACY      adv= 9,132.29
deployed  v2 (omits 7)  -> OK / LEGACY      adv= 9,132.29   <- identical
```

## The mechanism, and why four components disagreed

`finance.proto` puts per-operation restrictions in COMMENTS
(`// XNPV only; ignored by XIRR`, `// omit for the engine's own starting
guess`). Four components each parse the message; none reads comments:

| component | how it says "not this shape" |
| --- | --- |
| `build_mortgage_dataset.py` · `params_block` | emit `"0.00"` — asserts the complete key set |
| `mortgage_verification.cppm` · G2b | emit, or `MissingField` |
| `mortgage_verification.cppm` · G3 | exempts those zeros, *enabling* the convention |
| `finance_service.cpp` · dispatch | **omit** — keys on `!field.empty()` |

Three say emit a zero; the fourth says omit. `"0.00"` is never `.empty()`, so
both shape flags went true and the conflict guard fired.

The verifier's own comment recorded the design intent and it was correct — for
v2, which omits the seven fields it was never taught. Corpus B teaches all
sixteen. **The dispatch was built against a model that no longer exists.**

## The state space, measured

All 15 combinations sent to the live engine before the fix. Five cells wrong,
including both the assistant produces, a `negative` composite answered 200 OK,
and `absent x all-zero` inventing a 0% loan. After the fix, 20/20 correct
(a `malformed` column was added).

The rule: **substantive** (present AND non-zero) on BOTH sides, symmetrically.
The earlier `is_positive()` attempt failed because it tested one side; the same
inputs that broke it now reach the "neither shape" refusal.

## Two mutation checks, each reproducing a different symptom

- restoring `.empty()` → 4 failures incl. the assistant's amortising label;
- magnitude on the legacy side only → 4 different failures incl. the
  backward-compatibility equality.

## v8 vs v7, same corpus, same engine, 600 rows each

| | v7 | v8 |
| --- | --- | --- |
| raw params exact-match | 319/558 = 57.2% | **335/558 = 60.0%** |
| per-op total | 361 | **377** |

| operation | v7 | v8 | |
| --- | --- | --- | --- |
| ComputeDetailedAmortization | 7/44 | **41/44** | **+34** |
| ComputeAmortizationBatch | 4/12 | 11/12 | +7 |
| ComputeCumulative | 3/8 | 7/8 | +4 |
| **ComputePaybackPeriod** | **20/20** | **5/20** | **−15** |
| **ComputeRefinance** | 18/26 | 6/26 | **−12** |

Paired McNemar over the holdout: 52 lost, 68 gained, net +16, **p = 0.17**.
The aggregate does not survive significance; the per-operation moves plainly
do. **v8 is NOT deployed and should not be** until the PaybackPeriod and
Refinance regressions are explained.

The corpus is byte-identical to the GPU server's training copy — both files
and `meta.json` — so the holdout is provably unseen.

## Ungroundable slots still open (corpus, not model)

| field | proto says | corpus does | val rows |
| --- | --- | --- | --- |
| `ComputeRate.guess` | omit for the engine's seed | always `0.005000` | 11 (sole diff) |
| `ComputeXirr.rate` | ignored by XIRR | writes the computed answer | 9 |
| `ComputeRefinance.new_pmi_monthly` | — | always `0.00` | 9 (sole diff) |
| `ComputeXnpv.guess` | XIRR only | always `0.1` | 5 |

`ComputeXirr.rate` is the sharpest: it is the answer XIRR computes, in a field
the engine ignores, on an utterance that never states it. Nine rows no model
could learn.

`kDeclaredFields` is GENERATED with a drift test, so the exclusion needs its
own table rather than hand-edited rows — and removing a row outright would
break v2, which emits `guess`. Not done; not a ship blocker.

## What shipped

- the dispatch fix + 21 new checks (172 total in that binary, 0 failures)
- the sensen home-NPV deflator sibling fix
- the corpus 70/30 → 50/50 rebalance (its +34 is real; its regressions are recorded)
- sensen merged to master across 21 incoming commits, ctest 100/100

Serving model unchanged: **v2**.
