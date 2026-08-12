# SGEE queue cluster — topology, and what the durability audit actually found

@author Olumuyiwa Oluwasanmi

Status as of 2026-08-11: **deployed as a non-authoritative mirror. Postgres
remains the system of record.** The evidence for that decision is below, and it
is not "we have not got round to it" — it is a measured, reproducible defect in
the read path.

## Topology

| | |
| --- | --- |
| Services | `sgee-queue-1`, `sgee-queue-2`, `sgee-queue-3` (project `fearless-amazement`, environment `production`) |
| Replicas per service | **1**, and this is not adjustable |
| Volume | one per service, mounted at `/data` |
| Image | `backend/Dockerfile.queue-node` |
| Consensus port | 50052 (Raft + SWIM, `[::]`) |
| Client queue port | 50053 (TaskQueue gRPC, `[::]`) |
| Health/status port | 8080 — `/healthz`, `/statusz` |

Three services rather than one service with three replicas, for three
independent reasons, any one of which is sufficient:

1. Railway: *"Replicas cannot be used with volumes."* A Raft node must fsync
   `currentTerm`, `votedFor` and its log before answering any RPC.
2. `<service>.railway.internal` resolves to a **randomly chosen** replica, so
   peers of a multi-replica service cannot address one another.
3. `RAILWAY_REPLICA_ID` is visible only inside the container that owns it and
   there is no ordinal index, so a replica has no durable identity to run Raft
   with.

This is Railway's own idiom for N durable, individually-addressable peers — the
same shape as their MongoDB replica-set template.

## `/healthz` is liveness. `/statusz` is leadership.

Deliberate, and worth restating because it looks like a bug the first time.
`/healthz` answers 200 as soon as the driver is running. A follower is healthy.
A node mid-election is healthy. Gating health on leadership would make Railway
restart-loop the two non-leaders of a perfectly functioning cluster.

The consequence: **`/healthz` going green does not mean the queue will accept a
write.** Until a node wins its election, every `Enqueue` returns `NotLeader`.
Anything that waits on `/healthz` and then enqueues is racing; wait on
`/statusz` reporting `"is_leader": true` instead.

## What the durability audit found (WU-10)

`backend/external/SGEE/tests/integration/queue_node_durability_test.sh`. Three
real nodes, `SIGKILL` the leader — not `SIGTERM`, because a clean shutdown is
the easy case. `SIGKILL` is what a host failure, an OOM kill and a container
eviction all look like.

**Durability holds.** Three consecutive runs, 50 acknowledged tasks each: zero
lost, zero duplicated, zero invented. Every task the queue acknowledged before
the leader died was still there after it died and rejoined.

**The read path does not.** Two defects, both reproducible on every run, both
absent from the `NO_KILL=1` control on the same three-node cluster — which is
what establishes they are failover-specific rather than properties of running
three nodes:

1. The first drain after a failover **stalls once** with the lease RPC returning
   `WalError: TimedOut`, and recovers on retry. The queue is briefly unable to
   serve reads after a leader crash.
2. **Exactly one task goes invisible for the full 30 s visibility window.** A
   lease RPC that times out on the *client* can still commit on the *server*,
   leaving the task leased to a worker that never saw it. An at-least-once
   consumer sees a stall it cannot explain.

Neither is data loss. Both are why this cluster is a mirror.

## How that audit almost reported the opposite

Recorded because the mistake is more instructive than the result.

The first version of the script reported **"43 acknowledged tasks LOST across
the leader kill"**. That was false. The drain had aborted on
`WalError: TimedOut` after seven tasks, and the script reported the tasks it had
never looked at as tasks that were gone — its own failure to *read*, presented
as the queue's failure to *keep*, about the most serious failure a queue can
have.

Three checks now stand between the script and that claim, and each was added
because it caught a real false positive:

- **The drain must COMPLETE.** If it aborts, the result is UNPROVEN in both
  directions and says so, rather than defaulting to the frightening reading.
