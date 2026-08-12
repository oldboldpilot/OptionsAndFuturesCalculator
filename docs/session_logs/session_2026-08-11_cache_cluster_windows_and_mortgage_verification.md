# Session log — 2026-08-11 → 08-12

Option-chain caching and its freshness contract, SGEE Windows CI, the queue
cluster's first real operational incident, a sensen merge, and an end-to-end
verification of the mortgagefvcalculator.com backend.

@author Olumuyiwa Oluwasanmi

---

## The through-line

Almost every defect this session was the same shape, and it is worth naming
once at the top because it recurs below in five different subsystems:

> **A reading that looks like the answer, taken from a layer that does not own
> it.**

- A volume's SIZE read as "is this node replicating?" — it was leading.
- A single TERM delta read as "is the cluster churning?" — every interval
  measured happened to contain a restart.
- `serviceInstance.healthcheckPath: null` read as "Railway is not probing" — it
  is the dashboard OVERRIDE layer; `railway.json` was configuring it all along.
- The first `.panel-body--flush` on the page read as "the chain scroller" —
  there are five, and the chain's was scrolled correctly.
- A `LIVE` chip driven by request STATUS read as data freshness — under a
  15-minute TTL those are different things.

The correction in each case was not cleverness. It was a second reading from
the layer that actually owns the fact.

---

## Option-chain cache and the freshness contract

Chain TTL 15 s → **900 s**, quote TTL 15 s → **60 s**, both overridable
(`OPTION_CHAIN_TTL_SECONDS`, `OPTION_QUOTE_TTL_SECONDS`), serve-stale on
upstream failure to a 1-hour hard cap, and `fetched_at` (RFC3339) added to
`ChainResponse` as field 8.

Raising the TTL is what made the UI's badge load-bearing. A status-driven
`LIVE` dot over quarter-hour-old quotes is fabrication by labeling, so
`frontend/src/lib/chainFreshness.ts` now owns the rule — LIVE under 60 s,
DELAYED with an as-of time otherwise — and handles the two ways that claim gets
made accidentally:

- **Viewer-clock skew.** Age is a subtraction across two machines; a browser
  running behind the server yields a NEGATIVE age that `age < 60` reads as very
  fresh. Beyond a 5 s tolerance the age is treated as unknowable → DELAYED.
- **Elapsed time without a re-render.** Age computed at render freezes at
  render; the component ticks a clock every 15 s so the chip can go stale on
  its own.

**Verified on production by replay**, not by inspection: two loads 17 s apart
returned the SAME `fetched_at` (cache hit), a load past 60 s flipped the same
cached print to `DELAYED · 199s ago`, and 109 strikes rendered throughout.

**The cache is PER REPLICA.** A load 52 s in returned a NEW print with no TTL
expired and no override set. `TtlCache` is an in-process map and
`numReplicas: 3`, so there are three independent caches — up to three upstream
fetches per window, and the `fetched_at` you see depends on the replica you
land on. Still ~60x fewer Alpaca calls than the old 15 s TTL; just 3x less than
the headline. Recorded in CLAUDE.md beside the quota table, which documents the
same multiplication for the same reason.

---

## SGEE Windows CI: four defects in a stack

`continue-on-error` had been removed from the Windows legs the day before. It
immediately paid for itself by reporting **four distinct defects in sequence**,
each hidden behind the last:

1. **Configure** — the GP-ARA suite ended in `message(FATAL_ERROR)` when Z3 is
   absent, so runners without Z3 failed to configure the whole project over one
   optional suite.
2. **Build** — `health_http.cppm` and its test carried unconditional POSIX
   socket headers. Guarded; `start()` now refuses honestly on Windows rather
   than half-porting an accept loop onto WinSock for a platform that never
   hosts a queue node.
3. **Sanitized link** — `grpc_cluster_tests` is declared after the sanitizer
   overlay loop, which only covers targets above it.
4. **Test** — the compaction assertion, wrong in two OPPOSITE directions: first
   comparing fields across two `status()` snapshots (a race), then waiting for
   `snapshot_index == last_applied` (not an invariant — compaction only fires
   after `compact_min_new_entries`). Now asserts the ordering from ONE snapshot
   and drives fresh traffic to prove compaction keeps up.

