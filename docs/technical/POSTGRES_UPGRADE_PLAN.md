# PostgreSQL upgrade plan

@author Olumuyiwa Oluwasanmi

Plan only. Nothing here has been executed. The cutover needs the owner's approval.

## Measured state (2026-11-24)

| Database | Railway service | Image | Server |
|---|---|---|---|
| mortgagefv | `mfv-postgres` | `ghcr.io/railwayapp-templates/postgres-ssl:18` | PostgreSQL 18.6, pgcrypto 1.4 (SELECT version(), read-only) |
| OFC product | `Postgres` | `ghcr.io/railwayapp-templates/postgres-ssl:18` | UNMEASURED: no TCP proxy, `railway ssh` refused. Image major is 18; last deploy 2026-08-29, reason autoupdate |
| local engine tests | podman `sensen-postgres` | postgres by digest | not touched |

## Is it 19? Yes upstream, no on Railway

- postgresql.org/support/versioning lists 19 as released 2026-11-19 (19.0, the initial release), with 18.7, 17.12,
  16.16 and 15.20 as the current minors of the older majors.
- Docker Hub `postgres:19` and `postgres:19.0` exist.
- Railway's `postgres-ssl` image on ghcr.io has no `19` tag (registry tag list: 17.x and 18.x only, newest 18.7 and
  17.12). So 19 cannot be deployed on Railway today. Re-check the tag list before any 19 work.
- Railway's in-place upgrade guide lists source majors 14 through 17; the HA page lists 14 through 18. They disagree,
  so confirm 18 as a source before planning around it.

Both databases are therefore already on the newest major Railway offers. The upgrade that is available and urgent is
the MINOR one, 18.6 to 18.7. Moving to 19 should also wait for 19.1 or later, as is usual for a first major release.

## Advisories (postgresql.org/support/security, fixes published 2026-11-12)

All fixed in 18.7, 17.12, 16.16, 15.20. 18.6 is affected by every one that lists 18. The list was read from the
advisory table; only CVE-2026-120154 was also opened on its own page. Open each page before quoting it elsewhere.

| CVE | CVSS | Why it matters here |
|---|---|---|
| CVE-2026-120154 | 4.3 | INSERT ... ON CONFLICT reads rows despite row security: aimed at the RLS we depend on for `saved_strategies`, `users`, `profiles` |
| CVE-2026-116327 | 4.3 | security_barrier analysis can expose hidden values |
| CVE-2026-120153 | 7.2 | extension-script SQL injection (we load pgcrypto on mfv) |
| CVE-2026-120146 | 4.3 | REASSIGN OWNED bypasses SET and CREATEDB checks |
| CVE-2026-120145 | 8.8 | logical replication writes subscriber catalogs: relevant if logical replication is used for an upgrade |
| CVE-2026-116330 | 8.8 | pg_dump tar restore.sql psql command injection: we dump in custom format, not tar |
| CVE-2026-120151, -120150, -120149, -120148, -120147, -120143, -120141, -120139, -116335, -116332, -116331, -116329, -116326 | 8.1 to 8.8 | memory or type-confusion defects executing arbitrary code, reachable by an authenticated SQL user |

Most need an authenticated SQL session, and the engine's role is the only application login, so exposure is lower than
the scores. The RLS one is the exception that bears on the product's own security posture.

## Plan: 18.6 to 18.7 (recommended now)

1. Backup. `scripts/backup_database_to_nas.sh` (custom-format dump, `pg_restore --list` read-back) for both databases,
   plus a Railway Backup from the dashboard.
2. VERIFY the backup by restoring it, not listing it: start a scratch container (scratch_start in
   `scripts/db_common.sh`), `pg_restore` the dump into it, load roles with the baseline's role section, then run
   `backend/tests/test_database_posture.sql` and `scripts/check_schema_drift.sh` (--live) against the restore. Row counts
   of the five tables must equal the source's.
3. Before: record `SELECT version()`, `SELECT count(*)` per table, SELECT * FROM pg_extension.
4. Cutover: redeploy the service so tag 18 re-resolves to 18.7 (same major, same data directory, no pg_upgrade).
   Downtime is one Postgres restart. Every connected service (engine, GoTrue, PostgREST) reconnects.
5. Verify (below). Confirm from the deployment's own log, not the service's, per `CLAUDE.md`.
6. Rollback: a minor release is binary-compatible with the same data directory, so redeploy the previous digest
   (tag 18.6). Decision point: if the log shows an ERROR line or the posture check is red within 15 minutes, roll back.
   Restore from backup only if the data directory is damaged.

## Plan: 18 to 19 (later, when Railway ships it)

Mechanism options, in order of preference:

- In-place pg_upgrade from Railway's Database, Config, Major Version Upgrade. Requires the official image pinned to a
  major tag, takes a backup before and after, database stopped for typically a few minutes, revert window of unstated
  length, and writes after upgrade are lost on revert. Event triggers (our migration 08 guards) and md5 roles show up
  as preflight warnings. Do not edit the image tag by hand: that skips pg_upgrade and can leave the database unable
  to start.
- Dump and restore into a NEW service: `pg_dump` custom format, `pg_restore` into a fresh 19 service, repoint
  `DATABASE_URL`. Downtime equals the dump plus restore plus cutover, small at this data size. The old service stays as
  the rollback. Roles are not in the dump: apply the baseline's role section first, then check `service_role` still has
  BYPASSRLS (the posture check exists because a restore once dropped it). Restoring `DROP SCHEMA` guards need
  `app.allow_destructive`.
- Logical replication: no benefit at this size and CVE-2026-120145 is a reason to avoid it.

GoTrue: `supabase/gotrue:v2.151.0` owns `auth.*` and migrates it itself. Rehearse GoTrue against the 19 scratch
restore before cutover; this plan does not assume it works. Its token columns are what migration 09 repairs, so run the
drift gate afterwards. Extensions: pgcrypto only (measured on mfv); measure OFC with SELECT * FROM pg_extension.

Rollback for the major: repoint to the old service (dump route) or use the revert banner (in-place), accepting the loss
of writes since cutover. Decision point: posture or drift check red, or GoTrue admin endpoints non-200.

## Post-upgrade verification

```sql
SELECT version();
SELECT extname, extversion FROM pg_extension ORDER BY 1;
SELECT relname, relrowsecurity, relforcerowsecurity FROM pg_class
 WHERE relnamespace = 'public'::regnamespace AND relkind = 'r' ORDER BY 1;
SELECT count(*) FROM pg_policy;                              -- 7 expected on the designed schema
SELECT rolname, rolbypassrls FROM pg_roles WHERE rolname IN ('ofc_app','ofc_refresh','service_role');
SELECT evtname, evtenabled FROM pg_event_trigger;
```

Then `scripts/check_database_posture.sh`, `scripts/check_schema_drift.sh` (--live), GoTrue
`GET /auth/v1/admin/users` (200, not the NULL-scan 500), and a signed-in saved-scenario round trip through the engine.
