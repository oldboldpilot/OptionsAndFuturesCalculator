# 2026-08-12 (second session) — a refused io_uring ring bricked the queue cluster, and four layers hid it

@author Olumuyiwa Oluwasanmi

One production outage found and fixed, one bound discharged through an SMT
solver rather than read off the page, and five defects in the machinery that was
supposed to *report* the outage. The through-line is the one this repository
keeps finding, in a new place: **a reading that looks like the answer, taken from
a layer that does not own it** — here, four error types in a row each widening
the one below until a day of downtime said the single word `QueueError`.

Commits: SGEE `86b51829` (await budget), `cd3c62b9` (broker error), `351793d0`
(WAL error), `76604775` (write-path errors), `604c161d` (**the fix**),
`b483afdb` (the gate), `e61bf33e` (Win32 branches), `44dc7fbe` (docs); parent
`8a10b14` (Z3 bounds), `321320f` (deploy gate), `590dd34` / `048942d` / `c684caf`
(docs).

---

## 1. The await budget was frozen at constants the knob no longer set

The Raft timing knob added earlier the same day (`SGEE_ELECTION_TIMEOUT_MS` /
`SGEE_HEARTBEAT_MS`) shipped with `kAwaitRoundsForFailover` still a
**compile-time constant** derived from `RaftNode`'s class constants:
`(150*2 + 50) * 4` = 1400 rounds, spent 1 ms per round.

Setting the election base to 1500 randomises the deadline into `[1500, 3000)`,
so the worst-case election is 3000 ms — against a budget still sized for 150.
**Every await spanning a failover would have timed out**, and the knob added to
cure failover churn would instead have guaranteed no drain could survive one.

`await_rounds_for_failover(base, heartbeat)` replaces the constant and the node
derives its budget from the timing actually in force. At the defaults the
function returns 1400 — bit-for-bit what the constant was — so no existing caller
changes.

## 2. A second bound, found by discharging it rather than reading it

The budget also has an **upper** limit nothing enforced: it must stay strictly
under the broker's 30000 ms lease visibility window, or a caller waits past its
own lease expiry and completes against a **stale fencing token**.

`RaftNode::set_timing`'s precondition (`base > 0`, `heartbeat > 0`,
`heartbeat * 2 <= base`) does not imply it and *cannot* — `set_timing` knows
nothing about the broker's lease window. Z3 produced the counterexample directly:
**(3000, 1500)** satisfies the precondition exactly and derives a budget of
*exactly* 30000.

Discharged through sensen's own GP-ARA `Z3Reasoner`, not raw Z3, because its
default-deny wrapper rejects a blank, unparseable or **vacuous** SAT rather than
reporting it as proof:

| property | verdict | meaning |
| --- | --- | --- |
| P1 budget spans worst-case failover | UNSAT | no counterexample — always sufficient |
| **P2 budget < visibility window** | **SAT (3000/1500)** | **bound not enforced** |
| P3 deployed pair 1500/300 | SAT | admissible, 13200 ms |
| P4 defaults still derive 1400 | UNSAT | no regression for existing callers |
| P5 old budget too short | SAT | the defect in §1 was real |

Every SAT passed the vacuity audit. Now **P6 of
`backend/tests/test_sgee_automated_reasoning.cpp`**, discriminating at the
boundary — (3000,1500) → 30000 rejected, (3000,1499) → 29996 admitted. A gate
that merely refused large values would prove nothing.

Two properties of that harness are load-bearing and easy to get wrong:

- `prove_safety` returns `true` for **genuine SAT**, so an invariant is verified
  by asserting *preconditions ∧ ¬invariant* and requiring `false`.
- **No formula may contain the substring `timeout`** — `prove_safety`
  short-circuits on it before the solver runs, which would turn the whole
  property into a verification that silently never happened.

## 3. The deploy gate reported success for nodes that never booted

`deploy.sh`'s `await_healthy` waited for a `Raft baseline:` line, and its comment
asserted that line was one "ONLY the new binary emits". True the day it was
written, false ever after — **that binary had since become the deployed one.**

