# SGEE queue promotion — stages 1–5, and five defects found on the way

@author Olumuyiwa Oluwasanmi

**Date:** 2026-08-12 / 2026-08-13
**Outcome:** `INFERENCE_QUEUE=sgee` is live on the engine. The SGEE Raft cluster
serves inference admission for both assistants; Postgres remains writable as the
degrade target.

---

## What this was

Making the queue cluster the admission path was a **protocol change, not a
configuration flip**, and the reason is worth stating plainly because it is the
whole shape of the work: the queue could not return an ANSWER. `CompleteRequest`
carried no result, `Task` had no result field, and there was no RPC to read a
task back at all. A submitter on replica A could not learn the outcome of a job
executed on replica B — which is precisely what `PostgresAdmission` exists to do.

Five stages, each independently deployable:

1. **Readers** for all three formats, writing nothing new.
2. **Writers** — the result is persisted, replicated, and applied.
3. **RPC surface** — `result` on `Task`/`CompleteRequest`, and `GetTask`.
4. **Engine admission** — `SgeeAdmission` / `SgeeLeaseSource`.
5. **Promotion** — the flip, with Postgres as the fallback.

## The rule that shaped stages 1 and 2

`BrokerComplete` is a replicated command with **no version byte and no length
prefix** — `[tag u8][fields]`, so the per-tag exact-size check IS the versioning.
Since `fbcd2d63` a frame a node cannot decode is no longer silently dropped:
`apply_committed` stops without marking it, `last_applied` freezes, and the node
retries the same entry forever.

Writers before readers is therefore **a stalled node per un-upgraded replica**,
not a degraded window. Readers shipped a full deploy ahead of any writer.

Two consequences fall out of that and both are load-bearing:

- **Size is the discriminator, so there is a dead zone that must be REFUSED** —
  9..11 bytes for the WAL record, 17..19 for the command body. Reading one as a
  short v2 builds a length from whatever bytes follow, indistinguishable from a
  real one.
- **An empty result is encoded as the v1 shape.** So an empty result and a v1
  record are the same bytes, and **the Stage-2 writer flip is inert until
  something actually supplies a result** — which is what made it safe to roll
  writers to a live cluster before any client routed to it.

## Five defects, and what each looked like

### 1. My own Stage-1 commit claimed the stage and delivered half of it

It versioned the index snapshot and stopped; `codec.cppm` and
`broker_command_codec.cppm` were untouched, so two of three formats still
refused a v2 frame. A scoping pass flagged it, I verified independently
(`git show --stat`, and both structs lacked `result`), and corrected it in code
and in the plan rather than quietly.

The missing half was the dangerous half: the snapshot is the one format that
already had a version field, and the two that did not are the two where a wrong
guess is unrecoverable. `BrokerCommandCodecTests` is new — **that format had no
test at all**, and every replica decodes every one of those frames.

### 2. The queue image could not build — a symlink broke path arithmetic

`Dockerfile.queue-node` passed `-DCMAKE_CXX_COMPILER=clang++`. sensen resolves
the libc++ std module as
`dirname(dirname(CMAKE_CXX_COMPILER))/share/libc++/v1/std.cppm`, so
`/usr/bin/clang++` — a symlink to exactly the right compiler — resolved it to
`/usr/share/libc++/v1`, which does not exist. `std_module_precompile` was never
declared and every `import std;` died with "module 'std' not found".

`backend/Dockerfile` had been fixed for this in `bd11d00`; this file was missed.
Note that `-DLIBCXX_MODULES_PATH=…` sits on the same command line and sensen
never reads it — the flag that *looks* like it configures this is inert.

### 3. Then it failed at the link, and the error named the wrong library

`__cxxabiv1::__vmi_class_type_info`'s vtable, reported against
**`libstdc++.so.6`** — which reads as a standard-library mix-up and is not one.

A from-source libc++ installs `libc++.so` as a **linker script**, literally
`INPUT(libc++.so.1 -lc++abi -lunwind)`, so libc++abi arrives unasked.
apt.llvm.org ships a plain symlink, nothing pulls it in, and `ld` reported the
only C++ runtime that *was* reachable transitively. Fixed with an explicit
`-lc++abi`. The engine image had never hit it because it compiles no object
referencing a C++ ABI symbol directly.

### 4. The deploy script's own diagnostic lied twice in one message

It printed `railway logs --service` output — which replays a dead session's
scrollback — and the SSL handshake errors it showed were timestamped **three
hours before** the deployment it was blaming. It then called the failure a
healthcheck failure, pointing at a container that had never started.

The discriminator is the newest deployment's **`imageDigest`**: absent means no
image was produced, so no container ran and it is a BUILD failure; present means
the container started and did not stay up. Nothing else there tells them apart,
and guessing sent the whole diagnosis in the wrong direction.

This is the same trap `CLAUDE.md` already documents for `railway logs --build`,
arriving through the runtime log instead — and it was inside the very script
written to avoid it.

### 5. `ninja && ctest` was running stale binaries

SGEE is embedded with `add_subdirectory(... EXCLUDE_FROM_ALL)`, so `ninja` builds
none of its ~90 test executables while `ctest` runs whatever is on disk.
Measured after touching one `.cppm`: plain `ninja` scheduled **0** SGEE test
steps, an explicit build scheduled **235**.

It cost two full runs chasing a failure whose source had already been reverted —
`ninja` reported "no work to do" while the binaries were demonstrably older than
the sources. The reverse, a stale PASS over a real break, is the same mechanism
with the outcome that does not announce itself.

Fixed with a `build_tests` target that collects its dependencies from
`BUILDSYSTEM_TARGETS`, not a written list — a list edited alongside every new
`add_executable` will omit the newest test, which is the one most likely to be
failing. `ninja build_tests && ctest` is now the documented command.

