# Session Log: 2026-07-30 — Per-Leg Clocks, Matrix Date Axis, Locale-Free Dates

@author Olumuyiwa Oluwasanmi

Three defects in the P&L path, all in `backend/src/modules/calculator_service.cpp`,
all found by pulling one thread. Each was invisible while every position had a
single expiry, and each produced confident, wrong output rather than an error.

## 1. Every Leg Was Priced On One Clock

`value_at()` took a single `years_remaining` and applied it to every leg.
`payoff_at_expiry()` settled every leg at once. Correct while all legs shared an
expiry; wrong the moment two did not.

For a same-strike calendar spread the two legs then had identical
`(S, K, sigma, T)` with opposite direction. Every term cancelled and the whole
position collapsed to a flat line at the net debit — at every price and every
date. The signature is `max_profit == max_loss == -premium`.

Fixed by replacing both functions with `value_at_elapsed()`, which walks the
legs and gives each its own remaining maturity: Black-Scholes while it has time
left, intrinsic once its expiry has passed.

This is the same defect class as the Greeks bug fixed on 2026-07-29 — a value
hoisted out of the leg loop that stopped being loop-invariant when the input
space widened. Fixing `action_greeks` did not fix this, because the P&L path
had its own copy.

### The date the curve is drawn at

Per-leg clocks alone do not fix a calendar spread, and this is the part worth
remembering. At the FAR expiry both legs are intrinsic and a same-strike
calendar still collapses to a flat line. The tent shape lives at the NEAR
expiry, where the short leg has settled and the long leg still carries time
value.

So the curve is now evaluated at `curve_days()` — the earliest leg expiry —
rather than `horizon_days()`. For a single-expiry position the two are equal and
nothing changes. `StrategyResponse.curve_days_to_expiration` reports which date
was used, because a client that labels the curve "at expiration" without reading
it will mislabel every multi-expiry position.

Everything derived from that curve moved with it: `max_profit`, `max_loss`,
breakevens, and — critically — the terminal distribution behind PoP, EV and
VaR/CVaR. `risk_figures()` pairs each density with the P&L at the same grid
point, so a distribution on a different clock would weight the right P&Ls by the
wrong probabilities.

### The assumption being made

Past a leg's expiry we value it against the underlying price at the EVALUATION
date, not the unknowable price on the day it actually settled. Every payoff
diagram of this kind makes the same single-path assumption. It is stated in the
code, in the proto, and in the panel's tooltip rather than left implicit.

### Verified

Against live SPY, ATM 739 call, IV 0.147, short 30d / long 60d, both legs given
the same real ATM premium so that any curvature must come from the differing
clocks:

| | max_profit | max_loss | curve drawn at |
| --- | --- | --- | --- |
| Before | −0.00 | −0.00 | 60d (far) |
| After | 1378.78 | 0.00 | 30d (near) |

Hand-derived independently: BS(739, 739, r=0.05, sigma=0.147, T=30/365) =
13.9726, so the long leg contributes (13.9726 − 6.26) × 100 = 771.26 and the
short leg (0 − 6.26) × −1 × 100 = 626.00, totalling 1397.26 against a strike of
exactly 739. The engine reports slightly less because the price grid lands at
738.96, not 739.

## 2. The Matrix Date Axis Ran Backwards

`MatrixCell` carries two views of one axis — `days_to_expiration` and
`date_str` — and they pointed in opposite directions. The date was computed as
`now + dte`, where `dte` is days REMAINING. So the first column was labelled
"30 days to expiry" and dated 30 days in the future; the last was labelled
"0 days" and dated today. Every cell in the grid named the wrong day except the
midpoint.

Fixed by dating from `elapsed = horizon - dte`. The smoke client now asserts
that more days remaining implies an earlier date, which is enough to catch a
reversal.

## 3. Dates Carried A Thousands Separator

`date_str` was built with a `std::ostringstream`, which carries the **global**
locale. `logger`'s initialisation sets that to `en_US.UTF-8`
(`cpp23-logger/logger.cppm:2007`) so console output handles UTF-8. That locale
groups thousands, so the year was emitted as `2,026` and every cell in the grid
carried `2,026-07-30` — a date string no client can parse, read straight into
the frontend's date axis at `useCalculatorStore.ts:406`.

