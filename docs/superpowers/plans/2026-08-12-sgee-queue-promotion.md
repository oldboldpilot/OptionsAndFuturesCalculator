# SGEE queue promotion — implementation plan

@author Olumuyiwa Oluwasanmi

**Goal:** make the SGEE queue cluster the system of record for the inference
queue, replacing Postgres `inference_jobs`.

**Status of the prerequisites, as of 2026-08-12:**

| precondition | state |
| --- | --- |
| leader held long enough to complete a drain | met (tuned 1500/300 timing) |
| post-failover lease stall | fixed |
| timed-out-lease hides a task | fixed |
| audit against the deployed cluster | done |
| apply path drops committed entries | fixed, gated, deployed (`fbcd2d63`) |
| replica state machines agree | proven identical after rebuild |
| mutual TLS on both ports | done (`04ebc42`) |
| **an SGEE-backed admission path** | **not started — this document** |

---

## The gap, precisely

`PostgresAdmission` / `PostgresLeaseSource` (`inference_admission.cppm`) work
because the Postgres queue carries a **result**: `inference_jobs` has `result`
and `error` columns, `Queue::complete(job_id, token, result_json)` stores one,
and `Queue::await_result(job_id, …)` blocks a submitter until it appears.

The SGEE task queue carries none of that:

- `CompleteRequest` is `{task_id, fencing_token}` — no result.
- `Task` has no result field.
- There is **no RPC to read a task back at all** — no `GetTask`, no await.

So a submitter on replica A cannot learn the outcome of a job executed on
replica B. That is the entire reason `PostgresAdmission` exists, and it is why
promotion is a protocol change rather than a configuration flip.

**`SgeeQueueClient` is mirror-only** (`enqueue_mirror`, best-effort, dropped on a
full ring or an open breaker) and is not a starting point for a path that must
return an answer to a blocked gRPC handler.

## Design

Smallest change that is actually complete:

1. `bytes result` on `CompleteRequest`, `bytes error` on `FailRequest`.
2. `bytes result` / `bytes error` on `Task`, so a reader sees the outcome.
3. A `GetTask(task_id)` RPC, so a submitter can poll for terminal state.
   Polling, not streaming: the existing Postgres path already polls with a
   `pg_notify` wakeup hint that is explicitly *not* load-bearing for
   correctness, and matching that keeps the failure modes identical.

Then, engine side, `SgeeAdmission` / `SgeeLeaseSource` mirroring the Postgres
pair one-for-one, selected by `INFERENCE_QUEUE=sgee`, with the same
**degrade-never-hang** contract: any failure of the SGEE path falls back to
calling the wrapped local backend directly.

## What makes this risky, and the rules that follow

The result has to survive a leader change, which means it must be **replicated
and persisted** — it cannot live in a side table. That drags it through three
formats that are load-bearing:

- **`BrokerComplete`** (`broker_command_codec.cppm`) — a *replicated command*.
  Every replica decodes it. A node running old code against a new command is
  the `ConsensusError::Corrupt` path, which — before `fbcd2d63` — silently
  dropped the rest of the batch. It no longer does, but it will now **stall the
  node**, which is a rolling-deploy outage if the format changes without a
  version gate.
- **`TqEventType` / `codec.cppm`** — the WAL encoding. An old binary reading a
  new WAL must refuse, not misparse.
- **`InMemoryIndex::serialize_snapshot`** — entries are `kEntryFixedBytes = 62`
  plus one variable-length payload. A second variable-length field changes the
  layout, so `kIndexSnapshotVersion` must bump **and v1 must still decode**, or
  every existing volume is unreadable.

**Rules for this work, all learned the expensive way in this repo:**

1. **Version every format before changing it.** Write the v1-compat read path in
   the same commit as the v2 write path, never after.
2. **A replicated command format change is a two-phase deploy.** Every node must
   be able to *read* the new command before any node *writes* one. Phase 1 ships
   readers; phase 2 turns writing on. Skipping phase 1 stalls the cluster
   mid-roll.