**A red leg is one signal, not one bug.** Fix the first failure and re-read
from the top. Also: ninja reports the LAST error, not the first — the visible
failure named the test file while the library module had failed identically
20 s earlier in the same log.

Guarding a file out is how coverage silently reaches zero, so the Windows fix
moved coverage the other way: `format_driver_status_json` had never been tested
except through an HTTP round trip and now has a direct test on both platforms.
Mutation-checked — emitting `0` instead of `null` for an absent `leader_hint`
fails it and is missed by all four pre-existing tests. (`0` is not cosmetic:
node 0 is a legal id, so it names a real peer.)

Final: **75/75 Windows x2, 76/76 Linux x2**, up from 70/70.

---

## The queue cluster's first real incident

**A public domain took a node out of the cluster.** A railway-provided domain
was attached to `sgee-queue-1` purely to read `/statusz` from outside. About two
hours later that service was STOPPED — `deploymentStopped: true`, status
`REMOVED` — and the cluster ran on two of three until it was noticed.

Every signal pointed away from the cause:

- The node did **not** crash. Its last lines are `Shutdown signal received...
  Teardown complete. Clean exit.` — indistinguishable from a correct shutdown.
- Only the DOMAINED service stopped; 2 and 3 were untouched on the same image.
- The URL answered **404**, which reads like a target-port mistake.
- `railway ssh` named the mechanism: *"Send a request to wake the service...
  Or disable 'Sleep when idle'"*.

Stated honestly: the API reports `sleepApplication: false`, so idle-sleep is
correlated, not proven. Either way the rule stands and is now enforced by
absence — the domain is deleted (`serviceDomainDelete`; the CLI has no delete)
and queue nodes carry no public domain. Restoring the node needed the API too:
`railway redeploy` refuses ("No deployment found") because the latest deployment
is `REMOVED`, while `deploymentRedeploy(id:)` works and brought it back on its
ORIGINAL volume with Raft state intact.

Read a node's status over the private network instead. The slim image has no
curl/wget/python, but it has bash:

```
railway ssh --service sgee-queue-3 -- sh -lc \
  "bash -c \"exec 3<>/dev/tcp/localhost/8080 && \
   printf 'GET /statusz HTTP/1.0\r\n\r\n' >&3 && cat <&3\""
```

`bash` must be invoked INSIDE `sh -lc` — the remote runs dash.

### The cluster now says who leads

The nodes logged eight boot lines and then nothing, for hours, so the only
question that matters about a Raft cluster had no answer in the logs. They now
log leadership and term **on change** (silent when stable). Verified both
directions: three real nodes under the durability test produce one baseline
line each and ZERO change lines; a node pointed at peers that do not exist
fires `term 22 -> 42 (20 elections), is_leader false -> false` — ~4/s.

That number settled the open question. A term seen moving 1926 → 2112 was
**restart-induced, not steady state**: with all three up the term froze at 2538
for thirteen minutes. **The tell is `is_leader true -> true` while the term
climbs** — a leader re-winning elections its own followers keep starting, i.e.
absent peers. A marginal network shows leadership actually MOVING, which is
what a later healthy handover looked like: `2539 -> 2540 (1 election)`, node 1
`true -> false`, node 2 `false -> true`, all three agreeing `leader_hint=2`.

`kElectionTimeoutBaseMs = 150` / `kHeartbeatIntervalMs = 50` remain untunable
`static constexpr` and are LAN/in-process numbers next to etcd's 1000 ms. That
is a **known limitation, not an observed defect** — do not re-tune against a
term delta that brackets a restart.

### Rolling deploys are now actually rolled

`deploy.sh all` fired three uploads back to back and printed "Confirm it is
healthy before the next" — advice to a human, in a script nobody reads while it
runs. `railway up --detach` returns on upload acceptance, so all three
containers restarted together: quorum loss. It now gates on the `Raft baseline:`
line that only the new binary emits — verified to REFUSE against the old binary,
so a leftover boot banner cannot satisfy it — and stops the rollout on the first
node that does not return.

Also added: `deploy.sh` refuses to deploy a node whose service has no volume at
`/data`, verified in BOTH directions (admits the three queue services, refuses
one without a volume). A node on ephemeral storage elects, accepts writes and
answers `/healthz` exactly like a correct one, and loses its Raft log at the
next restart.

