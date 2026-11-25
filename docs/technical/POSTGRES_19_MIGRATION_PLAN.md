# PostgreSQL 19 migration plan

@author Olumuyiwa Oluwasanmi

PLAN ONLY. Nothing here has been executed, no live database was queried for it, and the plan is NOT ready to run.
Dated 2026-11-24; every external fact below was read on that day and is cited by where it was read.

## Read this first

1. **Recommendation: wait.** Do not migrate to 19 now, and do not move provider for it. Both Railway databases are on
   18.7, which upstream supports until 2030-11-14. The advisories of 2026-11-12 are closed. Nothing is forcing this.
2. **A 19 server will not start on an 18 data directory.** A major upgrade is not a tag change. Editing the image tag
   from 18 to 19 (once a tag exists) does not run pg_upgrade and does not convert the volume; the database goes down and
   stays down, and Railway's own guide says so (see the contradiction table for the page). The only supported ways to
   change major are the ones in section 2. If you remember one line of this document, remember this one.
3. **Three prerequisites are unmet, and they block everything below the "go" decision.** See "Prerequisites that are not
   yet met". A plan with unmet prerequisites is a reading list, not a runbook.
4. Railway publishes no 19 image today, so the cheapest route (Railway's in-place upgrade) does not exist yet.

## Prerequisites that are not yet met

These BLOCK the plan. Each states what "met" means, so it can be checked rather than asserted.

| # | Prerequisite | State on 2026-11-24 | Met when |
|---|---|---|---|
| P1 | The OFC database is reachable to tooling | NOT MET. The `Postgres` service has no TCP proxy, `railway ssh` was refused, and the credential loader refuses a shell or psql as its child. Its live version, RLS policies, roles and extensions are UNMEASURED. | The owner has run a read-only capture, or a TCP proxy exists, and `SELECT version()`, the `pg_extension` list, `pg_policy`, and the role attributes are written down from the live server. |
| P2 | The 0001 baseline describes production, not the design | NOT MET. `backend/db/migrations/0001_baseline.sql` was replayed from nine legacy files in a scratch database. `docs/technical/DATABASE_MIGRATIONS.md` calls it PROVISIONAL. | The four owner steps in that document's "Adopting production" section are done: `scripts/db_capture_baseline.sh` (--force), seal, `scripts/check_schema_drift.sh` (--live) printing green or every difference explained in the ignore file, then `scripts/db_migrate.sh` (adopt 0001). |
| P3 | A restore has been rehearsed end to end | NOT MET. `scripts/backup_database_to_nas.sh` writes a custom-format dump and reads its table of contents with `pg_restore --list`. That proves the archive parses. It does not prove it restores. No restore has ever been run. | The rehearsal in section 3 has run once against the CURRENT major (18) and its output is recorded. Rehearsing on 18 first separates "the procedure is wrong" from "19 broke it". |

Also unmet but not blocking the wait decision: Railway has not published a 19 tag, and the first 19 point release (19.1)
has not shipped.

## 1. Whether to go at all, and when

### What is verified

- Both services run the image `postgres-ssl:18.7`, digest `sha256:0abac5c0e29ac4fb1633aa644b4f3beaef29a05b4dc05f93190b5a7fbb0e7089`
  (Postgres service b089b0ae-8f6d-46ea-95e6-944c04886790, mfv-postgres e8e32e4b-e546-47a6-8ab1-078907700d40, project
  fearless-amazement, environment production 273bbc5d-b59f-4d36-9ef4-69b5d79dabaf). Verified earlier today by digest.
- postgresql.org/support/versioning: 19 first released 2026-11-19 (19.0), final release 2031-11-19; 18 current minor
  18.7, first released 2025-09-25, final release 2030-11-14. 14 is already unsupported as of that page.
- Docker Hub `postgres:19.0-trixie` exists (tag last updated 2026-11-24), so an upstream base image is available.
- Railway's `postgres-ssl` repository (github.com/railwayapp-templates/postgres-ssl, read via raw files and the GitHub
  API): Dockerfiles exist for 13 through 18, none for 19; the CI variable of supported majors is "13 14 15 16 17 18";
  the in-place upgrade job images are built for pairs that all end at 18 (14 to 18 through 17 to 18). No 19 branch and no
  open pull request mentioning 19 was found in the branch list or the 15 most recent pull requests.
