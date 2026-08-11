# Session log — 2026-08-10 — frontend ticket expiry defect, and the test suite it exposed

@author Olumuyiwa Oluwasanmi

## What was found

`optionsandfuturescalculator.com` was healthy — live SPY quote 773.03/773.26
from Alpaca, `grpc-status: 0`, 35 expiries and 126 strikes on the chain, rate
`CMT 3M · 2026-08-10` at 3.85%. The engine's answers were right: break-even
777.59 on a 773 call at 4.59, max loss −$459.00, Δ 52.505.

The defect was in leg construction, on the **default path**. Selecting a strike
and pressing Add — without touching the Expiry dropdown — produced a position
that no panel could analyse. Payoff, P&L matrix, P&L surface, probability
distribution and Outcome all reported *"No expiration on any leg — pick an
expiry from the chain"* while that dropdown visibly showed `2026-08-17 · 7d`.

The leg was not empty. It carried strike, premium, quantity and IV, and the
engine priced it — the position panel showed Δ 0.525, Γ 0.0371, Θ −127.756, and
theta cannot be computed without a maturity. Only the horizon was lost.

### Cause

`OptionTicket.tsx:34` displays `ticket.expiration || selectedExpiration`.
`commitTicket` read the bare `ticket.expiration`, which stays `''` until the
user changes the dropdown. The lookup against `chainExpirations` therefore
missed, and `?? 0` turned the miss into `expiration_days: 0` — a value that
looks like an answer and passes every structural check.

Displayed value and stored value were resolved by two different expressions.
Any time those drift, the UI truthfully shows a value the logic never had.

## What changed

`fix(frontend): Carry the ticket's expiry onto the leg it creates` (e87a8f6)

- `loadChain` seeds `ticket.expiration` from the same resolved value it writes
  to `selectedExpiration`, so display and storage cannot disagree.
- `commitTicket` resolves with the same fallback the component uses, and
  refuses on a lookup miss rather than fabricating a zero.
- Two refusal messages that denied what the user had just done were corrected:
  a position whose legs all expire today now says so, and a contract publishing
  no IV (ordinary at a same-day expiry, where IV is undefined as time to expiry
  vanishes) now points at the ticket's IV field.

`test(frontend): Add a store test suite, starting with the defect that had
none` (ebdc0c3) — Vitest, node environment, 33 tests across four files, plus
`src/test/grpc-harness.ts`.

## Verification

Against the live API, on the production bundle served locally (the API's CORS
reflects the origin, so `localhost:3000` is admitted and no deploy was needed
to test):

| Path | Before | After |
| --- | --- | --- |
| Default (Expiry untouched) | 4 panels "Unavailable" | computes; break-even 777.59, max loss −$459.00, PoP 42.9% |
| Explicit expiry | already worked | unchanged, identical figures |
| 0DTE, no published IV | misleading message | "These contracts publish no implied volatility…" |
| 0DTE + typed IV 25 | misleading message | "Every leg expires today…" |

The default path now produces figures **identical** to the explicit-expiry path
that already worked, which is the equivalence that matters.

## Notes for next time

- **The two halves of the fix overlap.** `loadChain`'s seeding masks a
  `commitTicket`-only regression, so a regression test written against the
  natural user flow passes even with `commitTicket` reverted. `ticket.test.ts`
  forces `ticket.expiration` back to `''` to isolate the contract; the
  natural-flow test is kept beside it and documented as *not* independently
  discriminating.
- **Both regression tests were confirmed by breaking the implementation.**
  Reverting `commitTicket` fails the expiry tests. Routing the Pro gate on
  message text instead of status code fails the reworded-message test while the
  four literal-message tests still pass — which is exactly why that test varies
  the wording.
- `pkill -f 'http.server 3000'` matched the shell running it and killed it
  (exit 144). Use a captured PID, per the existing note about `pgrep -f`.

## Carried, not resolved

- `sharp` (transitive via Next) picked up CVE-2026-33327/33328/35590/35591 via
  libvips. `output: "export"` makes it build-time only, so nothing reaches the
  browser — build-host exposure, not user-facing.
