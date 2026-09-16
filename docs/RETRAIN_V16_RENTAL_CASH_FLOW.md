# Retrain plan — v16, adding ComputeRentalCashFlow

@author Olumuyiwa Oluwasanmi

Status: **RUN on 2026-09-15**, on `oluwasanmi-tradingbot-server` (RTX 5090).
Everything below that does not need a GPU was done and measured here first; the
training step itself ran there. See "What actually happened" at the foot.

**IT IS v16, AND THIS DOCUMENT SAID v13 UNTIL THE RUN.** That was not a typo
with no consequence: `~/qlora-out/` on the GPU box already held `mortgage-v13`,
`mortgage-v14` AND `mortgage-v15` from 2026-09-14, and v15 is the GGUF
CLAUDE.md's solver-layer table is measured against. Following this file
literally would have written the new adapter over a trained model whose only
copy is on that box. **A version number chosen on this machine is a guess about
another machine's filesystem; check `~/qlora-out/` before naming a run.**

---

## Why a retrain is needed at all, and why only for this one thing

Adding a FIELD to an existing operation needs no retrain: an excluded field is
dropped by `mortgage_assistant_service.cpp` before the verifier ever sees it, so
whatever the model emits is irrelevant. Adding an OPERATION is different — the
model has to NAME it, and `ComputeRentalCashFlow` is a string these weights have
never produced. Until it is trained, the investor cash flow is unreachable from
natural language while remaining fully reachable from the UI, the JSON
transcoder, MCP and the public API.

That is the whole scope of this retrain. The other work landed this cycle —
`tax_deduction`, `rental_tax`, `mortgage_explanation`, the PMI cancellation
policy — has **no RPC surface**, so there is nothing for a model to name and
nothing for the corpus to teach. A retrain would not make them reachable; a
proto message would.

## Which machine, measured rather than recalled

This machine cannot train:

```
host: oluwasanmi-fedora-server
nvidia-smi: absent      /dev/nvidia*: absent
torch 2.10.0+cu128 -> torch.cuda.is_available() == False
```

CPU is not a slow path for a 0.6B QLoRA, it is not a path. The honest note is
"needs the GPU server", never "ungateable". That server is
**`oluwasanmi-tradingbot-server`** (192.168.1.99):

```
NVIDIA GeForce RTX 5090, 32,607 MiB, driver 595.71.05
~/qlora-venv: torch 2.12.1+cu130 cuda=True, transformers 5.5.0, peft 0.20.0, trl 0.24.0
~/ofc: a working copy of this repo, NOT a git checkout -- rsync the corpus in
~/qlora-out: where every mortgage-vN adapter and GGUF lives
```

`~/ofc` having no `.git` is worth knowing before you plan to `git pull` there.
The corpus is rsync'd, and the bytes are checksummed on both ends afterwards so
that what trains is what was validated here rather than something regenerated
there -- the generator's own `--seed` discrepancy has silently reseeded a corpus
once already.

## Corpus state, validated here

Regenerated with the documented invocation:

```
python3 build_mortgage_dataset.py --out data_mortgage/ --n 12000 --seed 0
```

| property | value |
| --- | --- |
| train / val | 11,400 / 600 |
| train∩val overlap | **0** |
| operations covered | 28 / 28, in both splits |
| `ComputeRentalCashFlow` rows | 415 train, 16 val |
| operations in train but missing from val | none |

The generator asserts coverage itself (`operations covered: 28/28 of the
proto-derived label space`), so a new operation that never reached the corpus
fails the build rather than silently training a model that cannot name it.

## The contamination, quantified — this is the part to not skip

Each corpus revision shuffles and splits independently, so a row that is val in
one revision is TRAIN in another. Measured against the DEPLOYED model's corpus,
preserved as `data_mortgage.pre-rentalcashflow-20260916T000150Z`:

```
new val rows that were in the deployed model's TRAIN split:  10 of 600
new val rows carried over from the old val:                   1
```

Ten is small beside the 304-of-600 that once made a genuinely better model look
like a wash (p = 0.17 on the full set, p = 0.0043 on the clean rows), but it is
not zero and it flatters the INCUMBENT. Any paired comparison must therefore
pass:

