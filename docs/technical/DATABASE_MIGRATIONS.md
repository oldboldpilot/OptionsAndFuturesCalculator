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
- The MFC (Supabase) archive is NOT restore-verified and has no roles file: verify_restore.sh asserts the OFC schema,
  and a pooler connection cannot dump the cluster's roles. The summary line says "NOT restore-verified" for it.

Proof (2026-11-25, scratch container, nothing hosted): a green run produced the pair and verify_restore.sh consumed
the roles file (restore, posture 28 of 28, drift all PASS) and exit 0. With one RLS policy dropped in the source,
the same command exited 1, printed the drift failure, reported "BACKUP FAILED", left the previous pair untouched and
parked the new one under failed/, with the most aggressive prune settings (KEEP_DAYS=0, KEEP_MIN=0) set.

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

- No production backup has ever been run through this: the pipeline has only been exercised on scratch data.
- The MFC archive is parse-checked only. Backups are not scheduled by anything in this repository.
- The Railway restore steps were written from the API names, not run; step 2 of path A says so.