- The repository is active: commits and merged pull requests dated 2026-11-23 and 2026-11-24, and an open pull request
  dated 2026-11-25. So the template is maintained; it is simply not on 19 yet.

### Will Railway publish 19, and when? Not knowable from what was found

No Railway changelog entry, roadmap or statement about 19 was found, and none is claimed here. The only evidence is the
lead time on past majors, read from the commit that added each Dockerfile (a commit is not a public release date, and
the date the dashboard first offered the version was not found):

| Major | Upstream first release | Railway added the Dockerfile | Gap |
|---|---|---|---|
| 17 | 2024-09-26 | 2024-10-01 | 5 days |
| 18 | 2025-09-25 | 2026-03-14 | about 5.5 months |

The most recent precedent is the slow one. A reasonable planning range is "days to roughly half a year", which is the
width of the evidence, not a forecast. The upgrade job also has to gain an 18 to 19 pair before the in-place route
exists; that pair is not in the CI matrix today.

### The options

| | Wait for a Railway 19 tag | Fork the Railway image and run 19 | Vanilla `postgres:19` on Railway | External managed Postgres |
|---|---|---|---|---|
| What changes | Nothing now; use Railway's upgrade flow later | A custom image built from Railway's repository with the base moved to 19 | The Docker Hub image as-is | Database leaves Railway |
| Cost of doing nothing | Zero. 18.7 is supported to 2030 | | | |
| Engineering cost | Smallest: dashboard flow plus our verification | Medium: own the image and its rebuilds | Largest: re-implement what the image adds (below) | Large: new provider, network path, auth.users moves |
| Main risk | Waiting is open-ended | We become the maintainer of a database image; Railway states custom images are not supported for its upgrade flow | TLS off or hand-built, volume layout different, no PITR | New latency and failure domain between engine and database |
| Gets 19 | Later, supported | Now, unsupported | Now, unsupported | Now (Neon lists 19 as available) |

**Recommendation: Wait.** There is no security reason (advisories closed), no support reason (18 runs to 2030), and the
first release of any major is the one most likely to receive early point fixes. Taking an unsupported image for a
database that holds users, profiles, saved scenarios and the GoTrue tables, in order to be on the newest major number,
trades a known-good platform for a maintenance burden. Re-open the decision when ALL of these hold:

1. Railway has published a 19 tag of `postgres-ssl`.
2. Railway's guide lists 18 as a supported SOURCE major (today it does not; see the contradictions).
3. 19.1 or later exists upstream.
4. Prerequisites P1 to P3 are met.

Until then the useful work is the prerequisites, which pay for themselves on any future change, including 18.x minors.

### What `postgres-ssl` adds over vanilla `postgres`

Read from the repository's README, its Dockerfile for 18, `init-ssl.sh`, `wrapper.sh` and the workflow, on 2026-11-24.
This is the crux of the self-managed option: every row is something a vanilla image does not do.