### The durability audit guarded only one of its two drains

The three things that make a drain's output admissible — retry, re-resolve the
leader, observe `DRAIN_DONE` — were written inline at the FIRST drain's call
site. The re-drain, whose result decides whether a task is reported LOST, had
none of them and its completion was never checked. A re-drain aborting on
`WalError: TimedOut` reported tasks it never looked at as tasks the queue had
lost — the exact false verdict the suite exists to prevent, reintroduced through
the path added to prevent it. A second defect sat underneath: completion was
detected with `tail -5` on a transcript that ACCUMULATES, so a stale
`DRAIN_DONE` rescued a later one-line failure.

One `guarded_drain()` now serves both call sites, and
`GuardedDrainUnitTest` (0.03 s, no ports, no gRPC) covers it. Its third case
asserts both that the new rule rejects the stale transcript AND that the
original predicate accepted the same input — a test that only did the former
would pass against a function that never had the bug.

---

## The engine did not build on main

`e08de05` called `md::rfc3339_now()` while defining it WITHOUT `export` in
`market_data.cppm`. That is not an error where it is defined — it fails at the
call site, and only once something builds that translation unit.
`calculator_engine` and two test targets were broken. Exported.

---

## Frontend regression pass on production

One real issue surfaced: a fresh page load showed **"Unavailable · Pick a strike
on the ticket before pricing exercise styles"** in loss red — telling a trader
the calculator is broken because they had not chosen a strike yet. Every
precondition in `executePricing` was written into `error`.

Same defect this project already fixed once, when a `PERMISSION_DENIED` refusal
rendered under "Unavailable" instead of the upgrade prompt. Now split: a
`notReady` field renders neutrally as "Not priced yet"; `error` keeps genuine
failures. Live check after deploy: **zero** `.empty-state--error` blocks on load.

Two things nearly made its test worthless — `priceTree` is debounced 300 ms
while the harness's `settle` polls for far less (so the first version asserted
on state from before anything ran), and the "clears a stale prompt" case called
a helper that resets the field itself. `tsc` then caught wrong harness
signatures and a mock of `priceOptionTree` on the wrong client; **vitest does
not typecheck**.

Everything else passed, including both directions of the Pro gate on production,
the engine's own arithmetic (break-even 775.75 = strike 771 + premium 4.75,
Δ 50.744 matching the chain), futures term structure, tablet layout at 768 px,
and the `clearLegs` fix shipped earlier the same day.

Known and deliberately not fixed: `commitTicket` has three guards of the same
shape that render red across five panels. Transient, fired on an explicit Add
press, and fixing them means threading `notReady` through those five panels.

---

## mortgagefvcalculator.com, verified end to end

See CLAUDE.md's "Mortgage assistant" section for the table. In short: the
Finance path returns an exact 18-place `BigDecimal` that agrees with the
closed-form annuity payment and with the site's own display; the assistant gate
refuses anonymously (code 7) and ADMITS the issued partner key; both models are
loaded; and on the first admitted call the verification layer caught a
hallucinated `present_value = 304000.00` against a stated 495000.

**CLAUDE.md's model-distribution section was materially wrong** — it said no
model ships and each assistant answers `MODEL_UNAVAILABLE`. Both models load at
every boot from a private artifact host with checksums pinned. Corrected.

One FALSE refusal found and deliberately left: annual-period
`ComputeFutureValue` questions are refused because `periods` grounds against
months only. Relaxing it would admit `periods 30` against a monthly rate — a
thirty-month loan answered as thirty years. The safe side of a deliberate trade;
the real fix is a rate-period consistency rule.

---

## Also

- **sensen merged** to `ef84a7d8` (9 commits, 121 files, ~6.9k insertions:
  Clang 22 toolchain, QLoRA fused GEMMs, CUDA allocator split-brain fix,
  DiffGemma NIBBLE/SIX tiers). Verified against the tree that ships it —
  `calculator_engine` links, backend suite 95/95.
- **Backend deployed** with the cache + that sensen merge; healthcheck `[3/3]`.
- **`healthcheckPath` was already set** via `railway.json` and passing on all
  three queue services. The API field that said otherwise is the override layer.
