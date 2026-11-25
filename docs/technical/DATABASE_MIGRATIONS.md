# Database migrations

@author Olumuyiwa Oluwasanmi

Every change to the database schema is a numbered, append-only SQL file, recorded in a ledger table, and checked
against the live database by a drift gate. A hand-edit that no migration records fails the gate; it cannot hide.

## What existed before (measured 2026-11-24)

- `backend/migrations/01_init.sql` to `09_auth_users_null_tokens.sql`: nine hand-applied files, written idempotent
  (`IF NOT EXISTS`, `DROP POLICY IF EXISTS`). No ledger, no runner: nothing recorded which had run where.
  a recursive grep for schema_migrations found only GoTrue's own `auth.schema_migrations`.
- `supabase/migrations/` and `infra/supabase/00_bootstrap.sql`: the design for a Supabase-auth layout that was never
  deployed (the header of `01_init.sql` says so). Not a record of production.
- `backend/migrations/05_saved_strategies_auth_fk.sql` DELETEs rows (those with no `auth.users` match). It is history
  and is never re-run; the baseline below replaces the whole set for any new database.

The old files are frozen as history. New work goes in `backend/db/migrations/`.

## Layout

| Path | Role |
|---|---|
| the numbered files in `backend/db/migrations` | the migrations, strictly ordered, never edited once sealed |
| `backend/db/migrations/MANIFEST.sha256` | append-only seal: version, sha256, name |
| `backend/db/schema_snapshot.sql` | the catalog snapshot the gate diffs |
| `backend/db/scratch_auth_stub.sql` | GoTrue-shaped `auth.users`, SCRATCH databases only |
| `backend/db/drift_ignore.txt` | accepted differences, each with a reason |
| `scripts/db_migrate.sh` | lint, new, seal, status, verify, apply, adopt |
| `scripts/db_capture_baseline.sh` | write a baseline from a database as it IS |
| `scripts/check_schema_drift.sh` | `--live` gate and `--selftest` positive control |

The ledger is `ofc_meta.schema_migrations` (version, name, checksum, kind applied or adopted, applied_at, applied_by,
execution_ms, pg_version). It lives outside `public` so it never appears in a baseline dump, and an UPDATE or DELETE on
it is refused by a trigger.

## Everyday use

1. `scripts/db_migrate.sh` (new add_thing) creates the next number. Write the SQL. Roles, grants, RLS and policies are
   schema: they go in the migration or the gate reports them.
2. `scripts/db_migrate.sh` (seal) records its checksum. `lint` then refuses any later edit of that file, and refuses a
   committed manifest line that changed.
3. `scripts/check_schema_drift.sh` (--selftest) proves the gate still goes red.
4. Deploy: run_with_env.py then `scripts/db_migrate.sh` apply, then `scripts/check_schema_drift.sh` (--live).

`apply` takes an advisory lock, runs each pending file in one transaction together with its ledger row, and stops at the
first failure with the ledger untouched. A file whose first line is `-- migrate:no-transaction` runs outside one
(CREATE INDEX CONCURRENTLY); write those re-runnable. A pending version below the highest applied one is refused.

## The drift gate

`--live` reads the highest version in the live ledger, builds the schema those migrations imply on a throwaway
container using the real runner, snapshots both, and diffs. Any difference is exit 1 and the diff is printed. It also
runs `verify`, so an edited or vanished recorded migration fails too. Exit 2 means it could not run, which is not green.

The snapshot covers columns, constraints, indexes, triggers, functions, event triggers, RLS flags (enabled and FORCE),
policies, grants, default privileges, enums, sequences, extensions, the attributes of every non-superuser role, the OFC
guard triggers on `auth.users`, the eight GoTrue token-column defaults of migration 09, and the rows of
`public.protected_relations`. It excludes the rest of `auth` (GoTrue migrates that itself), owners, passwords, other
row data and extension versions.

### Positive control (2026-11-25, scratch PostgreSQL 18.6)