## A bound that was wrong before results existed

Stage 2's own new test failed on first run, and it was right to.

`task_queue::kMaxPayloadBytes` and `persistence::kMaxPayloadBytes` are both
64 MiB — but that names the size of the **encoded record**, and the record
prefixes 12 bytes (WAL) or 21 (replicated command). A result at exactly the old
bound was accepted at the write and refused at the append: the precise
"accepted here, rejected downstream" hazard the bound exists to prevent, off by
the framing.

The comment on `kMaxPayloadBytes` had claimed a payload of that size "can never
produce a frame the codec would reject". That was false for enqueue too
(`encode_enqueued` prefixes 17 bytes), so **the defect predates results
entirely**. Now `kMaxUserBytes = kMaxPayloadBytes - 32` bounds what a caller
supplies — one rule, both fields.

Why it matters more than an off-by-one usually does: an oversized result is a
record this node syncs and then cannot replay, and — once replicated — a command
its peers refuse, which stalls them. A caller getting an error back is
recoverable; a cluster that cannot decode its own log is not.

## Two places the plan was overruled

**No `error` field.** The plan called for `bytes error` beside `result`. Carrying
an error string means widening `BrokerFail` — a fixed 16-byte replicated command
with no second shape any deployed node can read — plus the WAL's `Failed` and
`MovedToDlq` payloads and a snapshot v3: a second full readers-first deploy
cycle, for a diagnostic the worker already logs. A failure is signalled by the
task reaching terminal `Dead`, which a reader already sees in `state`. **A proto
field the replicated log silently dropped would be worse than no field — it
would read as an answer.**

**Poll, not await.** SGEE has no await, so `SgeeAdmission` polls `GetTask` every
25 ms. That is the same shape the Postgres path degrades to whenever its
`pg_notify` hint does not arrive — and `inference_queue.cppm` is explicit the
hint is never load-bearing for correctness — so the two share failure modes
rather than introducing a second set.

## The abstraction Stage 4 needed

`set_lease_source()` and `lease_source_` were typed on the **concrete**
`PostgresLeaseSource`, which made the storage substrate part of the decode
loop's type rather than a deployment choice. A second shared queue was
impossible without an abstract `LeaseSource` carrying one virtual `fill()`.

The two implementations are structurally identical, and that is not accidental
duplication: the constraint that shaped the first — lease on the owner thread,
never block there, hand the write-back to a short-lived helper — belongs to the
**decode loop**, not to Postgres.

## The mixed-version window, and what it actually proved

During the Stage-2 roll node 1 ran the writer binary while nodes 2 and 3 were
still on Stage-1 readers. Node 1 restarted at index 25173 against a leader at
26033, caught up, and converged. `last_applied == commit_index`,
`tick_errors: 0` on all three throughout.

Be precise: node 1 rejoined as a **follower**, so it emitted no v2 commands. The
direction exercised was new-reader-reads-old-writer, the safe one. The dangerous
direction is covered **by construction** rather than by that observation —
`encode` emits v1 whenever the result is empty, and nothing supplied a result
until the flip.

## Verifying the flip — the proof is a negative

| check | result |
| --- | --- |
| all three nodes on the writer binary | SUCCESS, rolled one at a time |
| cluster agreement | `last_applied == commit_index == 28981`, one leader |
| engine cutover | 6 `model is LOADED` (3 replicas × 2 assistants), after the upload |
| flip took effect | 3 mortgage + 3 strategy logging `INFERENCE_QUEUE=sgee` |
| real request | `ParseOperation`, live ingress, partner key, HTTP 200 in 2.18 s |
| degrade-path warnings | **0** |

**A request that degraded to the local backend returns the same answer as one
served by the cluster.** The response cannot distinguish them. Every fallback
branch in `SgeeAdmission` logs — submit failed, task not found, deadline passed —
so the proof is the absence of those lines, not the 200.

Nor is a rising `last_applied` proof: the sweep ticker advances it continuously
whether or not anything was enqueued. It went 29017 → 29029 across the call, and
that number establishes nothing on its own.

Both variables were set with `--skip-deploys` and applied with **one**
`railway redeploy`. Setting them one at a time restarts the engine into a
half-configured window — `INFERENCE_QUEUE=sgee` with no `SGEE_PEERS` degrades
every request to local, which is safe and is a silent outage of the thing being
promoted.

The request itself was refused by the mortgage verification layer —
`"rate" = 0.004167 does not correspond to anything in the request (the nearest
figure you gave is 0.5)` — because the model divided a stated PER-MONTH rate by
twelve. That is the grounding gate working, and a good outcome for this check:
the assistant ran, produced `raw model output`, and the verification layer
adjudicated. Nothing about the queue path was involved in the refusal.

## Still open

- **Postgres removal** is out of scope and stays out until a full deploy cycle
  of observation. It is the degrade target.
- **No non-empty result has crossed replicas in production yet.** The first
  inference served by a REMOTE replica will be the first v2 `BrokerComplete` the
  cluster has ever replicated. Readers have been deployed since Stage 1 and the
  formats are gated by tests, but the production first-write is unobserved —
  watch `tick_errors` on all three after it.
- **`QueueNodeDurabilityTest` does not assert a result across a leader
  SIGKILL.** It asserts tasks are neither lost nor duplicated; the result rides
  along untested at that level. Unit gates cover encode/decode/replay, which is
  why this was not treated as blocking, but it is the honest gap.
- **`SGEE_PEERS` means two different things** and now in three places. The nodes
  dial consensus (50052); the engine's admission client dials the client queue
  (50053). Copying one service's value onto the other yields a process that
  connects to a real port, speaks the wrong protocol at it, and fails like a
  network problem.
