#!/usr/bin/env bash
#
# Take a verified backup of the PRODUCTION database, unattended.
#
# @author Olumuyiwa Oluwasanmi
#
# The production database has no public endpoint. This wrapper is the whole
# route to it: it opens Railway's SSH tunnel on a free local port, waits until
# the port accepts connections, runs `railway run` so the service's DATABASE_URL
# is injected into ONE child, rewrites that URL's host and port to the tunnel,
# and hands it to scripts/backup_database_to_nas.sh (dump, roles, restore into a
# scratch container, posture and drift gates). It then ALWAYS closes the tunnel.
#
# WHY IT CALLS `railway run` ITSELF rather than being invoked under it. A timer
# starts a bare command; "run me under railway run" would put a second thing in
# the unit that can be wrong (the service name, the linked project) and would
# hand every variable of the Postgres service to the tunnel and the pre-flight
# checks too. Here the secret-bearing environment exists only in the child that
# needs it, and the tunnel is opened and closed by the same process that knows
# whether the backup worked. The inner half is this same file with --inner.
#
# WHAT IT NEVER DOES: print a URL, a password or a role hash. Everything the
# children write goes through `redact`, and the tunnel's own output (which
# carries connection details) is shown only after lines mentioning a password,
# a URL or a connection string are dropped.
#
# THE STATE IT LEAVES, and why. STATE_DIR/status records the last attempt and its
# outcome. A success is written ONLY when the backup exited 0, printed its
# "VERIFIED" line for this database, and the verified pair is on the NAS.
# scripts/check_backup_freshness.sh reads that file and, independently, the NAS
# itself, so a stale or absent record can never be read as success.
#
# EXIT: 0 verified backup taken; 1 the backup or its verification failed;
#       2 could not start (tool missing, not logged in, NAS unreachable,
#       tunnel never opened). Non-zero in every case where no NEW verified
#       backup exists.
#
# Variables (all optional):
#   OFC_BACKUP_STATE_DIR    default ~/.local/state/ofc-backup
#   NAS_ROOT                as for backup_database_to_nas.sh
#   RAILWAY_BIN             default: railway on PATH, else ~/.railway/bin/railway
#   RAILWAY_PG_SERVICE      default Postgres
#   OFC_TUNNEL_PORT         default: a free ephemeral port
#   OFC_TUNNEL_WAIT         seconds to wait for the tunnel to accept (default 60)
#   OFC_TUNNEL_TRIES        tunnel attempts (default 3; a cold boot has no route yet)
#   OFC_BACKUP_TIMEOUT      seconds the backup may run (default 5400)
#   OFC_TUNNEL_SSH_PROBE    leave unset
#   RAILWAY_API_TOKEN       an account token, if a stored login cannot be used
#                           (see docs/technical/DATABASE_MIGRATIONS.md)
set -uo pipefail

SELF="$(readlink -f "${BASH_SOURCE[0]}")"
REPO="$(cd "$(dirname "$SELF")/.." && pwd)"
STATE_DIR="${OFC_BACKUP_STATE_DIR:-${XDG_STATE_HOME:-$HOME/.local/state}/ofc-backup}"
NAS_ROOT="${NAS_ROOT:-/home/muyiwa/PrimaryNAS/DataFolder/PycharmProjects/db-backups}"
PG_SERVICE="${RAILWAY_PG_SERVICE:-Postgres}"
umask 077

redact() { sed -E 's#//[^@/]*@#//<redacted>@#g; s#(SCRAM-SHA-256|md5)\$?[A-Za-z0-9+/=:$]{16,}#<hash-redacted>#g'; }
say()    { printf '%s\n' "$*"; }
now()    { date -u +%Y-%m-%dT%H:%M:%SZ; }

# ----------------------------------------------------------------- inner half
# Runs inside `railway run`, so DATABASE_URL is the service's own PRIVATE-host
# URL. Rewrite host and port to the tunnel; never print any of it.
if [[ "${1:-}" == --inner ]]; then
    : "${OFC_TUNNEL_PORT:?inner half needs the tunnel port}"
    if [[ -z "${DATABASE_URL:-}" ]]; then
        say "FATAL: railway run injected no DATABASE_URL (is '$PG_SERVICE' the right service?)"; exit 2
    fi
    OFC_DATABASE_URL="$(python3 -I -c '
import os, sys
from urllib.parse import urlsplit, urlunsplit
p = urlsplit(os.environ["DATABASE_URL"])
if not p.hostname:
    sys.exit("injected DATABASE_URL has no host; not guessing")
ui = p.username or ""
if p.password:
    ui += ":" + p.password
if ui:
    ui += "@"