- `package-lock.json` carries transitive patch bumps to nanoid (3.3.16→3.3.18)
  and postcss (8.5.23→8.5.26), pulled in by vite 8's floor. Build-time only for
  a static export.
- The review gate returned APPROVED on **one** responding reviewer: `agy` was
  auto-denied a permission in headless mode and `cursor-agent` is
  unauthenticated. Treat it as one opinion, not the 2-of-3 consensus.

---

# Addendum — SGEE durable services, replicas and workflow: what is actually possible

Investigated 2026-08-10 after the frontend work, to answer whether the four
gRPC services can be enqueued onto SGEE durable services, replicas and workflow.

## Verdict per capability

| capability | state | usable here |
| --- | --- | --- |
| durable execution (`TaskBroker`, `DurableSession`) | complete, crash-tested, tests discriminate | **not on Railway** — needs a volume |
| replication (Raft, `ReplicatedTaskBroker`) | real, DST-tested, in-process only | **no** — see below |
| workflow (DSL, builder, interpreter) | real, and ALREADY LIVE in production | **yes** |

Workflow is not aspirational: `calculator_service.cpp:940` builds an
`sgee::Builder<Ctx>("OptionsWorkflow")` and `:1122` runs the interpreter on every
`CalculateStrategy`. The other three services have no graph.

## Why replication cannot be wired here

Two independent blockers, either one sufficient.

**The transport has never been compiled.** `SGEE_USE_GRPC`, `SGEE_USE_MPI` and
`SGEE_USE_UPCXX` are all `option(... OFF)`; the backend sets none. Standalone
configure fails at `SGEE/CMakeLists.txt:731` on `find_package(gRPC CONFIG
REQUIRED)`. No SGEE CI workflow references the flag. The five `Transport_Grpc_*`
tests are `#if defined(SGEE_USE_GRPC)` and have never executed in this tree.
Separately, that transport carries **only Raft/SWIM frames** over a single
generic `/sgee.consensus.Transport/Deliver` method — it exposes no queue
operation across a process boundary, so it is not a route to a distributed queue
even once it builds.

**Railway cannot host the topology.** Raft needs stable, individually
addressable peers with durable local state, and this platform provides neither:

- `<service>.railway.internal` resolves to a *randomly picked* replica address.
  `RAILWAY_REPLICA_ID` is a UUID in the container's own environment, unreadable
  by peers; there is no ordinal index.
- **Volumes are incompatible with `numReplicas > 1`.** There is nowhere to fsync
  currentTerm, votedFor or the log — the state Raft must persist before replying
  to any RPC.
- Deploys are a blue/green cutover of the *whole* container set, so every
  release replaces the entire membership at once rather than one node at a time.

Railway's own idiom for N durable addressable peers is **N separate services**,
each with its own volume and stable DNS name — how their MongoDB replica-set
template is built. That is a different topology from a `numReplicas` bump.

## Consequences

1. **Postgres is not merely the better durable substrate — it is the only one
   available.** Volumes and replicas are mutually exclusive, so as long as this
   service runs more than one replica, no local WAL can exist. `inference_jobs`
   already reimplements TaskBroker's protocol (fenced lease, heartbeat, complete,
   DLQ, sweeper) on a substrate that is shared across replicas and survives
   redeploys.
2. **`numReplicas: 3` stands on availability grounds only.** It buys nothing
   toward consensus; the blockers are structural, not headcount. The first
   version of that commit's rationale said otherwise and was wrong — CLAUDE.md
   is corrected.
3. **The workflow extension is the part worth building**, and it needs no
   volume, no transport and no consensus.

## Trap for whoever writes the next graph

Bind actions by `graph_->GetActionId(name)`, never by name.
`ActionRegistry::Register` hashes the name (`hash(name) % 10000`) while
`Builder::Execute` assigns sequential IDs from its own table. The two numbering
schemes never agree, so registering by name compiles, runs, and silently never
executes the action. `calculator_service.cpp:975-983` documents this.