- **The drain retries, and the attempt count is reported.** A stalled queue and
  a wedged one want different responses from an operator.
- **Nothing is called lost until the visibility window has been waited out and
  the queue drained again.** That single check turned "1 task LOST" — on two
  separate runs — into "1 task was hidden for 30 s and came back".

Duplicate delivery is deliberately *not* asserted when the drain took more than
one attempt: a task leased by an aborted attempt reappears legitimately on the
next one, and claiming "no duplicates" from that output would be unsupported.

## The first deployment crash-looped

All three nodes died on an uncaught `std::bad_variant_access` seconds after
finishing initialisation, restarted, and repeated — six to seven times each.

Two things about how it presented are worth more than the bug itself:

- **Railway reported every deployment `SUCCESS` throughout.** `/healthz` answers
  as soon as the driver is running, which is deliberate (see above), so it
  passes before the throw. `railway status` is not evidence a service is
  running; repeated boot banners in `railway logs` are what a crash loop looks
  like.
- **It was invisible for an entire deployment cycle.** `sgee::Log` writes with
  `std::println(std::cout, ...)` and never flushed. In a container stdout is a
  pipe and therefore fully buffered, so the node's eight boot lines never
  reached the log stream — the container answered `/healthz` while its log
  showed only Railway's own "Mounting volume" / "Starting Container". Fixing
  that (`std::cout << std::unitbuf`) is what made the crash visible at all.