3. **Never let an unknown field decode to a plausible value.** The 62-byte fixed
   entry has no room for a silent addition; a misread length is
   indistinguishable from real data.
4. **Gate each stage before the next.** `QueueNodeDurabilityTest` must pass on a
   mixed-version cluster, not only a uniform one.

## Stages

Each stage is independently testable and independently deployable.

**Stage 1 — formats, readers first. COMPLETE as of 2026-08-12, in two commits.**
Teach every reader both layouts and write nothing new. All three formats now
read v1 and v2:

| format | reader | commit |
| --- | --- | --- |
| index snapshot (`decode_entry`) | `kIndexSnapshotVersionMax = 2`, writes 1 | `ca8f0a16` |
| WAL `TaskCompleted` (`decode_completed`) | 8 bytes = v1, 12+len = v2 | this stage's second commit |
| replicated `BrokerComplete` (`decode`) | 16 bytes = v1, 20+len = v2 | this stage's second commit |

**The first commit claimed the whole stage and delivered half of it** — it
touched `index.cppm`, `types.cppm` and one test, leaving `codec.cppm` and
`broker_command_codec.cppm` untouched. That is worth recording rather than
quietly fixing, because the missing half is the dangerous half: the snapshot
reader is the one format that already had a version field, and the two that did
not are the two where a wrong guess is unrecoverable.

Note the shape each reader had to adopt, since there is no version byte in
either wire format — the SIZE is the discriminator, and there is a dead zone
between the two layouts (9..11 bytes for the WAL record, 17..19 for the command
body) which must be REFUSED rather than read as a short v2. A length assembled
from whatever bytes happen to follow is indistinguishable from a real one.

An empty result is deliberately encoded as the v1 shape, so an empty result and
a v1 record are the same bytes: the same fact, written the same way, still
readable by a binary that predates v2.

Gates: `BrokerCommandCodecTests` (**new — this format had no test at all**, 7
cases) and four new cases in `TaskQueueCodecTests`, plus the four Stage-1
snapshot cases in `TaskQueueCompactionTests`. Mutation-checked: restoring the
old `body.size() != 16` check fails exactly `BrokerCmd_CompleteV2_DecodesTheResult`
while the v1 compatibility case still passes.

**This stage must now be DEPLOYED to all three nodes before Stage 2 starts.**
That is not a formality: since `fbcd2d63` a frame a node cannot decode is no
longer silently dropped — `apply_committed` stops without marking it, so
`last_applied` freezes and the node retries the same entry forever. A
writer-first rollout is a stalled node per un-upgraded replica, not a degraded
window.

**Stage 1 was deployed to all three nodes on 2026-08-12**, and the deploy is
what found two defects in the queue-node image, both of which had been latent
since the `import std;` conversion and neither of which named its own cause:

- `Dockerfile.queue-node` passed `-DCMAKE_CXX_COMPILER=clang++`. sensen locates
  the libc++ std module at
  `dirname(dirname(CMAKE_CXX_COMPILER))/share/libc++/v1/std.cppm`, so the
  `/usr/bin/clang++` symlink — which points at exactly the right compiler —
  resolved it to a `/usr/share` path that does not exist. `std_module_precompile`
  was never declared and every `import std;` failed. `backend/Dockerfile` had
  been fixed for this in `bd11d00`; this file was missed.
- With that fixed the build reached the LINK and failed on
  `__cxxabiv1::__vmi_class_type_info`'s vtable, reported against
  `libstdc++.so.6` — which reads as a standard-library mix-up and is not one. A
  from-source libc++ installs `libc++.so` as a linker script,
  `INPUT(libc++.so.1 -lc++abi -lunwind)`; apt.llvm.org ships a plain symlink, so
  libc++abi never reached the link and `ld` reported the only C++ runtime that
  was reachable. Fixed with an explicit `-lc++abi`.