Fixed with `std::format`, which is locale-independent unless asked otherwise —
the right property for a wire value in ISO-8601, which this is. `<sstream>` is
gone from the translation unit.

Worth carrying forward: **a logging library changed process-global state, and
the damage surfaced three layers away in a protobuf field.** `date_str` was the
only stream-formatted output in `backend/src`, so the blast radius was one
field, but the next stream-formatted number will be silently wrong too. Prefer
`std::format` for anything that goes on a wire.

## Frontend

- `scripts/gen_proto.sh` regenerated; `getCurveDaysToExpiration()` now exists.
- `useCalculatorStore` carries `inputs.curveDays`, falling back to the horizon
  when the field reads 0 — proto3 cannot distinguish an unset double from a
  backend predating the field, and 0 would collapse the distribution.
- `ProbabilityCurve` models the density over `curveDays`, not `days`. Otherwise
  the shaded region and the PoP printed beside it answer the same question two
  different ways.
- `PayoffLadder`'s chip names the date instead of asserting "at expiry", and
  explains the settled-leg assumption on hover.
- `StrategyMetrics` shows `near → horizon` when the legs differ.

## Verification At Close

- `cmake --build backend/build --target calculator_engine smoke_client` — 0 errors.
- `cd frontend && npm run build` — clean, 33 static pages.
- Smoke test, all four RPCs live: SPY 738.63 from Alpaca; 3M CMT 3.83% published
  / 3.7938% continuous, `as_of 2026-07-29`; matrix axis `30d @ 2026-07-30 -> 0d
  @ 2026-08-29`; calendar spread `curve_at=30d`, max_profit 1378.78.

## The Policy Checker Audited Nothing

`scripts/code_policy_check.sh` was failing on `-ffast-math`. Every hit was in
gitignored build output (`backend/build/**`), downloaded dependencies
(`_deps/fastjson-src`), or SGEE documentation whose offence was the sentence
"no `-ffast-math`". No tracked first-party source carries the flag.

Looking closer, the scope was wrong in both directions at once. Rules 1 and 3
scanned `${REPO_ROOT}/src` — **a directory that does not exist**; the C++ is in
`backend/src`. `grep` matched nothing and both printed `[PASS]`
unconditionally. So the gate produced two false PASSes and one false FAIL: it
had never inspected a single file it claimed to audit.

Rewritten to take its scope from `git ls-files`, which excludes build output,
downloaded dependencies and `node_modules` by construction, and lists
submodules as gitlink entries instead of descending into them — SGEE and sensen
are separate projects with their own checkers. The `-ffast-math` rule now looks
only at build configuration, where a flag can actually be set, and ignores
commented-out and negated mentions.

Verified by planting each violation in turn and confirming it is caught by the
correct rule while the other two still pass, then restoring:

| Planted | Caught | Other rules |
| --- | --- | --- |
| `return new int(5);` in `backend/src/main.cpp` | Rule 3 | both PASS |
| `add_compile_options(-ffast-math)` in `backend/CMakeLists.txt` | Rules 50 & 55 | both PASS |
| `auto legacy_style(int x) { … }` | Rule 31 warning | both PASS |

A green checker that has never read a file is worse than a red one, because it
is quoted as evidence. Restoring the planted violations used `cp` from a
backup, not `git checkout --`, for the reason recorded in
[2026-07-30_libcxx_std_module_investigation.md](2026-07-30_libcxx_std_module_investigation.md).

## Deploying: `railway up` Lies About Failing

Seven consecutive `railway up` invocations printed:

```
Uploading...
error sending request for url (.../up?serviceId=...)
Caused by:
    operation timed out
```

each after almost exactly 32 seconds. **At least one of them deployed
successfully anyway.** Deployment `30647b56` reached `SUCCESS` and replaced
`a544b15c` while the CLI was reporting failure. The client deadline is shorter
than the endpoint's response time; the upload completes server-side and the
deployment proceeds regardless of what the CLI says.

Three hypotheses were tested and disproved before that became clear:

| Hypothesis | Test | Result |
| --- | --- | --- |
| Slow tree indexing (16 GB) | parked the 13 GB `backend/build` | still 32 s |
| Expired credential | `railway whoami` after CLI self-refresh | authenticated, still 32 s |
| Payload too large | trimmed 62 MB → 36 MB | still 32 s |

