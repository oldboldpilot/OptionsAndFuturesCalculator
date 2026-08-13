# Session log — 2026-08-13 — the id-skew root cause, and an intra-term lease fence

@author Olumuyiwa Oluwasanmi

Two defects closed in the SGEE queue cluster, plus an upstream merge. Everything
below was measured; where a prediction turned out wrong it is kept next to what
replaced it.

## 1. The convergence check finally produced a clean reading

Every earlier attempt compared digests taken at different indices, because the
probe's own round trip took longer than the cluster took to advance. This one
landed with all three nodes at `last_applied == commit_index == 36753` and
`state_digest_index == 36753`:

| | node 1 (leader) | node 2 | node 3 |
| --- | --- | --- | --- |
| `state_digest` | `0000000000000000` | `8451a4fef5895231` | `4e09adfe17a9e471` |
| `live_tasks` | 0 | 16 | 93 |
| `apply_id_mismatches` | 128 | 256 | 128 |
| `last_apply_rejection` | `BrokerEnqueue:IdMismatch` | `BrokerEnqueue:IdMismatch` | `BrokerComplete:UnknownTask` |

Three different live sets at one identical applied index. Unlike the reading
retracted the day before, this used the retention-corrected digest — live set
only — so locally-evicted terminal tasks cannot explain it.

The leader had drained to zero while the followers held 16 and 93 tasks that
would never complete. That is the downstream shape of an id skew: a
`BrokerComplete` naming the leader's id finds nothing under that id on the
follower, so the follower's own copy stays Pending forever.

## 2. Root cause (defect 5): a prediction derived from a local counter

`enqueue()` predicted `broker_.next_task_id() + proposed_enqueues_unapplied_`.
`next_task_id()` reflects only APPLIED state; the counter covered only this
leader's own proposals since the last term change. Neither covers an enqueue
entry that is already in the log, will certainly apply, and was proposed by
somebody else — a new leader's inheritance. Nothing gates `enqueue()` on
`last_applied == commit_index`.

### How it was confirmed

Not by reading. Node 1's deployment log:

```
Raft baseline: term=3550 is_leader=false leader_hint=none last_applied=34298
Raft change: term 3550 -> 3553 (3 elections), last_applied=34298
Raft change: term 3553 -> 3555 (2 elections), last_applied=34298
Raft change: term 3555 -> 3557 (2 elections), last_applied=34298
Raft change: term 3557 -> 3559 (2 elections), last_applied=34298
... 16x apply BrokerLease DECLINED: NotLeased ...
DIVERGENCE: applied BrokerEnqueue as id 538 but the leader logged id 426 (total 1)
```

Nine elections with `last_applied` frozen, then a drain, then the mismatch at
**+112 on the very first occurrence** and on all 128 that followed. 112 is the
leader's unapplied enqueue backlog at the moment it predicted. Node 1 was a
FOLLOWER at the time, which makes it cleaner: the leader predicted from its own
lagging applied counter while node 1, more caught up, assigned the higher id.

### Two hypotheses checked and eliminated first

- **The in-code comment's own explanation.** It blamed a rolling deploy of
  mismatched payload bounds — real, since fixed, but not this. The only
  pre-reserve bound is `payload.size() > kMaxUserBytes`, a constant applied to
  bytes carried in the command, so every replica computes it identically.
- **Snapshot restore lowering the counter.** `next_task_id_` is written
  explicitly at `index.cppm:358`, read back at `:415`, and thereafter only ever
  raised by `max(next_task_id_, t.id + 1)`. A rebuild cannot lower it.

### The fix

`pending_enqueues_in_log()` counts enqueue entries above `last_applied`, reading
ONE BYTE each — the command tag is the first byte of the body — over a window
that is single digits in steady state. The counter and the term-change reset are
both deleted: a truncated proposal leaves the log, so a derived quantity is right
by construction where a cached one had to be invalidated by hand.

Gated by `ReplicatedBroker_NewLeaderPredictsPastTheEnqueuesItInherited`.
`round_without_apply()` is load-bearing twice in it — committing the backlog
without applying, and electing the successor without applying — because `run()`
or `run_until_leader()` would drain the backlog and the test would pass against
the bug. Mutation-checked: returning 0 fails it with `Expected 4, got 1`, the
production signature exactly.

**It stops NEW skew and reconciles nothing.** An already-diverged replica still
needs its volume wiped and rebuilt.

## 3. The intra-term lease fence (the KNOWN GAP)

A lease changes hands inside one term when a sweep expires it, so the term fence
alone does not cover a delayed worker completing with a still-current term — and
the propose path forwarded `t->fencing_token`, laundering the stale completion
under the new owner's identity.

