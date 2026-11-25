#!/usr/bin/env bash
#
# Unattended, verified backup of BOTH production databases, over each one's own
# public TCP proxy. No SSH tunnel, no registered SSH key, no Railway session.
#
# @author Olumuyiwa Oluwasanmi
#
# WHY THIS REPLACED A TUNNEL, which is the whole point of the file. The previous
# wrapper reached the OFC database through `railway connect --tunnel-only`, so
# every backup AND every restore depended on an SSH key being registered on the
# Railway account. A revoked, rotated or lost key meant no logical backup and no
# way to take one -- the recovery path depended on a credential that is not part
# of the recovery path. Both databases now carry a public TCP proxy, so a plain
# connection string in config/.env reaches them from anywhere. That turns 150
# lines of tunnel lifecycle, port-collision detection and orphaned-ssh teardown
# into one child process, and it removes the key from the critical path instead
# of managing it more carefully.
#
# IT ALSO COVERS BOTH DATABASES, which the tunnel wrapper never did: it opened
# one tunnel to one service and set BACKUP_TARGETS=ofc, so the mortgage database
# was outside the schedule no matter how healthy the schedule looked.
#
# WHAT IS STILL TRUE OF THE TUNNEL PATH: if a proxy is ever removed, or Railway
# rotates a password and config/.env goes stale, this fails loudly and
# scripts/backup_production_db_via_tunnel.sh is the fallback. It is unchanged and
# still proven. Re-deriving the connection strings is documented in
# docs/technical/DATABASE_MIGRATIONS.md.
#
# SILENCE MUST NOT READ AS SUCCESS. A status file is written from an EXIT trap,
# and `ok` is recorded only when the backup exited 0 AND printed one VERIFIED
# line per expected database. A run that dies, is signalled, or verifies only
# one of the two records a failure, and check_backup_freshness.sh reads that
# file independently of this script ever running again.
#
#   backup_production_db.sh
#
# EXIT: 0 only when every expected database has a NEW verified backup on the
#       NAS. Non-zero in every other case, including a partial success.
#
# Variables (all optional):
#   OFC_BACKUP_STATE_DIR    default ~/.local/state/ofc-backup
#   NAS_ROOT                as for backup_database_to_nas.sh
#   OFC_BACKUP_TIMEOUT      seconds the backup may run (default 5400)
#   OFC_BACKUP_EXPECT       space-separated database names that must verify
#                           (default: both; see EXPECT below)
set -uo pipefail

SELF="$(readlink -f "${BASH_SOURCE[0]}")"
REPO="$(cd "$(dirname "$SELF")/.." && pwd)"
STATE_DIR="${OFC_BACKUP_STATE_DIR:-${XDG_STATE_HOME:-$HOME/.local/state}/ofc-backup}"
NAS_ROOT="${NAS_ROOT:-/home/muyiwa/PrimaryNAS/DataFolder/PycharmProjects/db-backups}"
TIMEOUT="${OFC_BACKUP_TIMEOUT:-5400}"
umask 077

# The databases that must each produce a verified backup. Naming them here, and
# requiring one VERIFIED line each, is what makes a half-done run a failure: a
# count alone would pass if one database verified twice, and "exit 0" alone
# would pass if a target were silently dropped from the inner script.
# `-`, not `:-`: an explicitly EMPTY list must stay empty and be refused below,
# not silently become the default. An empty expectation would verify nothing and
# still record success.
EXPECT="${OFC_BACKUP_EXPECT-optionsandfuturescalculator mortgagefvcalculator}"
[[ -n "${EXPECT// /}" ]] || { echo "FATAL: OFC_BACKUP_EXPECT is empty; refusing to 'verify' nothing" >&2; exit 2; }

redact() { sed -E 's#//[^@/]*@#//<redacted>@#g; s#(SCRAM-SHA-256|md5)\$?[A-Za-z0-9+/=:$]{16,}#<hash-redacted>#g'; }
say()    { printf '%s\n' "$*"; }
now()    { date -u +%Y-%m-%dT%H:%M:%SZ; }

mkdir -p "$STATE_DIR" || { say "FATAL: cannot create $STATE_DIR"; exit 2; }
STATUS="$STATE_DIR/status"
WORK="$(mktemp -d "$STATE_DIR/run.XXXXXX")" || { say "FATAL: no scratch dir"; exit 2; }
RLOG="$WORK/backup.log"
BPID=""
RESULT=fail
REASON="aborted before completion"
ARCHIVE=""
STARTED="$(now)"