`scripts/check_schema_drift.sh` (--selftest) introduced each drift in a scratch database and showed RED, then repaired it
and showed GREEN: policy dropped, role given BYPASSRLS, FORCE ROW LEVEL SECURITY off, RLS disabled, column added,
index added, grant widened to PUBLIC, guard row deleted, GoTrue default dropped, event trigger disabled; plus an edited
applied migration (refused by `verify` and `lint`) and a failing migration (rolled back, no ledger row). It printed
`SELFTEST PASS`.

The control found two defects in its own gate before passing, both now fixed. A UNION resolved the detail column to
type `name` and truncated every line at 63 bytes, so a role's `bypassrls` flag sat past the cut and was invisible.
Roles are cluster-level, so two databases in one scratch server share them, which hid role drift until the expected
side was frozen to a file first.

## Adopting production (owner step, nothing here has touched it)

`backend/db/migrations/0001_baseline.sql` was generated from a scratch database built by replaying the nine legacy
files, so it is PROVISIONAL: it is the schema as designed, not as captured. Replace it with reality:

1. run_with_env.py then `scripts/db_capture_baseline.sh` --force (read-only: pg_dump and catalog SELECTs).
2. `scripts/db_migrate.sh` (seal) after resetting the 0001 line (it is unsealed only until first commit).
3. run_with_env.py then `scripts/check_schema_drift.sh` --live. Whatever it prints is the truth about production;
   each line is either a bug to fix by migration or an entry for `drift_ignore.txt` with a reason.
4. run_with_env.py then `scripts/db_migrate.sh` adopt 0001 (re-runs the gate, then records the row without
   executing it).

Not done in this session: the hosted OFC database has no TCP proxy and `railway ssh` was refused, and the credential
loader refuses a shell or psql as its child, so step 1 could not be run here.

## Externally managed objects (measured against production 2026-11-25)

The 0001 baseline was replayed from legacy files, so it recorded the application schema but not eleven facts that
production holds. They are not all one kind of thing:

| Fact | Decision | Why |
|---|---|---|
| `public.handle_new_user()` | MIGRATION `0002_handle_new_user_function.sql` | It lives in `public` and inserts into `public.profiles`: this application's own object, and SECURITY DEFINER, so the gate must watch its text. No trigger references it (measured), so it is inert today; 0002 records it and creates no trigger. |
| `anon`, `authenticated`, `authenticator`, `service_role`, `supabase_auth_admin`, their `authenticator` memberships, `supabase_auth_admin`'s search_path, and USAGE on `public` for the first three | `drift_ignore.txt`, exact full lines | Created by self-hosted GoTrue/Supabase. A migration that CREATEs them would make this repo the owner of objects another system changes, and the first time it did, the migration would be a lie. |
| `gateway_auditor`, `gateway_maintainer`, `gateway_tenant` | `drift_ignore.txt`, exact full lines | Belong to the sensen gateway and its own schema, `sensen_gateway`. |

Disaster recovery does not need migrations to own the roles. Checked in the roles file of the 2026-11-25 backup
(`pg_dumpall --roles-only`, no passwords): it holds all eight roles with their exact attributes (service_role with
BYPASSRLS), `supabase_auth_admin`'s search_path and the three `authenticator` memberships; the dump holds the function
and the three `GRANT USAGE ON SCHEMA public` lines. Restore, posture and drift then all pass.

An ignore entry is an allowlist of one exact fact, never of a name. Each pattern in `backend/db/drift_ignore.txt` matches
the whole snapshot line including every attribute, so a changed attribute (service_role losing BYPASSRLS, gaining
CREATEROLE or SUPERUSER, `anon` gaining LOGIN), a new BYPASSRLS role, or a table grant to `authenticated` leaves the live
line unmatched and the gate goes RED. The self-test proves each of these. Writing it found a blind spot that existed before
this change: the snapshot skipped every superuser role, so a hand-made superuser was invisible. The snapshot now lists every
role except the bootstrap `postgres`, and the role line carries `superuser=`.

