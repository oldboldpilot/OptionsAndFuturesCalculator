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

**The state that defect corrupted has since been REPAIRED, and the divergence it
caused was proven first rather than assumed.** Decoding all three
`broker.wal.snap` files showed 17 of the 20 shared tasks disagreeing across
replicas — some `Completed` on one node and `Dead` on another, both terminal
states — with `next_task_id` at 219/178/178. Nodes 2 and 3 were rebuilt from the
leader; all three now decode to an identical state payload (md5
`b5fdb7abc47171586ae3494aee0035d3`, `next_task_id` 219, zero disagreements). See
"The divergence was real, was proven, and was repaired" at the end of this file.

**The correctness blocker is therefore gone. What remains is unfinished work, not
a defect:** mTLS is implemented but OFF pending certificates, and nothing routes
to the cluster yet — `SgeeQueueClient` is mirror-mode only, so promotion means
building an SGEE-backed admission path rather than flipping a flag. Postgres
remains the system of record until both are done.

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

## The divergence was real, was proven, and was repaired (2026-08-12)

The section above left `dlq_depth: 18` on node 3 unexplained. It is now settled,
and the answer was worse than "a retained view": **all three replicas held
different state machines.**

### How it was proven without guessing

`/statusz` exposes only a depth, and the queue client has no DLQ command — so
the evidence is the state-machine snapshot itself. `broker.wal.snap` is small
(1.5–12 KB), so all three were pulled off with `base64 -w0` over `railway ssh`
and decoded locally. The framing is
`[magic "SNAP" u32][version u32][lsn u64][entity_id u64][state_len u32][state…][crc32]`,
wrapping `InMemoryIndex::serialize_snapshot`'s own
`[version u32][next_task_id u64][last_token u64][count u32][entries…]`, each
entry 62 fixed bytes plus payload, all little-endian.

The first read failed because the outer `SNAP` header was mistaken for the index
header — worth knowing before decoding one of these by hand.

Decoded, at essentially the same log position and the same wall-clock minute:

| node | next_task_id | tasks | states |
| --- | --- | --- | --- |
| sgee-queue-1 | **219** | 121 | 121 Completed |
| sgee-queue-2 | 178 | 20 | 2 Pending, 18 Completed |
| sgee-queue-3 | 178 | 165 | 4 Pending, 143 Completed, **18 Dead** |

Of the 20 tasks present on all three, **17 disagreed on state**. Tasks 161–173
were `Completed` on nodes 1 and 2 and `Dead` on node 3. Task 167 was `Completed`
on node 1, `Pending` on node 2 and `Dead` on node 3 — three replicas, three
answers.

**`Completed` and `Dead` are both TERMINAL.** No amount of "one node is further
ahead" reconciles them, because a terminal state never changes again. This is a
State Machine Safety violation — precisely what `apply_committed` dropping the
tail of a batch produces.

`next_task_id` is the tell for which node was right: it is monotonic in enqueues
applied, and node 1 — the only node that never logged a tick error — had applied
41 more than the other two. **Nodes 2 and 3 were both diverged; node 1 was
correct.**

### The repair

Rebuild each bad replica from the leader, one at a time, keeping quorum: delete
`raft.wal`, `raft.wal.snap`, `broker.wal`, `broker.wal.snap` (keep `node.id`),
restart the container, let it rejoin empty via AppendEntries / InstallSnapshot.
Node 3 first — worst diverged — then node 2, with node 1 leading throughout so
the snapshot that propagates is the correct one.

A rebuilt node comes back at `current_term: 8`, `last_applied: 0` and no
`leader_hint`, then jumps straight to the leader's `snapshot_index`. It cannot
disrupt the cluster on the way: its term is far *below* the cluster's, so its
RequestVote is refused and the refusal carries it up to the current term, and
Raft's log-completeness check makes it unelectable until it has caught up.

Verified by re-pulling all three snapshots and diffing the decoded state:
identical md5 `b5fdb7abc47171586ae3494aee0035d3`, identical `next_task_id` 219,
identical task-id sets, **zero disagreements**. Only the outer frame's `lsn`
differs (768 on node 1, 0 on the two rebuilt), which is local WAL bookkeeping and
not state.

### `kill -9 1` inside a container does nothing

The first rebuild attempt deleted the WALs and ran `kill -9 1`. The files went
away and **the process kept running** — `dlq_depth` still 18, `sweep_attempts`
still climbing — holding open descriptors to the now-unlinked inodes.

