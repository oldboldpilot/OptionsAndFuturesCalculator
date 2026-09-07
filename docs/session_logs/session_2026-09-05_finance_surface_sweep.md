# 2026-09-03 → 2026-09-05 — the finance surface swept end to end: two wrong answers served with 200 OK, three dead operations, and a probe that lied

@author Olumuyiwa Oluwasanmi

## What this was

`ComputeXnpv` was returning a number that was not an NPV. Chasing that one
defect turned into a sweep of all 27 mortgage-assistant operations through the
live chain — utterance → `ParseOperation` → the `sensen.finance.Finance` RPC the
model itself names — which took the surface from **24/27 to 27/27** and found
two more operations serving *wrong answers* rather than errors.

## Measured outcomes

| operation | before | after |
| --- | --- | --- |
| `ComputeXnpv` (day grid) | `999.9954` — the undiscounted sum, 200 OK | `60374.999566489423`, matches the closed form |
| `ComputePeriods` (corpus signs) | `-119.702968202252976128`, 200 OK | refused; signed → `359.955447133832478720` |
| `ComputeRate` (corpus signs) | `Newton-Raphson failed to converge` | refused naming the convention; signed → `0.004683340486983064` |
| `ComputeXirr` | 100% refused on `"guess" = 0.25` | `0.2086` / `0.0900` / `0.1035` |
| `ComputeAmortizationBatch` | 100% refused on `extra_payments = 0` | computes |
| `ComputeDepreciation` | refused on `"factor" = 3` | `5017.948717948718` = `(199400-3700)/39` |
| MACRS 15/20/27.5/39-year | `{"value":0}` with 200 OK | 15y `5000`, 20y `7219`; real property refused |

Gates: **ctest 103/103**, 200 finance-service checks, 148 verification checks,
the full `smoke_client … finance` identity suite, every new rule
mutation-checked with `CCACHE_DISABLE=1`.

## What was wrong, and how each was found

**XNPV/XIRR dates — days on the wire, seconds in the engine.** `financial.cppm`
divides by 31,536,000; `finance.proto` documents day offsets. Nothing bridged
them, so every `year_frac` collapsed to ~0 and a plain sum came back as a
"present value".

*The first fix was wrong and is worth keeping next to the second.* It read the
corpus as being in the wrong unit and shipped a guard in `sensen` refusing spans
under one day **of seconds** — correct for the library, and it made the
documented wire contract unusable. Counting the artifacts reversed the answer:
**three of four speak days** (proto, `mortgage_verification.cppm`'s
`SlotKind::DayOffsets` bounded at 36,525, and a corpus labelling "after 372
days" as `372.0`); the engine is the outlier and is a general-purpose library
whose own callers hold real timestamps. So the wire stayed in days and
`finance_service.cpp::dates_to_seconds` bridges once. Moving the wire to seconds
would have cost a retrain to teach the model to emit `32,140,800` for a stated
372 — a number the user never wrote.

**A 224-row comment said those corpus rows were ungroundable, and it was stale.**
It described a generator that said "after 14 months"; commit `38730dd` had
rewritten it to say days long before. That comment was the thing arguing for the
expensive fix. *A stale comment recording why something is impossible is worse
than no comment: it is the one a reader trusts instead of measuring.*

**A solver seed is not a claim about the user.** `ComputeXirr` was 100%
unreachable because the corpus teaches `guess: 0.1`, the deployed model invents
`0.25`, and `kConventionValues` exempts a field at one exact value. That rule is
right for every field it governs and does not reach a Newton starting point —
no utterance can state one, so every value there is invented by construction.
`guess` is now exempt as a field, with G5 bounds still applying.

*Correction shipped the same day:* the commit message claimed "exclusion alone
would have fixed nothing". True of the verifier, false of the system —
`mortgage_assistant_service.cpp` **drops** an excluded field before building
`verifiable.fields`, so it never reaches grounding. Reading one layer's rule and
not the layer that calls it.

**The TVM sign convention.** `ComputeRate`/`ComputePeriods` solve
`PV*(1+r)^n + PMT*annuity + FV = 0`, which with `FV = 0` has a root only when
`PV` and `PMT` oppose. The corpus emits both positive. `ComputeRate` refused
uselessly; `ComputePeriods` returned **minus ten years for a thirty-year
mortgage** with a 200 OK.

The seed was the first suspect and was innocent — it fails identically at 0.005
(the gold), 0.001, 0.01 and 0.1, because no root exists. *Check whether the
answer exists before blaming the thing that looks for it.* The engine still
refuses, deliberately: it is a public API, and `nest-egg-loan` was checked and
**already sends `payment: -payment.value` at every call site**. Engine right,
client right, corpus wrong — which settled where the fix belongs.

**A probe reported a field inert and it was not.** Building the per-method
inert-field table for `DepreciationRequest`, a one-field-at-a-time probe called
`salvage` inert for `DECLINING_BALANCE`. `sensen::ddb` reads it twice — the
`cost <= salvage` guard and the per-period floor — and the probe had only looked
at an early period where the book value sits far above it. The table is derived
from the **function signatures** instead, and the mutation arm asserts exactly
that row.

**MACRS returned 0.0 for two different questions.** "No charge this year" and
"no table for that class" were indistinguishable at the call site, so 15-, 20-,
27.5- and 39-year classes all came back `{"value":0}` with a 200 OK while
3/5/7/10 were correct — invisible to any spot check using the common classes.
15 and 20 are now in the table, every table is asserted to sum to the whole
cost, and the two real-property classes are **refused rather than approximated**
because the mid-month convention needs a month this message cannot carry.

## Methodology notes worth keeping

- **Sampling is not measuring.** Three probes found one dead operation; sweeping
  all 27 found two more, including a wrong answer served 200 OK.
- **Check the harness before the system.** The first sweep reported 22/27 with
  two spurious clarifications — 129 of 600 holdout rows are multi-turn and the
  extractor had taken the *last* user turn, dropping the one stating the
  numbers. `ParseRequest` carries `prior_clarification`/`prior_question`.
- **ccache does not hash module BMIs.** A mutation check that edits a `.cppm`
  passes with the guard deleted, because consumers get cache hits carrying the
  old inlined body. Run the deletion arm with `CCACHE_DISABLE=1` and confirm it
  fails.
- **`ninja` said "no work to do" while the binaries predated the change.** After
  the sensen rebase the test binaries and the engine were removed and relinked
  rather than trusted; `calculator_engine` grew 28,497,656 → 28,567,376 bytes,
  which is the evidence the new library is linked in.
- **Three sweep-the-class misses in one week** — `guess` on two operations, the
  two batch plurals, and the missing day→second conversion. Each was a rule
  applied correctly to one member of a family and never carried to the rest.

## sensen

Both finance fixes are merged into **sensen master** on GitHub and the Gitea
mirror (`d12cb0e2`), and the submodule is pinned at master's tip rather than a
branch — a branch carrying live code is indistinguishable at a glance from an
abandoned experiment. The pin moved from 553 commits behind to master + 0; the
jump needed one change here, because `qwen38.cppm` and three siblings include
`sensen_mmvq_bridge.h` unconditionally and `sensen_slim` builds them from its
own hand-written module list.

The MACRS fix was **deployed and uncommitted** when this was audited —
`railway_deploy.sh` archives the working tree, so it shipped and was verified
live while existing in no clone.

## Deployments

Backend `456258f6` (both replicas cut over, 4× `model is LOADED`). Verified
against production: assistant sweep **27/27**, all sites and `/healthz` 200.