# Field names and semantics are the contract check_backup_freshness.sh reads.
# last_verified_* are carried forward on a failure: a failed run does not make
# yesterday's verified backup stop existing, and erasing them would turn one bad
# night into "there has never been a backup".
write_status() { # RESULT REASON [ARCHIVE]; replaces the file atomically
    local verified_at verified_archive tmp="$STATUS.tmp.$$"
    verified_at="$(sed -n 's/^last_verified_utc=//p' "$STATUS" 2>/dev/null)"
    verified_archive="$(sed -n 's/^last_verified_archive=//p' "$STATUS" 2>/dev/null)"
    if [[ "$1" == ok ]]; then verified_at="$(now)"; verified_archive="$3"; fi
    {
        echo "last_attempt_started_utc=$STARTED"
        echo "last_attempt_finished_utc=$([[ "$1" == running ]] && echo - || now)"
        echo "last_attempt_result=$1"
        echo "last_attempt_reason=$2"
        echo "last_verified_utc=${verified_at:--}"
        echo "last_verified_archive=${verified_archive:--}"
        echo "host=$(hostname -s)"
    } > "$tmp" && mv -f "$tmp" "$STATUS"
}

# A signal must not leave the status file saying "running" forever, and must not
# leave a half-written dump looking like a candidate. The inner script stages
# into a temporary directory and only moves a complete verified pair onto the
# NAS, so an interrupted run leaves nothing for the freshness check to mistake
# for a backup; this trap only has to record the interruption and reap the child.
finish() {
    local rc=$?
    trap - EXIT INT TERM HUP
    if [[ -n "$BPID" ]] && kill -0 "$BPID" 2>/dev/null; then
        kill -TERM -- "-$BPID" 2>/dev/null; wait "$BPID" 2>/dev/null
    fi
    write_status "$RESULT" "$REASON" "$ARCHIVE"
    rm -rf "$WORK"
    if [[ "$RESULT" != ok && $rc -eq 0 ]]; then rc=1; fi
    [[ "$RESULT" == ok ]] && say "=== PRODUCTION BACKUP VERIFIED ($ARCHIVE) ===" \
                          || say "=== PRODUCTION BACKUP FAILED: $REASON ==="
    exit "$rc"
}
trap 'REASON="interrupted by a signal"; exit 130' INT TERM HUP
trap finish EXIT

write_status running "in progress"
say "=== production backup $STARTED ==="
say "expecting a verified backup for: $EXPECT"

die() { REASON="$1"; say "FATAL: $1"; exit "${2:-2}"; }

command -v podman  >/dev/null || die "podman not found"
command -v python3 >/dev/null || die "python3 not found"
[[ -r "$REPO/config/.env" ]] || die "config/.env is not readable; the connection strings live there"

# The NAS first: a backup that cannot land is not worth taking. The probe writes
# and removes a real file, because a readable directory is not a writable one.
mkdir -p "$NAS_ROOT" 2>/dev/null \
    && : > "$NAS_ROOT/.write-probe.$$" 2>/dev/null \
    && rm -f "$NAS_ROOT/.write-probe.$$" \
    || die "the NAS is not writable at $NAS_ROOT"

# run_with_env.py parses config/.env and execve's the child, so no shell ever
# sees a value and the connection strings never reach argv. setsid gives the
# child its own process group, so the trap above can signal the whole tree
# rather than only the timeout wrapper.
say "running backup_database_to_nas.sh (timeout ${TIMEOUT}s, no tunnel)"
setsid timeout -k 30 "$TIMEOUT" \
    python3 "$REPO/scripts/run_with_env.py" -- "$REPO/scripts/backup_database_to_nas.sh" \
    >"$WORK/raw.log" 2>&1 &
BPID=$!
wait "$BPID"; brc=$?
BPID=""

redact < "$WORK/raw.log" > "$RLOG"
cat "$RLOG"

if (( brc == 124 || brc == 137 )); then
    die "the backup did not finish within ${TIMEOUT}s" 1
elif (( brc != 0 )); then
    die "backup_database_to_nas.sh exited $brc" 1
fi

# The run stamp names both archives, so one value identifies the whole run.
ARCHIVE="$(sed -n 's/^=== database backup \([0-9TZ]*\) ->.*/\1/p' "$RLOG" | head -1)"
[[ -n "$ARCHIVE" ]] || die "the backup printed no run stamp, so there is nothing to record" 1

# One VERIFIED line per expected database, checked by NAME. This is the positive
# control: without it, "exit 0 and some output" would pass a run that quietly
# backed up one database, or none.
missing=()
for db in $EXPECT; do
    grep -q "^  VERIFIED ${db}:" "$RLOG" || missing+=("$db")
done
if (( ${#missing[@]} )); then
    die "no verified backup for: ${missing[*]} (the run exited 0, which is why this is checked separately)" 1
fi

RESULT=ok
REASON="verified backups for: $EXPECT"
exit 0