The deploy script's own diagnostic was wrong in both directions and was fixed
alongside: it printed `railway logs --service` output, which replays a dead
session's scrollback (the SSL errors it showed were timestamped three hours
before the deployment), and it called every failure a healthcheck failure. It
now reads the newest deployment's `imageDigest` — absent means no image was
produced, so no container ran and it is a BUILD failure — and reads logs by
deployment id.

Cluster verified after the roll: one leader, all three at
`last_applied == commit_index == 25734`, `tick_errors: 0`, `dlq_depth: 0`.

**Stage 2 — writers. COMPLETE.** `TaskBroker::complete(id, token, result)`
persists it; `BrokerComplete` carries it; `apply_one` applies it;
`kIndexSnapshotVersion` 1 → 2.

The version bump and `encode_entry`'s result append are ONE commit, and the
mutation check shows why: reverting the bump alone fails three tests, two of
them pre-existing round-trips, because a v1 header over v2 entries mis-parses
from the second entry onward — the reader stops after the payload and reads the
result's length prefix as the next entry's task id.

**The size bound was wrong and this stage's own test caught it.** Both
`task_queue::kMaxPayloadBytes` and `persistence::kMaxPayloadBytes` are 64 MiB,
but that names the size of the ENCODED RECORD, and the record prefixes 12 bytes
(WAL) or 21 (replicated command). A result at exactly the old bound was accepted
at the write and refused at the append — the precise hazard the bound exists to
prevent. The comment on `kMaxPayloadBytes` had claimed enqueue "can never
produce a frame the codec would reject", which was false for the same reason
(`encode_enqueued` prefixes 17 bytes), so the latent defect predates results
entirely. Now `kMaxUserBytes = kMaxPayloadBytes - 32`, one rule for both fields.

**Stage 3 — RPC surface. COMPLETE.** `bytes result` on `Task` and
`CompleteRequest`; `GetTask`. The client gained `get_task` returning
`expected<optional<Task>, ClientStatus>` — "the server says no such task" and
"the call did not complete" are deliberately distinct, because a submitter must
retry the second and stop on the first.

**There is no `error` field, and that is a decision, not an omission.** The plan
above called for one. Carrying an error string would mean widening `BrokerFail`
— a fixed 16-byte replicated command with no second shape any deployed node can
read — plus the WAL's `Failed` and `MovedToDlq` payloads and a snapshot v3: a
second full readers-first deploy cycle, for a diagnostic the worker already
logs. A failure is instead signalled by the task reaching terminal state `Dead`,
which a reader already sees in `state`. A proto field the replicated log
silently dropped would be worse than no field — it would read as an answer.

One shared-code note: `detail::task_from_proto` now exists because the
proto→native conversion was open-coded inside `lease()`. A hand-copied field
list is how a field gets added to the wire and quietly dropped coming back —
nothing fails, it just returns a default. `result` was the first field to make
that concrete.

**Stage 4 — engine admission path. COMPLETE.** `SgeeAdmission` /
`SgeeLeaseSource`, selected by `INFERENCE_QUEUE=sgee`, degrade-never-hang on
every failure, with the `set_lease_source()`-before-`start()` ordering
`QueuedBackend` documents.

`set_lease_source()` and `lease_source_` were typed on the CONCRETE
`PostgresLeaseSource`, which made the storage substrate part of the decode
loop's type rather than a deployment choice; an abstract `LeaseSource` with one
virtual `fill()` is what made a second source possible at all. The two
implementations are structurally identical because the constraint that shaped
the first — lease on the owner thread, never block there, hand the write-back to
a helper — belongs to the decode loop, not to Postgres.