```
--assert-disjoint-from data_mortgage.pre-rentalcashflow-20260916T000150Z/train.jsonl
```

which refuses by default rather than warning.

## The training command, with every non-default explained

`train.py`'s argparse defaults are NOT the recipe, and reading them as one has
already cost this project a retrain:

| flag | default | use | why |
| --- | --- | --- | --- |
| `--epochs` | 2.0 | **4** | the 2-epoch model scored 5/16 against the deployed model's 16/16 |
| `--lora-r` | 16 | **64** | rank 16 was the measured CEILING: 297/508 at r16, 386/508 at r64, and `ComputePresentValue` went 0/11 → 11/11 on adapter rank alone |
| `--lora-alpha` | 16 | **64** | matches rank, as v12 |
| `--data` | `../dataset/data` | `../dataset/data_mortgage` | the default points at the STRATEGY corpus |

```bash
# On oluwasanmi-tradingbot-server, from ~/ofc/agent/train/, using ~/qlora-venv/bin/python
python3 train.py \
  --data ../dataset/data_mortgage \
  --out  ~/qlora-out/mortgage-v16 \
  --epochs 4 \
  --lora-r 64 --lora-alpha 64 \
  --seed 3407
```

Do NOT pass `--extend-vocab`. It calls `requires_grad_(True)` on the whole
embedding, which made 93.9% of parameters trainable, left the adapters at 6.1%,
and let global gradient clipping starve every adapter update. A gradient mask
does not fix it — the mask held, all 151,669 pre-existing rows moved 0.000e+00,
and the regression happened anyway. The model spells `ComputeRentalCashFlow` as
an ordinary BPE sequence.

## Evaluation, on sensen and not on llama.cpp

`ASSISTANT_BACKEND` defaults to `sensen` and production runs it, so a
`llama-cli` score describes an engine that never handles a request. That mistake
has cost one unnecessary retrain here already.

```bash
python3 eval_grpc_mortgage.py \
  --jsonl ../dataset/data_mortgage/val.jsonl \
  --label v16 --json-out v16.json \
  --assert-disjoint-from ../dataset/data_mortgage.pre-rentalcashflow-20260916T000150Z/train.jsonl \
  --compare-to v15.json
```

`--compare-to` is against **v15**, the model of record, not v12. `--compare-to` refuses to compare two scores taken on different holdouts, which
is what `--json-out`'s provenance block (path, sha256, bytes, rows scored) is
for. Before trusting any number, assert exactly one engine holds `:50051` —
`SO_REUSEPORT` lets several stale engines each holding a DIFFERENT model listen
at once, and the kernel splits requests between them.

## What "better" means here, stated before the run

- `ComputeRentalCashFlow` served from 0 → anything is the point of the exercise.
- The other 27 operations must not regress. Paired McNemar on the clean rows,
  not a difference of totals: the v6 decision turned on 40 lost / 28 gained being
  p = 0.182, which is not a regression however it reads.
- `raw_exact` is quoted alongside `served_exact` always, because it is what
  proves the MODEL moved rather than the serving layer around it.

## Open, and deliberately not in this retrain

`pmi_drop_off_ltv` is already grounded and speakable — a user who says "my
lender drops PMI at 78%" has that admitted today, verified by test. What is NOT
speakable is the PMI termination BASIS, because it is an enum and the label
space has no enum synthesis. That is a proto and exclusion decision, not a
training one.


---

# What actually happened — 2026-09-15

**v16 IS A REGRESSION AND MUST NOT SHIP. v15 REMAINS THE MODEL OF RECORD.**

The run itself was clean: 788 steps in 427.8 s on the RTX 5090, train_loss
0.2037, eval_loss 0.1454, `export_errors: []`, peak GPU 2.79 GB. Converted with
sensen's own converter in 0.925 s, 310 tensors, 639,447,136 bytes — byte-count
identical to v13/v14/v15, which is the expected invariant for an unchanged
architecture with no vocab extension. `validate_gguf`: OK. sha256
`476b8cbc3c332e8b454adc7ff98b2a12e8e2a23741a47dcb630c77889250a148`,
round-tripped after copying to the host that serves it.

