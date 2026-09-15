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
# restored is a hope. Every dump here is written in the custom format and then
# read back with `pg_restore --list`, which parses the archive's own table of
# contents -- so a truncated transfer, a half-written file or a wrong-version
# archive fails HERE, not on the day it is needed. The dump is discarded and
# the script exits non-zero if that read-back fails.
#
# CREDENTIALS COME FROM THE ENVIRONMENT AND ARE NEVER PRINTED. Pass them in, or
# put them in config/.env, which is gitignored and must stay that way. No URL is
# echoed: libpq takes the connection string through PGSERVICE-style variables or
# argv, and argv is visible in `ps`, so each dump reads its URL from a variable
# rather than from an expanded command line where practical. Errors are redacted
# with the repository's standard sed expression before anything is displayed.
#
#   OFC_DATABASE_URL    Railway Postgres behind optionsandfuturescalculator.com
#   MFC_DATABASE_URL    Supabase Postgres behind mortgagefvcalculator.com
#                       (Project settings -> Database -> Connection string ->
#                        URI, session pooler; the direct 5432 host is IPv6-only)
#
# Either may be absent. A missing URL is a SKIP with a stated reason and a
# non-zero exit, never a silent success -- a backup script that reports OK
# having backed up nothing is worse than no backup script, for the same reason
# migration 07's zero-write refresh had to start failing loudly.
set -uo pipefail

NAS_ROOT="${NAS_ROOT:-/home/muyiwa/PrimaryNAS/DataFolder/PycharmProjects/db-backups}"
KEEP_DAYS="${KEEP_DAYS:-30}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

redact() { sed -E 's#//[^@]*@#//<redacted>@#g'; }
say()    { printf '%s\n' "$*"; }

if [[ -f "$REPO/config/.env" ]]; then
    # Read ONLY the two keys this script needs. Sourcing the whole file runs
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

command -v pg_dump   >/dev/null || { say "FATAL: pg_dump not found";   exit 2; }
command -v pg_restore>/dev/null || { say "FATAL: pg_restore not found"; exit 2; }

mountpoint -q "$(dirname "$NAS_ROOT")" 2>/dev/null || \
    [[ -d "$NAS_ROOT" ]] || mkdir -p "$NAS_ROOT" || {
        say "FATAL: cannot reach the NAS at $NAS_ROOT"; exit 2; }

rc=0
dump_one() {
    local name="$1" url="$2"
    if [[ -z "$url" ]]; then
        say "SKIP  $name -- no connection string (set ${3})"
        rc=1
        return
    fi
    local dir="$NAS_ROOT/$name"
    local out="$dir/${name}-${STAMP}.dump"
    mkdir -p "$dir"
    say "dump  $name -> $(basename "$out")"
    # --format=custom so pg_restore can read selectively and verify the TOC.
    # --no-owner/--no-privileges so the dump restores into a database whose
    # roles differ, which a disaster restore's will.
    if ! PGCONNECT_TIMEOUT=15 pg_dump --dbname="$url" \
            --format=custom --compress=9 --no-owner --no-privileges \
            --file="$out" 2> >(redact >&2); then
        say "FAIL  $name -- pg_dump refused; nothing written"
        rm -f "$out"; rc=1; return
    fi
    # Read it back. This is the difference between a backup and a file.
    local tables
    if ! tables="$(pg_restore --list "$out" 2>/dev/null | grep -c 'TABLE DATA')"; then
        say "FAIL  $name -- the archive does not parse; discarding it"
        rm -f "$out"; rc=1; return
    fi
    if [[ "$tables" -eq 0 ]]; then
        say "FAIL  $name -- archive parses but contains NO table data; discarding it"
        rm -f "$out"; rc=1; return
    fi
    say "  ok  $name: $(du -h "$out" | cut -f1), ${tables} tables, TOC verified"
    # Retention is applied only after a VERIFIED dump lands, so a run of
    # failures can never age out the last good copy.
    find "$dir" -name "${name}-*.dump" -mtime "+${KEEP_DAYS}" -print -delete \
        | sed 's/^/  prune /'
}

say "=== database backup $STAMP -> $NAS_ROOT ==="
dump_one optionsandfuturescalculator "${OFC_DATABASE_URL:-}" OFC_DATABASE_URL
dump_one mortgagefvcalculator        "${MFC_DATABASE_URL:-}" MFC_DATABASE_URL

if [[ $rc -eq 0 ]]; then
    say "=== all databases dumped and verified ==="
else
    say "=== INCOMPLETE: at least one database was not backed up (see above) ==="
fi
exit $rc
