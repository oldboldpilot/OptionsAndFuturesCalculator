# The gateway store is migrated and serving, and the documented procedure could never have done it

**2026-10-11**, continuing the preflight session recorded in
`session_2026-10-11_gateway_schema_preflight.md`. That one measured the gap and built the guard
that refuses a redeploy which would replace a serving gateway with one that cannot boot. This one
closes it: §15 item 26 is done, the live PostgreSQL gateway store is at schema 12 and the merged
binary serves it. What took the time was not the migration. It was that the procedure the previous
session recorded, and that `redeploy.sh` printed as its own remedy, **cannot succeed on this
deployment** — and the reason is a privilege check whose statement is a no-op.

## What was done, in order

| step | result |
| --- | --- |
| store version read with the SERVING image | `schema_version=7 orgs=1` |
| `redeploy.sh` run as-is | refused at 1b, containers untouched, image built (`sha=3cfbd2a7298bde40`) |
| `admin init env:SGW_MAINT_DSN` | **refused**: `permission denied for database sensen_gw` |
| pre-migration `pg_dump -Fc` | 173,642 bytes, rc=0 — so the forward-only step became reversible |
| `admin init` as the schema's OWNER | rc=0, `ready at schema version 12` |
| `redeploy.sh` immediately after | rc=0; **T0 12:04:10 → T1 12:04:27, a 17-second window** |

The redeploy's own output is the evidence: `store ok: schema_version=12 orgs=1`,
`readyz: ready:true store_reachable:true epoch_id:ge_10 epoch_current:true admission_wired:true`,
regression `20 passed, 0 failed`, reaper `tick 1 outcome=not_due overdue_seen=0
held_by_break_glass=0`, **`restart ok: ready in 1s`** against the 20 s budget, and a second
regression `20 passed, 0 failed` after that restart.

**The restart is the only check that could have caught the defect it exists for.** A cold `/readyz`
passes on the broken image and the fixed one alike; the socket has to be in TIME_WAIT first, which
is what the preceding regression puts there. And `epoch_current: true` is the gateway saying the
derived policy epoch still matches its inputs after the schema moved underneath it — a migration
that invalidated the epoch would leave it serving a stale derivation behind a green healthcheck.

## Migration 12 verified AFTER the fact, not predicted

The previous session argued migration 12 is a no-op on this store's data. That argument was
checked rather than trusted, read back out of the live store:

```
rows 37 | orgs 1 | max_seq 37 | org_seq_ne_seq 0 | org_seq_null 0
per_org_contiguous yes | prev_hash_nulls 0
```

`org_seq = seq` on every row is exactly the condition under which every recorded hash recomputes
unchanged, and `org_seq_null 0` says the backfill reached every row. Note the row count: the
preflight session recorded 25 and the store is now at 37, because it has served traffic since. The
property holds at the larger count, which is a stronger statement than the prediction was.

Rollback, kept: the dump above, and the previous image id `e88d11ce6cf0` — recorded because the
`pg-tier` tag moves on every redeploy, so the tag is not a rollback target and the id is.

## THE ROLE IN THE DOCUMENTED PROCEDURE CANNOT MIGRATE, AND IT FAILS AT A STATEMENT THAT DOES NOTHING

`admin init env:SGW_MAINT_DSN` is what the preflight session recorded and what `redeploy.sh`'s
refusal printed. It answers `permission denied for database sensen_gw` (SQLSTATE 42501) and
changes nothing.

**The cause is `migrate()`'s first statement**, `gateway_store_pg_impl.cpp:576`:
`create schema if not exists <schema>`. **PostgreSQL checks `CREATE` on the DATABASE before
`if not exists` checks the schema**, so on a store whose schema already exists the statement is a
pure no-op whose only effect is to refuse. Measured here rather than recalled, with a positive
control — the owner's identical statement succeeded two lines earlier in the same session:

