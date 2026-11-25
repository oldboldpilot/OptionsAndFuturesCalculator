#!/usr/bin/env bash
# RESTORE REHEARSAL for the mortgage database (Railway service mfv-postgres, database mortgagefv).
#
# @author Olumuyiwa Oluwasanmi
#
#   verify_restore_mortgage.sh ARCHIVE --roles FILE --expect FILE [--image IMAGE]
#
#   ARCHIVE      a pg_dump custom-format archive of the mortgage database
#   --roles FILE the cluster's roles, as `pg_dumpall --roles-only --no-role-passwords` wrote them
#   --expect FILE the fingerprint of the SOURCE database taken right after the dump
#                (scripts/mortgage_fingerprint.sql run against it; the backup script writes it)
#   --image IMG  scratch server image (default: the production image, PostgreSQL 18.7)
#
# There is no migration baseline for this database, so unlike scripts/verify_restore.sh there is no
# schema to diff against and none is invented. The reference is the SOURCE ITSELF: the same read-only
# fingerprint query runs against the live database and against the restored copy, and they must agree.
#
#   1 ROLES     the roles file loads with ON_ERROR_STOP, and the five roles the app needs exist afterwards
#   2 RESTORE   pg_restore runs to completion with no error (a truncated archive, or a restore whose grants
#               name a role that does not exist, exits non-zero)
#   3 SHAPE     the restored schemas, table names, role memberships, grant count, policy count and RLS
#               count equal the source's, line for line
#   4 DATA      every table that had rows in the source has rows in the copy, auth.users included
#
# Verdict: PASS only if all four pass. Exit 0 PASS, 1 FAIL, 2 could not run (not green).
#
# What this does NOT prove: that row CONTENTS are right (only that no populated table came back empty),
# that counts match to the row (the source keeps taking writes between the dump and the fingerprint), that
# the app works against the copy, or anything about a target other than this image. See
# docs/technical/DATABASE_MIGRATIONS.md, "The mortgage database".
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/db_common.sh"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FINGERPRINT_SQL="$HERE/mortgage_fingerprint.sql"
REQUIRED_ROLES=(anon authenticated authenticator service_role supabase_auth_admin)
REQUIRED_SCHEMAS=(auth public)

ARCHIVE="" ROLES="" EXPECT=""
SCRATCH_IMAGE="${SCRATCH_IMAGE:-ghcr.io/railwayapp-templates/postgres-ssl:18.7}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --roles)  ROLES="${2:-}"; shift 2 ;;
        --expect) EXPECT="${2:-}"; shift 2 ;;
        --image)  SCRATCH_IMAGE="${2:-}"; shift 2 ;;
        -h|--help) sed -n 3,12p "$0"; exit 0 ;;
        -*) echo "unknown option $1" >&2; exit 2 ;;
        *) ARCHIVE="$1"; shift ;;
    esac
done
[[ -n "$ARCHIVE" && -f "$ARCHIVE" ]] || { echo "FATAL: archive not found" >&2; exit 2; }
[[ -n "$ROLES"  && -f "$ROLES"   ]] || { echo "FATAL: --roles FILE is required and must exist: a mortgage restore without its roles is not a restore" >&2; exit 2; }
[[ -n "$EXPECT" && -s "$EXPECT"  ]] || { echo "FATAL: --expect FILE is required and must be non-empty: without the source's fingerprint there is nothing to compare the copy to" >&2; exit 2; }
export SCRATCH_IMAGE SCRATCH_PGDATA=/var/lib/postgresql/data/pgdata

WORK="$(mktemp -d)"; CONTAINER=""
cleanup() { [[ -n "$CONTAINER" ]] && podman stop -t 2 "$CONTAINER" >/dev/null 2>&1; rm -rf "$WORK"; }
trap cleanup EXIT
trap 'exit 2' INT TERM

R_ROLES=FAIL R_RESTORE=FAIL R_SHAPE=FAIL R_DATA=FAIL
echo "== mortgage restore rehearsal: $(basename "$ARCHIVE") ($(du -h "$ARCHIVE" | cut -f1)) =="
CONTAINER="$(scratch_start)" || { echo "FATAL: could not start the scratch server"; exit 2; }
echo "scratch server $CONTAINER: PostgreSQL $(podman exec "$CONTAINER" psql -U postgres -Atc 'show server_version')"
scratch_db_create "$CONTAINER" restored || { echo "FATAL: could not create the target database"; exit 2; }

