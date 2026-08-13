# SGEE queue cluster — topology, and what the durability audit actually found

@author Olumuyiwa Oluwasanmi

Status as of 2026-08-12: **deployed as a non-authoritative mirror. Postgres
remains the system of record.** The evidence for that decision is below, and it
is not "we have not got round to it" — it is a measured, reproducible defect in
the read path.

**The leader-stability precondition is now MET, and promotion is blocked on a
different defect found while checking it.** See "The promotion audit, 2026-08-12"
below. In short: the tuned Raft timing cured the churn, and the audit then found
that a failed apply consumed the committed entries behind it on two of three
nodes — invisibly, because `/statusz`'s `last_applied` is Raft's cursor and not
a count of anything the broker applied. Fixed in SGEE `fbcd2d63`, **deployed to
all three nodes and re-audited clean**: term agreed, `last_applied ==
commit_index` on every node, `tick_errors: 0` everywhere.

**What still blocks promotion is the residual state, not the code.** The fix
prevents new divergence; it does not repair what was already dropped, and node 3
still carries a `dlq_depth` of 18 against 0 on both peers — baked into its
persisted snapshot, and NOT explained by the retention account this document
previously gave (see the section on it). Repair means rebuilding that node's
volume while a healthy leader exists.

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

## RESOLVED: a refused io_uring ring bricked all three nodes (2026-08-12)

**Root cause.** `DurableAppender::create` returned `OpenFailed` when
`io_uring_queue_init` failed, turning a COMPILE-time capability into a RUN-time
requirement. A binary built on a host with io_uring had no degradation path on a
host whose kernel refuses it, and container runtimes routinely block
`io_uring_setup` by seccomp after its CVE history.

The cost is not slower writes — it is **a process that cannot start**.
`TaskBroker::open` ends by sweeping leases that expired while it was dead, and
that sweep is the FIRST caller of `begin_batch`. So a node that died holding a
lease could never open its own WAL again. `sweep_expired` returns early when
nothing has expired, which is why the nodes ran for weeks and then bricked
*permanently* the moment one died mid-lease — and why all three failed
identically rather than independently: the expired-lease state is **replicated**,
so every node runs the same sweep and hits the same wall. Simultaneity was the
clue that ruled out disk corruption.

`create()` now leaves `ring_active_` false on a refused ring and `commit()`
dispatches on that RUNTIME flag instead of the compile-time macro.
`write_staged_blocking` is the same fallback batches above `UIO_MAXIOV` already
take, so a null ring is a supported state, not a degraded-mode special case.

Discriminated three ways before deploying:

| scenario | result |
| --- | --- |
| pre-fix + refused ring + expired lease | exits 1, reproducing production's log lines **verbatim** |
| post-fix + refused ring + expired lease | starts and stays up |
| post-fix + refused ring, `QueueNodeDurabilityTest` | passes — the fallback is DURABLE across a leader SIGKILL, not merely present |

**No volume was wiped, and that restraint was correct twice over.** The "corrupt
WAL" reading was wrong — the log read cleanly every time and only the WRITE
failed — and wiping would have destroyed the only evidence while leaving a defect
that would re-brick the rebuilt cluster the first time a node died mid-lease.

### Verified in production after the fix

Nodes 1 and 2 (node 3 still rolling at the time of writing):

| check | result |
| --- | --- |
| deployment status | SUCCESS — first since 04:39 |
| leader | node 2 `is_leader: true`; node 1 `leader_hint: 2` |
| term agreement | both `current_term: 2920` |
| replication | both `last_applied: 14578`, advancing together |
| the operation that bricked them | leader `sweep_successes: 359 → 376` |
| election churn | 20–23 per interval → **2** → none since term 2920 |
| **full drain** | `DRAIN_DONE n=66` — uninterrupted, no `NotLeader` |
| after the drain | still term 2920, `last_applied` 14767, `dlq_depth: 0` |

The 66 drained tasks are the 40 enqueued for the audit plus 26 stranded since
before the outage: nothing was lost while the cluster was down. **The leader held
through the entire enqueue-and-drain cycle without a single election**, which is
the precondition the promotion note below asks for. `sweep_successes` climbing is
the direct confirmation of the fix — that is the exact operation that could not
complete.

### All three nodes, after node 3 rejoined

| node | is_leader | term | last_applied | tick_errors |
| --- | --- | --- | --- | --- |
| sgee-queue-1 | false | 2940 | 15450 | 75 |
| sgee-queue-2 | **true** | 2940 | 15450 | 1 |
| sgee-queue-3 | false | 2940 | 15451 | 11 |

A second audit with the full cluster up drained clean: `DRAIN_DONE n=25`, no
`NotLeader` interruption, term unchanged throughout.

**Node 3's first deploy stalled at `scheduling build on Metal builder` for over
an hour and never started building.** That is a Railway-side scheduling stall,
not a build failure — the build log contains that one line, twice, and nothing
else. Re-running `deploy.sh 3` got it scheduled and it succeeded. Worth knowing
because the symptom (BUILDING forever) is indistinguishable from a slow compile
until you read the build log.