| Concern | What Railway's image does | Consequence of vanilla |
|---|---|---|
| TLS | `init-ssl.sh` generates a root CA and a server certificate at first init (valid 820 days by default, SSL_CERT_DAYS), with SAN for localhost and the service's private domain, writes `ssl = on` and the file paths into the config. On every boot the wrapper regenerates it if it expires within 30 days or lacks the SAN. | No TLS at all. Our engine connects with `sslmode=require` (documented in `backend/src/modules/pg.cppm`), so the engine would be refused. TLS must be rebuilt, including renewal. |
| Entrypoint | `wrapper.sh` under tini as PID 1, stop signal SIGINT, so a stop is a clean shutdown and not a kill followed by crash recovery. | Vanilla's entrypoint as PID 1 can turn every restart into a hard kill. Our own notes already record that container PID 1 ignores unhandled signals. |
| Volume safety | Refuses to boot if the volume is not mounted at the expected data path, if PGDATA is outside it, if the image major differs from the on-disk PG_VERSION, or if an upgrade marker is incomplete. Holds a lock so two containers cannot share a volume. Removes a stale postmaster.pid. | None of these guards. A wrong mount silently initialises an empty database. |
| Listening | The default command fixes the port at 5432 and `listen_addresses=*`. | Must be set; vanilla listens as its own config says. |
| Access rules | Re-appends the remote `host all all all` rule if pg_hba.conf was reset to initdb's default. | Vanilla writes it once at init. |
| Extensions and preload | Ships pgvector and pgaudit, and preloads pg_stat_statements,pgaudit via shared_preload_libraries. | If OFC's database has anything depending on those, it breaks (unmeasured, P1). |
| Collation safety | Distro pinned to Debian trixie; detects a glibc collation-version change and reindexes affected indexes before refreshing stamps. | A base-image move can silently corrupt text indexes; nothing watches for it. |
| Backups and PITR | pgBackRest, opt-in through the `WAL_ARCHIVE_*` variables: continuous WAL archiving to S3-compatible storage, base-backup scheduler, retention, restore to a time. Railway's managed point-in-time restore provisions the restored service through this contract. | No PITR. Volume snapshots (below) remain. |
| Major upgrades | A job image that runs `pg_upgrade --link` against the stopped volume, keeps the old cluster 24 hours as the rollback body, carries pg_hba across, re-applies settings, and reindexes on collation change. | The dashboard flow is unavailable: Railway's guide says custom images are not supported for it. |
| Logging | Optional LOG_TO_STDOUT so Railway reads severity correctly. | Every Postgres log line arrives as an error. |
| Data path | Volume mounted at `/var/lib/postgresql/data`, which the wrapper enforces. | Upstream's own Dockerfile for 19 sets PGDATA to `/var/lib/postgresql/19/docker` and the VOLUME to `/var/lib/postgresql` (the layout moved in 18). A volume mounted the old way is not found. |

Not determined: the service's actual PGDATA and variable set on Railway (service variables were not read), whether
Railway's dashboard features (Extensions card, Backups tab, metrics) depend on anything beyond this repository, and
whether PGDG ships pgvector and pgaudit packages for 19 yet.

A middle path exists and is better than vanilla: Railway's own docs invite forking the repository. A fork with the
base moved to 19 keeps every row above. It is still unsupported for upgrades and we would carry the rebuild cadence
(the upstream image is rebuilt on a schedule for OS and extension fixes). It is an option, not a recommendation.

### External managed Postgres

Neon was checked because it was named. Its page "Postgres version support" (last updated 2026-11-20) says it supports
14 through 19, 18 is the default for new projects, 19 must be selected at project creation, 19 does not yet support its
Data API, Realtime or data anonymization, and some extensions are limited on 19. The same page says: "Upgrading to a
new major version requires creating a new project and migrating data." That contradicts the premise that a major
upgrade is a branch-and-swap; on the evidence read, it is the same dump/restore or logical-replication move as
anywhere else, only into a new project.

What was NOT measured, and which would decide this option: latency from Railway's region to the provider's, behaviour
of the engine's connection handling and `statement_timeout` through a pooler, cost at our size, connection limits,
whether `SET LOCAL ROLE ofc_app` and role attributes survive the provider's role model (managed providers restrict
SUPERUSER and often BYPASSRLS, and RLS is the product's isolation), and where GoTrue connects. Two structural costs
are known without measuring: the engine and GoTrue currently reach the database over Railway's private network and
would cross the public internet or a paid private link; and `auth.users` is in this database, so GoTrue's whole schema
moves, and GoTrue must be repointed at the same time as the engine.

Do not move for 19 alone. Revisit only if Railway proves unable to serve a supported route within a period the owner
chooses.

## 2. The mechanism, if it goes

Order of preference once a Railway 19 tag and an 18 to 19 pair exist: Railway in-place upgrade; then dump and restore
into a new service; logical replication last.

### What can run on Railway

- Two majors side by side: yes, as two separate services, each with its own volume. Not in one container.
- The data directory: reachable only inside the service. OFC's database currently has no TCP proxy and `railway ssh`
  was refused, so file-level access is not available to us now (P1).
- Railway's own flow runs pg_upgrade in a job image against the stopped volume (documented on its major-upgrade page).

### Options compared

