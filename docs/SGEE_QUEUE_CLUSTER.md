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

- Fix the post-failover lease stall (`WalError: TimedOut`).
- Fix, or document a consumer contract for, the timed-out-lease-hides-a-task
  hazard.
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