None of that is evidence the model is better, and it is not.

## Both models, one holdout

Scored through the real `ParseOperation` RPC on sensen, one engine asserted on
`:50051`, on the **identical** 563-gold-row holdout — the only comparison this
document ever said was admissible:

| | v15 (record) | **v16** | delta |
| --- | --- | --- | --- |
| raw params exact-match | **422/563 = 75.0%** | 396/563 = 70.3% | **−26** |
| served params matching gold | **412/563** | 372/563 | **−40** |
| emitted valid `<params>` | 551/563 | 560/563 | +9 |
| answered-when-stated | **68/68 = 100%** | 24/68 = 35.3% | **−44** |
| asked-when-ambiguous | 21/81 = 25.9% | 47/81 = 58.0% | +26 |
| `ComputeRentalCashFlow` | 0/16 (cannot name it) | **0/16** | **0** |

**The retrain failed at the one thing it was for and damaged what already
worked.** On all sixteen rental rows v16 emits `ComputeRentalRoi` — the nearest
operation it already knew — exactly the shape `ComputeXnpv`→`ComputeNpv` takes
in the 2026-09-14 reasoning pass.

## The mechanism is visible, and it is not the pooled number

`answered-when-stated` collapsed **68/68 → 24/68** while `asked-when-ambiguous`
rose 21→47. v16 asks a clarifying question on rows where the value was already
stated. That single category accounts for 44 rows — more than the entire −26
raw delta — so this is a behavioural shift, not sampling noise, and reporting
"70.3% vs 75.0%" alone would hide it.

A paired McNemar was NOT run: `--json-out` writes aggregates and no per-row
verdicts, so the pairing is not recoverable from the artefacts. The 68→24
collapse in one pre-defined category is decisive without it, but the honest
statement is that the significance test this document asked for could not be
computed, and `eval_grpc_mortgage.py` should emit per-row outcomes before the
next comparison.

## Why it is NOT a measurement artefact

Each checked before the model was blamed, because this project has already paid
for three retrains chasing harness bugs:

- **The system prompts are byte-identical** between the training corpus and
  `mortgage_assistant_service.cpp`. (They need not have been — a model
  fine-tuned on 11,400 rows is not so brittle that a reworded clause reverts it
  to stock Qwen3 — but a *difference* would have been a live hypothesis, so it
  was worth one command to exclude.)
- **Exactly one engine held `:50051`** on both runs. `SO_REUSEPORT` lets stale
  engines holding different models split requests, which reads exactly like
  model damage.
- **The corpus genuinely carries the rows**: 415 train / 16 val
  `ComputeRentalCashFlow`, checksummed byte-identical on both machines after
  the rsync.
- **Both arms ran on the same holdout, same engine build, same day.**

## The other blocker, which a better model would still hit

`ComputeRentalCashFlow` appears **25 times in `mortgage_verification.cppm` and
ZERO times in `mortgage_assistant_service.cpp`** — the fourth of the five
tables, the one that refuses an operation AFTER the verifier admits it. So even
a model that named the operation perfectly would be refused at dispatch. That
is the four-tables trap this repository documents, hit while adding the very
operation the document is about.

## What to try next, in order

1. **Fix the fourth table** — it blocks the feature regardless of any model.
2. **Emit per-row verdicts from the eval** so the next comparison can be paired.
3. **Re-examine the CORPUS_MIX rebalance.** Weight was taken from amortization
   (0.152 → 0.116) to fund the new row at 0.036. The collapse is in
   modification/clarification behaviour, not in amortization accuracy, so the
   suspect is what that reweighting did to the multi-turn row proportions —
   measure the clarification/modification mix in both corpus revisions before
   changing anything.
4. **Consider that 415 rows of a 22-field operation may be too few to name it
   and too many to be free.** The operation was not learned AND the budget was
   spent.


---

# v17 — 2026-09-15. It worked, and the paired test says so

**v17 BEATS v15 ON BOTH MEASURES, AND ComputeRentalCashFlow WENT 0/32 -> 29/32.**
This is the first comparison in this project run as a proper paired test rather
than as a difference of totals.

## What changed, and why v16 could not have worked