| | Railway in-place (pg_upgrade --link) | Dump and restore into a new 19 service | Logical replication |
|---|---|---|---|
| Possible today | No: no 19 image, no 18 to 19 job | No for 19 on Railway; yes in a scratch container for rehearsal | No for 19 on Railway |
| Possible later | Only if Railway lists 18 as a source (contested, below) | Yes, once any 19 service can be created | Yes, with a publication on 18 and a subscription on 19 |
| Downtime | Database stopped for the job. Railway: "typically a few minutes, depending on data size". Plan for 5 to 30 minutes including redeploy and our verification, because link-mode work is metadata-sized but our checks and dependents' reconnects are not | Dump, restore, repoint, verify, all with writes stopped. Unmeasured: depends on table sizes, which are unknown (P1). Pure transfer is probably single-digit minutes for a database of this kind, but the range to plan is 15 to 90 minutes | Seconds to minutes at switchover; days of setup |
| Failure modes | Preflight blockers (event triggers and md5 roles are named warnings; our migration 08 uses event triggers); job fails mid-way; revert loses post-upgrade writes | Roles missing in the target; `DROP SCHEMA` guard from migration 08 blocking restore; extension version mismatch; sequences not advanced; GoTrue columns | Replicated roles do not exist; DDL is not replicated; sequences are not replicated; the schema must be pre-created; CVE-2026-120145 (logical replication writes subscriber catalogs) was in the 2026-11-12 advisory set and is fixed in 18.7 but the feature is the largest surface |
| Rollback | Revert banner restores the pre-upgrade backup; old cluster kept 24 hours | Old service untouched; repoint back | Old service untouched until cutover |

The CVE number is quoted from this repository's earlier plan (`docs/technical/POSTGRES_UPGRADE_PLAN.md`), where it was
read from the advisory table, not from its own page. Open the page before quoting it elsewhere.

The downtime figures are ranges built from reasoning, not measurements. The one measurement that would narrow them is
the live database size, which only P1 provides.

### Named step: roles and their attributes (RLS depends on it)

Roles are cluster-level. A dump of one database does not contain them, and a restore into a new cluster will not
create them. If this step is skipped the restore SUCCEEDS and row-level security quietly protects nothing, or the
engine's `SET LOCAL ROLE ofc_app` fails. Treat it as a gate, not a footnote:

1. Before restoring data, create `ofc_app` and `ofc_refresh` with their exact attributes (NOSUPERUSER NOCREATEDB
   NOCREATEROLE INHERIT NOLOGIN NOREPLICATION NOBYPASSRLS). The baseline in `backend/db/migrations/0001_baseline.sql`
   opens with exactly this section.
2. Create the other application roles named in the live capture from P1 (for example `service_role`, which must keep
   BYPASSRLS, `supabase_auth_admin` and `authenticator`, named in the engineering notes).
3. Restore the data.
4. Assert, on the TARGET: SELECT rolname, rolbypassrls, rolcanlogin FROM pg_roles WHERE rolname IN (...) equals the
   source; every RLS-enabled table has FORCE where the source does; `pg_policy` counts match.
5. Run `scripts/check_database_posture.sh` logic and the drift gate against the target (section 3). They exist because
   a restore once dropped `service_role`'s BYPASSRLS.

The drift snapshot covers the attributes of every non-superuser role, so a role that arrives wrong turns the gate red.

### Extensions

- Measured on `mfv-postgres` before tonight: pgcrypto 1.4 only.
- OFC: UNMEASURED (P1). The baseline's extensions section is empty, and the legacy files contain only the Supabase
  design's `uuid-ossp`, which was never deployed. That is not evidence of what is loaded in production.
- The Railway image preloads pg_stat_statements and pgaudit at the server level. A target that does not preload them
  can break any database that has pgaudit created. Check `pg_extension` on the live server and on the target.
- Railway's preflight reports each installed extension as missing from the target image (a blocker) or behind it
  (run ALTER EXTENSION ... UPDATE).
- pgcrypto is a contrib module and ships with the server; nothing here suggests it is missing on 19, but this was not
  tested. The rehearsal tests it.

### GoTrue on 19 (UNTESTED, and a hard gate)

Self-hosted GoTrue (image `supabase/gotrue:v2.151.0`, per `infra/supabase/README.md`) owns the `auth` schema in this
same database and runs its own migrations. Nothing has shown that version works against 19. What must be verified,
against a restored 19 scratch copy and before any cutover:

