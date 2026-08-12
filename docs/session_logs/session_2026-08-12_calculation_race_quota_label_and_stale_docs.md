# 2026-08-12 — the newest calculation wins, quota refusals name the right tier, and four stale claims

@author Olumuyiwa Oluwasanmi

Five defects, one dead-code removal, and three documents that asserted things
that had stopped being true. Every one of them is the same shape the rest of
this repository keeps finding: **a reading that looks like the answer, taken
from a layer that does not own it.**

Commits: `9c76da6` (frontend `notReady`), `9f9f13e` (calculation race + dead
code), `350cb49` (quota label, leg ids, docs), `b146372` (deploy destination
guard, docs), SGEE `290d9db8` (`/healthz` comment) and `9f02fa55` (the gate
behind it).

---

## 1. Overlapping `calculateStrategy` — the last response won

`PositionLegs` calls `calculateStrategy()` from its own change handler right
after `updateLeg`, and `StrategyWorkspace` fires it again from a `useEffect` on
`legs`. **One quantity keystroke starts two requests; a three-digit quantity
starts six.** Nothing ordered the responses, so whichever resolved last wrote
the store.

This is not an edge case and never was. Four distinct failures:

| | what happened |
| --- | --- |
| stale success | numbers computed for a position the user had already edited, rendered as the answer for the one on screen |
| stale `PERMISSION_DENIED` | **worse than "sits beside a result"** — `StrategyMetrics` checks `gateDenied` *before* it renders `result`, so an overtaken refusal *replaced* a position that had just priced successfully, with an upgrade prompt |
| stale `FAILED_PRECONDITION` | the same, through `modelLimit` |
| any stale response | cleared `isLoading` while the newer request was still running |

The fix is the staleness token `useTreePricerStore` already used
(`requestSeq`): capture a sequence number at entry, commit nothing if it has
moved on. Two consequences that are easy to miss and are both load bearing:

- **A precondition refusal bumps the token too.** Deciding there is nothing to
  compute must supersede what is in flight, or an older request lands a result
  for a position the user has since emptied.
- **Which then makes `isLoading: false` on every precondition return
  mandatory.** The superseded request used to be the only writer of it; once it
  commits nothing, a refusal that overtakes one would spin forever. This was
  found by asking "what did the guard just stop from happening?", not by a test
  failing.
- **The rate branch needs its own check**, because it sits after the Treasury
  `await` — a window the post-RPC guard cannot cover.

`frontend/src/store/calculator-race.test.ts` drives resolution ORDER by hand.
The harness's `calculateStrategy` handler may now return a *promise* of an
outcome; a synchronous handler can only express sequential calls, so a race test
written against one would pass for the wrong reason.

### Two traps in writing that test

**`return p` inside an `async` function is `return await p`.** A helper whose
job is "start the work and hand back the handle" cannot be `async` unless it
boxes the handle (`{ done: p }`). Returning it bare made `await start()` wait for
a request the test had not settled yet, and every case deadlocked at its first
line — presenting as seven identical 5-second timeouts, which reads like a
harness that does not work rather than one line of JavaScript semantics.

**`tsc` caught what the tests could not.** Widening
`calculateStrategy`'s handler type made `callbackRpc`'s `keyof RpcScript`
signature span a union that now included a `Promise`, which `'fail' in outcome`
would have inspected silently. Narrowed to the three callback-style RPCs by
name. Vitest does not typecheck; `tsc --noEmit` is a separate gate and it earned
its place here.

### Mutation results

Each guard is pinned to a distinct named test:

| mutation | tests that fail |
| --- | --- |
| drop the success-path token guard | 3 |
| drop the catch token guard | 3 |
| drop the rate-branch token guard | 1 |
| drop `isLoading: false` from a precondition return | 1 |
| drop the `gateDenied`/`modelLimit` clear on success | **0 — see below** |

That last row is not a gap and is labelled as one in the code. With the token in
place, the only writer of either field is a call that returns immediately after,
so neither can be standing when the success `set` runs. The clear is written
anyway so that `set` is complete on its own terms, and its comment says exactly
that rather than implying coverage it does not have.

---

## 2. Dead code that was also wrong

`saveStrategy` and `loadStrategies` had no callers, and `loadStrategies`
`console.log`ed its rows and discarded them. Worse than inert: **both set the
same `isLoading` that five analytics panels read as "calculating"**, so saving a
strategy would have blanked all five behind a spinner while a Postgres insert
ran.