echo "-- 1/4 ROLES: load the roles file, then check the roles the app needs"
if grep -v -E '^(CREATE|ALTER) ROLE postgres[ ;]' "$ROLES" \
    | podman exec -i "$CONTAINER" psql -X -q -U postgres -d postgres -v ON_ERROR_STOP=1 >"$WORK/roles.log" 2>&1; then
    missing=()
    for r in "${REQUIRED_ROLES[@]}"; do
        [[ "$(podman exec "$CONTAINER" psql -X -U postgres -Atc "select count(*) from pg_roles where rolname='$r'")" == 1 ]] || missing+=("$r")
    done
    if [[ ${#missing[@]} -eq 0 ]]; then R_ROLES=PASS; echo "  PASS  roles file loaded; ${REQUIRED_ROLES[*]} all exist"
    else echo "  FAIL  roles file loaded but these required roles do not exist: ${missing[*]}"; fi
else
    echo "  FAIL  the roles file did not load:"
    head -5 "$WORK/roles.log" | redact | cut -c1-200 | sed 's/^/    /'
fi

echo "-- 2/4 RESTORE: pg_restore into the scratch database"
podman exec -i "$CONTAINER" pg_restore -U postgres -d restored --no-owner < "$ARCHIVE" >"$WORK/restore.log" 2>&1
rc=$?
if [[ $rc -eq 0 ]]; then R_RESTORE=PASS; echo "  PASS  pg_restore finished with no error"
else
    echo "  FAIL  pg_restore exited $rc; first errors:"
    grep -E 'error|ERROR|FATAL' "$WORK/restore.log" | redact | cut -c1-200 | head -6 | sed 's/^/    /'
fi

# the restored copy's fingerprint, in the same format and order as the source's
podman exec -i "$CONTAINER" psql -X -q -At -F'|' -v ON_ERROR_STOP=1 -U postgres -d restored < "$FINGERPRINT_SQL" >"$WORK/restored.fp" 2>"$WORK/fp.err" \
    || { echo "FATAL: could not fingerprint the restored copy"; head -3 "$WORK/fp.err" | redact; }

echo "-- 3/4 SHAPE: schemas, tables, memberships, grants, policies, RLS equal to the source"
grep -v '^rows|' "$EXPECT" | LC_ALL=C sort >"$WORK/exp.shape"
grep -v '^rows|' "$WORK/restored.fp" | LC_ALL=C sort >"$WORK/got.shape"
shape_ok=1
for s in "${REQUIRED_SCHEMAS[@]}"; do
    grep -qx "schema|$s" "$WORK/got.shape" || { echo "  FAIL  schema $s is absent from the restored copy"; shape_ok=0; }
    grep -qx "schema|$s" "$WORK/exp.shape" || { echo "  FAIL  schema $s is absent from the SOURCE fingerprint: the reference itself is wrong"; shape_ok=0; }
done
ntab="$(grep -c '^table|' "$WORK/got.shape")"
[[ -s "$WORK/exp.shape" && "$ntab" -gt 0 ]] || { echo "  FAIL  restored copy holds no tables"; shape_ok=0; }
if ! diff "$WORK/exp.shape" "$WORK/got.shape" >"$WORK/shape.diff"; then
    echo "  FAIL  restored shape differs from the source (< source only, > copy only):"
    head -12 "$WORK/shape.diff" | grep -E '^[<>]' | cut -c1-160 | sed 's/^/    /'
    shape_ok=0
fi
if [[ $shape_ok -eq 1 ]]; then
    R_SHAPE=PASS
    echo "  PASS  $(grep -c '^schema|' "$WORK/got.shape") schemas, ${ntab} tables, $(grep '^grants|' "$WORK/got.shape"), $(grep '^policies|' "$WORK/got.shape"), $(grep '^rls|' "$WORK/got.shape") -- identical to the source"
fi

echo "-- 4/4 DATA: every table that had rows in the source has rows in the copy"
data_ok=1; populated=0
grep '^rows|' "$WORK/restored.fp" | LC_ALL=C sort >"$WORK/got.rows"
while IFS='|' read -r _ tbl n; do
    [[ "$n" =~ ^[0-9]+$ && "$n" -gt 0 ]] || continue
    populated=$((populated+1))
    g="$(grep -m1 "^rows|${tbl}|" "$WORK/got.rows" | cut -d'|' -f3)"
    if [[ -z "$g" || "$g" -eq 0 ]]; then echo "  FAIL  $tbl had $n rows in the source and has ${g:-no table} in the copy"; data_ok=0; fi
done < <(grep '^rows|' "$EXPECT")
if ! grep -q '^rows|auth.users|[1-9]' "$EXPECT"; then echo "  FAIL  the source fingerprint shows no auth.users rows: the reference is empty, so this proves nothing"; data_ok=0; fi
if [[ $populated -eq 0 ]]; then echo "  FAIL  the source fingerprint has no populated table"; data_ok=0; fi
if [[ $data_ok -eq 1 ]]; then
    R_DATA=PASS
    echo "  PASS  all $populated populated source tables are populated in the copy; auth.users: source $(grep '^rows|auth.users|' "$EXPECT" | cut -d'|' -f3), copy $(grep '^rows|auth.users|' "$WORK/got.rows" | cut -d'|' -f3)"
fi

echo
echo "roles: $R_ROLES   restore: $R_RESTORE   shape: $R_SHAPE   data: $R_DATA"
if [[ "$R_ROLES$R_RESTORE$R_SHAPE$R_DATA" == PASSPASSPASSPASS ]]; then
    echo "VERDICT: PASS -- this archive restores with its roles, and the copy has the source's shape and populated tables"
    exit 0
fi
echo "VERDICT: FAIL -- do not trust this archive until the failing step is explained"
exit 1