- GoTrue starts and its migrations report nothing pending or applied cleanly (it must not try to re-create objects).
- `GET /auth/v1/admin/users` returns 200, not the 500 that NULL token columns caused (migration 09 repaired those; the
  drift gate compares their defaults).
- A password sign-in, a token refresh and a sign-out succeed.
- A signed-in saved-scenario round trip through the engine succeeds (this exercises GoTrue's subject, the engine's
  verified token subject, `SET LOCAL ROLE ofc_app` and the RLS policy in one pass).
- GoTrue's connection setting (its `DATABASE_URL`, which was not read here) works with the target's TLS: our engine uses
  `require`; GoTrue's mode is unknown.

If GoTrue is incompatible, the migration stops there. Upgrading GoTrue at the same time as the database is two
changes in one window and is not permitted by this plan.

### What else to verify on 19

- The engine's schema assumptions: the engine runs its own queries and sets `statement_timeout` per connection;
  run the engine's database tests (`backend/tests/test_database_posture.sql` and the strategy-store tests) against the
  19 copy.
- Behaviour changes in the major's release notes. Not read for this plan: read the 19 release notes' incompatibilities
  section before any cutover, and do not rely on this document for them.
- Collation: moving glibc or ICU under existing text indexes needs the reindex step. Railway's image does this itself;
  a self-built path must do it by hand.

## 3. Backups, and what makes one real

A backup is real when it has been restored to a scratch server and that copy passes the same gates as production.
`pg_restore --list` is a parse check, nothing more.

### What exists (measured by reading the scripts)

- `scripts/backup_database_to_nas.sh`: custom-format dump per database, then `pg_restore --list` read-back. Refuses to
  keep a dump that fails the read-back. It does NOT restore.