What settled it: a **1-byte** POST to the same endpoint returned `HTTP 200`
with a **25.2 s time-to-first-byte**. The latency is server-side and unrelated
to payload, so it is Railway's to fix and ours to wait out.

Two lessons, both about evidence:

1. **A timeout with server-side effects is not a failed request.** Deployments
   were being created on every "failed" attempt — that alone showed the
   requests were arriving. Retrying seven times generated seven deployments,
   five of which went to `FAILED` as empty shells.
2. **`healthz` 200 does not identify which build answered.** Confirming the
   deploy needed a request that only the new engine can satisfy.

Hence `scripts/probe_live_engine.py`, which is now the post-deploy gate. It
hand-encodes a calendar spread over gRPC-Web — `smoke_client` speaks native
gRPC and so can only ever test a local engine — and keys on
`curve_days_to_expiration`, a field absent from every prior build:

```
trailers: grpc-status:0
curve_days_to_expiration  : 30.0
max_profit                : 1397.2577510147933
max_loss                  : 3.2855496101547033e-09
VERDICT: NEW ENGINE LIVE — curve at near expiry, calendar has real shape
```

`1397.2578` against the hand derivation of `1397.26` — the deployed engine
agrees with pencil and paper to four decimal places. The local smoke run
reports slightly less only because its price grid lands at 738.96 rather than
on the 739 strike.

The payload trim was kept even though it did not fix the timeout: 36 MB
uploads faster than 62 MB regardless, and the four excluded sensen directories
are `FORCE`d off in `backend/CMakeLists.txt:139-146`. Verified by parking all
four and re-running configure — exit 0, zero errors.

## Trimming sensen: 271 Files Compiled To Use 2 Modules

This engine imports exactly `sensen.options` and `sensen.portfolio`. Linking
sensen's own target compiled **all 271** of its translation units and produced a
**93 MB `libsensen.so`**, because `add_subdirectory` plus linking a target
builds every module interface unit in its `FILE_SET` whether or not anything
imports it. Headers do not behave this way, which is exactly why the cost stayed
invisible — nothing in our source referenced the other 253 modules.

The transitive closure is **7 modules in 8 files**:

```
sensen.options ──> sensen.bigdecimal, sensen.parallel
sensen.portfolio ─> sensen.linear_algebra, sensen.bigdecimal
                    (+ sensen.cpu_features, sensen.float_types)
```

`backend/CMakeLists.txt` now defines `sensen_slim` from those eight files and
links it instead. **Nothing in the sensen submodule is modified.**
`add_subdirectory(sensen …)` was already `EXCLUDE_FROM_ALL`, so its targets only
ever built because we linked `sensen`; linking `sensen_slim` instead is the
whole mechanism. sensen's tree is still configured, deliberately — `fastjson`,
`logger` and TBB come from it.

| | Before | After |
| --- | --- | --- |
| sensen files compiled | 271 | 8 |
| incremental build | minutes | 19 s (81 steps) |
| `libsensen.so` | 93 MB | not produced |
| engine binary | links `libsensen.so.1` | self-contained, 17.3 MB |
| runtime `libz3`/`libtbb` | required | neither linked |

`scripts/sensen_module_closure.py --check` computes the closure from the source
and fails if `sensen_slim` disagrees, so the list cannot drift silently. If a
future import needs another module the build fails with
`module 'sensen.x' not found`, which names its own fix.

### A latent deployment hazard removed

