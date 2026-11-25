#!/usr/bin/env bash
#
# Dump every database this project owns to the NAS, encrypted at rest.
#
# @author Olumuyiwa Oluwasanmi
#
# WHY THIS EXISTS. backend/migrations/08_data_hazard_guards.sql refuses the
# accidents it anticipates -- DROP, TRUNCATE, an unbounded DELETE. It says
# nothing about the ones it cannot see: a lost Railway volume, a deleted
# service, a Supabase project removed, a restore needed to a point in time.
# Before this script there was NO database backup of any kind in either
# repository; `grep -r pg_dump scripts/` returned nothing. Guards and backups
# are different properties, and only the second one gets the data back.
#
# THE CHECK THAT MAKES IT A BACKUP RATHER THAN A FILE. A dump nobody has
# restored is a hope, and `pg_restore --list` (which only parses the table of
# contents, written FIRST) was measured to catch none of: a truncation under
# ~500 bytes, a dropped RLS policy, a missing role. So for the OFC database
# every run does the real thing: the archive and a roles file from the same
# cluster are RESTORED into a throwaway PostgreSQL by scripts/verify_restore.sh,
# which runs the posture and drift gates on the result. The cost is minutes per
# run; the benefit is that "backed up" means "restores". It is done inline, on
# every run, because this script is unattended and nothing else looks.
#
# ORDER. A new backup is written to a staging directory and only moved beside
# the good ones after it verifies. A failure leaves the previous good backup
# untouched, parks the failed pair under failed/, prints what failed and what to
# do, and exits non-zero. Pruning runs only after a verified backup lands, and
# never takes the newest KEEP_MIN verified ones.
#
# ROLES. Roles are cluster-level and live in no pg_dump archive, so each OFC
# archive gets a sibling <name>-<stamp>.roles.sql from `pg_dumpall --roles-only`
# (whole cluster, passwords excluded; see DATABASE_MIGRATIONS.md). The pair
# shares one stamp so it survives being copied.
#
# CLIENT VERSION. pg_dump refuses a server newer than itself and the host
# client can be older than production (PostgreSQL 18.7), so every dump, roles
# dump and archive read runs in a throwaway container of OFC_PG_CLIENT_IMAGE.
# OFC_PG_CLIENT_IMAGE=host uses the host binaries instead.
#
# CREDENTIALS COME FROM THE ENVIRONMENT AND ARE NEVER PRINTED. Pass them in, or
# put them in config/.env, which is gitignored and must stay that way. No URL is
# echoed: libpq takes the connection string through PGSERVICE-style variables or
# argv, and argv is visible in `ps`, so each dump reads its URL from a variable
# rather than from an expanded command line where practical. Errors are redacted
# with the repository's standard sed expression before anything is displayed.
#
#   OFC_DATABASE_URL    Railway Postgres behind optionsandfuturescalculator.com
#   MFC_DATABASE_URL    the mortgage database behind mortgagefvcalculator.com: Railway service
#                       mfv-postgres, database mortgagefv, reached through its public TCP proxy
#                       (see DATABASE_MIGRATIONS.md, "The mortgage database")
#
# Either may be absent. A missing or unusable URL is a FAIL with a stated reason and a
# non-zero exit, never a silent success and never a fallback to the local socket -- a backup script that reports OK
# having backed up nothing is worse than no backup script, for the same reason
# migration 07's zero-write refresh had to start failing loudly.
set -uo pipefail

NAS_ROOT="${NAS_ROOT:-/home/muyiwa/PrimaryNAS/DataFolder/PycharmProjects/db-backups}"
KEEP_DAYS="${KEEP_DAYS:-30}"
KEEP_MIN="${KEEP_MIN:-3}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLIENT_IMAGE="${OFC_PG_CLIENT_IMAGE:-ghcr.io/railwayapp-templates/postgres-ssl:18.7}"
umask 077

redact() { sed -E 's#//[^@]*@#//<redacted>@#g; s#(SCRAM-SHA-256|md5)\$?[A-Za-z0-9+/=:$]{16,}#<hash-redacted>#g'; }
say()    { printf '%s\n' "$*"; }

