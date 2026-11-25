#!/usr/bin/env bash
#
# Exit non-zero unless the production database has a VERIFIED backup younger
# than N days, and the last attempt did not fail.
#
# @author Olumuyiwa Oluwasanmi
#
# Silence must not read as success, so this trusts nothing that can go stale or
# be absent:
#   - Age comes from the STAMP in the archive's file name, not from the file's
#     mtime or from a sentinel. A copy, a touch or a restored file cannot make an
#     old backup look new.
#   - "Verified" is a pair on the NAS: a non-empty dump, a non-empty roles file
#     and a verify log whose verdict is PASS. backup_database_to_nas.sh moves a
#     pair there only after the restore rehearsal passes, so no pair, no backup.
#   - A NAS that cannot be read, a directory with no pair, a missing directory
#     and a backup that never ran all fail the same way as a failed backup.
#   - The wrapper's status file (see backup_production_db.sh) adds the last
#     attempt: a recorded failure, or an attempt that started and never
#     finished, fails too. A MISSING status file does not fail by itself (a
#     hand-run backup leaves none), but the NAS evidence above still must hold.
#
#   check_backup_freshness.sh [--max-age-days N] [--alert] [--quiet]
#
#   --alert   on a problem, also raise it where a person will see it (journal at
#             crit, a desktop notification if one is available, and an
#             ALERT-BACKUP-STALE.txt beside the backups and in the state
#             directory); on a healthy result remove those files. No network,
#             no credentials.
#
# EXIT: 0 healthy; 1 a problem (every reason is printed); 2 usage.
set -uo pipefail

MAX_DAYS="${OFC_BACKUP_MAX_AGE_DAYS:-2}"
NAS_ROOT="${NAS_ROOT:-/home/muyiwa/PrimaryNAS/DataFolder/PycharmProjects/db-backups}"
STATE_DIR="${OFC_BACKUP_STATE_DIR:-${XDG_STATE_HOME:-$HOME/.local/state}/ofc-backup}"
NAME=optionsandfuturescalculator
ALERT=0
QUIET=0
while (( $# )); do
    case "$1" in
        --max-age-days) MAX_DAYS="${2:-}"; shift 2;;
        --alert) ALERT=1; shift;;
        --quiet) QUIET=1; shift;;
        *) echo "usage: $0 [--max-age-days N] [--alert] [--quiet]" >&2; exit 2;;
    esac
done
[[ "$MAX_DAYS" =~ ^[0-9]+$ && "$MAX_DAYS" -ge 1 ]] || { echo "max age must be a whole number of days >= 1" >&2; exit 2; }

D="$NAS_ROOT/$NAME"
PROBLEMS=()
INFO=()
NOW="$(date -u +%s)"

# --- evidence on the NAS
newest=""
if [[ ! -d "$D" ]]; then
    PROBLEMS+=("no backup directory at $D: the NAS is unreachable or no backup has ever run")
else
    # Names sort by stamp, so the last is the newest. Anything under failed/ is
    # not matched. pre-migration dumps carry no roles/verify pair and never match.
    while IFS= read -r f; do
        b="$(basename "$f" .dump)"
        if [[ -s "$f" && -s "$D/$b.roles.sql" ]] && grep -q '^VERDICT: PASS' "$D/$b.verify.log" 2>/dev/null; then
            newest="$b"
        else
            INFO+=("ignored $b: not a complete verified pair")
        fi
    done < <(ls -1 "$D/$NAME"-[0-9]*T[0-9]*Z.dump 2>/dev/null | sort)
    if [[ -z "$newest" ]]; then
        PROBLEMS+=("no verified backup exists in $D (a verified pair is dump + roles + verify log with VERDICT: PASS)")
    else
        s="${newest##*-}"                                 # 20261125T180159Z
        iso="${s:0:4}-${s:4:2}-${s:6:2}T${s:9:2}:${s:11:2}:${s:13:2}Z"
        if ts="$(date -u -d "$iso" +%s 2>/dev/null)"; then
            age=$(( NOW - ts ))
            days=$(( age / 86400 ))
            hours=$(( (age % 86400) / 3600 ))
            if (( age < -300 )); then
                PROBLEMS+=("the newest verified backup is stamped in the future ($iso); the clock or the file is wrong")
            elif (( age > MAX_DAYS * 86400 )); then
                PROBLEMS+=("STALE: the newest verified backup is $days days $hours h old ($iso), limit $MAX_DAYS days")
            else
                INFO+=("newest verified backup: $iso (${days}d ${hours}h old, limit ${MAX_DAYS}d)")
            fi
        else
            PROBLEMS+=("cannot read the stamp of $newest")
        fi
    fi
fi

# --- the wrapper's record of the last attempt
S="$STATE_DIR/status"
if [[ -r "$S" ]]; then
    get() { sed -n "s/^$1=//p" "$S" | head -1; }
    res="$(get last_attempt_result)"; reason="$(get last_attempt_reason)"
    st="$(get last_attempt_started_utc)"; fin="$(get last_attempt_finished_utc)"
    case "$res" in
        ok) INFO+=("last attempt $st: ok");;
        fail) PROBLEMS+=("the last backup attempt FAILED ($fin): $reason");;
        running)
            sts="$(date -u -d "$st" +%s 2>/dev/null || echo 0)"
            if (( NOW - sts > 7200 )); then
                PROBLEMS+=("the last attempt started $st and never finished: the process died or hung")
            else
                INFO+=("an attempt started $st is still running")
            fi;;
        *) PROBLEMS+=("the status file is unreadable or has an unknown result '$res'");;
    esac
else
    INFO+=("no status file at $S: the unattended wrapper has not run on this host")
fi

# --- report
if (( ${#PROBLEMS[@]} )); then
    echo "BACKUP PROBLEM (production database $NAME):"
    for p in "${PROBLEMS[@]}"; do echo "  - $p"; done
    (( QUIET )) || for i in "${INFO[@]}"; do echo "  . $i"; done
    if (( ALERT )); then
        msg="Production database backup problem: ${PROBLEMS[0]}"
        logger -p user.crit -t ofc-backup -- "$msg" 2>/dev/null
        text="$(date -u +%Y-%m-%dT%H:%M:%SZ)
$(printf '%s\n' "${PROBLEMS[@]}")

Run: scripts/backup_production_db.sh  (see docs/technical/DATABASE_MIGRATIONS.md, Scheduled backups)
This file is removed by the next healthy check."
        mkdir -p "$STATE_DIR" 2>/dev/null
        printf '%s\n' "$text" > "$STATE_DIR/ALERT-BACKUP-STALE.txt" 2>/dev/null
        printf '%s\n' "$text" > "$NAS_ROOT/ALERT-BACKUP-STALE.txt" 2>/dev/null
        command -v notify-send >/dev/null && notify-send -u critical "OFC database backup" "$msg" 2>/dev/null
    fi
    exit 1
fi

(( QUIET )) || { echo "backup healthy (production database $NAME):"; for i in "${INFO[@]}"; do echo "  . $i"; done; }
if (( ALERT )); then
    rm -f "$STATE_DIR/ALERT-BACKUP-STALE.txt" "$NAS_ROOT/ALERT-BACKUP-STALE.txt" 2>/dev/null
fi
exit 0
