# Deploying the 38-place money scale, and the four copies of one contract

@author Olumuyiwa Oluwasanmi

Date: 2026-09-23
Deployment: `9e842180-08ef-410d-9dcf-d8e5bf09aff1` (options-calculator-backend)

## What was asked

"fix the issues and deploy", then "makes sure you make the necessary changes to
the grpc backend and workflow".

## State before

`master` clean, all three remotes at `1c43765`. The sensen bump to `20c20a44`
(money scale 18 -> 38 places, storage `__int128` -> `Int256`) was built, tested,
committed and pushed -- and NOT deployed.

## Issues found before deploying

1. **`finance.proto` was newer than the engine binary.** Rebuilt; relinked.

2. **Two local engines were serving DELETED binaries.** `/proc/<pid>/exe` read
   `.../calculator_engine (deleted)` for both (pids 370735 on :50051, 883889 on
   :50072). `ps` looked healthy. Any smoke number taken would have measured
   pre-bump code. Both stopped; one fresh engine started.

   A first reading of this called it the `SO_REUSEPORT` request-splitting trap.
   That was WRONG and worth recording: they were on DIFFERENT ports, so nothing
   was being split. The real fault was narrower -- both were stale.

3. **A misdiagnosis of `scripts/run_with_env.py`.** `PRO_GATE_MODE=off <cmd>`
   appeared to be ignored, and the loader does do `merged.update(from_file)` so
   the file beats the ambient environment. It was called a defect; it is not.
   The script documents `--set`, applied after the file precisely to override.
   Correct invocation: `run_with_env.py --set PRO_GATE_MODE=off -- <binary>`,
   then verified in `/proc/<pid>/environ` rather than assumed.

## Gates before the deploy

| gate | result |
| --- | --- |
| `ctest` | 117/117 |
| `smoke_client … finance` | rc=0, every independent identity |
| `smoke_client … all` (`PRO_GATE_MODE=off`) | rc=0, multi-leg path exercised |
| OFC frontend | 372/372; `npm run build` export gates OK |
| mortgage-nest-egg | 1141 passed / 33 skipped |
| **MFV live contract suites vs PRODUCTION (old engine)** | **20/20 -- the baseline** |

That last row is the one that mattered.

## The deploy

Upload `http=200` in 2.4 s, builder flipped `RAILPACK -> DOCKERFILE`, BUILDING
for ~11 minutes, then SUCCESS. Cutover confirmed from the deployment's own logs
-- **4 `model is LOADED`** (2 replicas x 2 assistants), 2 `SIMD: runtime tier
avx512`, **0 `[WARN]` / 0 `[ERROR]`**, boot at 04:29:44Z against a 04:19:45Z
upload -- not from the healthcheck, which passes before a throw.

Live wire verified:

```
-> {"value":"-3210.56057801266526493048779502036505463691"}   38 places
```

## What broke, and why the baseline is the point

Re-running the consumer contract suites gave **19/20**. The failure:

```
closing-costs-contract.test.ts:194   /^-?\d+\.\d{18}$/
```

`docs/FINANCE_API.md` already told callers *"Parse these as decimal strings of
UNSPECIFIED length … and do not pin the count."* The consumer pinned it anyway,
so the deploy turned a suite red because the engine became MORE accurate.

**Without the pre-deploy baseline this was ambiguous** -- a single red test
after a deploy could equally have been pre-existing. With it, the 20 -> 19
delta was attributable to the one change.

A first sweep for exactly this hazard had grepped
`toBe(|toEqual(|parseFloat|Number(|toBeCloseTo|toMatch` and piped it through
`head -12`, which truncated before line 194. The method was right; the depth
was not.

### The replacement assertion

The literal is gone and NOT re-pinned at 38 -- arbitrary precision is coming
from sensen, so `\d{38}` only reschedules the same breakage. What is asserted
instead is strictly stronger:

* the value is a STRING, never a JSON number (the property the rule exists for:
  JavaScript's `number` is float64 and would round before any client code ran);
* precision may GROW but never silently shrink (`>= 18`);
* **every money field shares ONE scale** -- a field that starts being formatted
  by some other path surfaces as an inconsistency. The per-field literal could
  not see this at all.

Mutation-checked both ways: demanding 39 places fires the precision floor
naming the field; corrupting one field's scale fires the consistency check and
prints all 17 fields. The mutation output incidentally proves the live engine
emits a uniform 38 places across every money field.

## The contract has FOUR copies across TWO repositories

| copy | said |
| --- | --- |
| `backend/proto/finance.proto` (authoritative) | **18** |
| `clients/mortgagefv/proto/finance.proto` (OFC) | **18** |
| `clients/mortgagefv/proto/finance.proto` (MFV) | **18** |
| `clients/mortgagefv/{README.md,client.ts}` (MFV) | **18** |

All agreed with each other and all disagreed with the deployed engine. The
`ALLOWED_OPERATIONS` lesson again: a copy in another repository has no
mechanism in this tree that could keep it honest.

Also corrected: `finance_service.cpp:2103`, `scripts/probe_finance_service.py`,
`scripts/probe_key_and_quota.py` (which still said `__int128`),
`docs/architecture/CODE_MAP_EDGE_AND_CLIENTS.md:169`, and MFV's
`src/lib/finance-api.server.ts`.

Dated specs under `docs/superpowers/specs/` were left alone -- they are history.

## The workflow change

`scripts/check_vendored_protos.sh`, wired into the `policy` job of
`.github/workflows/ci.yml`. It asserts **byte-identity** of every
`clients/*/proto/*.proto` against `backend/proto/`.

**Byte-identity rather than a checksum manifest, deliberately.** MFV gates its
copy with a recorded sha256, and that manifest went stale for a month: it named
`62baf6ec` for a file that had become `68199ac9`, so `sha256sum -c` reported the
CORRECT contract as corrupt and the natural repair was to restore the stale one.
A recorded value must be regenerated by hand and the hand forgets; identity
against the source of truth is DERIVED, so there is nothing to forget. Same
reasoning as `pending_enqueues_in_log()` replacing a cached counter.

Three arms, all mutation-checked:

* drift in a vendored copy -> FAIL, naming the direction to fix it
  (`backend/proto` is authoritative because protoc compiles it into the engine)
* a vendored proto with no backend counterpart -> FAIL ("a dead entry beside a
  missing one reads exactly like coverage")
* **comparing zero files -> FAIL**, the positive control. Without it the gate
  passes perfectly on a tree where the vendored directory was renamed.

The backend suite is deliberately not run in CI -- a C++23-modules + sensen +
gRPC build does not fit a hosted runner -- so this is the ONLY automatic guard
on the contract.

## After

`ctest` 117/117 after the proto edit and rebuild. `code_policy_check.sh` clean.
`check_vendored_protos.sh` OK. MFV typecheck, lint, 1141/1141, and the live
contract suites back to **20/20 against the NEW engine** -- matching the
pre-deploy baseline exactly.

MFV's `CHECKSUMS.txt` was regenerated (only `finance.proto` moved) and
self-verifies with the command it prescribes.

## Left as it was

* `FINANCE_REQUIRE_KEY` stays in **Observe**.
* `financial.cppm`'s `npv` exists in both exact-`BigDecimal` and `double` forms
  and the service deliberately calls `npv_double`; `finance_service.cpp:647`
  already documents this and the proto correctly declares `double`, so the
  string-vs-double rule holds. No change.
