# Latest sensen, and the mortgage assistant retrained

**Date:** 2026-09-14
**Author:** Olumuyiwa Oluwasanmi

## sensen d12cb0e2 -> 3bd65c84 (925 commits)

Gated, not assumed:

| gate | result |
| --- | --- |
| `ninja calculator_engine` | links, 28,817,608 B |
| `ninja build_tests` | 263/263 targets |
| `ctest --test-dir backend/build` | **103/103 passed, 0 failed** (150 s) |
| `smoke_client localhost:50051 SPY finance` | every independent identity holds |

**It failed first, in the documented way.** `sensen_slim` died on
`module 'sensen.lowbit_flash_attention' not found` — the third instance of the
same trap this file already records for `kv_lowbit` and for
`dispatch_contract`/`paged_kv_dispatch`. The module list is hand-maintained and
nothing derives it, so every new module a listed module imports has to be added
alongside it. `scripts/sensen_module_closure.py --check` names the missing
entry directly; run it after every bump rather than waiting for the compiler.

That checker also reports `logger.cppm` MISSING and `numa_bind.cpp` EXTRA, and
**both are false**: only `src/utils/logger.cppm` exists and it is listed twice,
and `numa_bind.cpp` is deliberately extra with the reason stated in
`backend/CMakeLists.txt`. The build is the ground truth here; the checker's
basename matching is not exact.

## The retrain: mortgage-v13

Trained on `oluwasanmi-tradingbot-server` (RTX 5090) after the resident
`sensen_serve` was stopped — it held 30,932 of 32,607 MiB and a QLoRA run
cannot start beside it.

- corpus regenerated, `--n 12000 --seed 0` (the invocation the generator's own
  docstring gives, NOT argparse's 3407 default — that discrepancy silently
  reseeded a corpus once and contaminated a holdout), 11,400/600,
  generator sha `ecfd3d9a…`, now carrying `make_down_payment_extraction`
- QLoRA r=64 α=64, 4 epochs, 760 steps, **412.8 s**, train_loss 0.2048,
  eval_loss 0.1548, peak GPU 2.80 GB
- converted with sensen's own converter on the remote: **0.886 s**, 310
  tensors, 639,447,136 bytes — byte count identical to the model of record
- `validate_gguf`: OK. sha256 `bc460a5c…`, **round-tripped** after copying,
  because the checksum that counts is taken from where the bytes are served

## Scored through the real RPC, on latest sensen

One engine on `:50051` (asserted — `SO_REUSEPORT` silently splits requests
between engines holding different models), holdout proven disjoint from train
by `--assert-disjoint-from`.

```
raw params exact-match : 465/567 = 82.0%
emitted valid <params> : 566/567 = 99.8%
<params> but bad JSON  : 0/567
non-params correct     : 33/33
errors                 : 0
```

**This is NOT comparable to the model of record's 400/508 = 78.7%.** That figure
was measured on a different holdout from a different corpus revision, and this
file already records that every comparison on this holdout family before
2026-08-28 was contaminated — 304 of 600 rows were train rows for the older
model. Comparing v13 to v12 needs both models scored on one holdout proven
disjoint from BOTH train splits. That has not been done, so no promotion claim
is made here.

## What the reasoning pass found that the score could not

`scripts/emit_assistant_facts.py` turns the holdout, the eval output and
`finance.proto` into Prolog facts; `agent/eval/assistant_check.cpp` asks the
coverage questions through sensen's Horn-clause engine. Five checks, three
failing.