PID 1 of a PID namespace is immune to signals sent *from inside that namespace*
for which it has installed no handler, and that immunity **covers SIGKILL**.
Killing a container's init has to come from outside: `railway redeploy --service
<name> -y`, which is what actually restarts it.

That middle state is worse than doing nothing — the files are gone but the
divergent state is still in memory and would be re-persisted on the next
compaction. Delete and restart must be one action, and the restart must come from
the runtime.

### What this changes for promotion

The correctness blocker is gone: the code defect is fixed and gated, and the
state it corrupted has been rebuilt and proven identical across all three
replicas. What remains before this can be the system of record is not a defect
but unfinished work:

- **mTLS is implemented and OFF** pending certificates. Anything that can reach
  port 50053 can enqueue, which is tolerable for a mirror behind a private
  network and is not tolerable for the authority.
- **Nothing routes to it yet.** `SgeeQueueClient` is mirror-mode only —
  `enqueue_mirror`, best-effort, dropped on a full ring or open breaker. Making
  the cluster authoritative means an SGEE-backed admission path replacing
  `PostgresAdmission`/`PostgresLeaseSource`, not a configuration flip.

Postgres remains the system of record until both are done.

---

## Promotion, carried out 2026-08-12/13

Both blockers named directly above are closed. mTLS is deployed on both ports
(`04ebc42`), and the admission path that was missing now exists. Promotion was
done as **five staged deploys**, not a configuration flip, for the reason the
section above gives: the queue could not return an ANSWER.

That gap, precisely: `CompleteRequest` carried no result, `Task` had no result
field, and there was no RPC to read a task back at all. A submitter on replica A
could not learn the outcome of a job executed on replica B — which is the entire
reason `PostgresAdmission` exists, and why this was a protocol change.

### The two-phase rule, and why it is not negotiable here

A result must survive a leader change, so it must be **replicated and
persisted** — it cannot live in a side table. That drags it through three
formats, and one of them has no room to be polite about it.

`BrokerComplete` is a *replicated command*: every replica decodes every one of
them. The frame carries **no version byte and no length prefix** — it is
`[tag u8][fields]` and nothing else — so the per-tag exact-size check IS the
versioning. Since `fbcd2d63` a frame a node cannot decode is no longer silently
dropped: `apply_committed` stops without marking it, `last_applied` freezes, and
the node retries the same entry forever. **Writers before readers is a stalled
node per un-upgraded replica, not a degraded window.**

So Stage 1 shipped readers for all three formats and wrote nothing new:

| format | reader | writes |
| --- | --- | --- |
| index snapshot (`decode_entry`) | `kIndexSnapshotVersionMax = 2` | v1 |
| WAL `TaskCompleted` (`decode_completed`) | 8 bytes = v1, 12+len = v2 | v1 |
| replicated `BrokerComplete` (`decode`) | 16 bytes = v1, 20+len = v2 | v1 |

The first Stage-1 commit **claimed the whole stage and delivered half of it** —
it touched the snapshot reader and left `codec.cppm` and
`broker_command_codec.cppm` alone. That is recorded rather than quietly fixed
because the missing half was the dangerous half: the snapshot is the one format
that already had a version field, and the two that did not are the two where a
wrong guess is unrecoverable.

**Size is the discriminator, so there is a dead zone, and it must be REFUSED** —
9..11 bytes for the WAL record, 17..19 for the command body. Reading one as a
short v2 builds a length out of whatever bytes happen to follow, which is
indistinguishable from a real one.

**An empty result is encoded as the v1 shape.** An empty result and a v1 record
are therefore the same bytes: the same fact, written the same way, still
readable by a binary that predates v2. The consequence is the one that made the
rollout safe — **the Stage-2 writer flip is inert on a cluster that is not yet
recording results**, so the writers could roll before anything routed to them.

`BrokerCommandCodecTests` is new, 9 cases. **That format had no test at all**,
and it is the most dangerous one in the queue.

### The deploy found two toolchain defects, and neither named its cause

Both were latent from the `import std;` conversion and both surfaced on the
first Stage-1 deploy.

**The build never compiled.** `Dockerfile.queue-node` passed
`-DCMAKE_CXX_COMPILER=clang++`. sensen resolves the libc++ std module as
`dirname(dirname(CMAKE_CXX_COMPILER))/share/libc++/v1/std.cppm`, so the
`/usr/bin/clang++` symlink — pointing at exactly the right compiler — resolved it
to `/usr/share/libc++/v1`, which does not exist. `std_module_precompile` was
never declared and every `import std;` in the tree failed with "module 'std' not
found", naming neither the symlink nor the path arithmetic.
`backend/Dockerfile` had been fixed for this in `bd11d00`; this file was missed.
Note that `-DLIBCXX_MODULES_PATH=…` sits on the same command line and sensen
never reads it — the flag that looks like it configures this is inert.

**Then it failed at the LINK**, on `__cxxabiv1::__vmi_class_type_info`'s vtable,
reported against `libstdc++.so.6` — which reads as a standard-library mix-up and
is not one. A from-source libc++ installs `libc++.so` as a **linker script**,
literally `INPUT(libc++.so.1 -lc++abi -lunwind)`, so the ABI library arrives
unasked. apt.llvm.org ships a plain symlink to `libc++.so.1`, nothing pulls
libc++abi in, and `ld` reported the only C++ runtime that *was* reachable
transitively. Fixed with an explicit `-lc++abi`. The engine image had not hit it
because it compiles no object that references a C++ ABI symbol directly.

### The deploy script's diagnostic was wrong in both directions

It printed `railway logs --service` output — which replays a dead session's
scrollback — and the SSL handshake errors it showed were timestamped **three
hours before** the deployment it was blaming. It then called the failure a
healthcheck failure, pointing a reader at a container that had never started.

The discriminator is the newest deployment's **`imageDigest`**: absent means no
image was produced, so no container ran and it is a BUILD failure
(`railway logs --build <id>`); present means the container started and did not
stay up (`railway logs --deployment <id>`). Nothing else available there tells
them apart.

### Stage 2 — writers, and a bound that was wrong before results existed

`TaskBroker::complete(id, token, result)` persists it, `BrokerComplete` carries
it, `apply_one` applies it, `kIndexSnapshotVersion` 1 → 2.

**The version bump and `encode_entry`'s result append are ONE commit.**
Mutation-checked: reverting the bump alone fails three tests, two of them
pre-existing round-trips, because a v1 header over v2 entries mis-parses from the
second entry onward — the reader stops after the payload, then reads the result's
length prefix as the next entry's task id. Neither direction is detectable as a
version problem; both present as a corrupt snapshot.

**The size bound was wrong, and this stage's own test caught it on first run.**
`task_queue::kMaxPayloadBytes` and `persistence::kMaxPayloadBytes` are both
64 MiB, but that names the size of the **encoded record**, and the record
prefixes 12 bytes (WAL) or 21 (replicated command). A result at exactly the old
bound was accepted at the write and refused at the append — precisely the
"accepted here, rejected downstream" hazard the bound exists to prevent, off by
the framing.

The comment on `kMaxPayloadBytes` had claimed a payload of that size "can never
produce a frame the codec would reject". That was false for enqueue too
(`encode_enqueued` prefixes 17 bytes), so the latent defect **predates results
entirely**. Now `kMaxUserBytes = kMaxPayloadBytes - 32` bounds what a CALLER
supplies, one rule for both fields.

### Stage 3 — the RPC surface

`bytes result = 11` on `Task`, `bytes result = 3` on `CompleteRequest`, and a
new `GetTask` RPC. `GetTask` returns `grpc::Status::OK` with the outcome in the
embedded `Status`, per this proto's convention — an in-band code survives
gRPC-Web/Envoy transcoding unchanged and needs no status-string parsing.

The client's `get_task` returns `expected<optional<Task>, ClientStatus>` and the
two "no answer" shapes are deliberately distinct: `nullopt` means the server
answered and has no such task; `unexpected` means the call did not complete. A
submitter must retry the second and stop on the first — collapsing them turns a
network blip into "the job vanished".

**`found == false` is not proof the task never existed.** Compaction reclaims
terminal tasks past their retention window, and a reclaimed task reads
identically to an unknown one. A submitter must poll inside that window.

**There is deliberately NO `error` field.** The plan called for one. Carrying an
error string would mean widening `BrokerFail` — a fixed 16-byte replicated
command with no second shape any deployed node can read — plus the WAL's
`Failed` and `MovedToDlq` payloads and a snapshot v3: a second full
readers-first deploy cycle, for a diagnostic the worker already logs. A failure
is signalled by the task reaching terminal `Dead`, which a reader already sees
in `state`. **A proto field the replicated log silently dropped would be worse
than no field — it would read as an answer.**

One shared-code note: `detail::task_from_proto` now exists because the
proto→native conversion was open-coded inside `lease()`. A hand-copied field
list is how a field gets added to the wire and quietly dropped coming back —
nothing fails, it returns a default. `result` was the first field to make that
concrete.

### Stage 4 — the engine admission path

`set_lease_source()` and `lease_source_` were typed on the **concrete**
`PostgresLeaseSource`, which made the storage substrate part of the decode
loop's type rather than a deployment choice. An abstract `LeaseSource` with one
virtual `fill()` is what made a second source possible at all.

`SgeeLeaseSource` and `PostgresLeaseSource` are structurally identical, and that
is not duplication for its own sake: the constraint that shaped the first —
lease on the owner thread, never block there, hand the write-back to a
short-lived helper — belongs to the **decode loop**, not to Postgres.

`SgeeAdmission` **polls** `GetTask` on a 25 ms interval rather than awaiting,
because SGEE has no await. That is the same shape the Postgres path degrades to
whenever its `pg_notify` hint does not arrive — and `inference_queue.cppm` is
explicit the hint is never load-bearing for correctness — so the two share
failure modes rather than introducing a second set.

A poll that does not complete keeps retrying until the deadline; a poll
answering "not found" degrades immediately. A single lost RPC against a cluster
mid-election is expected and the work may already be running — degrading there
would run the whole inference twice.

Gate: `test_inference_admission` asserts an unreachable cluster still yields the
**local** backend's own answer AND yields it **promptly**. A path that degrades
only after holding a gRPC handler for ninety seconds satisfies the letter of
"degrade" and none of the point.

### `SGEE_PEERS` means two different things — a third time

The nodes read it and dial **consensus, 50052**. The engine's admission client
reads the same variable name and dials the **client queue, 50053**. The engine's
value is therefore:

```
1=sgee-queue-1.railway.internal:50053,2=...:50053,3=...:50053
```

Both ports carry the same mTLS credentials — "both ports or neither", since
protecting consensus while leaving the port that ACCEPTS WORK open would secure
the vote and not the queue — so the engine needs `SGEE_TLS_CA_CERT_B64` /
`SGEE_TLS_CERT_B64` / `SGEE_TLS_KEY_B64`. They are all-or-nothing and the client
logs loudly rather than downgrading to plaintext, because a silent downgrade
against a cluster requiring client certificates fails as a handshake error that
looks like a network problem.

### The mixed-version window was exercised on the real cluster

During the Stage-2 roll there was a genuine mixed-version window: node 1 on the
writer binary, nodes 2 and 3 still on Stage-1 readers. Node 1 restarted at index
25173 against a leader at 26033, caught up, and converged.
`last_applied == commit_index` and `tick_errors: 0` on all three throughout.

Be precise about what that proves. Node 1 rejoined as a **follower**, so it
emitted no v2 commands; the direction exercised was new-reader-reads-old-writer,
which is the safe one. The dangerous direction is covered **by construction**,
not by that observation: `encode` emits the v1 shape whenever the result is
empty, and nothing supplies a result until `INFERENCE_QUEUE=sgee` is set.

### State at the flip

| check | result |
| --- | --- |
| all three nodes on the writer binary | SUCCESS |
| cluster agreement | `last_applied == commit_index == 28981`, one leader |
| `tick_errors` / `dlq_depth` | 0 / 0 on all three |
| engine cutover | 6 `model is LOADED` (3 replicas × 2 assistants), after the upload |

`INFERENCE_QUEUE=sgee` and `SGEE_PEERS` were set together with
`--skip-deploys`, then applied with one `railway redeploy`, so the engine
restarted once rather than twice with a half-configured window in between.

**Postgres remains writable and is the fallback target** of the degrade path for
a full deploy cycle. Reverting the promotion is a variable change, not a
rollback: `INFERENCE_QUEUE=postgres` restores the previous path exactly, and the
queue-node binaries are compatible in both directions because the writer flip is
inert once nothing supplies a result.

## The promotion was ROLLED BACK the same day, on three defects (2026-08-13)

**Live value: `INFERENCE_QUEUE=postgres`.** Read from Railway, not assumed. The
user-facing path was never affected — Postgres is the configuration that had
been serving all along, which is the whole reason it is the degrade target.
Reverting really was a variable change, exactly as the section above predicted.

**What the flip verification could not have caught.** It was ONE request. A
single call cannot put two leases in flight, so it was *structurally incapable*
of exercising the concurrency path, no matter how carefully its logs were read.
Six real requests found the first defect within minutes. The lesson is not
"verify harder" — it is that a passing check has a shape, and the question to
ask of it is what that shape excludes.

### Defect 1 — propose-vs-apply skew on `lease()`

`earliest_leasable()` reads APPLIED state. Two concurrent `lease()` calls on the
leader therefore chose the SAME task and proposed it twice: the second call ran
before the first's `BrokerLease` had committed and applied, so the task was
still Pending as far as the chooser could see.

It showed as followers carrying `tick_errors: 5` and `dlq_depth: 2` while the
leader reported 0/0, with `broker_error=Corrupt` — the id-mismatch check firing
on replicas whose applied state no longer matched the leader's.

`enqueue()`, twenty lines above in the same file, had always compensated for
precisely this with a proposed-but-unapplied counter. `lease()` never did. Fixed
with `proposed_leases_unapplied_` and `earliest_leasable_excluding`, gated by
`ReplicatedBroker_ConcurrentLeases_DoNotProposeTheSameTask`.

### Defect 2 — mutate-then-validate, which bricked all three nodes

Underneath defect 1 sat a worse one. `apply_one` called the *choosing*
`broker_.lease()` and only then compared the returned id against the command's.
The mutation happened before the validation could reject it — so every retry of
an unapplicable entry consumed one more pending task, until the queue was empty
and the entry could never apply at all.

Fixed with `TaskBroker::lease_specific`, which leases the task it was NAMED and
is idempotent on an already-Leased one, gated by
`Broker_LeaseSpecific_IsIdempotent_AndConsumesNothingElse`.

### Defect 3 — a rejection is an answer, not a reason to retry forever

With defect 2 fixed and deployed to node 1, that node was **still** stuck:
`last_applied` 29285, `commit_index` past 29700, `tick_errors` climbing from
zero since its own restart. The fix was correct and insufficient, and the
difference is what matters here.

`fbcd2d63` had correctly stopped a failed apply from consuming the batch behind
it. But it made *every* apply failure stall — including failures that can never
resolve. A `BrokerLease` naming a task the local broker cannot lease (swept to
the DLQ, already terminal, never seen) is not a fault. It is the state machine's
ANSWER, computed from replicated state, identical on every replica, and
unchanged however many times it is retried.

**A committed entry is a FACT every replica must CONSUME at its index, but
whether it has an EFFECT is the state machine's business. The transition must be
TOTAL — it always advances; the answer may be "no".**

`is_deterministic_rejection` splits the two:

| class | errors | apply behaviour |
| --- | --- | --- |
| statement about the COMMAND, from replicated state | `UnknownTask`, `NotLeased`, `QueueEmpty`, `StaleFencingToken`, `PayloadTooLarge`, `InvalidArgument` | consumed, no effect, counted |
| statement about THIS REPLICA'S LOCAL STORAGE | `WalError`, decode failure / `Corrupt` | retried, stalls visibly |

The safety argument for the first row is that the rejection is a deterministic
function of (applied state, command): every replica computes the same answer
from the same bytes, so recording "no effect" converges exactly as recording an
effect does. The second row must NOT be recorded as a decision — writing a local
disk fault into replicated history is the worse error, and an operator has to
intervene either way.

Both directions are gated, and they are separate tests on purpose:
`ReplicatedBroker_DeterministicRejection_IsConsumedNotRetriedForever` asserts the
node advances past a well-formed but unapplicable `BrokerLease`, while the
existing `ReplicatedBroker_FailedApply_LeavesTheRestOfTheBatchPending` still
asserts a `0xEE` decode failure stalls. Mutation-checked: making `UnknownTask`
retryable fails the first at its `apply_committed` succeeds assertion —
reproducing the production stall — while the second keeps passing.

### Why this one was nearly unrecoverable

**The only exit from a permanently stalled node is wiping its volume.** That
destroys the evidence and leaves the defect in place to re-brick the rebuilt
cluster the first time it recurs — the identical trap as the io_uring outage,
arriving through the apply loop instead of the WAL.

And the diagnostic said one word. `broker_error=QueueError` covers eight
distinct `QueueError` values with completely different meanings; the log could
not distinguish "this command is invalid" from "my disk refused the write". That
is the four-layer error widening this document already records from 2026-08-12,
recurring in a different place, which is why `rejected_or_fault` now names the
specific error and the command tag.

### `apply_rejections` is a cross-replica probe, not a health number

`/statusz` now publishes `apply_rejections` and `last_apply_rejection` beside
`commit_index`. **Compare the count ACROSS replicas, not against zero.** A
declined entry is declined identically everywhere, so equal totals are the
expected steady state and a non-zero value is not a problem by itself. Unequal
totals mean the state machines disagree — the one thing agreement on
`last_applied` cannot rule out, because that number is Raft's cursor rather than
a count of anything the broker applied.

### Verified unstuck, and one residue that did NOT clear (2026-08-13)

All three nodes on the fix:

| check | node 1 | node 2 (leader) | node 3 |
| --- | --- | --- | --- |
| `last_applied` vs `commit_index` | 30111 / 30111 | 30111 / 30111 | 30111 / 30111 |
| `tick_errors` | 0 | 0 | 0 |
| `apply_rejections` | 6 | 6 | 6 |
| `last_apply_rejection` | `BrokerLease:NotLeased` | same | same |
| `dlq_depth` | **3** | **0** | **3** |

The gap closed from 29285-vs-29700 to zero, `tick_errors` went from tens of
thousands to none, and `last_apply_rejection` names exactly the predicted case: a
`BrokerLease` for a task that was neither Pending nor Leased. **`apply_rejections`
being equal at 6 on all three is the convergence evidence** — the counter exists
for that comparison and this is the first time it has been read.

**`dlq_depth` does NOT agree, and that is residual divergence from the incident
window rather than a new fault.** It is stable across repeated samples, so it is
not the `stats_interval` refresh cadence; no node has compacted this process
lifetime (`compactions: 0` on all three), so retention pruning does not explain
it; and the boot snapshot indices differ (29206 / 29197 / 29197).

The mechanism is the brick itself. Each node retried its unapplicable entry at
its own rate — `tick_errors` reached 118228, 88094 and 11821 — and each retry
consumed one more pending task through the mutate-then-validate path. Different
retry counts mean different numbers of tasks consumed, so the three local broker
WALs genuinely diverged while replaying the same Raft log. The residue persists
because **Raft repairs logs, not state machines**: two nodes that applied the
same log and reached different states only reconcile through an InstallSnapshot,
which a caught-up follower never receives.

Scope, stated precisely rather than waved away:

- It is confined to TERMINAL tasks. Dead tasks are not leasable and take no part
  in scheduling, so new work applies identically on all three — which is what
  the equal `apply_rejections` and zero `tick_errors` demonstrate.
- What it does affect is diagnostics: `dlq_depth` and `dlq_tasks()` answer
  differently depending on which node is asked, and a failover to node 1 or 3
  would make the reported depth jump 0 → 3.
- Postgres remains the system of record and this cluster remains a
  non-authoritative mirror, which is the reason the blast radius is this small.

**The repair is destructive and has not been performed.** Wiping one follower's
`/data` and letting it rejoin via InstallSnapshot is the designed mechanism for
a diverged node, and doing it one node at a time preserves quorum — but it
destroys that node's incident forensics, and nothing about the residue is
urgent. It needs an explicit decision, not a reflex.

### Re-promoted and verified under concurrent load (2026-08-13)

`INFERENCE_QUEUE=sgee` was set again with `--skip-deploys` and applied with one
`railway redeploy`. Cutover confirmed before anything downstream was trusted: 6
`model is LOADED` (3 replicas × 2 assistants) and 6 `INFERENCE_QUEUE=sgee`
(3 mortgage + 3 strategy), all timestamped after the upload.

Then **24 concurrent `ParseOperation` calls** through the live ingress with the
partner key — 8, then 16 — because concurrency is the one thing the original
verification could not reach.

| gate | result |
| --- | --- |
| HTTP | 24 / 24 → 200 (8 in 8.5 s, 16 in 17.7 s) |
| `last_applied` vs `commit_index` | equal on all three, gap 0 throughout |
| `tick_errors` | 0 / 0 / 0 |
| `apply_rejections` | 6 / 6 / 6 — equal AND unchanged under load |
| `current_term` | 3409, stable — no election churn |
| engine `[WARN]` / `[ERROR]` | **0 / 0** |

**`apply_rejections` staying equal and unchanged is the check that would have
caught defect 1.** Concurrent leases proposing the same task produced id
mismatches and rejections; 24 concurrent requests producing none is a direct
test of that path, not an inference from the absence of a crash.

**Zero `[WARN]` is the negative proof, and it must be checked on that
predicate.** A degraded request returns the same answer as a cluster-served one,
so the response cannot distinguish them — but every fallback branch in
`SgeeAdmission` calls `logger::Logger::getInstance().warn()`, so a zero warn
count is the statement. Do NOT grep for hand-picked phrases: a first pass
matched nine lines that were all benign — six were the boot INFO announcing that
a fallback *exists*, and three were Envoy's startup header-map dump, which
contains `x-envoy-degraded` as a header NAME. Count the log LEVEL, not words.

Postgres remains the system of record and the degrade target. The `dlq_depth`
residue described above is unchanged (3 / 0 / 3) and unaffected by the load,
which is consistent with it being inert terminal state.

**That 3 / 0 / 3 reading is superseded — see the next section.** It was measured
from `dlq_depth` alone, which was the only instrument available at the time, and
it is wrong: the real spread is 3 / 0 / **20**, and all three nodes differ rather
than two agreeing. The conclusion that the residue is inert survives; the numbers
do not.

## The divergence is CONFIRMED, and `dlq_depth` understated it (2026-08-13)

The line above records the residue as "3 / 0 / 3". That was the best reading
available from `dlq_depth` alone, and it was **wrong in both directions**: the
nodes do not agree two-to-one, and the odd node out is not off by three.

`/statusz` now publishes two digests, and their first live use settled it. All
three nodes on the post-sweep binary, **term 3449 held stable across a five
minute window**, `last_applied == commit_index == 31395` on every node, `gap 0`,
`tick_errors 0`, `apply_id_mismatches 0`:

| node | `state_digest` | `dlq_depth` | `apply_rejections` | `sweep_successes` |
| --- | --- | --- | --- | --- |
| 1 (leader) | `af5223191117d220` | 3 | 0 | 116 |
| 2 | `66983fc9fa91eed6` | 0 | 47 | 0 |
| 3 | `0ca1841cfd64a890` | **20** | 21 | 0 |

**Three nodes, three different state machines, in perfect Raft agreement.**
That combination is the entire reason the digest exists. `last_applied` is
Raft's cursor — a count of log entries consumed — not a statement about what the
broker did with them, so three nodes agreeing on it rules out nothing. The
cluster is not stalled, not lagging, and not erroring; it is simply not the same
cluster on all three nodes.

Four properties make this a finding rather than a measurement artifact:

- **The digests are STABLE.** Each node reported the same `state_digest` across
  four probes spanning indices 31321 → 31395. A digest racing the
  `stats_interval` refresh would wander; these do not.
- **`state_digest` excludes every per-replica field** — `fencing_token` (a
  locally minted WAL LSN), `visibility_deadline_ms` (a local clock), and
  `lease_owner`. It hashes replicated task CONTENT in sorted-id order. A
  difference is therefore divergence, not a locality artifact. This exclusion is
  load-bearing: a digest that included those fields would fire constantly, be
  disbelieved, and get switched off.
- **It is NOT growing.** `apply_rejections` held at 47 and 21 across separate
  samples minutes apart. No new divergence is being created; the sweep's fixes
  hold. What remains is historical residue from the defective period.
- **Only the leader sweeps** (116 / 0 / 0), which confirms lease expiry is
  leader-proposed and replicated rather than independently computed per node —
  so clock skew is not the explanation.

The followers' `last_apply_rejection` is `BrokerLease:NotLeased` on both: the
leader proposes a lease for a task those nodes cannot lease, they reject it
deterministically, and they CONSUME it. That is this session's apply-totality
fix behaving exactly as designed — and it is the only reason the cluster still
makes progress while divergent. Before that fix these entries retried forever
and froze `last_applied`.

**Raft cannot heal this and will not try.** `InstallSnapshot` is sent only to a
follower whose `nextIndex <= lastIncludedIndex_` (`raft.cppm:1042-1051`). These
followers are fully caught up, so they will never be sent one, and every future
entry applies identically on top of three different base states. Divergence in a
replicated state machine is permanent unless something outside Raft replaces the
state.

### Evidence was preserved BEFORE any repair was considered

Each node's `broker.wal.snap` and `broker.wal` were copied off and checksummed:

| node | snapshot | WAL | sha256(snapshot), first 16 |
| --- | --- | --- | --- |
| 1 | 26531 B | 3121 B | `4825c1f05f973fcd` |
| 2 | 28355 B | 24 B | `b87b1476b2dd3aa5` |
| 3 | 32323 B | 7310 B | `076fd1d74b876c2b` |

Three distinct snapshots, three distinct hashes — independent corroboration of
the digests, from the bytes on disk rather than from the process reporting on
itself. This repo has twice recorded wiping a volume as the move that "fixes"
the symptom and destroys the only evidence (the io_uring outage, and the
stalled-apply outage). The snapshots are also subject to compaction, so this
evidence self-erases; it was captured while it existed.

### The repair is destructive, feasible, and NOT urgent

Rebuilding a follower means deleting its **whole `/data` volume** — the whole
volume or nothing, since removing only the broker WAL leaves a Raft log that
claims state the broker no longer has. On reboot the node re-stamps `node.id`
from `SGEE_NODE_ID` (`node_config.cppm:331` verifies-or-stamps; identity comes
from the environment, so a wipe does NOT mint a fourth member), finds no log,
and the leader ships it an `InstallSnapshot`.

Two preconditions, both checked:

- **Snapshot size against gRPC's ~4 MiB default max message.** At 26–32 KB there
  are three orders of magnitude of headroom. A snapshot over that ceiling would
  fail the transfer and leave the node unable to rejoin at all — strictly worse
  than the divergence.
- **A stable leader for the duration.** Term 3449 held across the window. Wiping
  one follower at a time keeps two nodes up, so quorum survives; wiping two at
  once loses it.

It converges on the LEADER's state, which is a choice, not a truth — node 1's
broker state is one of three, and it is canonical only because it is the one
Raft will propagate. Postgres remains the system of record, so nothing
user-visible depends on which of the three is preserved.

**It is not urgent.** The divergent entries are terminal dead tasks, the
rejection counters are static, and new work applies identically on all three.
Leaving it is a defensible choice; it is recorded here so the next reader does
not rediscover it as a fresh alarm.

### Re-verified after the sweep fixes, under load (2026-08-13)

Both the queue nodes and the engine were redeployed with the multi-agent sweep's
fixes, then gated on the 24-concurrent shape rather than a single call.

Engine cutover confirmed the way a cutover has to be — not by Railway's SUCCESS,
which is not evidence the new image is serving: **6 `model is LOADED` lines**
(3 replicas x 2 assistants) timestamped after the upload, and 6 replicas logging
`INFERENCE_QUEUE=sgee`.

| check | result |
| --- | --- |
| 24 concurrent `ParseOperation` (8 then 16) | **24 / 24 HTTP 200** |
| engine `[WARN]` / `[ERROR]` | **0 / 0** |
| `last_applied == commit_index` | all three, gap 0 |
| `tick_errors` | 0 on all three |
| `apply_id_mismatches` | 0 on all three |
| `apply_rejections` before / after | 0 / 47 / 21, **unchanged** |
| term across the window | 3449, stable — no election churn |

**`apply_rejections` being unchanged is the load-bearing row.** Equal is not the
claim — these three are unequal, and that is the known residue. The claim is
that 24 concurrent requests added NONE, which is a direct test of the
propose-vs-apply skew: two concurrent `lease()` calls picking the same task
produced exactly these rejections before `proposed_leases_unapplied_` existed.

**Zero `[WARN]` is the negative proof and cannot be replaced by reading the
responses.** A degraded request returns the same answer as a cluster-served one.
Every fallback branch in `SgeeAdmission` warns, so the count of that log LEVEL is
the statement — not a grep for chosen phrases, which has already matched nine
benign lines once.

Two production behaviours were confirmed incidentally, both previously fixed
here and neither verified live until now:

- *"Compute the future value of 1000 at 5% for 10 years"* parsed correctly
  (`ComputeFutureValue`, rate 0.05, periods 10). That is the annual-rate /
  annual-periods pair the grounding gate used to refuse with a message that
  contradicted itself.
- `payment` came back as `0` on lump-sum questions rather than being refused —
  the convention value added alongside `future_value = 0`.

The refusals and clarifications in the remainder are expected: the mortgage
model measures 27.8% params exact-match through the real RPC, and honest
refusals are the designed behaviour, not a failure of the queue.

## RETRACTION: the "confirmed divergence" above was the INSTRUMENT (2026-08-13)

**The two sections above are wrong and are kept for the reasoning trail.** The
divergence they report as confirmed is not established, and the digest that
"confirmed" it was measuring per-replica state.

What broke the conclusion open was a result that could not be true. Node 2 was
rebuilt from **empty** — `/data` wiped, container restarted, rejoined via
`InstallSnapshot` from the leader — and its `dlq_depth` moved 0 -> 3, matching
the leader exactly. Yet at an **identical `last_applied` (32032, 32033, sampled
concurrently rather than sequentially)** its `state_digest` still differed from
node 1's. A replica whose entire state came from the leader's own snapshot cannot
genuinely disagree with the leader. Something in the digest was not a function of
replicated state.

It was the terminal tasks. `serialize_snapshot` keeps a Completed or Dead task
only while `retained(id, now_ms, retention_ms)` holds, `evict()` reclaims the
rest, and `replicated_queue_runtime_driver.cppm:77` states plainly that
**`compact_log()` is LOCAL and runs on every node regardless of leadership**.
Each replica therefore drops terminal tasks against its own clock at its own
compaction moment. Two healthy replicas hold different terminal sets **by
design**, and the digest folded all of them.

Three consequences, and the middle one invalidates a lot of earlier reasoning:

- **`state_digest` now hashes only the LIVE set (Pending + Leased).** Fixed and
  gated by `StateDigest_IsInsensitiveToLocalRetention`, mutation-checked:
  restoring the fold over every task reproduces the production symptom exactly.
  `StateDigest_TracksLiveTaskContent` sits beside it, because insensitivity
  alone is satisfied by a function returning a constant.
- **`dlq_depth` is not a divergence signal and never was.** It counts a locally
  evicted set. Every reading of it in this document — 3 / 0 / 3, 3 / 0 / 20 —
  says nothing about whether the state machines agree. The original incident's
  `dlq_depth` difference, called out earlier as the one number that exposed a
  real divergence "by luck", may well have been retention timing too.
- **`apply_rejections` survives as a real signal.** The followers'
  `BrokerLease:NotLeased` concerns LIVE tasks — the leader proposing a lease for
  a task a follower cannot lease — which local retention cannot explain.

The node 2 rebuild was NOT wasted: it is what produced the impossible reading,
and it is the reason the instrument was caught rather than trusted. Node 3 was
not rebuilt, and should not be until the corrected digest has been deployed and
read.

**The preserved snapshots keep their value.** Three distinct hashes remain the
on-disk record of what each node held on 2026-08-13, and they can now be
re-examined against a digest that means something.

The general lesson is the one this file keeps relearning, arriving from a new
direction each time: **check which layer computes a number before believing what
it implies.** `last_applied` is Raft's cursor, not the state machine's. Railway's
SUCCESS is the healthcheck, not the process. The LIVE badge is a timestamp, not a
status. And `state_digest`, as first written, was local retention timing wearing
the costume of replicated state.