### `dlq_depth` is NOT comparable across replicas

Node 1 reported `dlq_depth: 17` while nodes 2 and 3 reported 0, **at the same
`last_applied` and the same term**. That reads exactly like a state-machine
divergence, and it is not one.

`InMemoryIndex::serialize_snapshot` keeps a completed-or-dead task only while
`retained(id, now_ms, retention_ms)` holds, and **compaction is local**: each node
compacts on its own schedule, at its own wall clock, so dead tasks age out of
each node's retained view at different moments. Node 2 had compacted further
(`snapshot_index` 15158 vs node 1's 15105) and its retention window had already
evicted those 17; node 1's had not.

The replicated log is identical — `current_term`, `last_applied` and the applied
commands all agree. What differs is a locally-retained *view*. An operator
alerting on a cross-replica `dlq_depth` mismatch would be chasing a phantom;
compare `last_applied` and `current_term` instead, which ARE invariants.

**The paragraph that used to follow here was wrong, and it was the dangerous
kind of wrong.** It said `tick_errors` accumulating on followers was "the same
shape" — that `sweep_expired` runs on every tick, can only succeed on the leader,
and its failed attempts are counted as tick errors, so the number was not worth
alerting on.

`maybe_sweep` does not touch `tick_errors` at all. It counts a failed sweep in
`sweep_attempts` versus `sweep_successes` and deliberately does not even log it,
precisely so that a follower's permanent `NotLeader` stays distinguishable from a
leader that is genuinely failing. `tick_errors` is incremented in exactly one
place — `refresh_status_after_tick`, when `runtime_->tick()` itself failed, which
means `apply_committed` rejected a committed entry.

So `tick_errors` on a follower is **not** benign bookkeeping. It is the state
machine refusing a replicated command, and every one of them was a dropped entry
until 2026-08-12. This paragraph is the reason to check a claim like that against
the code before repeating it: the explanation was plausible, self-consistent, and
would have waved someone straight past the 108 and 121 that exposed the
dropped-batch defect.

### The `dlq_depth` asymmetry does NOT fit the account above (2026-08-12)

Measured on the fixed binary, all three nodes freshly restarted, `compactions: 0`
everywhere (so each node's `dlq_depth` comes from the snapshot it restored, not
from anything it has compacted since):

| node | snapshot_index | dlq_depth |
| --- | --- | --- |
| sgee-queue-1 | 22151 | 0 |
| sgee-queue-2 | 22208 | 0 |
| sgee-queue-3 | **22209** | **18** |

The retention account is an ORDERING argument — the node that compacted further
had already evicted the dead tasks. **Here the ordering runs backwards:** node 3
is the furthest along and is the one retaining 18, while node 1 is the furthest
behind and retains none. Retention cannot be ruled out (whether a task is
retained depends on when it died relative to a node's compaction clock, not
monotonically on `snapshot_index`), but the specific argument this document made
does not explain this observation, and **the asymmetry survived a full restart**,
so it is baked into node 3's persisted snapshot rather than living in a
transient in-memory view.

It is consistent with residual divergence from the entries node 3 dropped before
the peek/mark fix. **That matters for promotion: the fix prevents NEW divergence,
it does not repair EXISTING divergence.** The safe repair is the one described
under the rebuild note above — discard BOTH WALs on that node and let it rejoin
empty via AppendEntries / InstallSnapshot, which is only possible while a healthy
leader exists. One exists now. Until that is done and the three snapshots agree,
this cluster stays a mirror.

## How the outage presented, and why the deploy gate hid it

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
| `QueueNodeBlockingIoTest` | a refused async-I/O backend bricking a node — the 2026-08-12 outage as a test. Forces the fallback with `SGEE_FORCE_BLOCKING_IO=1`, then kills a node mid-lease and restarts it so recovery must WRITE the expired-lease sweep |

All four hold one `RESOURCE_LOCK`: they bind fixed ports and kill by process
name, so running them concurrently under `ctest -j` would reproduce the
`SO_REUSEPORT` trap they each warn about, arriving through the test runner
instead of a stale developer process.

`QueueNodeBlockingIoTest` exists because **no other test could reach the path it
covers.** The async backend works on every developer and CI host, so the whole
suite passed over the fallback by construction — which is precisely how a fatal
`io_uring_queue_init` reached production. It drives the whole sequence rather
than unit-checking `create()`: the bug was never that `create()` failed, it was
that the failure was fatal at the one moment recovery needs a write.

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

## The promotion audit, 2026-08-12

Run because the promotion note above asks for exactly one thing: *a deployed
cluster that holds a leader long enough to complete a drain, measured the same
way the churn was measured.* The tuned pair (`1500/300`) had been deployed the
previous day and never audited against.

### The precondition passes

Three `/statusz` samples over four minutes, all three nodes:

| node | leader | term | last_applied | sweep_successes |
| --- | --- | --- | --- | --- |
| sgee-queue-1 | false | 2981 | 21463 → 21481 → 21507 | 3461 (frozen — follower) |
| sgee-queue-2 | false | 2981 | same | 0 (never led) |
| sgee-queue-3 | **true** | 2981 | same | 37 → 54 → **80** |

Term 2981 held throughout, `last_applied` advanced in lockstep, and the leader's
`sweep_successes` climbed — the io_uring fix confirmed live, because the
expired-lease sweep is the exact operation that used to brick a node. Against
20–23 elections per interval before the tuning, the churn is cured.

`sweep_successes: 0` on node 2 is **not** a fault: `maybe_sweep` documents
`NotLeader` as the permanent, expected outcome on every follower, and node 2 has
never led.

### What it found instead — and why promotion is still blocked

Nodes 2 and 3 carried `tick_errors` of 108 and 121; node 1 carried zero. Decoded
against `ConsensusError` (**not** `QueueError` — both are reachable here and
their values 5 and 6 mean different things) those are `QueueError` "the local
TaskBroker rejected an applied command" and `Corrupt`, which `apply_one` returns
from exactly one place: the check that a replica leased the SAME task the leader
logged, whose own comment says *"a mismatch means replica state diverged —
fail-stop"*.

**Those are divergence detectors, firing on two of three replicas — and the
fail-stop did not stop.** `apply_committed` called `RaftNode::committed_entries()`,
which advances Raft's `lastApplied` across the whole batch *before* the state
machine applies any of it, then returned on the first failing entry. Every entry
behind the failure was already recorded as applied and was never offered again.

It was invisible because **`/statusz`'s `last_applied` resolves to
`broker_.raft().last_applied()`** — Raft's cursor, not a count of what the broker
applied. A replica that dropped a batch tail reports itself perfectly caught up.
Three nodes agreeing on `21507` is not evidence their state machines agree. Same
shape as the io_uring outage the day before, and as the frontend's LIVE badge: a
reading that looks like the answer, taken from a layer that does not own it.

All 121 errors on node 3 fall in one 25-second window at `19:33:11–19:33:36Z`,
ten minutes after boot, with nothing in the 11.75 hours since — so this is not
ongoing damage. It does mean both nodes discarded committed entries, with no
repair path short of a follower falling far enough behind to take an
InstallSnapshot.

**It also reopens the `dlq_depth` explanation below.** Node 3 reports
`dlq_depth: 18` against 0 on the other two. The local-wall-clock-retention
account was reasonable and may still be right, but it can no longer carry the
weight on its own: there is now an independent reason those replicas could
genuinely differ, and the two must be told apart before anything is promoted.

### The fix

SGEE `fbcd2d63`. `RaftNode` gains a peek/mark pair —
`pending_committed_entries()` returns committed entries without advancing
`lastApplied_`, and `mark_applied()` advances by exactly one index, refusing
anything else. `apply_committed` marks each entry only after applying it, so a
failure leaves `lastApplied_` pointing AT the failing entry: the next tick
retries it, a transient fault recovers itself, and an unapplicable entry stalls
the node **visibly** instead of being skipped. `committed_entries()` is untouched;
`consensus_node` and the Raft/DST tests depend on its consuming semantics.

Observability changed with it, because the defect was undetectable from outside:

- **`/statusz` publishes `commit_index` beside `last_applied`.** The gap is the
  only thing separating "applied everything" from "stalled on an entry it cannot
  apply" — watch `last_applied` alone and both are a number that stopped moving.
- **`/statusz` publishes `last_tick_error`** as a name, and the driver logs
  `to_string(...)` rather than `static_cast<int>(...)`. A bare enumerator here
  invites a confident wrong diagnosis against the wrong enum — which happened
  during this very audit before it was caught, and is the same error-widening
  that turned the io_uring outage into a full day of downtime.

Gated by `ReplicatedBroker_FailedApply_LeavesTheRestOfTheBatchPending`
(mutation-checked: the pre-fix body fails it with `Expected 1, got 3`, the tail
being consumed) and `Raft_PeekMark_ConsumesOnlyWhatWasMarked`. 97/97 ctest.

### Two things this audit established about probing the nodes

Both cost time and are not otherwise written down:

- **`railway ssh` swallows the first line of remote output.** A probe whose
  first line is its own `CONNECT_FAIL` message therefore looks like a silent
  hang. Emit a throwaway line first.
- **The health server binds `::` only**, so `/dev/tcp/localhost/8080` resolves to
  `127.0.0.1` and is refused. Dial `::1` explicitly. The documented probe command
  earlier in this file predates that and hangs.
- **The Railway CLI resolves its project from the CURRENT DIRECTORY.** A probe
  script run from anywhere but the repo root answers `No linked project found`
  instantly, on stderr — which reads exactly like an empty result rather than an
  error.