`railway logs --service` replays the LAST session's output when nothing is
running. It does not say so and does not return empty. So the gate matched a
leftover from a dead container and printed

> `[deploy] all three nodes rolled, one at a time, each proven up before the next.`

while every deployment was still BUILDING and the two before them had FAILED.
Nothing was proven; the sentence came from a grep against a dead node's
scrollback. **A marker chosen because "no previous binary could emit it" has a
shelf life of exactly one deploy and cannot be a gate.** It now polls
`railway deployment list`, and reads the CONTAINER log on FAILED — where a
healthcheck failure is actually explained.

I then repeated the same mistake an hour later, reading a poll's output that
matched `initialized successfully` from the same stale source. The habit is the
hazard, not the command.

## 4. The outage: `DurableAppender::create` was fatal on a refused ring

**Root cause.** `create()` returned `IoError::OpenFailed` when
`io_uring_queue_init` failed — turning a COMPILE-time capability into a RUN-time
requirement. A binary built where the ring exists had no degradation path where
the kernel refuses it, and container runtimes routinely block `io_uring_setup`
by seccomp.

The cost is not slower writes. `TaskBroker::open` ends by sweeping leases that
expired while the process was dead, and that sweep is the **first** caller of
`begin_batch` — so a node that died holding a lease **could never reopen its own
WAL**. `sweep_expired` returns early when nothing has expired, which is why the
nodes ran for weeks and bricked permanently the moment one died mid-lease.

### Three things that made the diagnosis hard, and each is a lesson

**It presented as a corrupt WAL and was nothing of the kind.** The log read
cleanly every time — header, snapshot and record stream all decoded — and only
the WRITE failed. Wiping the volumes would have "fixed" it, destroyed the only
evidence, and left a defect that would re-brick the rebuilt cluster the first
time a node died mid-lease. **No volume was wiped.**

**All three failed identically and simultaneously.** Independent disk corruption
on three volumes at one moment is implausible; the simultaneity was the clue
that pointed at replicated state — the expired-lease set is the same on every
node, so every node runs the same sweep and hits the same wall.

**Four layers each widened the error until it meant nothing:**

```
persistence::WalError{OpenFailed|IoError|Corrupt|BadHeader|PayloadTooLarge|InvalidLsn}
  -> QueueError::WalError          (TaskQueueLog — six values become one)
  -> ConsensusError::QueueError    (ReplicatedTaskBroker — every broker fault becomes one)
  -> "Failed to create ReplicatedQueueRuntimeDriver"   (the node — both halves dropped)
```

Every one of those types already had a `to_string`. The node exits immediately on
this path, so each layer was the last place its own distinction existed, and each
threw it away. Three deploys were spent re-instrumenting one layer at a time.

### Six hypotheses tested and discarded

Recorded so they are not re-checked: disk pressure (~1 GB of 50 GB); the timing
knob (the broker guards `0/0`, and the override is confirmed applied *before* the
failure); a torn compaction snapshot (writes are temp + `fullsync` +
`durable_rename`); a WAL identity mismatch (`kTaskQueueWalHash` is a fixed
literal, never changed since the file was created); a WAL format-version bump
(same); and `NotLeased`-on-replay (`index.cppm` only ever returns `Corrupt`).

### Discriminated three ways before deploying

| scenario | result |
| --- | --- |
| pre-fix + refused ring + expired lease | exits 1, reproducing production's log lines **verbatim** |
| post-fix + refused ring + expired lease | starts and stays up |
| post-fix + refused ring, `QueueNodeDurabilityTest` | passes — the fallback is DURABLE across a leader SIGKILL, not merely present |

That last row is the one that mattered: it proves the blocking path preserves the
durability contract rather than just letting the process start.

### Verified in production