What an ignore entry cannot see is the object VANISHING: both sides lose the line. `service_role` is covered by a posture
check (`service_role exists and holds BYPASSRLS`, ABSENT when missing). The other ignored roles are not covered by anything
except GoTrue failing loudly. Measured on a restored copy: with `service_role` dropped the posture file fails that check
and the drift gate says nothing about it.

Production has no ledger yet (`ofc_meta.schema_migrations` does not exist), so the gate compares it with ALL migrations.
Recording 0001 and 0002 with `db_migrate.sh adopt` is still an owner step: adopt creates the `ofc_meta` schema and inserts
ledger rows, which is a write to production. It is `adopt`, not `apply`, that is right once the owner takes that step: both
files describe what already exists, and `apply` would replay the whole baseline against a live database.

## Restore rehearsal

`pg_restore --list`, which `scripts/backup_database_to_nas.sh` runs on every dump, reads the archive's table of
contents. It proves the archive PARSES. scripts/verify_restore.sh (ARCHIVE, optionally --roles FILE) proves it
RESTORES: it starts a throwaway PostgreSQL 18.7 container (the production image), runs pg_restore into it, then checks
the restored copy with the two existing gates, `backend/tests/test_database_posture.sql` and the `--live` mode of
`scripts/check_schema_drift.sh` (pointed at the restored copy through its own `DB_TARGET_LIVE` mechanism). The verdict
is PASS only when all three steps pass; exit 0 PASS, 1 FAIL, 2 could not run. The container is removed on every exit
path. It reuses `scratch_start` and `pg_exec` from `scripts/db_common.sh`; the only change there is an optional
`SCRATCH_PGDATA`, because the hosted image's wrapper refuses to start otherwise.

What it proves: pg_restore completes without error; the restored copy has RLS enabled and forced with its policies,
no BYPASSRLS on application roles, armed guards, and a schema (columns, constraints, indexes, grants, policies, roles'
attributes, guard rows) equal to what the recorded migrations build.

What it does NOT prove:

- Row contents. Tables are checked for shape, not for the right rows or the right count. The one data check is the
  guard list and the GoTrue token columns, inside the posture file.
- The four TLS checks of the posture file. They assert the SERVER's settings, which no archive carries. The hosted
  image ships ssl_ciphers without the exclusions the file expects, so the script sets them on the rehearsal server
  first; those checks are not evidence about the backup.
- That production's roles can be rebuilt EXACTLY. `scripts/backup_database_to_nas.sh` now writes the roles file
  (see Backups below) and feeds it to this script, so the restore is rehearsed with the roles it will really need.
  What is not rehearsed is a target that forbids creating superusers or has a different `postgres` role.
- That production is a restorable size and time. The rehearsal restores into a small scratch database.
- Anything about a PostgreSQL minor version other than 18.7, or extensions the image does not carry.

The backup script runs this on every OFC backup (next section). To run it by hand on a stored pair, pass the dump and
its sibling roles file with --roles; neither needs a production connection and this script never makes one.

STATUS: this has only ever been run on scratch data, never on a real production backup. Measured 2026-11-25 on a
database built from `backend/db/migrations/0001_baseline.sql`, dumped as the backup script dumps it:

| Archive | restore | posture | drift | would `pg_restore --list` catch it |
|---|---|---|---|---|
| intact, roles supplied | PASS | PASS (28 of 28) | PASS | n/a |
| last 100 bytes cut off | FAIL | FAIL | FAIL | NO: it passes up to a cut of about 500 bytes here and fails at 1000 |
| one RLS policy dropped before the dump | PASS | FAIL | FAIL | NO: it lists six policies instead of seven and exits 0 |
| roles not supplied | FAIL | FAIL | FAIL | NO: it never looks at the cluster |

Finding that this rehearsal made: the backup script used `--no-privileges`, which drops every GRANT. A healthy restore
then had the right rows and policies and no role could read anything, and the drift gate went red on the grants. The
flag is removed from the backup script (`--no-owner` stays). A dump written before that change fails step 3.

## Backups: what the script does, and why

