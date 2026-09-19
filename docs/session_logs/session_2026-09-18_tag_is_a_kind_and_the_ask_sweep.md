# A tag is a kind, not a field — and the gate for the direction nothing could see

@author Olumuyiwa Oluwasanmi

Date: 2026-09-18

## What was asked

Verify whether the `asked_ok 46/86` clarification gap was real; fix it if it
was; then add regression coverage so the defect class is caught if it returns.

## The gap was real, and it reproduced before anything was touched

`asked_ok` counts the five-turn holdout rows whose FIRST gold assistant turn is
a question, and how many the service answers with a `clarification` outcome on
the first `ParseOperation`. Measured on the CURRENT holdout —
`agent/dataset/data_mortgage/val.jsonl`, sha256 `1aa3ce94c344217e12f7…`, 600
rows, **86 clarification and 68 modification** five-turn rows — against the
deployed v20 Q8_0 through the real RPC, one engine asserted on `:50051`:
**46 of 86**.

**The denominator itself was a defect.** `eval_grpc_mortgage.py`'s docstring
said 81 and 61. It was stale, and being stale is what made `46/86` reconcile
against nothing in the tree: the string appears in no file, so the only way to
confirm it was to re-measure. The docstring now carries 86/68 and the sha, and
says plainly that these counts have already gone stale once.

The 40 rows that did not ask were not scattered. They collapsed to three
fields, which is what pointed at the mechanism rather than at the model.

## The defect: a tag is a KIND, not a FIELD

`utterance_states_nothing_for` asked "does the utterance contain any literal of
a compatible TAG?". `LiteralTag::Percent` says *this number is a percentage*;
it does not say WHICH percentage. So on

```
"My home is worth $1,047,400, I owe $576,500. I want to draw $46,900 from a
 HELOC at 9.6% over 20 years."
-> "max_ltv_rate" = 0.80 does not correspond to anything in the request
   (the nearest figure you gave is 9.6)
```

the stated INTEREST RATE made the utterance look like the user speaking about
the LTV cap, so the model's invented 80% conventional default was refused
instead of asked about. 18 rows. `annual_rate` blocked by a stated tax bracket
was the same shape for 5 more.

**The discriminator is whether the literal is SPOKEN FOR.** A compatible
literal blocks the question only while no OTHER emitted field grounds against
it — the 9.6 is consumed by `annual_rate`, so it is not available to be what
the user said about the cap. `refine_unstated` now receives the emitted
`MortgageParamsInput`, which is the thing the two-argument form could not see;
the two-argument form is kept and delegates with a default input, so every
existing caller is unchanged by construction.

**It preserves the documented dangerous failure BY CONSTRUCTION, not by
exception.** `present_value = 304000.00` against a 495,000 utterance stays
refused because 495000 grounds NOTHING else on that operation — it is unclaimed
and therefore still evidence.

## The first version was wrong, and the new gate found it in under a minute

**A claim only counts ACROSS slot kinds.** Two slots of one kind are
interchangeable enough that the user's single number could belong to either.
`loan_amount` and `original_home_value` are the case that proves it: on
"Amortize $467,500 at 5.96% over 15-year" they are the SAME amount, so letting
one consume the 467500 made a mangled `loan_amount` look unstated — *"How much
is the loan or the property worth?"*, asked of someone who had just said.
`recovery_period` against `life` is the same shape in YearCount. **114 corpus
rows.**

Across kinds the opposite holds, and `classify_slot` is already where this
project decides it: `max_ltv_rate` is a **Ratio** and `annual_rate` a **Rate**
precisely because "ends in rate" misleads for one of them. The fix leans on a
distinction that was already argued and already tested.

A convention value claims nothing either — it grounds against no literal by
construction, so letting `pmi_annual_rate: 0.0000` consume a stated 6.5% would
silence a question about a rate that really was mangled.

## The gate for the direction that has no alarm

`GroundingCorpusSweepTest` gained an **ask sweep** in the same binary and the
same pass (`backend/tests/dbg_grounding.cpp`). The existing half proves the
verifier does not REFUSE its own gold; this half proves the layer above it does
not turn a refusal into a QUESTION about something the utterance states.

**The two fail in opposite directions and only one has an alarm.**
Under-asking surfaces as `asked_ok`, and needs a model, an engine and a
holdout — 46/86 is what sent anyone looking. **Over-asking surfaces as nothing
at all:** the row is a refusal either way, `raw_exact` cannot move,
`served_exact` cannot move, and the only symptom is a user being asked for a
number they just gave. That is the direction that gets a gate.

It is a MUTATION sweep, so it tests the question rather than the corpus: every
field with a clarifying wording is moved off-corpus one at a time (×1.37, so the
result is an ordinary ungrounded value rather than an out-of-range one) and the
verdict must stay a refusal. A mutation that still verifies is counted and
skipped. **1,355 probes, 0 asked wrongly**, and it found the 114 immediately.

## Measured, paired, same engine and weights