`sensen/CMakeLists.txt:573` appends `-march=native` to its whole directory
scope. That lands *after* this project's canonical `-march=x86-64-v3
-mtune=generic`, so it won, and every sensen module was compiled for whichever
CPU ran the compiler — in the container, Railway's **builder**, not the host
that runs the engine. `config/cpp_details.txt` mandates x86-64-v3/generic
precisely to avoid this, for cross-host FP parity and durable-replay
determinism.

`sensen_slim` is declared in the parent directory scope, so it never sees the
override and gets the canonical flags. AVX2 and FMA remain enabled explicitly;
only AVX-512 and host tuning are given up, and no closure module uses either.

The other defines `sensen_objects` carried — `SENSEN_HAS_AVX512F`,
`HAS_LIBURING`, `SENSEN_HAS_LIBPQ`, `_LIBCPP_NO_ABI_TAG`, the `mm_malloc`
suppressions — are referenced by none of the eight files, verified by grep
before being dropped.

### Verification

- Configure: exit 0, zero errors. Ninja plans **8** sensen steps, not 271.
- Build: exit 0, zero failures, 19 s.
- `ldd calculator_engine`: no `libsensen`, no `libz3`, no `libtbb`, zero
  missing sonames. TBB is now statically linked (808 in-binary symbols).
- `build/lib/` still holds the three static libs the Dockerfile copies, so the
  `COPY --from=builder /src/build/lib/` layer is unaffected.
- Smoke test: all four RPCs live, calendar spread `curve_at=30d`, matrix axis
  `30d @ 2026-07-30 -> 0d @ 2026-08-29`.

A first flag comparison "passed" while extracting nothing from either side —
both greps matched zero lines and `diff` duly called them identical. That is the
same vacuous-check failure as the policy gate above, caught the same way: by
asking what the check would have printed had it actually read something.

`libz3-4`, `libpq5` and `liburing2` are now very likely vestigial in the runtime
image. They are left in place on purpose — removing them wants its own container
build and `ldd` pass, and the failure mode if that reasoning is wrong is an
image that will not start.

## Fixing The Deploy Properly

Eleven consecutive `railway up` invocations failed at ~32 s. The chain:

1. Railway's `/up` endpoint answers in **~25 s even for a 1-byte body** (measured).
2. `railway up` imposes a fixed **~30 s** client deadline, with no flag to raise it.
3. Any real payload overruns it, so the CLI aborts mid-transfer.
4. Railway finds no `railway.json`, falls back to its RAILPACK auto-detector,
   finds nothing to build, and the deployment FAILS with no build attached.
5. The CLI prints `operation timed out`, which says nothing about whether the
   deploy happened — deployment `30647b56` succeeded while printing it.

`scripts/railway_deploy.sh` uploads the same archive to the same endpoint with
curl, which has no such deadline. This is not a workaround of Railway's API; it
is the same call, allowed to finish. First run, after eleven CLI failures:

```
archive: 6.4 MB
archive verified: 1921 entries, railway.json + Dockerfile + sensen closure present
http=200 elapsed=10.451645s
  f4e2c76c INITIALIZING RAILPACK
  f4e2c76c BUILDING DOCKERFILE
upload landed intact — Railway is building.
```

The script verifies the archive contains `railway.json`, the Dockerfile and the
sensen closure *before* spending the upload, then waits for the builder to flip
to DOCKERFILE rather than trusting an exit code.

**A wrong diagnosis, corrected.** RAILPACK was read as proof of a truncated
upload. It is not: `builder` reads RAILPACK on *every* deployment initially, as
a placeholder while the archive is extracted, flipping to DOCKERFILE once
railway.json is found. The successful deploy showed exactly that transition.
RAILPACK only indicts an upload once the deployment has stopped moving.

**A bug written into the validator itself.** `tar … | grep -q` looks obvious and
is wrong under `set -o pipefail`: `grep -q` exits at the first match, `tar` takes
SIGPIPE, and the pipeline reports failure although the match succeeded. Whether
it bites depends on whether tar finished writing, so it fails intermittently and
on a different entry each run — which is why the `railway.json` check passed and
the very next one did not. It failed safe, refusing to upload. The listing is
now materialised once and grepped as a file.

### Container build, verified before deploying

`podman build -f backend/Dockerfile -t options-backend:trim .`

| | Before | After |
| --- | --- | --- |
| build steps | 2362 | 1840 |
| image | 346 MB | 257 MB |
| `/app/lib` | + 93 MB `libsensen.so` | two static libs |
| missing sonames | — | 0 |
| `libsensen`/`libz3`/`libtbb` linked | yes | none |

### The trim changed no numbers

The deployed trimmed engine returns **1397.2577510147933** for the calendar
spread probe — bit-identical to the untrimmed engine on the same inputs, despite
moving from `-march=native` to `-march=x86-64-v3`. Determinism is the point of
those canonical flags, and this is it being demonstrated rather than asserted.

## Known Outstanding (unchanged)
- No dividend-yield term in the pricing path.
- `StrategyResponse.matrix` is filled correctly but not yet rendered; the UI
  still shows only the one-dimensional curve.
