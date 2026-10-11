# 2026-10-07 — the mortgage assistant answers with what the visitor stated

## Why

The owner reported the mortgagefvcalculator.com AI deal assistant as "terribly broken". It refused ordinary questions
with developer text ("left out `title_settlement_percent`, which ComputeClosingCosts needs"), asked about fields no visitor
states, and returned zeros for every input the visitor never mentioned. The site's form patch then wrote those zeros
over the visitor's own values. The cause was the contract: G2b required every declared field of the named RPC. The release
gate was a 600-row holdout drawn from the training generator. That holdout could not show a sparse human phrasing, and it
scored 560/560 while the live assistant failed.

## What changed (branch `fix/mortgage-assistant-stated-fields`, 7 commits, merged to master)

- **Contract:** stated-only. The assistant emits a field only when the visitor stated it, or when it is derived from
  something stated. It asks only for an operation's essential inputs (`kEssentialFields`), and refuses in plain words.
  `Clarification` and `Refusal` carry structured `operation`/`field`. The proto numbers are append-only.
- **Verification and derivation:** `shape_stated_params` (Stated / Defaulted / Unsupported). Lexer fixes:
  - spaced-hyphen dash
  - "5.375% down to 4.375%"
  - HOA claim in both directions
  - vacancy before the figure
  - no-deposit home value
  - percent words
  - scaled negation
  Derivation fixes: bare money facts, non-monthly compounding, annual-cadence rate and periods.
- **Service:** shape → ask/erase → verify, and a calculable-content check, so "should I refinance 7.5% to 6.25%?" is
  computed rather than refused as advice.
- **Finance:** a lump-sum future value needs no payment, and a batch's optional arrays may be omitted.
- **Model:** encoder v5e, GGUF sha256 `b03dcdeff5da7c18d0e0871457d54ef1a9e41b674fe9e3af496f2bb2af3434db`, retrained on a corpus that labels
  only stated fields, in visitor phrasing.

## Measured

The visitor-regression gate is `backend/tests/data/visitor_regression.jsonl`: 272 real visitor phrasings over every site
scenario and operation. Results:

| | production (before) | this tree |
| --- | --- | --- |
| rows passing | 19 / 272 | 265 / 272 |
| "left out X" refusals | 81 | 0 |
| spurious unstated field | 63 | 1 |
| asked where params were expected | 52 | 0 |

Other measurements:
- Old holdout: 559/559 on the stated-only comparator (production 557/559).
- Strategy assistant: byte-identical over 2,013 requests.
- Parity:
  - reconstruct 2169/2169
  - tokenizer 2472/2472 on both routes
  - lexer 2472/2472
- Tests:
  - ctest 218: 208 passed, 8 skipped, 2 not run, 0 failed. `GroundingCorpusSweepTest` and `MortgageGrammarTest`
    were failing at baseline and now pass.
  - Mutation arms: 27 of 27 RED, then restored.

Evidence: `docs/evidence/visitor-regression/RESULTS.md`.

## Open

- At n=25,000, three derivation rewrites replace a correct stated value with a wrong one. They are being diagnosed
  before the engine deploys.
- Deploy order: mortgage-nest-egg first. It must send the original request plus `prior_question`/`prior_clarification`,
  merge the form's values before calling Finance, stop writing `downPayment=0` and `homePrice=loan` for unstated fields,
  render the structured refusal, and byte-copy the proto. After that, upload the GGUF and deploy the engine.
- Then move the assistant into mortgage-nest-egg (#81).

## Deployed — 2026-10-07

- **Client first:** mortgage-nest-egg `83553fa` (Railway mfv-web `e3947e7e`, SUCCESS 05:21). It sends the original request
  plus `prior_question`/`prior_clarification`, merges form values before Finance, writes no unstated zeros, and renders
  plain-word refusals. Verified by content: the live routes chunk carries the new `priorQuestion` field, which the old
  source does not.
- **Residue closed before the engine** (`625ef99`): three derivation rewrites (nearest-figure increment rule, sentence
  break ends the look-back), doubled/dropped-letter slips on concept words (dictionary-measured floor of six letters),
  and two ask defects. Sweeps clean at n=500/6000/25000; ctest 218: 208 passed, 8 skipped, 2 not run, 0 failed.
- **Engine:** the first release, `36b1d4e0`, FAILED at configure. `cpp23-logger/cmake/ToolchainBMIGuard.cmake` is tracked
  but matches the logger's `*.cmake` ignore rule, and the deploy archive used `tar --exclude-vcs-ignores`. Fixed in
  `11d0069`: the archive now keeps every tracked file `.railwayignore` does not exclude (6 re-added).
  `f1d1625a` SUCCESS.
- **Cutover, read from the deployment's own logs:**
  - 4 `model is LOADED` and 4 `ENCODER assistant ready`.
  - The mortgage encoder reports 129 (slot,map) pairs and vocab 4096; the old one reported 109 and 2694.
  - 2 SIMD lines; 0 `[ERROR`; 0 `[WARN ]`, against 36 `[INFO ]` as the positive control.
- **Container sha256:** mortgage encoder `b03dcdef…` (v5e); strategy encoder `abd40b21…` (unchanged).
- **Bucket:** v5e is uploaded beside the previous object as `mortgagefv-encoder-stated-only-v5e-2026-10-07.gguf`, with
  the checksum verified by downloading it back. The previous URL and sha are kept for rollback.
- **Production, against the live engine:**
  - site and live visitor rows: 0/12 before, **12/12 after**;
  - `ComputePayment` still `-3210.56057801266526493048779502036505463691`.