`scripts/backup_database_to_nas.sh` writes, for the OFC database, a PAIR with one stamp:
the archive (name-STAMP.dump), its roles file (name-STAMP.roles.sql, from `pg_dumpall --roles-only`) and the
verification log (name-STAMP.verify.log). Roles are cluster-level and live in no archive, so a dump alone cannot be
restored into a fresh instance: `ofc_app` and `ofc_refresh` would not exist and every policy and grant naming them fails.

Decisions, with the reasons:

- Roles file: WHOLE CLUSTER, not filtered. Filtering to the roles this database uses means parsing generated SQL or
  walking pg_auth_members, ACLs, default privileges and policies by hand; a role missed there is a restore that fails
  on the day it is needed, and the file is under 1 KB. The cost of whole is that it names superusers and carries
  their attributes; that is a list of names and flags, not a secret. The secret part is the password hash, and the
  file is written with `--no-role-passwords`, so no hash exists to reach a log or the NAS (the script also fails the
  backup if one appears, and its redaction removes SCRAM and md5 strings from anything it prints). Set
  KEEP_ROLE_PASSWORDS=1 to keep them; then the file is a credential and must be handled like one. After a restore from
  a password-less file, set each login role's password again from the service's own variables. The script requires
  `ofc_app` and `ofc_refresh` to be present in the file and fails the backup otherwise. `verify_restore.sh` drops the
  file's `postgres` lines, because the scratch server already has that role.