if [[ -f "$REPO/config/.env" ]]; then
    # Read ONLY the keys this script needs. Sourcing the whole file runs
    # unquoted values as commands -- a value containing a space produced
    # "command not found" lines carrying key material on 2026-09-15.
    for k in OFC_DATABASE_URL MFC_DATABASE_URL DATABASE_URL; do
        v="$(grep -m1 -E "^${k}=" "$REPO/config/.env" 2>/dev/null | cut -d= -f2-)"
        [[ -n "${v:-}" && -z "${!k:-}" ]] && export "$k=$v"
    done
fi
# The engine's own variable is the Railway database; accept it as a fallback so
# a host already configured for the backend needs no new setting.
: "${OFC_DATABASE_URL:=${DATABASE_URL:-}}"

# pg_url_check URL: succeed only for a postgres URL that names a remote host AND a database. Prints the
# reason on failure. WHY: libpq given an empty string, a bare database name, "postgres:///db", a URL with
# host= in its query, or a percent-encoded socket directory as the host connects to the LOCAL unix socket.
# With a database listening there that is a successful dump of the WRONG DATABASE, which is worse than no
# dump. Nothing about the URL is echoed, only the rule it broke.
pg_url_check() {
    local u="$1"
    local re='^postgres(ql)?://([^/?#@]*@)?(\[[0-9A-Fa-f:]+\]|[^]/?#:@[]+)(:[0-9]+)?/[^/?#]+(\?.*)?$'
    [[ -n "$u" ]] || { echo "the connection string is empty"; return 1; }
    [[ "$u" =~ $re ]] || { echo "the connection string is not a postgresql URL with an explicit host and a database name"; return 1; }
    [[ "${BASH_REMATCH[3]}" != *%* ]] || { echo "the host is percent-encoded, which libpq decodes (a socket directory looks like that)"; return 1; }
    [[ "$u" =~ [?\&](host|hostaddr|service|dbname)= ]] && { echo "the query string overrides host, hostaddr, service or dbname"; return 1; }
    return 0
}

# pgtool URL TOOL [args...]: run a PostgreSQL client tool against URL; the URL
# travels by environment NAME, never in argv. stdin and stdout pass through.
pgtool() {
    local url="$1" tool="$2"; shift 2
    if [[ "$tool" != pg_restore ]]; then
        local why
        why="$(pg_url_check "$url")" || {
            say "REFUSED: $tool was not run: $why. A client given no usable host connects to the LOCAL socket," >&2
            say "         which would back up whatever database happens to be there and report success." >&2
            return 97; }
    fi
    if [[ "$CLIENT_IMAGE" == host ]]; then
        DB_URL="$url" PGCONNECT_TIMEOUT=15 "$tool" "$@"
    else
        DB_URL="$url" podman run --rm -i --network=host --entrypoint sh \
            -e DB_URL -e PGCONNECT_TIMEOUT=15 "$CLIENT_IMAGE" \
            -c 'tool="$1"; shift; case "$tool" in pg_restore) exec pg_restore "$@";; *) exec "$tool" --dbname="$DB_URL" "$@";; esac' sh "$tool" "$@"
    fi
}

if [[ "$CLIENT_IMAGE" == host ]]; then
    for t in pg_dump pg_dumpall pg_restore; do
        command -v "$t" >/dev/null || { say "FATAL: $t not found"; exit 2; }
    done
else
    command -v podman >/dev/null || { say "FATAL: podman not found (needed for the PostgreSQL client and the restore rehearsal)"; exit 2; }
fi

mountpoint -q "$(dirname "$NAS_ROOT")" 2>/dev/null || \
    [[ -d "$NAS_ROOT" ]] || mkdir -p "$NAS_ROOT" || {
        say "FATAL: cannot reach the NAS at $NAS_ROOT"; exit 2; }

rc=0
SUMMARY=()