Fixed by comparing `DistributedFencingToken::raft_index` against a new
`Task::lease_raft_index`, on the PROPOSE path, so a stale completion never
enters the log. **No replicated command needed widening**: the apply loop
already has the committed entry's own index in scope, so `apply_one(*cmd,
e.index)` stamps it.

Shipped readers-first in two commits, per the same rule as the lease timestamp:
Stage 1 accepts both shapes and writes neither new one; Stage 2 flips the
writers. The 29..35 and equivalent dead zones are refused rather than salvaged.

The rejected fix is recorded in place: comparing `token.local_token` against
`t->fencing_token` breaks the good path, because `local_token` is a per-replica
WAL LSN by design. `QueueNodeDurabilityTest` is the canary and passes at both
stages.

## 4. Upstream merge

`cf55b754` added `State`/`PutResult`/`GetResult` and a C-ABI gRPC shim. Both
sides had appended to the same two files, so git conflicted on placement, not
meaning; both sets are kept.

One resolution was not mechanical: the incoming per-RPC deadline defaults to 0,
meaning no deadline, which is the unbounded hang the degrade-never-hang contract
exists to prevent. This side's 20 s default is kept, and the incoming reasoning
is preserved as the LOWER bound on that value — the deadline must exceed the
server's lease await budget or a slow-but-successful Lease reads as a transport
failure.

`GetTask` and `GetResult` overlap in purpose and not in durability: `GetTask`
reads the REPLICATED task, `GetResult` reads a service-layer in-memory store
that does not survive a restart. Both service definitions now say so.

## 5. A correction worth keeping

Node 1's "rebuild" earlier in the day was a REDEPLOY onto its existing volume,
not a volume wipe — its boot line reads `term=3550 ... last_applied=34298`,
which is restored state. It fixed the writeback failures and did not reset the
counter, so the +112 survived it. When the real wipe happens, the boot baseline
is the discriminator; deploy status is not.

## 6. Deploy, wipe, and the convergence proof

Deployed readers-first: Stage 1 to all three nodes, each verified ALIVE via
`/statusz` rather than via Railway's SUCCESS, then Stage 2 + the id fix to all
three. Six deploys, no scheduling stalls.

**An idle cluster looked converged and was not.** Immediately after Stage 2, all
three read identically: `last_applied == commit_index == 38810`,
`apply_rejections` 121 on every node, `apply_id_mismatches` 0,
`state_digest 0000000000000000`, `live_tasks 0`. That is exactly the healthy
signature — and it was agreement on EMPTINESS. An empty live set hashes to zero,
which is the all-zero agreement `live_tasks` was added to distinguish from a
strong one. No enqueue had happened since the fix, so the id counters had not
been exercised at all.

Driving 24 concurrent `ParseOperation`s through the live ingress reversed it:

| | node 1 (leader) | node 2 | node 3 |
| --- | --- | --- | --- |
| `apply_id_mismatches` | 0 | **24** | **24** |
| `live_tasks` | 0 | 16 | 24 |
| `state_digest` @ 38915 | `0000000000000000` | `1415537e144fd47b` | `6628b34606474b86` |
| `apply_rejections` | 121 | 137 | 169 |

24 mismatches per follower against 24 requests: EVERY enqueue mismatched. This
is the residue the fix explicitly does not clean — the followers' `next_task_id_`
was already offset in their volumes. The leader reads 0 because it predicts from
its own state and matches itself; the disagreement is only visible on replicas.

Wiped `broker.wal`, `broker.wal.snap`, `raft.wal`, `raft.wal.snap` on the two
followers, ONE AT A TIME (quorum is two of three), preserving `node.id`. **The
gate is the boot baseline, not the deploy status** — `Raft baseline: term=2
is_leader=false leader_hint=none last_applied=0`. Compare with node 1's earlier
"rebuild" the same day, which booted `term=3550 ... last_applied=34298`: that was
a redeploy onto the existing volume and reset nothing.

Both followers rebuilt from the leader's snapshot and caught up within one
polling interval, which also proves the v3 index snapshot round-trips between
nodes in production and not only in tests.

The same 24-request load then produced:

| | node 1 | node 2 | node 3 |
| --- | --- | --- | --- |
| `apply_id_mismatches` | 0 | **0** | **0** |
| `apply_rejections` | 121 | **121** | **121** |
| `last_applied` / `commit_index` | 39090 / 39090 | 39090 / 39090 | 39090 / 39090 |

Equal `apply_rejections` across replicas is the healthy state; they were
121/137/169 before.

**The negative proof needed a second attempt, and the first one was worthless.**
Checking the engine for degrade warnings returned zero — from a window
containing NO LOG LINES AT ALL, because `railway logs` returns a bounded tail
that had not yet reached the load test. A fresh 12-request burst, then a
re-pull, gave 12 lines in the window and 0 WARN / 0 ERROR in it. **Count the
lines in the window before believing a zero found in it.**

## Verification

- `ninja -C backend/build build_tests && ctest --test-dir backend/build` —
  **99/99, 0 failed**, before and after.
- Production unaffected throughout: `/healthz` `ok`, `Finance/ComputePayment`
  `-3210.560578012665289866`, 36/36 concurrent `ParseOperation` HTTP 200.
