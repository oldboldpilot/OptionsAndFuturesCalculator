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

## Before this is promoted to authoritative

- Fix the post-failover lease stall (`WalError: TimedOut`).
- Fix, or document a consumer contract for, the timed-out-lease-hides-a-task
  hazard.
- TLS/mTLS on both ports. Today the consensus port is authenticated by a shared
  token over **plaintext**, and the client queue port is **not authenticated at
  all** — `TaskQueueService`'s constructor takes no token. Railway's private
  network is a boundary, not an authentication: a co-tenant container is on the
  other side of it.
- Run the audit against the deployed Railway cluster, not only locally. Every
  number above is from three local processes on loopback.

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

Measured the same day, and NOT yet explained:

| service | volume | used |
| --- | --- | --- |
| sgee-queue-1 | sgee-queue-1-volume → /data | 870 MB |
| sgee-queue-2 | sgee-queue-2-volume → /data | 852 MB |
| sgee-queue-3 | sgee-queue-3-volume → /data | **8 MB** |

Node 3 is not crash-looping — its log shows one boot banner, `self=3`,
`data_dir=/data`, and all four subsystems started. The likeliest explanation is
simply that it holds a shorter history than 1 and 2: a volume created later, so
its WAL begins where it joined rather than at the cluster's first entry.

Two things about it are open, and both are recorded rather than assumed:

1. **Node 3's own `last_applied` has not been read.** Only `sgee-queue-1` has a
   public domain (port 8080, `/statusz`), so 2 and 3 cannot be probed from
   outside the private network. Whether node 3 is genuinely caught up is
   therefore unverified — the small volume is *consistent with* a late join and
   *also* consistent with a node that is not replicating. Do not treat the
   benign reading as established.
2. **870 MB is a lot for 4,218 applied entries** — roughly 200 KB each, for a
   queue whose payloads are small. `sgee-queue-1` reports `compactions: 1` with
   `snapshot_index 4030` against `last_applied 4218`, so compaction is running
   but has run once. Whether snapshotting actually reclaims WAL bytes, or only
   advances the index while the file keeps growing, is the question to ask
   first; on a 50 GB volume there is time to ask it properly.

Node 1 at the same moment: `running: true`, `is_leader: false`,
`leader_hint: 2`, `current_term: 1922`, `tick_errors: 0`, `ticks_run: 524151`.
The term has moved from 1919 to 1922 since the leadership check earlier that
day — three elections, not zero, which is worth knowing when reading "stable".