```
as|probe_nocreate
ERROR:  permission denied for database rls_scratch      <- create schema IF NOT EXISTS, schema EXISTS
```

**Fixing that statement would not have made the procedure work, which is the part worth
recording.** Past it the pending migrations `alter table` existing tables, and ALTER requires
OWNING them. Measured against the live store:

```
schema_owner postgres | table_owners postgres=40 | rls_enabled_forced 39/39 of 40
gw_maint: CREATE on database false | CREATE on schema false | USAGE on schema true
gw_maint is member of postgres: false | roles gw_maint belongs to: gateway_maintainer
```

So `gw_maint` fails at the first statement and would have failed at the next. **Least privilege is
right for SERVING and a migration is an owner action**; what was wrong was an instruction that did
not say so. Because all 40 existing objects are already `postgres`-owned, running `admin init` as
the owner introduces no ownership divergence — which was the only real objection to it.

The DSN travelled as a podman secret and was removed afterwards, so it never appeared in an
argument list or in `/proc/<pid>/environ` of anything but the one container.

## What shipped here

`deploy/gateway/redeploy.sh`'s refusal now names an OWNER DSN and states the two privileges
`admin init` needs, with the measurement behind them in a comment above it. The old text is not
merely replaced: `scripts/check_gateway_deploy_guard.sh` gains two checks that REFUSE a message
naming `admin init env:SGW_MAINT_DSN` and REQUIRE one naming an owner DSN, **28 → 30 checks, 0
failed**. Mutation-checked by restoring the old message: it fails exactly those two and nothing
else, and the file was restored byte-identically.

A message that prints an impossible remedy is worse than no message, because the reader has no
reason to doubt it — the same shape as `railway_deploy.sh` recommending the log command that
replays a dead session's scrollback.

**NOT done, and recorded as a choice:** making `migrate()` skip `create schema` when the schema
exists. It is a correct change on its own merits — a no-op should not demand a privilege — and it
would let a deployment whose migrator role OWNS the objects use a least-privilege DSN. It would
**not** make the documented procedure work here, so shipping it as "the fix" would be the
stale-claim defect in a new costume.

## Two things I had recorded wrongly, corrected

**`rls_scratch` is ours, and it is the compliant way to run the RLS gate.** An earlier status
called it another workstream's and not mine to touch. `backend/sensen/tools/check_pg_rls.sh:52`
asserts `tenant_admin` by name as the NOLOGIN/NOSUPERUSER/NOBYPASSRLS app role its policies key
on, and `deploy/postgres/pg.sh` defaults its database to `sensen` — the LIVE agent store — against
sensen's own rule that no test gate may reach it. A separately named database is therefore the
policy, not a leftover.

**And the gate passes, measured:** `all 28 tenancy tables have row security ENABLED and FORCED`;
the four-row contrast `superuser 2 | no subject 0 | empty 0 | subject A 1 | A's view of B 0 |
write into B refused`; the shared-layer and policy-injection checks; 8 mutation arms all detected;
`check_pg_rls: OK`.

**Its first run reported two FAILS and they were a STALE SCHEMA, not a defect** — one of them
`POLICY INJECTION: a tenant published a trusted, globally visible layer`, which reads as a live
cross-tenant hole, with every mutation arm firing so the instrument was demonstrably live. The
schema in that database predated the migrations that added the shared/trusted policy layer.
`pg_apply_migrations.py --recreate` (20 migrations from source) and it goes green. The gate is
versioned with the source and the scratch schema is not: `pg_apply_migrations.py` deliberately
writes no `schema_migrations` rows, so nothing in that database records which commit built it.
**Passing mutation arms prove a gate can SEE, not that the schema is current.** The fixtures were
safe to drop because `n_tup_ins 130` against `n_live_tup 0` is positive evidence of
`BEGIN..ROLLBACK` fixtures — `pg_stat_user_tables` counts inserts that were rolled back.