- Verification: EVERY backup, inline. Measured 2026-11-25 on scratch data with the production image (18.7): the
  small schema backs up and verifies in 16 to 19 seconds end to end. A 2.5 GB database (larger than the production
  volume's 1.1 GB, with an index) dumped in 36 seconds and restored in 19 seconds. So a full verify costs well under
  a minute plus a fixed 15 seconds, against a script that runs unattended where no person looks at a log. A weaker
  periodic check would leave days in which a "successful" backup is unproven, which is the defect this fixes. If the
  restore time ever grows past what a daily run can afford, shorten the cadence of verification, not its rigor, and
  say so here.
- Order: the new pair is written to a staging directory and moved beside the good ones only after it verifies. A
  failure exits 1, prints which step failed and what to do, parks the failed files under failed/ with the suffix
  .failed, and touches nothing else. Pruning (KEEP_DAYS, default 30) runs only after a verified pair lands and never
  removes the newest KEEP_MIN (default 3) pairs, so a run of failures cannot age out the last good copy.
- The client runs in a container of the production image, because pg_dump refuses a server newer than itself and the
  host's client was 14 against production 18.7 (set OFC_PG_CLIENT_IMAGE=host to use host tools).
- The mortgage database is verified by its own rehearsal; see "The mortgage database" below.

Proof (2026-11-25, scratch container, nothing hosted): a green run produced the pair and verify_restore.sh consumed
the roles file (restore, posture 28 of 28, drift all PASS) and exit 0. With one RLS policy dropped in the source,
the same command exited 1, printed the drift failure, reported "BACKUP FAILED", left the previous pair untouched and
parked the new one under failed/, with the most aggressive prune settings (KEEP_DAYS=0, KEEP_MIN=0) set.

## The mortgage database

What it is: the Railway service `mfv-postgres` (PostgreSQL 18.7, database `mortgagefv`), not a Supabase project.
Evidence, from the services' own variables (hosts and database names only, no values): `mfv-postgrest` and `mfv-auth`
both connect to `mfv-postgres.railway.internal` database `mortgagefv`, and the web app's Supabase URLs are the
Railway domains of `mfv-gateway` and `mfv-auth`. It has no public endpoint and no `DATABASE_URL` variable, so
railway connect --tunnel-only cannot open it; an SSH tunnel to the service (ssh -N -L 127.0.0.1:PORT:127.0.0.1:5432
to the host railway ssh config --dry-run prints) does. The tunnel was measured to stop forwarding after a few
minutes, so open it just before the run. The URL is built from the service's POSTGRES_USER, `POSTGRES_PASSWORD` and
POSTGRES_DB with the host rewritten to the tunnel, and handed to the backup script as `MFC_DATABASE_URL`.

What a backup writes (one stamp): the archive, a pg_dumpall --roles-only --no-role-passwords file, a fingerprint of the
source taken right after the dump (scripts/mortgage_fingerprint.sql: names, counts and role membership, no row
contents), and the verify log. scripts/verify_restore_mortgage.sh restores them into a throwaway container of the
production image and requires four steps to pass: the roles file loads and `anon`, `authenticated`, `authenticator`,
`service_role` and `supabase_auth_admin` exist; `pg_restore` exits 0; the restored schemas, table names, role
memberships, grant count, policy count and RLS count equal the source's; every table with rows in the source has rows
in the copy, and `auth.users` is populated. The reference is the source itself, because there is no migration
baseline for this database and none is invented.

What it proves: the archive and roles file restore together into a clean server and the copy has the source's shape
and no populated table came back empty. What it does not prove: row contents or exact counts (the source takes writes
between the dump and the fingerprint), that the app works against the copy, or anything about other images.

Wrong-database guard: every client run refuses a connection string without an explicit host and database name, with
`host=`, `hostaddr=`, `service=` or `dbname=` in its query, or with a percent-encoded host, because libpq then falls
back to the local socket and would dump whatever database is there. The archive's own `dbname` must also equal
`MFC_EXPECT_DB` (default `mortgagefv`). A missing variable is a FAIL, not a skip.

Measured 2026-11-25 against production (read-only): green run, 37 tables, 217 grants, 25 policies, 21 RLS tables,
15 populated tables, verified in 16 seconds. Damaged inputs, each exit 1: archive cut to half (restore, shape, data
fail); last 100 bytes cut (restore and shape fail); roles file without the app roles (roles, restore, shape fail);
a complete schema with no data (only the data step fails); wrong expected database name (refused before any restore).

## Scheduled backups (unattended, with failure made visible)

Nothing scheduled the backup script before 2026-11-25, and the script could not reach production on its own, which is
how three ad-hoc dumps from 2026-09-15 were taken for "the backups". The production database has no public
endpoint, so the route is: Railway SSH tunnel, then `railway run` to inject the service's DATABASE_URL, then a rewrite of
that URL's host and port to the tunnel, then the existing verified backup. scripts/backup_production_db.sh does all of
it and ALWAYS closes the tunnel; scripts/check_backup_freshness.sh is the alarm. Units are under deploy/systemd/user.

Decisions, with the reasons:

- The wrapper calls `railway run` itself. A timer starts a bare command; making the caller wrap it would put the service
  name and linked project in a second place, and hand the Postgres service's variables to the tunnel and the
  pre-flight checks. Now the secret-bearing environment exists in one child only. The wrapper re-invokes itself
  with --inner under `railway run`.
- It can authenticate from a systemd user unit. Measured 2026-11-25: with an empty environment (env -i, only HOME and
  PATH) `railway whoami` succeeds from the stored login, and a verified production backup ran to completion as a
  transient unit under the user manager (systemd-run --user --wait) with no terminal, display or ssh-agent. A
  RAILWAY_API_TOKEN in the environment overrides the stored login (a bogus one is rejected with Unauthorized), so an
  account token is a supported alternative; it was not tested against the tunnel because none exists yet. Two traps:
  railway is installed under the home directory's .railway/bin, which no systemd PATH contains (the unit sets PATH), and
  the stored access token lives about an hour and is refreshed from a refresh token on each CLI use. A daily
  timer therefore depends on that refresh token staying valid between runs. That was NOT measured beyond one day. If it
  lapses, the next run fails with "railway login is not valid", exit 2, and the freshness check alarms. The
  durable fix is a Railway account token in a 0600 environment file named by EnvironmentFile in the unit, and it is the
  owner's to mint.
- The tunnel is railway connect Postgres --tunnel-only -P PORT, on a free port the wrapper chooses. Readiness is a
  TCP connect to the port, retried for up to 60 seconds, and a port that already accepts is refused (a fixed port once
  answered with another session's tunnel and the dump failed with "database does not exist"). `railway connect` starts an
  ssh child in its own session, so killing the CLI's process group left the ssh process listening, orphaned (measured).
  Teardown therefore judges the PORT: SIGINT to the PID the wrapper started, then SIGTERM to its group, then SIGTERM and
  SIGKILL to the listener found by port, only if its command line is an ssh forward for exactly that port and it is
  owned by this user. Never by name. After teardown the wrapper probes the port and reports a leak as a failure. The
  unit's cgroup is a second net: stopping it kills every process of the run.
- Persistent=true on the backup timer. A run missed because the machine was off fires once at the next start. The result
  is a real dump stamped with the time it ran, so it cannot pass for an older one, whereas skipping it lets the last
  backup age with nothing happening. At boot the network or the NAS may not be up yet: the wrapper checks the NAS is
  writable first, retries the tunnel three times, and a failure there is a visible failure, not a skip.
- Time: 03:07 UTC daily, clear of Railway's volume backups (04:19 product, 04:36 mortgage).
- Only the OFC database is scheduled. The wrapper sets BACKUP_TARGETS=ofc: the Supabase database has a different
  credential and no route through this tunnel, and an always-SKIPPED half would make every run exit 1.

How failure and silence become noticeable:

- The wrapper writes a status file (default ~/.local/state/ofc-backup/status) at the start of every attempt as
  "running" and at the end as ok or fail, from an EXIT trap, so a signal still records a failure. Success is recorded only
  when the backup exited 0, printed VERIFIED for the production database, and the verified pair (dump, roles, verify log
  with VERDICT: PASS) is on the NAS.
- check_backup_freshness.sh exits 1 when any of these holds: no verified pair exists; the NAS directory is missing; the
  newest verified pair is older than N days (default 2) by the STAMP IN ITS FILE NAME, so touching or copying a file
  cannot refresh it; the stamp is in the future; the last attempt failed; an attempt started over two hours ago and never
  finished. A missing status file is not a problem by itself (a hand-run backup leaves none) but the NAS evidence must
  hold. "Never ran" and "ran and failed" are both exit 1.
- With --alert it also logs at crit to the journal (tag ofc-backup), sends a desktop notification if one is available,
  and writes ALERT-BACKUP-STALE.txt beside the backups on the NAS and in the state directory; the next healthy check removes
  them. No webhook, no credentials.
- Two timers on purpose. The backup service has OnFailure= pointing at the freshness service, so a failure alarms
  at once. The freshness timer runs every six hours, independently, so a backup timer that was never enabled,
  was removed, or never fired is also caught. What this cannot catch: the whole machine being off or the user manager not
  running (linger is enabled here, so the manager runs without a login). Only something off this host would see that.

Install (owner step; nothing is enabled by this repository):

    mkdir -p ~/.config/systemd/user
    cp deploy/systemd/user/ofc-backup.service deploy/systemd/user/ofc-backup.timer \
       deploy/systemd/user/ofc-backup-freshness.service deploy/systemd/user/ofc-backup-freshness.timer \
       ~/.config/systemd/user/
    systemd-analyze verify --user ~/.config/systemd/user/ofc-backup*.service ~/.config/systemd/user/ofc-backup*.timer
    systemctl --user daemon-reload
    systemctl --user enable --now ofc-backup.timer ofc-backup-freshness.timer
    systemctl --user list-timers 'ofc-backup*'

Enabling starts a daily production read through the SSH tunnel and a six-hourly check; the check alarms until the first
verified run exists. To try it by hand first:
systemctl --user start ofc-backup.service, then journalctl --user -u ofc-backup.service. Check by hand any time:
scripts/check_backup_freshness.sh; its exit status is the answer. To see an alarm at login, add
scripts/check_backup_freshness.sh --quiet to a shell profile.

Proof (2026-11-25, production, through the tunnel): a verified backup took 26 to 35 seconds end to end with an empty
environment and as a systemd transient unit, and the port was closed afterwards each time. A logged-out run exits 2 before opening
a tunnel; a failure after the tunnel was open (client image missing) exited 1 with the tunnel closed; SIGTERM mid-run
closed the tunnel, recorded the failure and removed the staging directory. The freshness check exited 1 for: an empty
directory, a verified pair stamped five days ago with a fresh mtime, a missing NAS directory, a fresh dump whose
verify log said FAIL, and a last attempt that failed.

## Disaster recovery runbook

Decision rule: if the Postgres major version is staying the same and you want the whole cluster back as it was,
use the Railway volume backup (path A); if the volume is gone, the version is changing, or the volume backups are
suspect, rebuild from the logical dump and its roles file (path B).

Before either: stop writers (scale the engine and GoTrue to zero or pause deploys) so nothing writes to a database
that is about to be replaced; note the time; do not run any deploy script until the end.

### Path A: Railway volume backup restore

What it is: a snapshot of the whole database volume, so it brings back data, roles, grants, RLS and GoTrue's tables
exactly as of the backup. It cannot cross a major PostgreSQL version. Daily, weekly and monthly schedules exist on
both database volumes.

1. In the Railway dashboard, open the Postgres service, then its Backups tab, and note the backup you will use
   and its timestamp. Everything written after that timestamp is lost.
2. State plainly what you are about to destroy: restoring replaces the volume's CURRENT contents with the backup.
   Through the API this is the volumeInstanceBackupRestore mutation, and its wipeServiceIds argument names
   services whose volume data is WIPED as part of the restore. Pass only the Postgres service, never the engine,
   GoTrue or any sensen service. I could not run this mutation from the build session; read the argument types from
   Railway's current API schema before using it, and prefer the dashboard's Restore button for a first use.
3. Before restoring, if the current volume is readable at all, take a manual backup of it so the restore is undoable.
4. Restore. Wait for the Postgres service to report running, then check its log shows recovery completing and the
   server ready to accept connections.
5. Start GoTrue, then the engine, one at a time. For the engine, confirm from the deployment's own log (not the
   service's) one "model is LOADED" line per replica and no ERROR lines.
6. Check: sign in, open a saved scenario (this proves `ofc_app` and the RLS path), and run
   scripts/check_schema_drift.sh --live through the credential loader; it must print GREEN.

### Path B: logical dump restore (can cross a major version)

What it is: the latest VERIFIED pair on the NAS (the newest name-STAMP.dump with its name-STAMP.roles.sql, under
the optionsandfuturescalculator directory of the backup root). Anything in failed/ is untrusted.

1. Create the empty target PostgreSQL (new service or new major version). Note its connection string; do not paste
   it into a ticket or chat.
2. Rehearse first if time allows: scripts/verify_restore.sh DUMP --roles ROLES against a scratch container takes
   about a minute and tells you whether this pair is any good. If it says FAIL, use the next older pair.
3. Load the roles file first, as a superuser, skipping the `postgres` lines: grep -v -E '^(CREATE|ALTER) ROLE postgres[ ;]'
   ROLES, piped to psql with ON_ERROR_STOP=1 against the postgres database. If the target is a managed service that
   forbids superuser attributes, edit those attributes out of the copy, not the original.
4. Set a password for each login role from the service's own variables (the file has none by design). `ofc_app` and
   `ofc_refresh` are NOLOGIN; leave them so.
5. Create the database, then pg_restore --no-owner into it using a client of the TARGET's major version (an older
   client cannot read a newer archive).
6. Verify: run backend/tests/test_database_posture.sql and scripts/check_schema_drift.sh --live against the new
   database. Both must pass before any traffic is pointed at it.
7. Point DATABASE_URL (engine and GoTrue) at the new server, restart GoTrue then the engine, and check as in path A
   steps 5 and 6. Data written after the dump's stamp is lost; the stamp is in its file name.

Afterwards, in both paths: write down the time of loss and the backup used, and run scripts/backup_database_to_nas.sh
once so the next verified pair exists.

## Still missing

- One production backup has been run through this (2026-11-25, restore PASS, posture 37 of 37, drift PASS). The mortgage half is covered in its own section below.
- Backups are not scheduled by anything in this repository.
- The Railway restore steps were written from the API names, not run; step 2 of path A says so.
