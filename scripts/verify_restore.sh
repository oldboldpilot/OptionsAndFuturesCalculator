#!/usr/bin/env bash
# RESTORE REHEARSAL: prove a backup archive restores, and that what came back is the database.
#
# @author Olumuyiwa Oluwasanmi
#
#   verify_restore.sh ARCHIVE [--roles FILE] [--image IMAGE]
#
#   ARCHIVE      a pg_dump custom-format archive, as scripts/backup_database_to_nas.sh writes
#   --roles FILE the CLUSTER's roles, as `pg_dumpall --roles-only` writes them. Roles are
#                cluster-level and live in no database dump, so without this file the restore
#                starts with none and anything that names ofc_app fails. That failure is the
#                correct verdict: it is what a disaster restore into a new instance looks like.
#   --image IMG  scratch server image (default: the production image, PostgreSQL 18.7)
#
# WHY. backup_database_to_nas.sh runs `pg_restore --list`, which reads the archive's table of
# contents. That proves the archive PARSES. It does not prove it RESTORES: a truncated archive
# still has its whole TOC, because the TOC is written first. This script does the restore, into
# a throwaway container, and then checks the restored copy three ways:
#   1 RESTORE  pg_restore runs to completion with no error
#   2 POSTURE  backend/tests/test_database_posture.sql passes on the restored copy
#   3 DRIFT    scripts/check_schema_drift.sh --live finds the restored copy equal to what its
#              recorded migrations build (RLS flags and policies, roles, grants, guard rows)
# Verdict: PASS only if all three pass. Exit 0 PASS, 1 FAIL, 2 could not run (not green).
#
# Reuses db_common.sh (scratch server, pg_exec) and check_schema_drift.sh (its own DB-target
# mechanism, DB_TARGET_LIVE). Touches no hosted database: the only database involved is the
# container this script starts and removes, on every exit path.
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/db_common.sh"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
POSTURE="$REPO/backend/tests/test_database_posture.sql"

ARCHIVE="" ROLES=""
SCRATCH_IMAGE="${SCRATCH_IMAGE:-ghcr.io/railwayapp-templates/postgres-ssl:18.7}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --roles) ROLES="${2:-}"; shift 2 ;;
        --image) SCRATCH_IMAGE="${2:-}"; shift 2 ;;
        -h|--help) sed -n 3,12p "$0"; exit 0 ;;
        -*) echo "unknown option $1" >&2; exit 2 ;;
        *) ARCHIVE="$1"; shift ;;
    esac
done
[[ -n "$ARCHIVE" && -f "$ARCHIVE" ]] || { sed -n 3,6p "$0"; echo "FATAL: archive not found" >&2; exit 2; }
[[ -z "$ROLES" || -f "$ROLES" ]] || { echo "FATAL: roles file not found" >&2; exit 2; }
# exported so check_schema_drift.sh builds ITS scratch server on the same image
export SCRATCH_IMAGE SCRATCH_PGDATA=/var/lib/postgresql/data/pgdata

WORK="$(mktemp -d)"; CONTAINER=""
cleanup() { [[ -n "$CONTAINER" ]] && podman stop -t 2 "$CONTAINER" >/dev/null 2>&1; rm -rf "$WORK"; }
trap cleanup EXIT
trap 'exit 2' INT TERM

R_RESTORE=FAIL R_POSTURE=FAIL R_DRIFT=FAIL
echo "== restore rehearsal: $(basename "$ARCHIVE") ($(du -h "$ARCHIVE" | cut -f1)) =="
CONTAINER="$(scratch_start)" || { echo "FATAL: could not start the scratch server"; exit 2; }
echo "scratch server $CONTAINER: PostgreSQL $(podman exec "$CONTAINER" psql -U postgres -Atc 'show server_version')"
# The posture file's TLS checks assert the SERVER's settings, which no archive carries. The
# hosted image ships ssl_ciphers without the exclusions the file expects, so the rehearsal
# server is set to the documented policy first; those four checks are then NOT evidence about
# the backup (see DATABASE_MIGRATIONS.md).
podman exec "$CONTAINER" psql -X -q -U postgres -c "alter system set ssl_ciphers = 'HIGH:MEDIUM:!3DES:!aNULL:!RC4:!MD5:!DES:!EXPORT'" -c "select pg_reload_conf()" >/dev/null 2>&1 \
    || echo "WARN: could not set the rehearsal server's TLS policy; posture TLS checks may fail for that reason"