After adding the exclusions for the new RentVsBuy fields the corpus was
regenerated and compared:

```
train.jsonl   current 7d0bc596...   regenerated 7d0bc596...   SAME
```

Byte-identical. v16 had already been trained on exactly that corpus, so
retraining with the same seed and recipe would have reproduced v16's regression
and learned nothing. **A retrain is only worth the GPU time if the corpus
changed where the defect is.**

One change was made, from the measurement rather than from taste:
`ComputeRentalCashFlow` carries **22 fields** and its confusable sibling
`ComputeRentalRoi` carries **6**, and both sat at weight 0.036. Equal weight is
not equal exposure. v16 scored 0/16 on it -- every row answered
`ComputeRentalRoi`, and the refusals named RentalRoi's OWN fields
(`periodic_mortgage_payment`, `periods_per_year`), which is what proves the
model chose the wrong OPERATION rather than fumbling a field.

Weight raised 0.036 -> 0.060, taken from amortization so the total is unchanged
and no other row type is diluted as a side effect. Corpus: 705 train / 32 val
rental rows, train∩val overlap 0, 28 operations, 11/600 contaminated against
v15's train split.

## The comparison

Both models scored through the real RPC on sensen, one engine asserted on
`:50051`, on the IDENTICAL 561-gold-row holdout. v15 was RE-SCORED rather than
compared against its earlier number, because the corpus revision reshuffled the
split and the old figure describes a different holdout.

| | v15 | **v17** |
| --- | --- | --- |
| raw params exact | 389/561 = 69.3% | **423/561 = 75.4%** |
| served matching gold | 398/561 | **422/561** |
| served as params | 473/600 = 78.8% | **498/600 = 83.0%** |
| `ComputeRentalCashFlow` | **0/32** | **29/32 raw, 25/32 served** |

Paired exact-binomial McNemar over per-row verdicts:

```
raw_exact     gained 64  lost 30  net +34  p = 0.0006  IMPROVED
served_exact  gained 50  lost 26  net +24  p = 0.0079  IMPROVED
```

Per operation:

```
ComputeRentalCashFlow       +29   -0     ComputeHeloc                 +1  -10
ComputeAmortization         +11   -2     ComputeDetailedAmortization  +0   -8
ComputeRefinance             +6   -0     ComputePaybackPeriod         +0   -3
ComputeRentVsBuy             +5   -0     ComputeInterestPayment       +0   -1
```

## The regression, stated rather than buried

**ComputeHeloc lost 10 of 31 rows -- a 32% regression on that operation**, and
ComputeDetailedAmortization lost 8 of 24. The net is strongly positive and
significant, and these two are the price. Anyone reading only the headline
would not know a third of HELOC parses moved the wrong way.

`asked-when-ambiguous` also collapsed 32/89 -> 1/89: v17 almost never asks a
clarifying question. `answered-when-stated` is 66/66 for both, and non-params
rows are 39/39 for both, so the prose-gold rows are unaffected -- but v17 is
markedly less willing to ask, which is the opposite of v16's failure and worth
watching rather than celebrating.

## NOT PROMOTED, and why

Promotion needs the GGUF on the private artifact host and
`MORTGAGE_MODEL_URL` / `MORTGAGE_MODEL_SHA256` changed on the Railway service.
Both require handling credentials this repository deliberately does not carry,
and the owner's standing instruction is to stay out of them. The model is
built, validated and proven; the swap is the owner's to make.

```
artifact : ~/qlora-out/mortgage-v17-Q8_0.gguf   (GPU server)
local    : backend/models/mortgagefv-assistant-v17-q8_0.gguf
sha256   : 0ce84e1c317e7248ce582c01783968b8c633e68d08b73e3094b0f475faedbb04
           (round-tripped after copying -- the checksum that counts is taken
            where the bytes are served)
recipe   : QLoRA r=64 alpha=64, 4 epochs, 804 steps, 440 s, seq_len 512
           derived from p99=470, extend_vocab false, export_errors []
           train_loss 0.201, eval_loss 0.140
```

`docs/STRATEGY_ASSISTANT_PIPELINE.md` section 4 has the swap procedure.