# An interrupted run (a timer stopped, a signal) must not leave its half-written
# staging directory beside the good backups. Only THIS run's stamp is removed;
# a verified pair is moved out of staging before the run can end, so nothing
# good is ever in there.
cleanup_staging() { rm -rf "$NAS_ROOT"/*/".staging-$STAMP" 2>/dev/null; }
trap cleanup_staging EXIT
trap 'cleanup_staging; exit 143' INT TERM HUP

toc_check() { # FILE -> prints table count, nonzero when unreadable or empty
    local n
    n="$(pgtool x pg_restore --list < "$1" 2>/dev/null | grep -c 'TABLE DATA')" || return 1
    [[ "$n" -gt 0 ]] || return 2
    echo "$n"
}

# prune NAME: keep the newest KEEP_MIN pairs always, drop older than KEEP_DAYS.
prune() {
    local dir="$1" name="$2" f i=0 old
    while IFS= read -r f; do
        i=$((i+1)); [[ $i -le $KEEP_MIN ]] && continue
        if [[ -n "$(find "$f" -mtime "+${KEEP_DAYS}" -print 2>/dev/null)" ]]; then
            say "  prune $(basename "$f")"; rm -f "$f" "${f%.dump}.roles.sql" "${f%.dump}.verify.log" "${f%.dump}.fingerprint.txt"
        fi
    done < <(ls -1t "$dir"/${name}-*.dump 2>/dev/null)
    # roles and log files whose archive is gone
    for f in "$dir"/${name}-*.roles.sql "$dir"/${name}-*.verify.log "$dir"/${name}-*.fingerprint.txt; do
        [[ -e "$f" && ! -e "${f%.roles.sql}.dump" && ! -e "${f%.verify.log}.dump" && ! -e "${f%.fingerprint.txt}.dump" ]] && { say "  prune $(basename "$f") (orphan)"; rm -f "$f"; }
    done
}

fail_parked() { # NAME STAGE REASON
    local name="$1" stage="$2" dir="$3" reason="$4"
    mkdir -p "$dir/failed"
    for f in "$stage"/*; do [[ -e "$f" ]] && mv -f "$f" "$dir/failed/$(basename "$f").failed"; done
    rmdir "$stage" 2>/dev/null
    say "FAIL  $name -- $reason"
    say "      The previous good backup in $dir was NOT touched and nothing was pruned."
    say "      The failed pair is parked in $dir/failed/ (*.failed) for inspection. Do not trust it."
    say "      Next: read the output above, fix the cause, rerun this script. Until a run says"
    say "      'verified', the newest backup is the previous one, and it is as old as its stamp."
    SUMMARY+=("FAILED   $name: backup NOT verified")
    rc=1
}

# OFC: dump + roles + inline restore rehearsal. Promotes only on PASS.
dump_ofc() {
    local name="$1" url="$2" envname="$3"
    if [[ -z "$url" ]]; then
        say "FAIL  $name -- no connection string: set ${envname}. Nothing was dumped, and no local database was tried."
        SUMMARY+=("FAILED   $name: no connection string (${envname} unset or empty); nothing dumped"); rc=1; return
    fi
    local dir="$NAS_ROOT/$name" stage="$NAS_ROOT/$name/.staging-$STAMP"
    local base="${name}-${STAMP}" t0=$SECONDS
    mkdir -p "$stage" || { say "FAIL  $name -- cannot create $stage"; rc=1; return; }
    local dump="$stage/$base.dump" roles="$stage/$base.roles.sql"

    say "dump  $name -> $base.dump"
    # --no-owner so the dump restores into a database whose roles differ.
    # NOT --no-privileges: it drops every GRANT, so a restore has rows and RLS
    # policies and no role may touch anything (found 2026-11-25).
    if ! pgtool "$url" pg_dump --format=custom --compress=9 --no-owner \
            > "$dump" 2> >(redact >&2); then
        fail_parked "$name" "$stage" "$dir" "pg_dump refused or failed"; return
    fi
    # Roles are cluster-level: whole cluster, NO password hashes (see the doc).
    say "roles $name -> $base.roles.sql"
    local rflags=(--roles-only)
    [[ "${KEEP_ROLE_PASSWORDS:-0}" == 1 ]] || rflags+=(--no-role-passwords)
    if ! pgtool "$url" pg_dumpall "${rflags[@]}" > "$roles" 2> >(redact >&2); then
        fail_parked "$name" "$stage" "$dir" "pg_dumpall --roles-only failed (is the connecting role allowed to read pg_authid?)"; return
    fi
    local r
    for r in ofc_app ofc_refresh; do
        grep -q -E "^CREATE ROLE ${r}[ ;]" "$roles" || {
            fail_parked "$name" "$stage" "$dir" "the roles file has no role $r, so a restore could not apply its grants or policies"; return; }
    done
    if [[ "${KEEP_ROLE_PASSWORDS:-0}" != 1 ]] && grep -q -i "PASSWORD '" "$roles"; then
        fail_parked "$name" "$stage" "$dir" "the roles file carries a password hash although --no-role-passwords was set"; return
    fi

    local tables
    tables="$(toc_check "$dump")"; local tc=$?
    if [[ $tc -ne 0 ]]; then
        fail_parked "$name" "$stage" "$dir" "the archive does not parse or holds no table data"; return
    fi

    say "verify $name -- restoring $base into a scratch server (scripts/verify_restore.sh)"
    local vlog="$stage/$base.verify.log" vrc
    "$REPO/scripts/verify_restore.sh" "$dump" --roles "$roles" > "$vlog" 2>&1
    vrc=$?
    redact < "$vlog" | sed 's/^/    | /'
    if [[ $vrc -ne 0 ]]; then
        local why="the restore rehearsal FAILED (exit $vrc): this archive does not restore into a database with the expected posture"
        [[ $vrc -eq 2 ]] && why="the restore rehearsal could not run (exit 2), so this backup is UNVERIFIED and is treated as failed"
        fail_parked "$name" "$stage" "$dir" "$why"; return
    fi

    # Verified: promote. Roles first, so a dump never sits without its roles.
    mv -f "$roles" "$dir/" && mv -f "$vlog" "$dir/" && mv -f "$dump" "$dir/" && rmdir "$stage" || {
        fail_parked "$name" "$stage" "$dir" "could not move the verified pair into place"; return; }
    say "  ok  $name: $(du -h "$dir/$base.dump" | cut -f1) dump, ${tables} tables, roles file, restore VERIFIED in $((SECONDS-t0))s"
    prune "$dir" "$name"
    SUMMARY+=("VERIFIED $name: $base.dump + $base.roles.sql restored and checked")
}

# MFC: the mortgage database, which is the Railway service mfv-postgres (database mortgagefv), NOT a
# Supabase project: PostgREST (mfv-postgrest) and GoTrue (mfv-auth) both connect to it, and the web app's
# Supabase URLs are Railway domains. It carries Supabase's roles and an auth schema, so it gets the same
# pair as OFC plus a fingerprint of the source taken right after the dump. There is no migration baseline
# for it, so the rehearsal (scripts/verify_restore_mortgage.sh) compares the restored copy to that
# fingerprint instead of to a recorded schema. Promotes only on PASS.
dump_mfc() {
    local name="$1" url="$2" envname="$3"
    if [[ -z "$url" ]]; then
        say "FAIL  $name -- no connection string: set ${envname}. Nothing was dumped, and no local database was tried."
        SUMMARY+=("FAILED   $name: no connection string (${envname} unset or empty); nothing dumped"); rc=1; return
    fi
    local dir="$NAS_ROOT/$name" stage="$NAS_ROOT/$name/.staging-$STAMP" base="${name}-${STAMP}" t0=$SECONDS
    local expect_db="${MFC_EXPECT_DB:-mortgagefv}"
    mkdir -p "$stage" || { say "FAIL  $name -- cannot create $stage"; rc=1; return; }
    local dump="$stage/$base.dump" roles="$stage/$base.roles.sql" fp="$stage/$base.fingerprint.txt"

    say "dump  $name -> $base.dump"
    if ! pgtool "$url" pg_dump --format=custom --compress=9 --no-owner > "$dump" 2> >(redact >&2); then
        fail_parked "$name" "$stage" "$dir" "pg_dump refused or failed"; return
    fi
    # The archive names the database it came from. If it is not the one this script is for, the URL
    # pointed somewhere else and the file is worthless however well it restores.
    local got_db
    got_db="$(pgtool x pg_restore --list < "$dump" 2>/dev/null | sed -n 's/^;[[:space:]]*dbname: //p' | head -1)"
    if [[ "$got_db" != "$expect_db" ]]; then
        fail_parked "$name" "$stage" "$dir" "the archive is of database '${got_db:-?}', expected '$expect_db' (MFC_EXPECT_DB)"; return
    fi
    say "roles $name -> $base.roles.sql"
    local rflags=(--roles-only)
    [[ "${KEEP_ROLE_PASSWORDS:-0}" == 1 ]] || rflags+=(--no-role-passwords)
    if ! pgtool "$url" pg_dumpall "${rflags[@]}" > "$roles" 2> >(redact >&2); then
        fail_parked "$name" "$stage" "$dir" "pg_dumpall --roles-only failed (is the connecting role allowed to read pg_authid?)"; return
    fi
    local r
    for r in anon authenticated authenticator service_role supabase_auth_admin; do
        grep -q -E "^CREATE ROLE ${r}[ ;]" "$roles" || {
            fail_parked "$name" "$stage" "$dir" "the roles file has no role $r, so a restore could not apply its grants or serve its API"; return; }
    done
    if [[ "${KEEP_ROLE_PASSWORDS:-0}" != 1 ]] && grep -q -i "PASSWORD '" "$roles"; then
        fail_parked "$name" "$stage" "$dir" "the roles file carries a password hash although --no-role-passwords was set"; return
    fi
    local tables
    tables="$(toc_check "$dump")" || { fail_parked "$name" "$stage" "$dir" "the archive does not parse or holds no table data"; return; }

    say "fingerprint $name (read-only, counts and names only)"
    if ! pgtool "$url" psql -X -q -At -F'|' -v ON_ERROR_STOP=1 < "$REPO/scripts/mortgage_fingerprint.sql" > "$fp" 2> >(redact >&2) || [[ ! -s "$fp" ]]; then
        fail_parked "$name" "$stage" "$dir" "could not fingerprint the source database"; return
    fi

    say "verify $name -- restoring $base into a scratch server (scripts/verify_restore_mortgage.sh)"
    local vlog="$stage/$base.verify.log" vrc
    "$REPO/scripts/verify_restore_mortgage.sh" "$dump" --roles "$roles" --expect "$fp" > "$vlog" 2>&1
    vrc=$?
    redact < "$vlog" | sed 's/^/    | /'
    if [[ $vrc -ne 0 ]]; then
        local why="the restore rehearsal FAILED (exit $vrc): this archive does not restore into a copy that matches its source"
        [[ $vrc -eq 2 ]] && why="the restore rehearsal could not run (exit 2), so this backup is UNVERIFIED and is treated as failed"
        fail_parked "$name" "$stage" "$dir" "$why"; return
    fi
    mv -f "$roles" "$dir/" && mv -f "$fp" "$dir/" && mv -f "$vlog" "$dir/" && mv -f "$dump" "$dir/" && rmdir "$stage" || {
        fail_parked "$name" "$stage" "$dir" "could not move the verified set into place"; return; }
    say "  ok  $name: $(du -h "$dir/$base.dump" | cut -f1) dump, ${tables} tables, roles file, restore VERIFIED in $((SECONDS-t0))s"
    prune "$dir" "$name"
    SUMMARY+=("VERIFIED $name: $base.dump + .roles.sql + .fingerprint.txt restored and compared to the source")
}

say "=== database backup $STAMP -> $NAS_ROOT ==="
# BACKUP_TARGETS narrows the run, and the default is BOTH, which is also what the
# unattended wrapper now runs. The mortgage database is NOT an external Supabase
# project: it is the Railway service mfv-postgres, database mortgagefv,
# PostgreSQL 18.7, proved on 2026-11-25 by mfv-postgrest and mfv-auth both
# connecting to it while the supabase-auth service points at the OFC database
# instead. It is reachable and restore-verified -- see "The mortgage database" in
# docs/technical/DATABASE_MIGRATIONS.md.
# It used to sit outside the schedule because it is a DIFFERENT service and the
# wrapper opened one SSH tunnel to one service. That reason is gone: both
# databases now have a public TCP proxy, so both are reached by a plain
# connection string and both are scheduled. Narrowing this is now a choice for a
# one-off run, not a limitation.
case " ${BACKUP_TARGETS:-ofc mfc} " in *" ofc "*)
    dump_ofc optionsandfuturescalculator "${OFC_DATABASE_URL:-}" OFC_DATABASE_URL;; esac
case " ${BACKUP_TARGETS:-ofc mfc} " in *" mfc "*)
    dump_mfc mortgagefvcalculator        "${MFC_DATABASE_URL:-}" MFC_DATABASE_URL;; esac
if [[ ${#SUMMARY[@]} -eq 0 ]]; then
    say "FATAL: BACKUP_TARGETS='${BACKUP_TARGETS:-}' selected nothing (valid: ofc mfc)"; exit 2
fi

say "--- summary ---"
for l in "${SUMMARY[@]}"; do say "  $l"; done
if [[ $rc -eq 0 ]]; then
    say "=== backup complete: every database listed above is as strong as its line says ==="
else
    say "=== BACKUP FAILED: at least one database has no new verified backup (see above) ==="
fi
exit $rc
