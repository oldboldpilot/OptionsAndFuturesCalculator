# A redeploy would have taken the gateway down, and the preflight that stops it

**2026-10-11.** This repository's share of a session whose main work was in sensen (the audit
chain becoming per-org; see that repository's `docs/session_logs/session_2026-10-11_per_org_audit_chain.md`
and §15 items 23-26 of `docs/agentic/MULTI_TENANT_GATEWAY.md`). What landed HERE is one
preflight, one predicate and its gate — and the measurement that made them necessary.

## The measurement

The live PostgreSQL gateway store is at **schema version 7** (migrations 1-7), one org, 25 audit
rows. The merged sensen binary carries **12**. The serving image was built on 2026-10-06 from
`a041789af`, which predates migrations 8, 9, 10 and 11, and nobody redeployed after they merged,
so `admission_plans`, `org_plans` and the models registry **do not exist in production**.

**The version check is not broken, and that is what makes this safe to have found late.**
`GatewayStore::open` calls `verifyLocked` on EVERY open and refuses a mismatch in either
direction, so the deployed binary and the store agree with each other: 7 and 7. What is behind is
the deployment.

**But `redeploy.sh` as it stood would have replaced a serving gateway with one that refuses to
boot.** The container never migrates — deliberately, so that a typo is a refusal rather than an
empty database — so a 12-migration binary cannot open a version-7 store at all. Section 2 removes
the serving containers with `podman rm -f` BEFORE anything would have learned that, so the only
sign would have been the readiness timeout in section 2, by which point the gateway was gone.

Proven by running both binaries against the live store, read-only, through `admin show`:

| binary | result |
| --- | --- |
| the deployed image (`a041789af`) | `schema_version=7 orgs=1` |
| the merged tree (migration 12) | refused: *"migration 8 ('gateway_hold_principal') has not been applied to this store"* |

## What shipped

**`deploy/gateway/redeploy.sh` section 1b** runs that same `admin show`, in a throwaway container,
with the gateway's OWN credential and scope — so the check cannot disagree with what section 2 is
about to do — and it runs after the image is built and before any container is touched. It
**refuses and names the `admin init` that migrates**, rather than migrating: applying pending
migrations to the system of record is an operator's decision with its own evidence, not a side
effect of a redeploy. At the time of writing the pending set is four feature migrations plus one
that rewrites an audit control.

**`deploy/gateway/store_version_ok.sh`** holds the verdict as a predicate, for one reason: the
first version was an inline `case` and **it was wrong in the direction that hides itself.** It
allowed a looser `*schema*version*`, and `verifyLocked`'s AHEAD refusal reads *"the store is at
schema version N but this binary only knows M"* — so a store NEWER than the image would have read
as a pass and the deploy would have gone ahead. Matching the refusals instead is the other wrong
answer: the BEHIND refusal measured above names neither spelling, so a refusal-matching rule would
have missed exactly the state this exists for. The rule is `schema_version=` with the equals sign:
`show`'s own output and nothing else.

**`scripts/check_gateway_deploy_guard.sh` section 7** feeds that predicate the real success line
and three real refusals (the BEHIND one measured against the live store, the AHEAD one verbatim
from `verifyLocked`, and a loader failure), plus an empty read, plus an assertion that
`redeploy.sh` actually calls it — because a predicate nothing calls is not a gate. **21 → 27
checks, 0 failed.** Mutation-checked: restoring the loose pattern fails **exactly one** check, the
AHEAD refusal, and nothing else; the file was restored byte-identically and the gate re-run green.

Both arms of the preflight were also exercised against the real live store with the two real
binaries, which is the table above.

## Not done, and why it is a decision rather than an omission

**The live store is not migrated and the gateway is not redeployed.** The procedure is
`admin init <maint-dsn>` (which applies 8, 9, 10, 11 and 12 in one transaction — DDL is
transactional and each migration is checksummed) followed immediately by `redeploy.sh`, because
once the store is at 12 the currently running image can no longer open it: it keeps serving on its
existing connection and would refuse at its next restart. Migration 12 is a no-op on this store's
data — one org, 25 rows, `max(seq)` equal to the row count, so `org_seq` equals `seq` for every row
and every recorded hash recomputes unchanged, which sensen's `test_gateway_store_pg` §17f asserts
on a seeded store with a gap deliberately present so the backfill's choice is observable.

It is left to the owner because a forward-only migration of an audit control on the system of
record has no rollback, and the window between `admin init` and a completed redeploy is one in
which a restart of the old container is an outage.