Deleted, along with the then-unused supabase import. `isLoading` now has
**exactly one writer**, which is what those panels have always assumed.

---

## 3. Leg ids could collide, and a collision is silent

`Math.random().toString(36).substring(7)` keeps only what follows `"0."` plus
five characters. A short mantissa yields a very short id and an
exactly-representable one yields the **empty string** — `(0.5).toString(36)` is
`"0.i"`. Measured over five million draws: none empty, but roughly **one in five
thousand under four characters**.

`updateLeg` uses `map` and `removeLeg` uses `filter`, both on `l.id === id`, so
two legs sharing an id is one edit applied to both and one delete removing both,
with nothing thrown and nothing logged. Replaced with a counter, unique by
construction for the life of the tab — the only scope a leg id has, since
nothing persists one.

---

## 4. `/healthz` is liveness only, and the comment said otherwise (SGEE)

`ReplicatedQueueRuntimeDriver::is_healthy()` checks running + ticked + ticked
recently. Its comment claimed it also required *"EITHER leading OR a
leader_hint."* **It never did.**

The comment was fixed, not the code, and the reason matters more than the
correction. Railway restarts a container and gates a deploy cutover on
`/healthz`, and its zero-downtime release replaces the **whole set** of
containers at once — so every node in the new set starts with no leader and no
`leader_hint` until they find each other and elect. A health check requiring a
leader would return 503 from all three during exactly that window: the cutover
would never complete, the nodes would be killed and restarted, and the cluster
could never form quorum. The same applies to any ordinary election after a
leader loss.

**Implementing what that comment described would have been a self-inflicted
outage presented as a bug fix.** "Is there a leader?" has a real answer —
`is_leader` and `leader_hint` on `DriverStatus`, served as JSON by `/statusz`,
where a 503 does not kill the node.

### And then a gate, because a comment is not one

The fix above was comment-only, which left the semantics resting on a sentence.
`is_healthy()` had **no test at all** — the function Railway restarts a
container and gates a cutover on. Two cases now pin it:

- **running + ticking + NO LEADER is healthy.** Peers `{2, 3}` are named but
  never constructed, so quorum 2 is unreachable and node 1 campaigns forever
  without winning — a durable no-leader state a single-node harness cannot
  produce, since quorum 1 elects itself in milliseconds. The test asserts the
  **term moves** rather than reading `is_leader` once, so an idle node cannot
  pass for a campaigning one.
- **the negatives**, so "liveness only" cannot quietly become "always true":
  false before start, false after stop, and false once the last tick falls
  outside `tick_interval * kStaleTickMultiplier`. Staleness is injected through
  `is_healthy()`'s own `now_ms` parameter rather than by sleeping, which would
  race the thread under measurement.

Adding `&& (s.is_leader || s.leader_hint.has_value())` — exactly what the old
comment described — fails both health cases and nothing else: **7 passed, 2
failed.** Unmutated, 9/9.

The multi-peer harness stays inside the file header's `inbound_` argument: the
hazard there is another node's *thread* delivering into an unsynchronized
vector, and nodes 2 and 3 are never registered with the `InProcessRegistry`, so
there is no second thread and nothing to deliver.

---

## 5. Quota refusals named a tier whose limits were not in force

`limits_for_tier` falls back to the anonymous allowance for a tier
`QUOTA_POLICY` does not define. The direction is deliberate — an entitlement
naming a renamed tier must not become unlimited access — but the fallback was
**silent**, and `Decision::tier` still carried the *requested* name.

`"quota exceeded for tier 'pro'"` against the anonymous allowance reads as pro's
own limit being hit, and sends an operator to raise a number that is not the one
being applied.

Two changes:

- refusals read `pro (undefined in QUOTA_POLICY; anonymous limits)`, following
  the `(per-key)` marker convention already on that line for the same reason —
  **a label names where the NUMBER came from**, not what the caller asked for;
- the first occurrence of each unknown tier logs an error naming it, once per
  distinct name rather than per request, since the condition is a
  misconfiguration that persists.

**Why the boot-time check does not already cover this.** `load_policy` rejects a
`QUOTA_API_KEYS` entry naming an unknown tier, so `admit` cannot reach this
state. `admit_identity` can: it takes the tier from a *verified* identity —
Supabase `app_metadata.tier` or a signed licence — and neither is checked
against `QUOTA_POLICY`, because they are issued somewhere else entirely.