**Gateway phases G3–G6 are built and gated, not pending feature work.** An earlier status called
them out of scope. Measured in the tree: 42 gateway modules, 51 gateway test files — G3
`gateway_breaker.cppm` + `gateway_pool.cppm` (`test_gateway_breaker`, `test_gateway_pool_e2e.py`),
G4 `gateway_admit_queue.cppm` (`test_gateway_queue`, `_e2e.py`, `_live`), G5 fallback chains in
`gateway_admit_action`/`_http`/`gateway_body_keys` (`test_gateway_admit` 11i,
`test_gateway_admit_http` 11.0 and §28, `test_gateway_body_view`), G6 `gateway_autoscale.cppm`
(`test_gateway_autoscale`, `_e2e.py`).

## `config/.env` for sensen and tbqwf, and the entry that broke a gate

Both repos ignore `config/.env` at `config/.gitignore:1`, verified with `git check-ignore -v`
before anything was written; every file is mode 0600.

**sensen** gets 12 keys: `PGHOST/PGPORT/PGUSER/PGPASSWORD`, the live stores named for operator
tooling (`SENSEN_AGENT_DB`/`_SCHEMA`, `SENSEN_GATEWAY_DB`/`_SCHEMA`), and the gate targets pointed
at SCRATCH databases — `DATABASE_URL`, `SENSEN_AGENT_TEST_PG`, `SENSEN_AGENT_TEST_PG_URI` at
`rls_scratch` and `SENSEN_GATEWAY_TEST_PG` at a new `sensen_gw_gate`. **`PGDATABASE` is
deliberately unset** so a bare `psql` cannot default into a live store. Written to all four sensen
working trees, since the file is per-checkout and gitignored; that is four copies of one
credential to rotate, which is the cost of the mechanism rather than a preference.

**`SENSEN_GATEWAY_TEST_PG` carries NO `sslmode`, and the first version did.** With
`?sslmode=disable` the gate failed two checks —
`the DSN from the environment carries no sslmode and therefore defaults to REQUIRE` and
`the default sslmode=require REFUSES when no TlsUpgrade is injected` — because the test's premise
is that the store applies its own `require` default and can be proven to refuse rather than
silently drop to cleartext. 556/2 with it, **558/0** without. The reason is now written beside the
key, since the next person to add `sslmode=disable` there will have the same good intention.

**tbqwf gets a POINTER, not a credential, and that is deliberate.** It reads
`TBQWF_TEST_PG_DSN_FILE`, a file path, and its own session log says the DSN is read from that file
*"not from the environment's value, so it is never in `/proc/<pid>/environ` or an argument list and
is never printed."* Putting a DSN in its `config/.env` would undo a documented security property,
so the DSN lives at `~/.config/tbqwf/pg_test_dsn` (0600) with the pointer plus
`TBQWF_TEST_PG_USER` and `TBQWF_TEST_PG_DB` in `config/.env`. The password is therefore NOT in
tbqwf's environment, on purpose.

All three were exercised rather than assumed: sourcing sensen's file and running the RLS gate gives
`check_pg_rls: OK`, `sensen_gw_gate` answers, and the tbqwf DSN file's credentials reach
`tbqwf_test`.

## A grep that passed vacuously, twice, on the live site

Verifying the release, `grep -oE 'id="group-[^"]*"'` against the fetched page returned nothing and
`0` space-containing ids — which reads as a clean pass. The page had a `<title>` the whole time.
`content-encoding: br` sent the first suspicion to compression, and `curl --compressed` changed
nothing; the actual cause is that `file` reports the body as `data`, so **grep treats it as binary
and suppresses matches**. With `grep -a` and a positive control (`title tags = 1`) the real answer
is 5 kebab-case ids, **0** space-containing, 6 `aria-labelledby` refs. A negative grep needs a
positive control — this file records that lesson twice already and it still cost two readings.
