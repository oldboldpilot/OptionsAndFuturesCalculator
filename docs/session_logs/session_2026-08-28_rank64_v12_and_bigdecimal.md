# Session log — 2026-08-28 — rank 16 was the ceiling, and a struct that lied about being exact

@author Olumuyiwa Oluwasanmi

## Shipped

- **v12 deployed** (`10db6978`), replacing v6. 4 `model is LOADED` lines, 0 errors.
- `ComputeRentVsBuy` dispatch fix deployed the previous day (`c99dbfd7`) and
  verified through the public ingress.
- sensen merged (6 upstream commits) and pushed; parent pushed. Both remotes.

## The headline: rank, not corpus

543-row holdout, **disjoint from every training set involved**:

| arm | raw exact / 508 |
| --- | --- |
| v6 (was live, r16) | 276 = 54.3% |
| r16 base | 297 |
| r16 + grounded corpus | 308 |
| r32 | 338 |
| r64 | 386 |
| **v12 = r64 + grounded** | **400 = 78.7%** |

v12 vs v6 paired: **fixes 135, breaks 11.**

| operation | v6 | v12 |
| --- | --- | --- |
| ComputeRefinance | 9/38 | **38/38** |
| ComputeDetailedAmortization | 23/48 | **48/48** |
| ComputeHeloc | 27/30 | **30/30** |
| ComputeFutureValueDetailed | 1/31 | 15/31 |
| ComputeRentVsBuy | **0/18** | 8/18 |
| ComputeDepreciation | 12/14 | 11/14 (sole regression) |

**Rank is worth ~8x the corpus fix.** `ComputePresentValue` 0/11 → 11/11 on
nothing but adapter rank — no convention, no corpus change, pure
operation-choice reasoning. At r16 the adapter could not represent it at all.

**Two external reviews rejected the capacity hypothesis and both were wrong.**
The best objection — "a static minus sign is rank-1" — is locally true and
globally wrong: the adapter budget is SHARED across 27 operations. A nine-minute
retrain settled what an hour of argument did not.

## The measurement was broken before the model was

**304 of 600** holdout rows were byte-identical members of the older model's
TRAIN split, because each corpus revision shuffles and splits independently.
It subsidised the OLDER model: full set p = 0.17 (looked like a wash), clean
rows 45 gained / 21 lost, p = 0.0043. Every earlier comparison on this holdout
family is suspect. `--assert-disjoint-from` now refuses by default.

## Three defects found and fixed

1. **A solver seed forwarded as garbage.** ComputeRate scored 0/10 for every
   model, differing on `guess` and only `guess`. Removing it from the training
   labels did NOT make the model omit it — the documented
   `prepaid_interest_days` result again — it just changed what it emits
   (measured: 0.80, 0.0000, scraped from the utterance). `guess` is a Newton
   STARTING SEED the engine really uses. `ParseOperation` now DROPS fields the
   operation's contract excludes. Only the serving side can guarantee absence.

2. **A struct that said BigDecimal and computed in double.**
   `RentVsBuySummary` declares every money field BigDecimal; three places
   computed in `double` and wrapped — `std::pow` over up to 100 years, a
   1200-term rent sum, and a subtraction of two large near-equal doubles.
   Measured: 9132.293106093437 → **9132.29310609375**, wrong from the 10th
   significant digit. Money is a decimal string here *because* the client is a
   browser where `number` IS float64; computing in double and rendering to 18
   places moves the rounding out of sight rather than removing it.

3. **Every dated cash-flow parse was refused on the epoch.** XNPV/XIRR `dates`
   are offsets from a common epoch so `dates[0]` is always 0, and the utterance
   says "I invest $243,800 TODAY" — it says today, not zero. Found only because
   the new exclusion test asserts **Proven** rather than "not MissingField";
   the weaker assertion would have passed green while every XIRR/XNPV parse
   stayed refused. **Assert the outcome you want, not the absence of the bug
   you just fixed.**

## Two process traps hit again

- **A readiness signal that isn't the one that matters.** The engine logs
  `model is LOADED` at line 29 and `Server listening` at line 33 — weights load
  BEFORE the socket binds. An eval gated on the model line connected to a port
  that did not exist and took 543 instant refusals in 0.0 s, which the harness
  reported as a clean 0/0.
- **A stray `calculator_engine` held :50051 during a full ctest run.**
  SO_REUSEPORT would let it answer for the tests' own engines, so a pass
  obtained that way proves nothing. Re-ran the engine-dependent tests with zero
  strays: 7/7.

## Deletion

Deleted the v3/v4/v5 adapter directories on the GPU server (12.8 GB). **v5 had
no GGUF** — deleting its adapter would have destroyed that model outright, so
it was converted first. The `rm` is gated on a GGUF existing for all three.

There was no space pressure: NAS 65 TB free of 86 TB, /scratch 9.0 TB of 11 TB.
And `df /home/muyiwa/PrimaryNAS` reports the **local** 1.9 TB disk — only
`PrimaryNAS/DataFolder` is the CIFS automount, so checking NAS space that way
answers about the wrong filesystem.

## Open

- `ComputeRentVsBuy` 8/18 and `ComputeFutureValueDetailed` 15/31 remain the two
  weakest operations; a deterministic disambiguation graph is being designed.
- `ComputeRentalRoi` is 16/33 across every arm — unmoved by rank or corpus.