sys.stdout.write(urlunsplit((p.scheme, ui + "127.0.0.1:" + os.environ["OFC_TUNNEL_PORT"], p.path, p.query, p.fragment)))
')" || { say "FATAL: could not rewrite the injected URL to the tunnel"; exit 2; }
    export OFC_DATABASE_URL
    unset DATABASE_URL DATABASE_PUBLIC_URL MFC_DATABASE_URL
    export BACKUP_TARGETS=ofc
    exec "$REPO/scripts/backup_database_to_nas.sh"
fi

# ----------------------------------------------------------------- outer half
mkdir -p "$STATE_DIR" || { say "FATAL: cannot create $STATE_DIR"; exit 2; }
STATUS="$STATE_DIR/status"
WORK="$(mktemp -d "$STATE_DIR/run.XXXXXX")" || { say "FATAL: no scratch dir"; exit 2; }
TLOG="$WORK/tunnel.log"
RLOG="$WORK/backup.log"
TPID=""
BPID=""
TPORT=""
RESULT=fail
REASON="aborted before completion"
ARCHIVE=""
STARTED="$(now)"

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

port_open() { (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null; }

# The tunnel is `railway connect`, which starts an ssh child in a session of its
# own (measured 2026-11-25: killing railway's process group left the ssh process
# listening, orphaned under init). So the CLOSE is judged by the only thing that
# matters, the port, and escalates until it is shut:
#   1. SIGINT to the PID started here (the CLI's documented Ctrl+C stop),
#   2. SIGTERM to that PID's group,
#   3. SIGTERM then SIGKILL to the process LISTENING on the tunnel port, found
#      by port (never by name), and only if its command line is an ssh -L for
#      exactly this port and it belongs to this user.
port_listener_pid() { # PORT -> pid of the listener, if any
    ss -ltnpH "sport = :$1" 2>/dev/null | sed -n 's/.*pid=\([0-9]*\).*/\1/p' | head -1
}
wait_port_closed() { # PORT SECONDS
    local i
    for ((i=0; i<$2*4; i++)); do port_open "$1" || return 0; sleep 0.25; done
    ! port_open "$1"
}
close_tunnel() { # idempotent
    [[ -n "$TPORT" ]] || { TPID=""; return 0; }
    local lp cmd
    if [[ -n "$TPID" ]] && kill -0 "$TPID" 2>/dev/null; then
        kill -INT "$TPID" 2>/dev/null
        wait_port_closed "$TPORT" 6 || { kill -TERM -- "-$TPID" 2>/dev/null; wait_port_closed "$TPORT" 4; }
    fi
    if port_open "$TPORT"; then
        lp="$(port_listener_pid "$TPORT")"
        cmd="$(tr '\0' ' ' < "/proc/$lp/cmdline" 2>/dev/null)"
        if [[ -n "$lp" && "$cmd" == *"-L 127.0.0.1:$TPORT:"* && -O "/proc/$lp" ]]; then
            say "tunnel: the ssh child (pid $lp) outlived the CLI; closing it by port"
            kill -TERM "$lp" 2>/dev/null
            wait_port_closed "$TPORT" 4 || { kill -KILL "$lp" 2>/dev/null; wait_port_closed "$TPORT" 3; }
        fi
    fi
    [[ -n "$TPID" ]] && { wait "$TPID" 2>/dev/null; kill -TERM -- "-$TPID" 2>/dev/null; }
    TPID=""
    if port_open "$TPORT"; then
        say "LEAK: tunnel port $TPORT still accepts connections after teardown"
        RESULT=fail; REASON="the tunnel was not closed"
    else
        say "tunnel: closed (127.0.0.1:$TPORT no longer accepts connections)"
    fi
    TPORT=""
}

finish() {
    local rc=$?
    trap - EXIT INT TERM HUP
    if [[ -n "$BPID" ]] && kill -0 "$BPID" 2>/dev/null; then
        kill -TERM -- "-$BPID" 2>/dev/null; wait "$BPID" 2>/dev/null
        redact < "$WORK/raw.log" 2>/dev/null | tail -20
    fi
    close_tunnel
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

die() { REASON="$1"; say "FATAL: $1"; exit "${2:-2}"; }

RW="${RAILWAY_BIN:-$(command -v railway || true)}"
[[ -z "$RW" && -x "$HOME/.railway/bin/railway" ]] && RW="$HOME/.railway/bin/railway"
[[ -n "$RW" ]] || die "railway CLI not found (set RAILWAY_BIN or put it on PATH)"
command -v podman >/dev/null || die "podman not found"
command -v python3 >/dev/null || die "python3 not found"

# NAS first: a backup that cannot land is not worth a tunnel.
mkdir -p "$NAS_ROOT/optionsandfuturescalculator" 2>/dev/null \
    && : > "$NAS_ROOT/optionsandfuturescalculator/.write-probe.$$" 2>/dev/null \
    && rm -f "$NAS_ROOT/optionsandfuturescalculator/.write-probe.$$" \
    || die "the NAS is not writable at $NAS_ROOT"

# Authentication is checked on its own so a dead login is reported as one, not
# as a tunnel timeout. whoami also refreshes a stored login's access token.
# A transient network error is retried; a rejected login is not, it will not heal.
auth_ok=0
for t in 1 2 3; do
    if "$RW" whoami >/dev/null 2>"$WORK/whoami.err"; then auth_ok=1; break; fi
    grep -q -i 'unauthorized\|not logged in\|login' "$WORK/whoami.err" && break
    (( t < 3 )) && sleep 10
done
if (( ! auth_ok )); then
    what="$(redact < "$WORK/whoami.err" | head -2 | tr '\n' ' ')"
    if grep -q -i 'unauthorized\|not logged in\|login' "$WORK/whoami.err"; then
        die "railway login is not valid: run 'railway login' once as this user (or set RAILWAY_API_TOKEN). CLI said: $what"
    fi
    die "railway whoami failed (no network route to Railway, or an expired login). CLI said: $what"
fi

# Free local port. The window between choosing and binding is tiny, and a
# collision shows up as the tunnel failing to start, which is retried.
pick_port() { python3 -I -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])'; }

TRIES="${OFC_TUNNEL_TRIES:-3}"
WAIT="${OFC_TUNNEL_WAIT:-60}"
attempt=0
ok=0
while (( attempt < TRIES )); do
    attempt=$((attempt+1))
    TPORT="${OFC_TUNNEL_PORT:-$(pick_port)}"
    # Something already listening there would pass the readiness probe below and
    # send the backup into someone else's tunnel (found 2026-11-25: another
    # session's tunnel on the fixed port answered "database does not exist").
    if port_open "$TPORT"; then
        say "tunnel: 127.0.0.1:$TPORT is already in use; not reusing it"
        [[ -n "${OFC_TUNNEL_PORT:-}" ]] && die "OFC_TUNNEL_PORT $TPORT is already in use"
        TPORT=""; continue
    fi
    say "tunnel: opening to $PG_SERVICE on 127.0.0.1:$TPORT (attempt $attempt of $TRIES)"
    # setsid: its own session and process group, so the whole group can be closed
    # by the one PID started here, and a terminal ^C does not reach it first.
    setsid "$RW" connect "$PG_SERVICE" --tunnel-only -P "$TPORT" </dev/null >"$TLOG" 2>&1 &
    TPID=$!
    ok=0
    for ((i=0; i<WAIT*2; i++)); do
        if ! kill -0 "$TPID" 2>/dev/null; then break; fi
        if port_open "$TPORT"; then ok=1; break; fi
        sleep 0.5
    done
    (( ok )) && break
    say "tunnel: not ready; its output (connection details removed):"
    grep -v -i -E 'pass|postgres(ql)?://|url|secret|token' "$TLOG" | redact | head -5 | sed 's/^/    | /'
    close_tunnel
    (( attempt < TRIES )) && sleep 15
done
(( ok )) || die "the SSH tunnel never accepted connections after $TRIES attempts (network, or the registered SSH key)"
say "tunnel: accepting on 127.0.0.1:$TPORT"

say "backup: starting"
export OFC_TUNNEL_PORT="$TPORT"
# Background plus `wait`, so a signal reaches the trap at once instead of after
# the backup. The raw output stays in a private temp file and is shown only
# after redaction.
setsid timeout "${OFC_BACKUP_TIMEOUT:-5400}" \
    "$RW" run --service "$PG_SERVICE" -- "$SELF" --inner </dev/null >"$WORK/raw.log" 2>&1 &
BPID=$!
wait "$BPID"
brc=$?
BPID=""
redact < "$WORK/raw.log" | tee "$RLOG"

if (( brc == 124 )); then
    REASON="the backup exceeded its time limit"
elif (( brc != 0 )); then
    REASON="the backup script exited $brc"
else
    ARCHIVE="$(sed -n 's/^  *VERIFIED optionsandfuturescalculator: \([^ ]*\)\.dump .*/\1/p' "$RLOG" | tail -1)"
    D="$NAS_ROOT/optionsandfuturescalculator"
    if [[ -z "$ARCHIVE" ]]; then
        REASON="the backup exited 0 but never reported VERIFIED for the production database"
    elif [[ ! -s "$D/$ARCHIVE.dump" || ! -s "$D/$ARCHIVE.roles.sql" ]] \
         || ! grep -q '^VERDICT: PASS' "$D/$ARCHIVE.verify.log" 2>/dev/null; then
        REASON="reported VERIFIED but the verified pair is not on the NAS ($ARCHIVE)"
        ARCHIVE=""
    else
        RESULT=ok; REASON="verified"
    fi
fi
# finish() closes the tunnel, records the outcome and sets the exit code.
[[ "$RESULT" == ok ]] && exit 0
exit 1