scratch_db_create "$CONTAINER" restored || { echo "FATAL: could not create the target database"; exit 2; }
T="scratch:$CONTAINER:restored"

# roles first: they are cluster-level and the archive cannot carry them
if [[ -n "$ROLES" ]]; then
    if grep -v -E '^(CREATE|ALTER) ROLE postgres[ ;]' "$ROLES" \
        | podman exec -i "$CONTAINER" psql -X -q -U postgres -d postgres -v ON_ERROR_STOP=1 >"$WORK/roles.log" 2>&1; then
        echo "roles: loaded from $(basename "$ROLES")"
    else
        head -5 "$WORK/roles.log" | sed 's/^/  /'; echo "FATAL: the roles file did not load"; exit 2
    fi
else
    echo "roles: none supplied (the archive carries none)"
fi

echo "-- 1/3 RESTORE: pg_restore into the scratch database"
podman exec -i "$CONTAINER" pg_restore -U postgres -d restored --no-owner < "$ARCHIVE" >"$WORK/restore.log" 2>&1
rc=$?
if [[ $rc -eq 0 ]]; then R_RESTORE=PASS; echo "  PASS  pg_restore finished with no error"
else
    echo "  FAIL  pg_restore exited $rc; first errors:"
    grep -E 'error|ERROR|FATAL' "$WORK/restore.log" | redact | cut -c1-200 | head -6 | sed 's/^/    /'
fi
ntab="$(pg_scalar "$T" "select count(*) from pg_class c join pg_namespace n on n.oid=c.relnamespace where c.relkind='r' and n.nspname='public'" 2>/dev/null)"
echo "  restored copy holds ${ntab:-0} tables in public"

echo "-- 2/3 POSTURE: backend/tests/test_database_posture.sql on the restored copy"
if pg_exec "$T" -v ON_ERROR_STOP=1 -f - < "$POSTURE" >"$WORK/posture.log" 2>&1; then
    R_POSTURE=PASS
    echo "  PASS  $(grep -E '^ +[0-9]+ \| +0 \| +[0-9]+' "$WORK/posture.log" | tr -s ' ' | head -1) (passed | failed | checks)"
else
    echo "  FAIL  posture checks failed:"
    grep -E 'FAIL|ERROR|FATAL|does not exist' "$WORK/posture.log" | redact | cut -c1-200 | head -8 | sed 's/^/    /'
fi

echo "-- 3/3 DRIFT: check_schema_drift.sh --live against the restored copy"
DB_TARGET_LIVE="$T" "$HERE/check_schema_drift.sh" --live >"$WORK/drift.log" 2>&1
rc=$?
if [[ $rc -eq 0 ]]; then R_DRIFT=PASS; tail -1 "$WORK/drift.log" | sed 's/^/  PASS  /'
else
    [[ $rc -eq 2 ]] && echo "  (the gate could not run, which is not green)"
    echo "  FAIL  exit $rc; first lines:"
    grep -E '^[+-][^+-]|^DRIFT|FATAL|ERROR|RED|mismatch|CHANGED|MISSING' "$WORK/drift.log" | redact | cut -c1-170 | head -12 | sed 's/^/    /'
fi

echo
echo "restore: $R_RESTORE   posture: $R_POSTURE   drift: $R_DRIFT"
if [[ "$R_RESTORE$R_POSTURE$R_DRIFT" == PASSPASSPASS ]]; then
    echo "VERDICT: PASS -- this archive restores, and the restored copy has the posture and schema its migrations record"
    exit 0
fi
echo "VERDICT: FAIL -- do not trust this archive until the failing step is explained"
exit 1
