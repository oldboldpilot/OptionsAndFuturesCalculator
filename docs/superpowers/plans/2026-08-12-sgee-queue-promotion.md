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

**Stage 2 — writers.** `TaskBroker::complete(id, token, result)` persists it;
`BrokerComplete` carries it; `apply_one` applies it. Gate:
`QueueNodeDurabilityTest` with results asserted across a leader SIGKILL, plus a
mixed-version run against a stage-1 node.

**Stage 3 — RPC surface.** `result`/`error` on `CompleteRequest`/`FailRequest`
and `Task`; `GetTask`. Gate: an end-to-end enqueue → lease → complete-with-result
→ GetTask through the real service.

**Stage 4 — engine admission path.** `SgeeAdmission` / `SgeeLeaseSource`,
`INFERENCE_QUEUE=sgee`, degrade-never-hang on every failure, with the ordering
contract `set_lease_source()`-before-`start()` that `QueuedBackend` documents.
Gate: the `inference_admission` tests, extended, plus a real assistant round trip.

**Stage 5 — promotion.** Flip `INFERENCE_QUEUE` to `sgee` in production, watch
`last_applied`/`commit_index`, and keep Postgres writable as a fallback for one
full deploy cycle before removing it.

## Explicitly not in scope

- Removing Postgres. It stays as the fallback target of the degrade path.
- Per-node certificates. One node cert with SANs is deployed and node identity
  is already fenced by `SGEE_NODE_ID` + `node.id`.