| check | result |
| --- | --- |
| deployments | SUCCESS on all three — first since 04:39 |
| leader / term | node 2 leading, all three at term 2940 |
| replication | `last_applied` 15924 on all three, in lockstep |
| the operation that bricked them | leader `sweep_successes` 359 → 970 |
| churn | 20–23 elections per interval → 2 → none since term 2920 |
| drains | `DRAIN_DONE n=66` then `n=25`, both uninterrupted, `dlq_depth: 0` |

The 66 included **26 tasks stranded since before the outage** — nothing was lost
while the cluster was down. The leader held term through both full
enqueue-and-drain cycles without a single election, which is the precondition
`docs/SGEE_QUEUE_CLUSTER.md` names for promotion.

## 5. The gate that could not exist, and now does

No test could **reach** the fallback: the async backend works on every developer
and CI host, so 96 suites passed over it by construction — which is exactly why
this reached production. `SGEE_FORCE_BLOCKING_IO=1` is the seam, and it earns its
place twice: it makes the path testable, and it lets an operator on a host with a
refused ring select the safe path deliberately instead of discovering it through
a crash-loop.

`QueueNodeBlockingIoTest` drives the WHOLE sequence — enqueue, lease, kill
mid-lease, wait out the 30 s visibility window, restart — rather than
unit-checking `create()`. The bug was never that `create()` failed; it was that
the failure was fatal at the one moment recovery needs a write, so a test
asserting "create() returns something" would have passed against the broken code.
Mutation-checked both ways. 96 → 97 suites.

## 6. The Win32 branches had the identical defect

`QueryIoRingCapabilities` / `CreateIoRing` and `CreateIoCompletionPort` all
returned `OpenFailed` on init failure. **Windows 10 and Server 2019 have no
IoRing at all**, so that branch is reachable in production merely by running on
an older supported OS. The old comment argued an unavailable IoRing should
surface as `OpenFailed` because "a build that wanted IoRing-on-an-old-OS should
select IOCP instead" — a BUILD-time remedy for a RUN-time condition, when the
binary is already running by the time it finds out.

Verified as far as a Linux host allows, per the standing lesson that guessing at
a `_WIN32` branch costs 22 minutes of CI per attempt: the edited control flow was
extracted against stubbed Win32 symbols, compiled with `-Wall -Wextra`, and both
directions asserted. The Win32 API calls themselves are unchanged.

`SGEE_DISABLE_IO_URING` was renamed to `SGEE_FORCE_BLOCKING_IO` — it was an hour
old and covers three backends, so naming it after the Linux one was wrong the
moment these branches were fixed.

## 7. Two smaller defects, and one non-invariant

**`ConfigError::PartialTls` and `PartialRaftTiming`** were added without
extending `config_error_name`'s switch, so both misconfigurations printed
`unrecognised ConfigError`. `-Wswitch` had been saying so.

**`SGEE_NO_IMPORT_STD=ON` with no parent `logger` target** is now refused at
configure time. A fetched cpp23-logger builds against libc++ while the flag
leaves SGEE on libstdc++; a C++20 BMI freezes the standard library it was built
against, so the ABI conflict surfaced as
`no known conversion from 'std::string_view' to 'std::string_view'` in
`transpiler.cppm` — a file with nothing to do with the cause.

**`dlq_depth` is NOT a cross-replica invariant.** Node 1 reported 17 while nodes
2 and 3 reported 0, at the *same* `last_applied` and term, which reads exactly
like state-machine divergence. It is not: `serialize_snapshot` retains
completed/dead tasks only inside a wall-clock retention window and **compaction
is local**, so dead tasks age out of each node's retained view at different
moments. Compare `last_applied` and `current_term`, which *are* invariants.
`tick_errors` accumulating on followers is the same shape — `sweep_expired` can
only succeed on the leader.

---

## What this session cost, and why

Three deploys (~30 min each) were spent instrumenting one error layer at a time,
because each layer had discarded what the one below it knew. That is the price of
error widening, paid in wall clock. The fix in every case was three lines and a
`to_string` that already existed.

The one thing that went right early: **not wiping the volumes.** The evidence
they held is the only reason the root cause was found rather than papered over.