**Cause:** a data race on `ReplicatedQueueRuntime::inbound_`.
`GrpcConsensusTransport` delivers inbound frames on its **own** server drain
thread; the runtime's `tick()` swaps that same `std::vector` on the owner
thread; neither took a lock. The module header argued none was needed — an
argument about *reentrancy*, correct on its own terms, that says nothing about
*concurrency* and held only while the sole transport was `InProcessTransport`
(which delivers synchronously, on the caller's thread).

**Fixed** with one mutex guarding that queue and nothing else. **Not confirmed
fixed in place:** it never reproduced locally. Five hypotheses were refuted
first — unreachable peers, unresolvable peer DNS, a three-node cluster with
leader SIGKILL, mismatched tokens, and a health-endpoint fuzz of fourteen
malformed shapes plus a 120-probe storm — and the local three-node durability
test passes **with the race present**, which is exactly why it was never caught.

A `std::set_terminate` handler now prints the exception type, `what()` and a
symbolised backtrace, so a recurrence names its own thread and frame instead of
one line naming neither.

### The `inbound_` fix, confirmed under ThreadSanitizer (2026-08-12)

The fix itself was never in doubt as *code* — `inbound_mutex_` visibly guards
both the push (the transport's drain thread) and the drain (the owner thread).
What was never established is that no OTHER unsynchronised path existed, because
the bug never reproduced locally and the durability test passes with the race
present.

Built the parent tree with `-DSGEE_SANITIZE=thread` and ran `GrpcClusterTests` —
the real gRPC transport, whose own drain thread is the second thread the original
argument overlooked. **Zero data races reported on `inbound_`**, or anywhere in
the runtime's state.

Read the rest of that run carefully, because the headline number is misleading:
1396 warnings, of which **gRPC's own `channelz_registry.cc` accounts for 461**.
`-fsanitize=thread` reaches SGEE's translation units and **not** gRPC's (confirmed
in `build.ninja` per-TU, not from `flags.make`, which does not carry it). TSan is
therefore blind to gRPC's internal synchronisation, so an object handed across
that boundary under gRPC's own locks looks racy the moment SGEE touches it — and
every SGEE-located report sits exactly there: `from_byte_buffer` reading a
`grpc::ByteBuffer` (15), `ClientCallData::~ClientCallData` (3), the server drain
lambda (3).

Those are consistent with uninstrumented-dependency false positives and are
**not proven benign**; proving it would mean building gRPC itself with TSan.
What the run does establish is the thing that was asked: the member the crash was
traced to is clean under the transport that used to race it.

## Before this is promoted to authoritative

- ~~Fix the post-failover lease stall (`WalError: TimedOut`).~~ **Fixed
  2026-08-12, and it was never a stall.** `lease/submit_and_await_commit` spun
  `max_rounds = 200` against a `ProgressFn` that sleeps 1 ms per round — a
  ~200 ms budget. `RaftNode` arms its election deadline at
  `kElectionTimeoutBaseMs + rand % kElectionTimeoutBaseMs`, i.e. **150–300 ms**.
  The wait budget was shorter than the upper half of a single election timeout,
  before adding the winner's first `AppendEntries` round trip and the local
  apply, so a lease issued as a leader died *could not* confirm inside it.
  Retrying "worked" because by then the election was over.
  `kAwaitRoundsForFailover` is now derived from Raft's own constants
  (`(2*election + heartbeat) * 4`) so it cannot drift when those change.

- ~~Fix, or document a consumer contract for, the timed-out-lease-hides-a-task
  hazard.~~ **Fixed 2026-08-12.** On timeout the predicted task id was
  discarded, so a lease that committed on the leader a millisecond later left
  the task `Leased` to a worker that never learned its id — invisible until the
  30 s visibility window expired. `RuntimeError` now carries `pending_task_id`
  and the **leader-minted** `pending_token` (re-deriving the token locally would
  be wrong exactly when it matters: a timeout means this replica has not applied
  the entry). `TaskQueueService::Lease` hands the lease straight back with
  `fail()`, returning the task to Pending immediately — the same path a crashed
  worker's expired lease takes, without the wait. Best-effort by design: a
  rejected token means the lease never committed, and nothing happens.

- **Run the audit against the deployed Railway cluster.** Done 2026-08-12, and
  **it found a blocker that loopback testing cannot produce.** See the section
  below.
- ~~TLS/mTLS on both ports.~~ **Implemented 2026-08-12, and OFF until certificates
  are provisioned.** `SGEE_TLS_CA_CERT` / `SGEE_TLS_CERT` / `SGEE_TLS_KEY` turn on
  mutual TLS for **both** ports — consensus and the client queue — from one
  credentials object, because protecting the vote while leaving the port that
  ACCEPTS WORK open would secure the wrong half.

  Four properties worth knowing before relying on it:

  - **All-or-nothing.** Some-but-not-all of the three is
    `ConfigError::PartialTls` and the node **refuses to boot**. The dangerous
    direction is not "TLS fails to turn on" — that is loud — it is a node with a
    certificate and no key quietly serving plaintext while whoever set
    `SGEE_TLS_CERT` believes the port is protected. Unreadable or empty PEM files
    are fatal for the same reason.
  - **Mutual, not merely encrypted.** The server uses
    `GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY`, the only option
    that both DEMANDS a client certificate and CHECKS it against the CA. The
    similarly-named `REQUEST_CLIENT_CERTIFICATE_AND_VERIFY` verifies one if
    offered and admits a caller that presents none — encryption with no
    authentication, which reads as "TLS is on".
  - **Unset is still supported**, and is what the in-process and local test
    clusters run. The boot log now states which of the two states the node is in
    rather than unconditionally warning that the queue port is open — that line
    was true when written and would have become a lie the moment mTLS landed.
  - **The shared token still guards consensus** and is unchanged. mTLS replaces
    "knows a secret that travels in the clear" with "holds a key signed by our
    CA"; the token remains as defence in depth.

  Railway's private network was the standing mitigation and it is a boundary, not
  an authentication: a co-tenant container is on the other side of it.
- Run the audit against the deployed Railway cluster, not only locally. Every
  number above is from three local processes on loopback.

## The cluster is DOWN, and the deploy gate said otherwise (2026-08-12) — BLOCKING

Read this before the churn analysis below it. **No queue node has been running
since 04:39 PDT on 2026-08-12.** All three crash-loop on

```
[ERROR] [sgee_queue_node] Failed to create ReplicatedQueueRuntimeDriver
```

seconds after boot, so every deployment since then has FAILED its `/healthz`
check — correctly, this time. The engine and both live sites are on a different
service and are unaffected.

**The rollout reported the opposite, and that is the more important defect.**
`deploy.sh`'s `await_healthy` waited for a `Raft baseline:` line, and its comment
asserted that line was one "ONLY the new binary emits". That was true the day it
was written and false ever after, because that binary has since *been* the
deployed one. `railway logs` returns the LAST session's output when nothing is
running, so the gate matched a leftover from a dead container and returned
success for a node that never booted. It printed

> `[deploy] all three nodes rolled, one at a time, each proven up before the next.`

while every deployment was still BUILDING and the two before them had already
FAILED. Nothing was proven; the sentence was produced by a grep against a dead
node's scrollback. This is the same trap this repository documents for
`railway logs --build`, arriving through the runtime log instead.

**A marker chosen because "no previous binary could emit it" has a shelf life of
exactly one deploy.** It cannot be a gate. `await_healthy` now polls the
deployment's own status — Railway's answer about *this* rollout — and reads the
CONTAINER log on FAILED, which is where a healthcheck failure is explained. The
build log says `Healthcheck failed!` and nothing about why.

`railway deployment list --service <svc>` is the ground truth, and it was never
consulted:

| deployment | status | note |
| --- | --- | --- |
| `e567f4cb` | SUCCESS | 04:39 — the last time a node ran |
| `1aaba7a5` | FAILED | 04:47 |
| `37f89aca` | FAILED | 05:16 |
| `f98b1a05` / `f0cfce5f` / `5beeef6b` | FAILED | 06:07, all three |

### Diagnosis, and four hypotheses that were wrong

The failure is state-dependent: a **fresh** `SGEE_DATA_DIR` boots fine locally,
and so does a **restart onto a WAL the same binary just wrote**. It is the
production volumes specifically. Four plausible causes were checked and each is
disproved, recorded so they are not re-checked:

- **Not disk pressure.** `railway volume list`: ~1000 MB of 50000 MB on each.
- **Not the timing knob.** `ReplicatedTaskBroker::create` guards it —
  `if (election_base != 0 || heartbeat != 0)` — so the unset 0/0 case never
  reaches `set_timing`. The override is confirmed applied in the logs
  (`election base 1500 ms, heartbeat 300 ms`) *before* the failure.
- **Not a torn compaction snapshot.** `persistence::save_snapshot` writes to
  `path + ".tmp"`, `fullsync`s it, then `durable_rename`s. A crash mid-write
  cannot leave the "snapshot file present but unreadable" state that
  `TaskBroker::open` reports as `Corrupt`.
- **Not a WAL identity mismatch.** `kRaftWalHash` is the fixed literal
  `0x5241465453544D31` ("RAFTSTM1"), not anything derived from the code, so a
  code change cannot invalidate an existing WAL.

### Four layers each widened the error until it meant nothing

The obstacle was not the fault, it was the reporting. The same information was
discarded four times on its way out:

```
persistence::WalError{OpenFailed|IoError|Corrupt|BadHeader|PayloadTooLarge|InvalidLsn}
  -> QueueError::WalError            (TaskQueueLog::open — six values become one)
  -> ConsensusError::QueueError      (ReplicatedTaskBroker::open — every broker fault becomes one)
  -> "Failed to create ReplicatedQueueRuntimeDriver"   (the node — both halves dropped)
```

Every one of those types already had a `to_string`. The node exits immediately
on this path, so each layer was the last place its own distinction existed, and
each threw it away. A day of downtime produced the word `QueueError`.

All three are fixed, and the chain now reads end to end. Verified by inducing a
`BadHeader` locally (a foreign `broker.wal`):

```
[TaskQueueLog] Wal::open(.../broker.wal) failed: BadHeader. size=137
    expected_header_hash=0x5451554555450002.
[ReplicatedTaskBroker] TaskBroker::open(.../broker.wal) failed: WalError.
    This is the BROKER wal, not the raft wal.
[sgee_queue_node] Failed to create ReplicatedQueueRuntimeDriver: kind=Broker
    broker_error=QueueError (data_dir=...)
```

**What production says so far:** `kind=Broker`, `broker_error=QueueError`,
`TaskBroker::open(/data/broker.wal) failed: WalError`. So it is the **broker**
WAL, not `raft.wal`, and it is **not** `Corrupt` at the queue layer — it is one
of the six `persistence::WalError` values, which the next roll names. Note that
a genuinely corrupt file still arrives here as `QueueError::WalError` rather
than `QueueError::Corrupt`, because `scan()`'s error is propagated verbatim
through `Wal::open`; do not read the queue-layer value as ruling corruption out.

**A reset of `broker.wal` ALONE would be a correctness hazard, not a repair.**
`ReplicatedTaskBroker::open` seeds `snapshot_applied_index_` from
`raft_.snapshot_index()`, so a node with fresh broker state and an intact Raft
snapshot never re-applies the entries below that boundary: it would start
cleanly, pass `/healthz`, and be silently missing history. A safe rebuild
discards **both** WALs and lets the node rejoin empty via AppendEntries /
InstallSnapshot — which is only possible while a healthy leader exists, and all
three nodes are currently down.

## The deployed cluster does not hold a leader (2026-08-12) — superseded in part

The fourth precondition existed because every number in this document came from
three local processes on loopback. Running it against the deployed cluster
produced a hard negative, and it is the reason this cluster is **still a
mirror** even though the other three preconditions are now met.

What works, verified against the live Railway cluster from inside the private
network (`railway ssh`, `/app/sgee_queue_node_client`):

| check | result |
| --- | --- |
| leader accepts writes, followers refuse | `ENQUEUE_SUCCESS` on the leader; `NotLeader` on both others |
| enqueue 50, drain | `acked=50 of=50`, `DRAIN_DONE n=51` — the extra is a prior single probe. Nothing lost, nothing invented |
| leader crash (`kill -9 1`) | container restarted, `Raft baseline: term=2646 last_applied=13077` — the log survived |
| post-failover enqueue | succeeded, ids continuing (91 → 92) |

What does not work:

**The cluster elects continuously.** Consecutive `Raft change` lines on
`sgee-queue-2`:

```
term 2576 -> 2598 (22 elections)
term 2598 -> 2619 (21 elections)
term 2619 -> 2640 (21 elections)
term 2640 -> 2664 (24 elections)
term 2664 -> 2685 (21 elections)
```

Twenty-plus elections between log samples is not a cluster with a leader that
occasionally changes; it is a cluster that never settles. Three consecutive
drains were each interrupted mid-flight — one recovered 16 of the enqueued tasks
and then hit `COMPLETE_FAILED: NotLeader`. **No task was lost or duplicated in
any of it**, which is the reassuring half: the failure is availability, not
correctness.

The likely cause is that `RaftNode`'s timing is fixed at
`kElectionTimeoutBaseMs = 150` (jittered to 300) with `kHeartbeatIntervalMs = 50`,
and Railway's internal IPv6 overlay between separate services does not reliably
deliver a heartbeat inside 150 ms. A follower that misses one starts an
election; with three nodes doing that independently, terms climb without anyone
holding leadership long enough to serve a drain. These constants are
`static constexpr` — noted elsewhere as a known limitation, and now demonstrated
to be a **blocking** one rather than a cosmetic one.

**This is what the precondition was for.** Locally the same cluster elects once
and holds; nothing short of running it on the deployed network could have shown
this.

### The timings are configurable now (2026-08-12)

`RaftNode::set_timing(election_base_ms, heartbeat_ms)` makes the pair
per-instance; the class constants remain the defaults, so every existing caller,
test and DST harness is bit-for-bit unchanged. Reached from the outside as
`SGEE_ELECTION_TIMEOUT_MS` and `SGEE_HEARTBEAT_MS`, through
`ReplicatedTaskBroker::Config`.

**Both-or-neither, and the relationship is enforced rather than trusted.** The
dangerous misconfiguration is not "no override" — it is raising the election
base while leaving a heartbeat that no longer refreshes it in time, which
*reproduces* the churn the knob exists to cure. A half-set pair, and any pair
where `heartbeat * 2 > election_base`, are refused at both layers:
`ConfigError::PartialRaftTiming` at parse time and a `false` return from
`set_timing` that changes nothing. A node that runs different timing from what
its operator configured is how a cluster becomes unexplainably unstable, so
there is no silent half-application anywhere on the path. The override is logged
at boot when it is in force.

Two gates, both asserting the refusal direction as well as the admit:
`Raft_SetTiming_AcceptsWorkablePair_RefusesChurnReproducingOne` and
`NodeConfig_Raft_Timing_Is_Both_Or_Neither_And_Keeps_Its_Relationship`.

**Promotion stays blocked until a tuned pair is deployed and the audit re-run
against it.** The knob is the means, not the evidence: what would justify
promotion is a deployed cluster that holds a leader long enough to complete a
drain, measured the same way the churn was measured.

### The knob shipped with a defect that would have made the churn worse (2026-08-12)

`kAwaitRoundsForFailover` was a **compile-time constant** derived from
`RaftNode`'s class constants: `(150*2 + 50) * 4` = 1400 rounds, spent at 1 ms
per round by the driver's `ProgressFn`. Making the timing configurable did not
move it, so it stayed sized for a 150 ms election base no matter what the
operator set.

Setting `SGEE_ELECTION_TIMEOUT_MS=1500` randomises the election deadline into
`[1500, 3000)`, so the worst-case election is 3000 ms — against a budget still
frozen at 1400. **Every await that spans a failover would have timed out**, and
the knob added to cure failover churn would instead have guaranteed that no
drain could survive one. Same shape as everything else in this file: a reading
taken from a layer that no longer owns it.

`await_rounds_for_failover(base, heartbeat)` replaces the constant, the queue
node computes its budget from the timing actually in force, and logs it at boot.
At the defaults the function returns 1400 — bit-for-bit what the constant was —
so no existing caller changes.

### A second bound, found by discharging it rather than reading it

The budget also has an **upper** limit that nothing enforced. It must stay
strictly under the broker's 30000 ms lease visibility window: past that, a
caller is still waiting on a lease the broker has already reclaimed, and
completes against a **stale fencing token** — precisely the split-brain case the
token exists to catch.

`RaftNode::set_timing`'s own precondition (`base > 0`, `heartbeat > 0`,
`heartbeat * 2 <= base`) does **not** imply it, and cannot: `set_timing` knows
nothing about the broker's lease window. Z3 produced the counterexample
directly — **(3000, 1500)**, which satisfies the precondition exactly
(`2*1500 <= 3000`) and derives a budget of *exactly* 30000. Nothing in RaftNode
would have rejected it.

The queue node now refuses that pair at startup, naming the consequence rather
than the arithmetic. Verified in all three directions against the real binary:
(3000,1500) exits 1 with the refusal, (1500,300) boots with a 13200 ms budget,
and a half-set pair exits 1 with `PartialRaftTiming`.

**P6 of `backend/tests/test_sgee_automated_reasoning.cpp`** discharges the bound
through sensen's GP-ARA `Z3Reasoner` on every build. It restates the derivation
symbolically, so changing the formula in `replicated_queue_runtime.cppm` without
changing the gate fails it. It discriminates at the boundary, which is the whole
point: **(3000,1500) → 30000 is rejected, (3000,1499) → 29996 is admitted.** A
gate that merely refused large values would prove nothing.

Two things about that harness are worth knowing before extending it:

- `Z3Reasoner::prove_safety` returns `true` for **genuine SAT** and `false` for
  UNSAT, and it runs a **vacuity audit** on every SAT — a tautology or a free
  safety variable is rejected as Indeterminate rather than reported as proof. An
  invariant is therefore verified by asserting *preconditions ∧ ¬invariant* and
  requiring `false`.
- **No formula may contain the substring `timeout`.** `prove_safety`
  short-circuits on it before the formula reaches the solver, returning a
  `Timeout` error. Every identifier in P6 says "visibility window" or "election
  base" for that reason, and a verification that silently never ran is worth
  less than none.

### `ConfigError::PartialTls` and `PartialRaftTiming` rendered as "unrecognised"

Both enum values were added without extending `sgee_queue_node.cpp`'s
`config_error_name` switch, so the two misconfigurations those values exist to
explain both printed `unrecognised ConfigError`. `-Wswitch` had been reporting
it. Fixed, and the half-set run above is what proves the real message now
reaches the operator.

## Gates

| test | what it would catch |
| --- | --- |
| `QueueNodeSmokeTest` | node does not elect, does not exit on SIGTERM, or does not persist to `SGEE_DATA_DIR` |
| `QueueNodeAuthTest` | `SGEE_QUEUE_TOKEN` not reaching the transport — mismatched tokens must fail to elect |
| `QueueNodeDurabilityTest` | acknowledged tasks lost, duplicated or invented across a leader kill |

All three hold one `RESOURCE_LOCK`: they bind fixed ports and kill by process
name, so running them concurrently under `ctest -j` would reproduce the
`SO_REUSEPORT` trap they each warn about, arriving through the test runner
instead of a stale developer process.

## Volumes, and a disk asymmetry worth watching (2026-08-11)

`deploy/queue-node/deploy.sh` now refuses to deploy a node whose service has no
volume mounted at `/data`, checked before the upload. This is not a hypothetical
guard: a node on ephemeral storage starts, elects, accepts writes and answers
`/healthz` exactly like a correct one, and the difference appears only at the
next restart — when the Raft log, `currentTerm` and `votedFor` it must fsync
before replying to any RPC turn out to have been on the container filesystem.
It then rejoins with an empty log and votes as though it had never seen the
cluster. Nothing in the node's own output can tell you this, because the mount
is a property of the service rather than of the process. Verified in both
directions: it admits all three queue services and refuses a service without a
volume.

Measured the same day:

| service | volume | used |
| --- | --- | --- |
| sgee-queue-1 | sgee-queue-1-volume -> /data | 870 MB |
| sgee-queue-2 | sgee-queue-2-volume -> /data | 852 MB |
| sgee-queue-3 | sgee-queue-3-volume -> /data | **8 MB** |

**Node 3's small volume is not a replication failure — it was LEADING the
cluster when this was checked.** Read from inside its own container:
`is_leader: true`, `current_term: 1926`, `last_applied: 4508`,
`tick_errors: 0`, `ticks_run: 667648`. Node 1 twenty minutes earlier was at
`last_applied: 4218` in term 1922, so node 3 was ahead, not behind. The
remaining explanation is the ordinary one: its volume was created later, so its
WAL begins where it joined rather than at the cluster's first entry.

What is still open is the SIZE, not the asymmetry: 870 MB for ~4,200 applied
entries is roughly 200 KB each, for a queue whose payloads are small, and
`compactions: 1` with `snapshot_index 4032` against `last_applied 4508` says
compaction has run once. Whether snapshotting reclaims WAL bytes or only
advances the index is the question to ask first; on a 50 GB volume there is
time to ask it properly.

## Do not attach a public domain to a queue node — the second reason

The `TaskQueue client port is UNAUTHENTICATED` warning this node prints at every
boot has always given the security reason. There is an availability reason too,
and it cost this cluster a node on 2026-08-11.

A railway-provided domain was attached to `sgee-queue-1` (port 8080, `/statusz`)
to verify leadership from outside. Roughly two hours later the service was
**stopped** — `deploymentStopped: true`, status `REMOVED`, at 22:39:37Z — and
the cluster ran on two of three nodes until it was noticed and redeployed.

How it presented, which is the part worth remembering:

- **The node did not crash.** Its final log lines are `Shutdown signal
  received... Teardown complete. Clean exit.` — a clean SIGTERM. Nothing in the
  node's own output suggests anything is wrong, because from the node's point of
  view nothing was.
- **Only the service with a domain stopped.** `sgee-queue-2` and
  `sgee-queue-3` stayed `SUCCESS` throughout, on the same image, the same
  variables and the same deploy.
- **The domain answered 404 while stopped**, which reads like a routing or
  target-port problem rather than a stopped container.
- **`railway ssh` named the mechanism**: "Send a request to wake the service,
  then retry. Or disable 'Sleep when idle' in service settings."

Stated honestly: the API reports `sleepApplication: false` on all three
services, so idle-sleep is not PROVEN to be the mechanism. What is established
is the correlation and the shape — the only domained service, stopped cleanly,
with the tooling itself offering waking as the remedy. Either way the operational
rule is the same and is now enforced by absence: **the domain has been deleted**
(`serviceDomainDelete`, verified `serviceDomains: []`), and queue nodes carry no
public domain.

Read a node's status over the private network instead. `railway ssh --service
<name>` works, and the image has no curl, wget or python — but it does have
bash, so:

```
railway ssh --service sgee-queue-3 -- sh -lc \
  "bash -c \"exec 3<>/dev/tcp/localhost/8080 && \
   printf 'GET /statusz HTTP/1.0\r\n\r\n' >&3 && cat <&3\""
```

Two notes on that command. `bash` must be invoked INSIDE `sh -lc` — the remote
runs `sh`, so a top-level `bash -c` is parsed by dash and fails with
`cannot create /dev/tcp/...: Directory nonexistent`. And cross-node probes
(`sgee-queue-1.railway.internal:8080` from another node) hang rather than
connect; the private network is IPv6-only and this was not chased down, so
probe each node from inside itself.

`railway redeploy` will NOT restore a stopped node — it answers "No deployment
found for service" because the latest deployment is `REMOVED`. The deployment
still reports `canRedeploy: true`, so the API mutation is what brings it back:

```
mutation redeploy($id: String!) { deploymentRedeploy(id: $id) { id status } }
```

Node 1 came back on its ORIGINAL volume (`vol_287p0apufudehqvc`, same id as
before the stop) with `self=1` and all four subsystems started, so no Raft state
was lost.

## Election churn: measured, and it is restart-induced (2026-08-12)

The open question from the previous section — was the term moving 1926 -> 2112
in twenty-five minutes a burst around a node restart, or sustained churn — is
answered, and the answer is the benign one.

The queue nodes now log leadership and term changes, so the cluster says this
itself instead of requiring an SSH probe. Rolling all three onto that binary
produced bursts of **16-24 elections per five-second sample** (~4/s) — exactly
the rate a node campaigns at when it cannot reach its peers, and its peers were
precisely that, one at a time, while each rebuilt.

With all three up, the term **froze at 2538 and did not move for thirteen
minutes** (04:47:42Z -> 05:00:53Z), with `last_applied` steady at 8886 on all
three and node 2 leading throughout.

Two things worth carrying forward:

- **The tell is `is_leader true -> true` while the term climbs.** That is a
  leader re-winning elections its own followers keep starting — i.e. peers that
  are ABSENT. A genuinely marginal network shows leadership actually moving
  between nodes. The two look identical if you only watch the term.
- **Never conclude from a single term delta.** Every interval measured before
  this one happened to contain a restart, which is why the question stayed open
  for a day. Two readings across a window known to be quiet settled it in
  thirteen minutes.

`RaftNode`'s `kElectionTimeoutBaseMs = 150` / `kHeartbeatIntervalMs = 50` remain
`static constexpr` and untunable, and they are still LAN/in-process numbers
(etcd defaults to 1000 ms). That is a known limitation, **not** an observed
defect on this deployment — do not re-tune consensus timing against a term
delta that brackets a restart.
