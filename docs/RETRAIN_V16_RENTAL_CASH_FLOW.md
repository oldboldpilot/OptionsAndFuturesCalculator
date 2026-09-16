# Retrain plan — v13, adding ComputeRentalCashFlow

@author Olumuyiwa Oluwasanmi

Status: **ready to run, and it cannot be run on this machine.** Everything below
that does not need a GPU has been done and measured; the training step itself
needs the GPU server.

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

## This machine cannot train, measured rather than recalled

```
host: oluwasanmi-fedora-server
nvidia-smi: absent      /dev/nvidia*: absent
torch 2.10.0+cu128 -> torch.cuda.is_available() == False
```

CPU is not a slow path for a 0.6B QLoRA, it is not a path. The honest note is
"needs the GPU server", never "ungateable".

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
# On the GPU server, from agent/train/
python3 train.py \
  --data ../dataset/data_mortgage \
  --out  /scratch/agents/mortgage-v13 \
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
  --label v13 --json-out v13.json \
  --assert-disjoint-from ../dataset/data_mortgage.pre-rentalcashflow-20260916T000150Z/train.jsonl \
  --compare-to v12.json
```

`--compare-to` refuses to compare two scores taken on different holdouts, which
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