- A Railway volume backup (the Backups tab on the service): incremental, copy-on-write, schedules of daily (kept 6
  days), weekly (27 days) or monthly (89 days), restore stages a change and mounts a new volume, leaving the old one
  unmounted (from Railway's backups page). Opt-in PITR needs `WAL_ARCHIVE_*` variables; whether it is configured here was
  not determined.
- Railway's in-place upgrade takes a backup before and after (its guide).

### The verification, command by command

There is no single command today; the rehearsal is these steps, using what landed in commit d419a77. Nothing is run
against production in this procedure other than the read-only dump.

1. Take the dump: `scripts/backup_database_to_nas.sh` with the connection string supplied through the credential
   loader (`scripts/run_with_env.py`), never sourced.
2. Start a scratch server on the TARGET major: SCRATCH_IMAGE=docker.io/library/postgres:19 plus the `scratch_start`
   function in `scripts/db_common.sh` (default image is `postgres:18-alpine`, set by `PG_IMAGE`). For the first
   rehearsal use 18, to separate procedure errors from version errors.
3. Create the roles (the named step above), then `scratch_db_create` and `pg_restore` the dump into it.
4. Row counts: every table in the five (users, profiles, saved_strategies, state_assumptions, inference_jobs) equals the
   source, read from the same snapshot time.
5. Posture check against the restored copy: `scripts/check_database_posture.sh` takes connection strings only, so run
   the SQL it wraps, `backend/tests/test_database_posture.sql`, through the scratch target, for example by piping the
   file to `pg_exec` with target `scratch:<container>:<db>` from `scripts/db_common.sh`.
6. Schema drift gate against the restored copy: `DB_TARGET_LIVE=scratch:<container>:<db> scripts/check_schema_drift.sh`
   with `--live`. The script reads `DB_TARGET_LIVE`, so a scratch database is a valid "live" side. It builds the expected
   schema on its own second scratch server and diffs the two snapshots. Green is the pass condition; any difference is a
   failed backup.
   - Caveat: on a restored copy the ledger may be absent (P2), in which case the gate compares against all migrations,
     and that is only meaningful once the baseline is real. This is why P2 precedes P3.
   - Caveat: if the two scratch servers run different majors, a catalog difference between majors can appear as drift.
     Run both on the same image for the rehearsal.
7. Prove the gates can fail: `scripts/check_schema_drift.sh` with `--selftest` must print `SELFTEST PASS` on the same
   machine first (it ran RED for ten deliberate drifts and GREEN on repair when it landed).
8. GoTrue and a saved-scenario round trip against the restored copy (section 2).

Record the outputs. A backup that has not passed steps 4 to 6 is labelled "unverified" in the log, not "taken".

## 4. Rollback, with a decision point

### The two routes and their points of no return

**Route A, Railway in-place upgrade (when it exists).** The database is stopped while the job runs, the pre-upgrade
backup is taken, and the service redeploys on the new image. The old cluster stays beside the new one as the rollback
body for 24 hours (the image's default grace period), then is deleted. Point of no return: the moment the grace period
ends AND writes have been accepted on 19; after that, "revert" means restoring a backup and losing every write since
the backup point (Railway: any writes made after the upgrade are lost on revert).

**Route B, dump and restore into a new service.** The old 18.7 service is left running and untouched, only its
`DATABASE_URL` consumers are repointed. Point of no return: the first write accepted by the 19 service that cannot be
replayed on the old one. Until that write, going back is a repoint. After it, going back means choosing between
discarding those writes or reconciling them by hand.

### The decision

| Item | Value |
|---|---|
| Signal (any one) | `scripts/check_database_posture.sh` red; `scripts/check_schema_drift.sh` (--live) red; a GoTrue admin endpoint non-200; a saved-scenario round trip fails; an ERROR line in the new deployment's own log after the upload; the engine's health failing for 5 consecutive minutes |
| Who decides | The owner, Olumuyiwa Oluwasanmi. No one else holds authority to continue past a red signal, and no automation proceeds on its own |
| Decide by | 30 minutes after the 19 service first serves traffic, before any maintenance-window write is allowed to be unrecoverable. If undecided at 30 minutes, the default is ROLLBACK |
| State afterwards (Route A) | 18 serving from the restored pre-upgrade backup; writes after the backup lost |
| State afterwards (Route B) | 18.7 service serving, unchanged; 19 service kept stopped for inspection; no data lost if no write reached 19 |

### Before crossing the point of no return, all of these must be true

1. A backup exists AND passed the verification in section 3 (not merely taken).
2. The rehearsal on a 19 scratch copy passed, including GoTrue and the saved-scenario round trip.
3. P1, P2 and P3 are met.
4. The roles step was done and its assertion printed equal attributes.
5. The cutover is confirmed from the deployment the deploy script prints, not from the service's log or a green
   healthcheck (the repository's own rule).
6. Writes are stopped (maintenance state) from the final dump to the repoint, so no write is lost between instances.
7. The owner has said go.

## Contradictions found in Railway's documentation

| Source | Says | Conflict |
|---|---|---|
| The page "Upgrading PostgreSQL Major Versions" | "Supported source majors: 14 through 17, upgrading to any newer major Railway publishes." | 18 is not a listed source. On this page an 18 to 19 in-place upgrade is not supported. |
| The page "PostgreSQL High Availability" | "Supported versions are 14, 15, 16, 17, and 18", and says HA clusters support in-place major upgrades. | Lists 18 as a version. Two pages, one platform, different ranges. Not resolved; ask Railway or test on a scratch service before relying on either. |
| The page "PostgreSQL" | "we do not plan to add extensions to the PostgreSQL templates" | The template repository's README says the image ships pgvector and pgaudit. |
| This repository's earlier plan | Described both databases as on 18.6 and the minor upgrade as the action | Now stale: both are on 18.7. The earlier plan's 18.6 to 18.7 section is complete. |

## What this document could not verify

- The OFC database's version, extensions, policies, roles and size (P1).
- Whether production matches the baseline (P2).
- That any restore works (P3).
- Whether Railway will publish 19, and when; lead-time evidence is two data points (5 days, about 5.5 months).
- Whether Railway's guide really excludes 18 as a source, given the HA page.
- Service variables: the actual PGDATA, `WAL_ARCHIVE_*` settings, SSL variables, and GoTrue's connection string.
- Whether PGDG packages for pgvector and pgaudit exist for 19.
- 19's release notes and incompatibilities.
- GoTrue v2.151.0 on 19.
- Any measurement of Neon or another provider: latency, cost, pooling, role model.
- The CVE numbers beyond the one earlier plan's table; none are asserted here.