`SgeeAdmission` POLLS `GetTask` on a 25 ms interval rather than awaiting,
because SGEE has no await. That is the same shape the Postgres path degrades to
whenever its `pg_notify` hint does not arrive — and `inference_queue.cppm` is
explicit that the hint is never load-bearing for correctness — so the two share
failure modes rather than introducing a second set. A poll that does not
complete retries until the deadline; a poll answering "not found" degrades
immediately, because retention makes a reclaimed task and one that never existed
the same reply.

Gate: `test_inference_admission` asserts an unreachable cluster still yields the
LOCAL backend's own answer AND yields it promptly — a path that degrades only
after holding a gRPC handler for ninety seconds satisfies the letter of
"degrade" and none of the point.

**Stage 5 — promotion. COMPLETE, 2026-08-13.** `INFERENCE_QUEUE=sgee` is live on
the engine. Postgres stays writable as the degrade target.

Order, and each step's evidence:

| step | evidence |
| --- | --- |
| Stage 2/3 rolled to all three nodes | three SUCCESS deployments, one at a time |
| cluster uniform on the writer binary | `last_applied == commit_index == 28981`, one leader, `tick_errors: 0` |
| engine deployed with Stage 3/4 | 6 `model is LOADED`, timestamped after the upload |
| `SGEE_PEERS` + `INFERENCE_QUEUE` set | one `--skip-deploys` call, then one `railway redeploy` |
| flip took effect | 3 mortgage + 3 strategy logging `INFERENCE_QUEUE=sgee`, no fallback warning |
| a real request served through it | `ParseOperation` via the live ingress, partner key, HTTP 200 in 2.18 s |

**Both variables were set with `--skip-deploys` and applied with a single
redeploy.** Setting them one at a time would restart the engine into a
half-configured window — `INFERENCE_QUEUE=sgee` with no `SGEE_PEERS` degrades
every request to local, which is safe but is a silent outage of the thing being
promoted.

**The end-to-end check is a NEGATIVE one, and that is the point.** A request that
degraded to the local backend returns the same answer as one served by the
cluster; the response cannot distinguish them. Every fallback branch in
`SgeeAdmission` logs — submit failed, task not found, deadline passed — so the
proof is **zero** of those lines in the window, not the 200. Nor is a rising
`last_applied` proof: the sweep ticker advances it continuously whether or not
anything was enqueued.

The request itself was refused by the mortgage verification layer —
`"rate" = 0.004167 does not correspond to anything in the request (the nearest
figure you gave is 0.5)` — because the model divided a stated PER-MONTH rate by
twelve. That is the grounding gate working, and it is a good outcome for this
check: the assistant ran, produced `raw model output`, and the verification layer
adjudicated it. Nothing about the queue path was involved in the refusal.

**Reverting is a variable change, not a rollback.** `INFERENCE_QUEUE=postgres`
restores the previous path exactly, and the queue-node binaries stay compatible
in both directions because the writer flip is inert once nothing supplies a
result — an empty result is encoded as the v1 shape.

### Still open

- **Remove Postgres.** Explicitly out of scope here and still is: it remains the
  fallback target of the degrade path. Removing it needs its own decision after
  a full deploy cycle of observation.
- **Nothing yet exercises a non-empty result end to end in production.** The
  admission path writes one on every completion, so the first real inference
  served by a REMOTE replica will be the first v2 `BrokerComplete` the cluster
  has ever replicated. The formats are gated by tests and the readers have been
  deployed since Stage 1, but the production first-write is unobserved. Watch
  `tick_errors` on all three after the first cross-replica completion.
- **`QueueNodeDurabilityTest` does not yet assert a result across a leader
  SIGKILL.** It asserts tasks are neither lost nor duplicated; the result field
  rides along untested at that level. The unit gates cover encode/decode/replay,
  which is why this was not treated as blocking, but it is the honest gap.

## Explicitly not in scope

- Removing Postgres. It stays as the fallback target of the degrade path.
- Per-node certificates. One node cert with SANs is deployed and node identity
  is already fenced by `SGEE_NODE_ID` + `node.id`.