| | raw | served | asks when it should | answers when stated |
| --- | --- | --- | --- | --- |
| v20 + the earlier asking fix | 412/560 | 433 | **46/86** | 68/68 |
| **v20 + claimed-literal narrowing** | 414/560 | 435 | **69/86** | 68/68 |

`asked_ok` **+23 and ZERO lost**, row-paired. `answered_ok` stays 68/68, so
nothing began asking on a complete request. raw/served move +2 with McNemar
**p = 0.7539 — not significant**, gained 6 and lost 4, every one of them
`ComputeHeloc`: asking the LTV question changes turn two's `prior_question`, so
the model sees a different prompt and answers differently in both directions.
This file already records that effect at 449 -> 443 and at +17. **It is not a
capability change and must not be quoted as one.**

## What was deliberately left undone

**The 17 rows still not asking are not this defect.** 12 are the model
answering with a full params block that verifies — capability, not serving. The
other 5 are `ComputeRentalRoi`, where the model SWAPPED two fields: it put the
stated "$1,400/month mortgage payment" into `periodic_operating_expenses` and
zeroed the payment. Those keep their refusal correctly — but they keep it
because `clarifying_question` has no wording for `periodic_mortgage_payment`.
**Do not add one to close the gap:** it would ask the user for a figure they
had just supplied. The swap is the per-field blindness the 20%-down and
repair-budget defects already record, and it belongs in the lexer.

The down-payment adjacency guard is UNREACHABLE today and is kept, marked as
such, for the reason the `guess` grounding exemption is kept: it states a
property that outlives today's slot kinds. Do not read it as live coverage.

## Gates

- `test_mortgage_verification`: **235 checks / 0 failures** (+9, production
  utterances and emissions verbatim, both directions pinned).
- `GroundingCorpusSweepTest`: `swept 485 rows: 0 refused` /
  `ask sweep: 1355 probes, 0 asked wrongly, 1 unusable`.
- `ctest`: **116/116.**
- Mutation-checked three ways, all with `CCACHE_DISABLE=1` because the edits are
  to a module interface:
  - deleting the same-kind guard reproduces **exactly 114** wrong asks;
  - disabling claims entirely fails **5** unit checks — the restored HELOC and
    tax-bracket questions — while the ask sweep still passes, which is the
    signature that proves the claim rule is what restores asking;
  - deleting the down-payment adjacency guard fails **nothing**.
- A `-Wswitch` warning on `ReasonCode::UnstatedField` in `map_verification_reason`
  was pre-existing; the enum is now exhaustive and fails closed on
  `INVALID_PARAMETERS`. Behaviourally inert — `UnstatedField` is intercepted
  upstream and turned into a Clarification.

## Addendum: the SGEE and cpp23-logger pins moved, and one of them did not build

Both submodules were behind their remotes and were moved to the tips:

| submodule | from | to |
| --- | --- | --- |
| `backend/external/SGEE` | `1eee7f08` | `6351d4df` |
| `backend/cpp23-logger` | `23343a8` | `c60d3c9` |

**SGEE `6ec13bfc` did not configure in this tree.** It adds
`capi_lease_filter_tests` and points it at its own shim header with
`${CMAKE_SOURCE_DIR}/bindings/capi` — which names this repository only when SGEE
is the top-level project. Embedded through `add_subdirectory`, that is the
SUPERPROJECT, so the scan died before compiling anything:

```
capi_lease_filter_test.cpp:27:10: fatal error: 'sgee_capi.h' file not found
-I/.../OptionsAndFuturesCalculator/backend/bindings/capi     <- ours, not SGEE's
```

Fixed upstream rather than patched here — SGEE `6351d4df`, using
`SGEE_SOURCE_DIR`, which CMake sets from `project(SGEE …)` and which therefore
names that repository wherever it sits. `PROJECT_SOURCE_DIR` would not do:
`tests/CMakeLists.txt` declares its own `project(sgee_tests …)`.

The same file's `include_directories(${CMAKE_SOURCE_DIR}/src)` was the identical
defect and was **already active and silent**, adding this project's
`backend/src` to every SGEE test's include path. Fixed in the same commit. A
path that resolves to the wrong existing directory does not fail the build; it
finds the wrong file.

**`backend/cpp23-logger` is not compiled by this build at all** —
`add_subdirectory(cpp23-logger EXCLUDE_FROM_ALL)` is commented out in
`backend/CMakeLists.txt` and what actually builds is sensen's nested copy at
`sensen/external/fastestjsoninthewest/external/cpp23-logger`, which was already
at `c60d3c9`. So this bump changes no bytes in any artefact; it makes the pin
agree with what is really used. Stated because the gate below does NOT cover it,
and saying "ctest passed" about a submodule the build never reads would be a
false claim.

Re-measured while there: CLAUDE.md's "wasted build time" note about the logger's
second `std.pcm` was pessimistic. The rule feeds only the phony
`build_logger_std_modules`, nothing depends on it, and the BMI is never
produced — one `std.pcm` exists in the whole build tree.

**Gate: ctest 117/117** (the new `CapiLeaseFilterTests` is the 117th), engine
relinked with plain `ninja -C backend/build` as well as `build_tests`, because
`build_tests` does not build `calculator_engine`.
