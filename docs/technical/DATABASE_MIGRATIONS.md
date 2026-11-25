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
- That production's roles can be rebuilt. Roles are cluster-level and live in no archive. The --roles file must come
  from pg_dumpall --roles-only on the source cluster, and nothing in the backup script writes one yet. Without it the
  restore fails on every policy and grant naming ofc_app, which is the honest result.
- That production is a restorable size and time. The rehearsal restores into a small scratch database.
- Anything about a PostgreSQL minor version other than 18.7, or extensions the image does not carry.

Running it on a real backup (owner step): take a dump from the NAS directory the backup script writes, and a roles
file from the same cluster, then run scripts/verify_restore.sh with the dump and --roles. Neither needs
a production connection from this script; the script never connects to one.

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
