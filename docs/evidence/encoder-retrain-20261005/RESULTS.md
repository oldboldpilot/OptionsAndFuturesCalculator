# Encoder retrain, 2026-10-05 — the four runs behind `9e9c2b5`

`9e9c2b5` ("typos, a percentage down payment, and a PMI the corpus always zeroed") committed the
generator and trainer changes and NO evidence directory, while all eight sibling `encoder-*`
directories here carry one. These are the runs that produced it, recovered from
`agent/train/runs/` before that path was gitignored as scratch output.

The `encoder_fp32.pt` checkpoints (16 MB) are NOT kept: they regenerate from the recorded
seed and args in each `metrics.json`. `tokenizer.json`/`schema.json` are likewise derivable
from the corpus plus the seed. What cannot be regenerated is the MEASUREMENT, so that is
what is here.

| run | epochs | seed | row_acc | op_acc | val rows identical to train | row_acc on disjoint rows |
| --- | --- | --- | --- | --- | --- | --- |
| `mort_2026-10-05_typo` | 6 | 0 | 0.9966666666666667 | 1.0 | 11 of 600 | 0.9966044142614601 |
| `mort_2026-10-05_v2` | 6 | 0 | 0.995 | 1.0 | 11 of 600 | 0.9949066213921901 |
| `mort_2026-10-05_v3` | 7 | 0 | 1.0 | 1.0 | 17 of 600 | 1.0 |
| `mort_2026-10-05_v4` | 7 | 0 | 0.9966666666666667 | 1.0 | 11 of 600 | 0.9966044142614601 |

**The contamination column is why `row_acc` is quotable at all.** This repository records that
304 of 600 rows in an older holdout were train members of the model being compared against,
which SUBSIDISED that model. A handful of identical rows with the figure holding on the
disjoint remainder is a different situation, and it is checked here rather than assumed.

`failures.jsonl` beside each run names the rows that failed; it is empty for the run that
scored 1.0.