**Four operations scored ZERO, and three of those were the harness.**
`ComputeRate` 0/7, `ComputeIrr` 0/6, `ComputeXirr` 0/6 — every failure is the
`guess` seed (plus XIRR's `rate`), fields `OP_EXCLUDED_FIELDS` says the
operation discards and which `mortgage_assistant_service.cpp` DROPS before
grounding. Scored as production serves them all three are perfect. 20 of the
102 failures are that alone: **485/567 = 85.5% production-equivalent**.

**The fourth is real and is the reason this pass exists.** `ComputeXnpv` is
0/9, and **8 of the 9 are answered `ComputeNpv`** — the wrong operation, with
the dates dropped. NPV over evenly-spaced periods and XNPV over dated flows are
different questions; the engine would answer the wrong one plausibly. At 82%
pooled this is 1.6 points and reads as noise.

Two checks passed and both are worth stating: every one of the 27 operations
has holdout rows, and the model invented no operation outside the proto label
space.

**A wrong comment was written and corrected in the same session.** The first
version of `assistant_check.cpp` explained a goal-parse failure by claiming
`workflow_check`'s period-less goals only work because its binary is stale, and
that rebuilding it would reproduce the failure. Rebuilt: it does not. The real
mechanism is that `workflow_check` appends the terminator at the CALL SITE —
`read_term(c.goal + ".")`. Probed directly rather than reasoned about, and this
file now does the same thing.

## Not done

- **v13 is NOT deployed.** `MORTGAGE_MODEL_URL` is unchanged. Promotion needs
  the paired comparison above and a decision on `ComputeXnpv`.
- The `ComputeXnpv`/`ComputeNpv` confusion is a corpus question, not a serving
  one, and needs a retrain to fix.

## ADDENDUM: the feature was never tested, and the paired comparison exists now

The section above evaluated a generic 600-row holdout and never asked the one
question the retrain existed to answer. That was the gap, and it was raised
rather than found.

**The feature works.** 29 held-out `make_down_payment_extraction` rows, scored
apart from the pool:

| | deployed model | retrained v13 |
| --- | --- | --- |
| loan derived correctly | **0/29** | **17/29** |
| emitted the GROSS price (the defect) | **21/29** | **0/29** |
| named `ComputeXnpv` on XNPV rows | **6/9** | **1/9** |

The deployed figures are measured against PRODUCTION through the live ingress
with the issued partner key, on the same utterances — not recalled.

`assistant_check` now carries the two rules that separate the failure modes,
because they are different defects:

- `feature_absent(I)` — the emitted loan EQUALS the stated price. This is the
  pre-retrain signature, and it is **OK, zero solutions** on v13. The deployed
  model produces it 21 times.
- `feature_arithmetic_slip(I)` — the subtraction was attempted and missed.
  **12 solutions.** Grounding REFUSES these, so they cost reachability and
  never price a loan nobody asked for.

**v13 is still NOT deployed, and now for a measured reason rather than a
cautious one.** `ComputeXnpv` 6/9 -> 1/9 is a real regression, and it is the
half that does NOT fail safe: naming `ComputeNpv` discards the dates and
returns a plausible NPV for a different question, where a down-payment slip
returns an honest refusal. +17 safe-failing rows against -5 silently-wrong ones
is not a trade to make quietly.

It is also the saturation pattern this file already records at rank 16 — one
capability displacing another — appearing now at rank 64. The next iteration
should hold the down-payment gain and recover XNPV, and `ComputeXnpv` vs
`ComputeNpv` is a corpus question: the two are told apart by whether the
utterance carries DATES.

## ADDENDUM 2: the regression is now TESTED, and the corpus that caused it is fixed

"Add it to the testing requirements" was asked, and the first attempt was to
write it in two checklists. That was the wrong answer to a problem those
checklists had already failed at: `ASSISTANT_EVALUATION.md` item 8 has required
a paired comparison against the deployed model the whole time, and it was
skipped on this very retrain.

So the requirement is now executable in three places.

**1. An engine guard, tested both ways.**
`mv::dated_utterance_rejects_operation` refuses `ComputeNpv`/`ComputeIrr` when
the utterance states day offsets. It REFUSES rather than remapping -- Graph A
beside it can rewrite `ComputeFutureValue` because every value it writes is
already in the utterance, and this cannot, because the missing field is the day
grid the model never emitted.

Section 26 of `test_mortgage_verification` (8 checks, 148 -> 156) gates it, and
the specificity half is the one that matters: an evenly-spaced NPV utterance is
still served, `ComputeXnpv` is never refused for carrying dates, and "15 days of
prepaid interest" on `ComputeClosingCosts` is untouched. Mutation-checked with
`CCACHE_DISABLE=1` -- removing the operation scope fails exactly 2 checks and
the restore returns to 0.

**2. A promotion gate that refuses.**
`scripts/assistant_promotion_gate.py` refuses on a missing baseline, on any
per-operation regression beyond one row, and on any operation that goes to
zero. Mutation-checked in three arms; the synthetic baseline reproduces
`ComputeXnpv 6/9 -> 0/9  <-- NEW ZERO` and exit 1, which is the real case.

**3. The corpus, which is where the defect actually came from.**
`make_cashflow_extraction` built its dated grid with
`triangular(300, 430, 365)`. Every interval was about a year, so
"after 394 days; after 725 days; after 1102 days" IS an annual grid to three
significant figures. **The entire distinction between XNPV and NPV is irregular
spacing, and the generator was producing regular spacing and labelling it
XNPV.** Both families opened with a byte-identical clause, and dated rows were
35% of the family (183 NPV against 103 XNPV).

The model was not confused. It was taught two things that look the same and
told they are different.

Fixed: intervals `randint(40, 900)`, a distinct opening clause per family, and
an even split. No label became less derivable -- every day count is still
stated verbatim in the utterance.

**Methodology note for the v13-vs-v14 comparison.** v13's baseline runs on the
pre-Graph-B binary and v14's on the post-Graph-B one. That is safe for the
RAW layer and only the raw layer: `[assistant] raw model output` is written at
`mortgage_assistant_service.cpp:2987` and `validate_and_populate_params` is
called at `:2997`, so no guard can touch the number the gate compares. The
SERVED layer is not comparable across the two and is not used for the verdict.

## ADDENDUM 3: per-generator seeding, and why the v13-vs-v14 result was unreadable

v14 was refused, and the refusal itself was nearly wrong twice.

**First the gate's verdict was invalid.** It reported eight per-operation
regressions. 364 of the 600 holdout rows were byte-identical members of v13's
TRAIN split -- the baseline had memorised 61% of the test. The eval had been run
with `--assert-disjoint-from` against *v14's* train, which is the check that
feels sufficient and is not: the BASELINE was trained on a different corpus
revision and most rows survive a revision unchanged. CLAUDE.md records the same
defect at 304/600. Contamination does not fail loudly; it fails in the older
model's favour, which is the direction that blocks a good candidate. The gate
now takes `--train` once per model and scores only the rows clean for all of
them; on the clean 236 the eight regressions became three and pooled went
-33 to -11.

**Then the experiment turned out to be unattributable anyway.** One shared RNG
drove the weighted selection, every generator's internal draws AND the
train/val shuffle. Measured: adding ONE extra `rng.random()` inside
`make_cashflow_extraction` changed **2032 of 4000 rows**, across every family.
So the XNPV edit also moved ComputeHeloc, ComputeRefinance and
ComputeFutureValueDetailed, and nothing could distinguish "my edit caused this"
from "this is a different sample of the same distribution".

Fixed: selection, each generator, and the split draw from separate streams
seeded by `stream_seed(master, name)` = sha256 of `"{seed}:{name}"`. **NOT**
`random.Random((seed, name))` -- Python salts `hash()` of a str per process, so
the corpus would differ between runs of the same command.

The same perturbation now changes **360 of 4000**, and all 360 belong to the
four operations that generator emits. Rows are also sorted before the shuffle,
so the split is a function of the SET of rows and the seed rather than of the
order generators happened to be selected in.

The effect on measurement is larger than the tidiness suggests. The v15 holdout
overlaps v13's train on **4 rows** and v14's on **5**, against v14's 364 -- so
the next comparison has ~595 usable rows instead of 236, on the same 600-row
holdout.

Gated by the isolation group in `test_corpus_invariants.py` (37 checks now),
including that each generator's row is unchanged after every OTHER generator
has run, and that `stream_seed` is stable ACROSS PROCESSES rather than merely
within one.

**Where the XNPV fix actually stands.** v13 answered 8 of 8 held-out
ComputeXnpv rows as ComputeNpv. v14 answered 6 of 8 that way and 2 correctly --
0/8 to 2/8 production-equivalent. Real movement, not a fix, and not worth
promoting on its own.
