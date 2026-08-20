# Closing costs: a new RPC, and the four tables an operation has to reach

@author Olumuyiwa Oluwasanmi

**Date:** 2026-08-19
**Trigger:** "can the mortgagefvcalculator AI agent handle the closing costs
information?" The answer was no, twice over, for two unrelated reasons.

---

## 1. The assistant could not, and closing costs were not why

Measured through the live ingress with the partner key, against held-out
`val.jsonl` rows scored on their own gold labels:

| set | reached `FinanceParams` | params exact |
| --- | --- | --- |
| 16 closing-cost rows | 0 | **0/16** |
| 24 mixed-operation rows | 4 | **0/24** |

The documented figure is 27.8%. At that rate, zero successes in forty rows has
probability ~2e-6, so this is a regression rather than sampling noise. The
canonical utterances CLAUDE.md records as parsing on 2026-08-12 now fail, and
ten identical sequential requests returned **two different answers** under
greedy decoding with `repetition_penalty` pinned to 1.0 — either decode
corruption or three replicas in different states.

**The 2026-08-13 SGEE promotion gate could not have caught it.** It asserted 24
concurrent `ParseOperation`s "all HTTP 200" with zero warnings, and a REFUSAL IS
HTTP 200. That gate was structurally incapable of seeing accuracy collapse. Same
lesson as the single-request verification it replaced: ask what a passing
check's shape excluded.

This is unfixed and is NOT what the rest of this log is about.

## 2. The website grew a screen the contract could not express

`mortgagefvcalculator.com/mortgage-closing-costs-calculator` is a standalone,
itemised screen: 15 inputs across lender fees, third-party services, government
fees, and prepaids/escrow. `finance.proto` had closing costs only as SCALAR
FIELDS inside `ComputeRefinance` and `ComputeHomeNpv`, and the assistant's whole
job is to name one of the operations in its label space. There was no operation
to name, so the failure was structural rather than a weak model — and the
grammar correctly refused every invented name as `UNSUPPORTED_OPERATION`.

Two behaviours were measured by driving the live page, because the served HTML
is a client-rendered shell:

- **A credit reduces the TOTAL, not the itemisation.** With a 5,000 credit the
  subtotal held at 15,336 while the headline fell to 10,336. The response
  therefore carries both `itemised_subtotal` and `total_closing_costs`; folding
  the credit into a line would make the itemisation stop summing to its own
  subtotal, which is the one property a reader checks by eye.
- **Prepaid interest is a fixed 15 days**, linear in the rate (1,123 -> 2,247 at
  double), i.e. `loan * rate / 365 * 15`.

## 3. `ComputeClosingCosts`

`sensen::calculate_closing_costs`, exact `BigDecimal` throughout, reproduces the
live site exactly: subtotal 15,335.96, cash to close 60,335.96, 3.41% of price,
and both perturbations.

**The percentages do not share a base, and mixing them yields a plausible figure
rather than an error.** Origination and discount points are shares of the LOAN;
title and transfer tax are shares of the PRICE.

`validate_closing_costs` is split out so the gRPC service can run the input
contract and answer `INVALID_ARGUMENT` — the right code for a bad argument —
while direct sensen callers still get the identical refusal from the engine.
One contract, two callers, no second copy to drift.

Bounds that took a reviewer to get right:

- `down_payment_percent` may be exactly **1.0**: an all-cash purchase, no loan,
  but title/appraisal/recording/transfer/escrow are all still owed. The first
  version refused it, with a comment claiming the loan would go negative — which
  is false at exactly 1.0.
- `seller_lender_credits` may not be negative (a surcharge wearing a credit's
  name: `subtotal - (-5000)`) nor exceed the subtotal (a negative cheque).
- `prepaid_interest_days` is `optional int32`, the only explicit-presence field
  in the file. Absent means the 15-day convention; an explicit 0 means zero
  days, which a closing on the last day of a month genuinely owes. A sentinel
  mapping 0 to 15 made that case unrepresentable.

## 4. An operation must reach FOUR tables, and the fourth gates dispatch

1. `mortgage_verification.cppm` `kLabelSpace` (160 -> 176) and `kOperationIds`
   (26 -> 27).
2. Slot classification. **Eight fields classified `Unclassified`** — `home_price`,
   `appraisal_fee`, `recording_fees` and five more were absent from
   `kMoneyFields` — and an Unclassified slot makes `translate()` return
   Indeterminate, so EVERY closing-cost parse would have been refused. That
   failure looks exactly like a bad model.
3. `kConventionValues` for the costs a closing may genuinely not have, plus
   `prepaid_interest_days` at 15 and 0.
4. **`mortgage_assistant_service.cpp` carries a fourth copy of the label space**
   (`kOperations`, `kFields_*`) and it gates dispatch — without it the service
   refuses the operation AFTER the verifier admits it.

