# The open gateway defects, the deploy guard, and two mutation arms that passed

**2026-10-10, continuing the same session.** Everything on the open list that could be
closed here was closed; what could not is named below with the reason. The useful content is
in the three things that were measured rather than reasoned about.

## The deploy guard (this repository)

`deploy/gateway/redeploy.sh` takes its source tree from `SRC`, defaulting to
`/home/muyiwa/.cache/sensen-gwpg` — a WORKTREE, which is whatever branch somebody last
checked out there. A worktree left on a lane therefore makes a default `redeploy.sh` ship
unreviewed code to production and PRINT ITS SHA, which reads exactly like a release. That
hazard was created by the convenience of the default, and the previous session closed it by
hand (repointing the worktree); this one closes it structurally.

`deploy/gateway/require_merged.sh` runs FIRST in section 0: HEAD must be reachable from a
master ref, and the ref that answered is NAMED. It is evaluated against a CACHED
remote-tracking ref and says so — `for-each-ref` answers from local disk, only `ls-remote`
asks the server — so a stale cache can refuse code that is merged (the refusal says to
fetch) and can admit code pushed to a master since rewritten. It guards against the ambient
worktree; it is not an attestation of review. `ALLOW_UNMERGED=1` is the deliberate exception
and labels itself in the output.

Gated by `scripts/check_gateway_deploy_guard.sh`, 21 checks, wired into CI's `policy` job.

**TWO OF THE THREE MUTATION ARMS PASSED ON THE FIRST ATTEMPT, and both were my bugs.**

* Deleting the call from `redeploy.sh`: the `sed` pattern was `require_merged.sh "$SRC"` and
  the real line is `require_merged.sh" "$SRC"` — a closing quote sits between — so nothing
  was mutated and the arm "passed".
* Widening the glob `*/master` → `*/*` changed nothing, because the check used
  `origin/lane/some-work`. Git's `*` does not cross `/`, so a three-component ref is refused
  by ANY two-component glob whatever its last component is; the check could not tell the two
  globs apart. The discriminator had to be a one-component branch, `origin/develop`.

A third correction: one check matched the word `podman` in redeploy.sh's own HEADER COMMENT
(line 33, explaining why secrets travel as podman secrets) and reported the guard as called
too late. Strip comments before matching — the same thing the CSPRNG policy gate does.

## The four gateway defects (sensen, merged to master)

Left open on the 2026-10-06 PostgreSQL cutover. All four answered; the detail is in sensen's
own CHANGELOG and `docs/agentic/MULTI_TENANT_GATEWAY.md` §15 items 23–25.

1. **An unreadable store path core-dumped.** The THROWING `std::filesystem::exists(path)`;
   `admin show <path-under-a-0000-dir>` died at **rc 134**. Now rc 1 and a refusal, with
   "cannot tell" kept apart from "is not there".
2. **"No epoch has been published" was a wrong answer**, not a missing one — an unscoped
   tenant session reads `gate_epochs` as empty whatever it holds. The refusal is on the
   QUESTION, not on `open()`: a first attempt that refused at open broke an existing check
   asserting that a tenant login may open unscoped.
3. **SIGTERM: the mechanism is PID 1**, and the note I wrote last session said something that
   cannot be true ("kept its default action" — a process that dies where it stands is one
   `podman stop` stops in milliseconds). The kernel applies no default action to a
   namespace's init, so an unhandled SIGTERM is DISCARDED. Verified as a pair as container
   PID 1: pre-fix **10.24 s / exit 137 / "resorting to SIGKILL"**, fixed **0.57 s / exit 0**.
4. **The suspected hash-chain FORK is refuted and a worse defect confirmed.** `prev_hash` and
   the `max(seq)+1` prediction are read through the same row security, so they are wrong
   together and the guard catches the pair. What is real: **one refused append wedges the
   chain store-wide and permanently** — a rolled-back insert keeps its sequence draw, so every
   later append by any role predicts a value the identity column will not give, each attempt
   widening the gap. Measured through `admin break-glass`: 3/3 granted, 3/4 after a
   rolled-back insert, 3/5 REFUSED on the same command.

   **The obvious repair was built, measured and reverted.** Supplying the seq with
   `overriding system value` fixes the wedge and removes the coupling that is the only thing
   stopping a fork. So the wedge cannot be fixed without deciding what the chain covers.
   What shipped is the diagnosis: both causes named, the `setval` that recovers one, and the
   other ruled OUT for a session that bypasses row security.

**`requests_processed` 697 vs 401 is not reproducible as written** — that field is in no
gateway source, no `/metrics` or `/statusz` field and no test, only in TBQWF's archived
examples. Left open with the reason.

## Two more things a review found, and one a measurement did

* **`rlsEvidence` reported MET from a session that can see nothing**, claiming "this store
  scopes every statement to org '' and drops to 'gateway_tenant'" for an unscoped tenant
  login that scopes nothing and drops nobody. The fabrication that function already refuses
  for a BYPASSING session, pointed the other way.
* **`installedEpoch`'s remedy named a role the same commit's test proves is refused** — the
  maintainer holds no privilege on `gate_epochs`. Advice contradicted by the test beside it.
* **`podman exec` without `-i` does not attach stdin**, so a heredoc'd `psql` gets EOF, does
  nothing, and **exits 0**. The first scratch-database setup reported `rc=0` having created
  neither the role nor the database; only listing the databases afterwards caught it.

## The TBQWF CSPRNG gate has now RUN

`tests/csprng_policy_tests.cpp` was registered and built in no build directory, so it had
very likely never executed. Configured in a new `build-csprng` (reusing the existing
dependency sources, disturbing no other build dir): it builds, runs, and **passes — 3 tests,
0 failures, 382 source files scanned**, and a planted `rand()` makes it exit 1 by name, so it
can fail. A standing note that TBQWF has no `std.pcm` anywhere was wrong: most build
directories have one.

## Still open, with reasons

* The audit chain's design (per-org vs a `security definer` read) — an owner decision on an
  audit control over the system of record, written up with a recommendation.
* `blindTenantSession()` is applied to two reads; six siblings are NAMED rather than swept,
  because recording a half-swept class as swept is how this defect arose.
* `sen code rules` could not run on the lane: it needs `.scf` fact files, which require a
  facts-emitting rebuild of the whole gateway closure.
* The gateway's unbuilt plan phases G3–G6 are feature work, not defects.