`tests/test_quota_tier_label.cpp` (`QuotaTierLabelTest`) gates it and asserts
**where each caller is cut off**, not just the string: `pro` gets 5 req/min and
`anonymous` 2, so an undefined tier refused on its *third* request is what
proves the anonymous number is the one applied. A test reading only the message
would still pass if the label were fixed and the limits were not.
Mutation-checked both ways — dropping the marker fails one check, applying it
unconditionally fails four.

**Checked in production afterwards:** the live `QUOTA_POLICY` defines
`anonymous`, `free`, `pro`, `partner` — every tier the entitlement paths can
emit. So the new warning is currently silent, which is the correct outcome. It
is a tripwire, and it is not tripped.

`docs/FINANCE_API.md`'s example omitted `pro` until today, which is precisely
how a live policy loses a tier: someone pastes the doc. The example now includes
it and carries a warning against pasting it at all.

---

## 6. Three documents asserting things that had stopped being true

`docs/MORTGAGE_MODEL_DISTRIBUTION.md`'s status banner still said neither GGUF
was in the deployed container, no model URL was pinned, both assistants reported
their model unavailable, and a replacement transport was "being designed". **All
four were overtaken by events and none was corrected there**, so the page
contradicted `CLAUDE.md` for a week and `CLAUDE.md` had to carry a warning
pointing readers away from it.

Rewritten to what is true and verified at every boot, keeping the history that
still binds: the HuggingFace repository was deleted on 2026-08-05, the weights
are proprietary trade secrets, **do not re-upload to any model registry to
"restore" anything.**

SGEE's three synced docs said `95/95 ctest`. Now `96/96`, with a note saying
what that number actually is — the **parent** build's total, which moves when
OptionsAndFuturesCalculator adds a suite. The extra one is `QuotaTierLabelTest`,
which is not an SGEE test and should not be looked for there.

---

## Verification

| | |
| --- | --- |
| backend | **96/96** ctest (was 95) |
| frontend | **133/133** across 13 files, `tsc --noEmit` clean, eslint clean, build clean |
| engine deploy | boot 09:35:09–09:35:15, `Mortgage assistant model is LOADED` **3×**, `Strategy assistant model is LOADED` **3×**, zero crashes, healthcheck `[3/3]` |
| live checks | `ComputePayment` → `-3210.560578012665289866`; anonymous 2-leg → code 7; anonymous `ParseOperation` → code 7 `kMortgageSurface` |
| frontend deploy | both custom domains serve this build's chunk set |

### Two deploy traps — one resolved, one only defused

**`railway status` was linked to `sgee-queue-3`, not the engine.** A bare
`railway up` from the repo root would have pushed the engine tree to a queue
node.

This one turned out to be a real defect in our own tooling, not just a footgun.
`scripts/railway_deploy.sh` verifies the ARCHIVE meticulously — `railway.json`,
`backend/Dockerfile`, the sensen module closure — and took the DESTINATION from
`~/.railway/config.json`, i.e. from whatever `railway link` last pointed at.
Ambient state with no relationship to what is being deployed. **Every check in
that script would have passed while uploading a perfectly-formed engine archive
onto a queue node**, whose service would then have taken the engine's
`numReplicas: 3` — a combination Railway forbids with a volume attached, so it
would not even have failed cleanly. Same shape as everything else in this
session: a value read from a layer that does not own it.

Fixed: the script resolves the linked service id to its NAME and refuses
anything but `options-calculator-backend`, before building the archive so a
wrong link costs a message rather than an upload. `--service-name` overrides it
deliberately. Both directions exercised — it refuses `sgee-queue-3` and admits a
matching name, because a gate that refuses everyone passes a refuse-only test.

**`railway logs --build` without a deployment id shows the PREVIOUS
deployment's log.** It reported `[3/3] Healthcheck succeeded!` while the new
build was still cloning gRPC submodules, and the absence of a fresh boot banner
in the runtime logs is what caught it.

This one is Railway CLI behaviour and cannot be fixed here, only stopped being
depended on. The deploy script now prints the deployment id and the exact
follow-up commands, including the one that actually answers the question:
**confirm the cutover, not the build.** A green healthcheck is not evidence the
new image is serving; a fresh boot sequence with one `model is LOADED` line per
replica per assistant, timestamped after the upload, is.

### One number that looks wrong and is not

`ComputePayment` returns `-3210.560578012665289866` where `CLAUDE.md` records it
positive. All eighteen digits match; the sign follows from passing
`present_value` positive, so the payment is an outflow. A sign convention in how
the check was invoked, not a regression — nothing in this session can flip it.