`kMaxNewTokens` was re-measured rather than assumed: the widest generated block
is now **547 characters** against the 473 its comment claimed, because
`ClosingCostsRequest` has sixteen fields and `HomeNpvRequest` fourteen. 384
tokens still clears it, so the value is unchanged and only the false claim was
corrected — it is also what `cost_llm_generate` charges against.

### Two verifier/engine disagreements, both closed

- The verifier's Ratio ceiling is **1.5**; the engine caps shares at **1.0**. A
  verifier LOOSER than the engine it guards is not a safety property: it proves
  a parse admissible and the engine then refuses it, so the caller gets a
  transport error where an honest refusal belongs. `kUnitCappedRatioFields`
  tightens the five share fields; the global 1.5 stays, because an LTV above
  1.0 is real on an underwater loan.
- The verifier refused `tax_escrow_months = 0` on a positivity bound while the
  engine accepts it. Loans with no escrow account are ordinary. Carved out by
  FIELD NAME, mirroring the existing `values` carve-out on Money — and it has to
  live in `bound_violation`, because bounds run in G5 before
  `ground_emitted_values` ever consults a convention.

## 5. The tests were not wired, and wiring them found seven defects

`test_financial.cpp` is in sensen's tree, which this repo builds with
`BUILD_TESTS OFF ... FORCE`. `smoke_client` needs a running engine and is not a
ctest target. So of 99 ctest tests, **zero touched `ComputeClosingCosts`** while
coverage appeared to come from three places.

Section 23 of `test_finance_service_validation.cpp` (in-process gRPC, wired to
ctest) closed that, and **failed 7 checks immediately**: every engine-level
refusal was reaching clients as `FAILED_PRECONDITION` rather than
`INVALID_ARGUMENT`, because `fail()` maps engine errors that way. Now 151
checks, 0 failures, mutation-checked — deleting the negative-credit guard fires
it by name.

One refusal deliberately keeps `FAILED_PRECONDITION` and is asserted on that
exact code: a credit larger than the bill. 100,000 is not malformed — it is
ordinary on a larger closing, and wrong only relative to a subtotal that cannot
be known until the itemisation runs.

## 6. Adversarial review found what the author could not

Two independent reviewers over the same diff.

- **A wrong base was untestable where the reference scenario is zero.** Discount
  points are 0 in the site's defaults, and `0 x price == 0 x loan`, so
  reproducing the reference faithfully reproduced its blind spot. A second call
  at one point now checks the base independently.
- **Only `origination` was recomputed**, so a wrong base on title, transfer tax,
  escrow or prepaid interest would still satisfy the sum identity — that identity
  sees a dropped term, never a wrong base. All lines are now recomputed against
  their own base.
- Both reviewers independently rejected the documented `prepaid_interest_days`
  sentinel. Two readers rejecting the same stated rationale is different evidence
  from one.
- **One claim was checked and rejected:** a reported cent-level rounding error
  from dividing before multiplying. `BigDecimal` is 18-place fixed point, so the
  real error is 1e-18, not 1250.01. The reorder is free and was made anyway, but
  accepting the severity as stated would have written a false rounding-bug story
  into the record.

## 7. The proto parsers, and the merge

Adding the file's first `optional` field silently broke **all three** text-level
proto parsers. The C++ pair strip `repeated ` but not `optional `, so they read
the presence marker AS the type and shifted the name across — dropping the field
from the drift comparison rather than erroring. A gate that quietly stops
comparing is worse than one that fails.

The sensen merge (60 commits) was clean but broke the engine three ways. The
root cause is worth carrying: **llm_pipeline.cppm guarded its qwen38 USE with
`#ifdef SENSEN_HAS_CUDA` and left the IMPORT unguarded.** An import compiles the
module whether or not any path reaches it, so a CPU build died on
`cuda_runtime.h` and missing device types long before it could reach the honest
"requires a CUDA build" error the use site already threw.

`scripts/sensen_module_closure.py --check` named all five missing modules at
once instead of five sequential rebuilds. Its two remaining complaints are
pre-existing false positives: `logger.cppm` is really `utils/logger.cppm`, and
`numa_bind.cpp` is a plain source rather than a module interface.

**A stale binary lied once during this work.** The post-merge smoke test was run
against a pre-merge binary whose rebuild had already failed, and it passed. Every
later run compares binary mtime against every touched source before the result
is believed.

## 8. State

- 99/99 ctest on the merged tree; 151 checks in the service test; 94 in the
  verifier; 139 in the grammar.
- Identity gate green against a verified-fresh engine over real gRPC.
- Dataset regenerated, 27/27 operations. The previous dataset — the deployed
  model's, no longer reproducible since the generator changed — is archived
  beside it.
- Six fully-merged `feat/q38-*` branches deleted on both remotes; the two
  unmerged `preserved/pinned-*` deliberately kept.
- **Not done:** the retrain. It should not be spent until the serving
  nondeterminism in section 1 is fixed, because its score could not be trusted.
