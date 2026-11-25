#!/usr/bin/env bash
# THE DRIFT GATE: fail when the live schema is not what the recorded migrations say.
#
# @author Olumuyiwa Oluwasanmi
#
#   check_schema_drift.sh --live       compare the hosted database to its migrations
#   check_schema_drift.sh --selftest   POSITIVE CONTROL: prove the gate goes red
#
# "Changes are recorded automatically" means an unrecorded change cannot hide.
# A hand-edit (a policy dropped, a role given BYPASSRLS, a column added in a
# console) changes no answer the application returns, so nothing else notices.
# This builds the schema the ledger SAYS the database has -- a scratch server,
# the migrations up to the highest version in the live ledger, applied by the
# real runner -- takes backend/db/schema_snapshot.sql of both, and diffs them.
# Any difference is a failure, and the diff is the thing to write a migration
# for (`db_migrate.sh new <name>`).
#
# Also fails when the live ledger and the files disagree (a recorded migration
# was edited, or its file is gone): the other way a plan silently changes.
#
# Exit: 0 clean, 1 drift or ledger mismatch, 2 could not run. A gate that could
# not run is not green. Ignore patterns: backend/db/drift_ignore.txt (extended
# regexes against whole snapshot lines; every entry needs a reason).
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/db_common.sh"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SNAP="$DB_DIR/schema_snapshot.sql"
IGNORE="$DB_DIR/drift_ignore.txt"
WORK="$(mktemp -d)"; CONTAINER=""
cleanup() { [[ -n "$CONTAINER" ]] && scratch_stop "$CONTAINER"; rm -rf "$WORK"; }
trap cleanup EXIT

snapshot() {   # snapshot TARGET > stdout (ignore-filtered, empty lines dropped)
    local out; out="$(pg_exec "$1" -At -v ON_ERROR_STOP=1 -f - < "$SNAP")" || return 2
    local pats=()
    if [[ -f "$IGNORE" ]]; then
        while IFS= read -r l; do [[ -z "$l" || "$l" == \#* ]] && continue; pats+=("${l%%  #*}"); done < "$IGNORE"
    fi
    if [[ ${#pats[@]} -gt 0 ]]; then
        printf '%s\n' "$out" | grep -v '^$' | grep -v -E -f <(printf '%s\n' "${pats[@]}") || true
    else
        printf '%s\n' "$out" | grep -v '^$'
    fi
}

# build_expected CONTAINER DB [UPTO]: stub auth, then the real runner.
build_db() {
    local c="$1" db="$2" upto="${3:-}"
    scratch_db_create "$c" "$db" || return 2
    pg_exec "scratch:$c:$db" -v ON_ERROR_STOP=1 < "$DB_DIR/scratch_auth_stub.sql" >/dev/null || return 2
    DB_TARGET="scratch:$c:$db" MIG_UPTO="$upto" "$HERE/db_migrate.sh" apply
}

compare() {    # compare EXPECTED_TARGET LIVE_TARGET LABEL -> 0 same, 1 differ
    snapshot "$1" > "$WORK/expected.snap" || return 2
    snapshot "$2" > "$WORK/live.snap" || return 2
    if diff -u --label "expected (migrations)" --label "$3" "$WORK/expected.snap" "$WORK/live.snap" > "$WORK/diff.txt"; then
        return 0
    fi
    return 1
}

ensure_scratch() { [[ -n "$CONTAINER" ]] || CONTAINER="$(scratch_start)" || { echo "FATAL: no scratch server" >&2; exit 2; }; }

cmd_live() {
    local live="${DB_TARGET_LIVE:-${DB_TARGET:-url}}" rc=0 upto="" ledger
    echo "== drift gate: live database vs the schema its ledger records =="
    ledger="$(pg_exec "$live" -At -v ON_ERROR_STOP=1 -c \
        "select coalesce(max(version),'') from ofc_meta.schema_migrations" 2>/dev/null)"
    if [[ $? -ne 0 ]]; then
        echo "NOTE: the live database has no ledger yet (ofc_meta.schema_migrations)."
        echo "      Comparing it with ALL migrations. Once it matches, run: db_migrate.sh adopt 0001"
        upto=""
    else
        upto="$ledger"; echo "ledger head: ${upto:-<empty>}"
        [[ -z "$upto" ]] && upto=""
        if ! DB_TARGET="$live" "$HERE/db_migrate.sh" verify; then rc=1; fi
    fi
    ensure_scratch
    local ver; ver="$(pg_exec "$live" -At -c 'show server_version' 2>/dev/null)"
    echo "live server: PostgreSQL ${ver:-unknown}; scratch server: $(podman exec "$CONTAINER" psql -U postgres -Atc 'show server_version')"
    build_db "$CONTAINER" expected "${upto:-}" >"$WORK/build.log" 2>&1 || { cat "$WORK/build.log"; echo "FATAL: could not build the expected schema"; return 2; }
    compare "scratch:$CONTAINER:expected" "$live" "live database"; local c=$?
    [[ $c -eq 2 ]] && return 2
    if [[ $c -eq 1 ]]; then
        echo "DRIFT: the live schema differs from what the recorded migrations produce:"
        cat "$WORK/diff.txt"
        echo
        echo "Every '+' is something in the live database no migration accounts for; every '-' is"
        echo "something a migration promised that is missing. Write the migration that makes this"
        echo "true on purpose (db_migrate.sh new <name>) or undo the hand-edit."
        rc=1
    fi
    [[ $rc -eq 0 ]] && echo "drift gate GREEN: live schema == recorded migrations ($(wc -l < "$WORK/live.snap") catalog facts compared)"
    [[ $rc -ne 0 ]] && echo "drift gate RED"
    return $rc
}

# ---- positive control ------------------------------------------------------
fails=0
# Roles are CLUSTER-level, so the scratch `expected` and `live` databases share
# them and a role drift would hit both sides. The expected side is therefore
# frozen to a file BEFORE any drift is introduced and compared from that.
expect() {  # expect red|green LABEL
    local want="$1" label="$2" got
    snapshot "scratch:$CONTAINER:live" > "$WORK/live.snap" || return 2
    cp "$WORK/pinned.snap" "$WORK/expected.snap"
    diff -u --label "expected (migrations)" --label "scratch live" "$WORK/expected.snap" "$WORK/live.snap" > "$WORK/diff.txt"; local c=$?
    [[ $c -eq 1 ]] && got=red || { [[ $c -eq 0 ]] && got=green || got=error; }
    if [[ "$got" == "$want" ]]; then printf '  %-5s as expected  %s\n' "$(echo "$got" | tr a-z A-Z)" "$label"
    else printf '  %-5s WANTED %s  %s\n' "$(echo "$got" | tr a-z A-Z)" "$(echo "$want" | tr a-z A-Z)" "$label"; fails=$((fails+1)); fi
    if [[ "$got" == red && "$want" == red ]]; then grep -E '^[+-][^+-]' "$WORK/diff.txt" | head -3 | cut -c1-150 | sed 's/^/          /'; fi
}
drift() {  # drift LABEL "break sql" "repair sql"
    local live="scratch:$CONTAINER:live"
    pg_exec "$live" -v ON_ERROR_STOP=1 -c "$2" >/dev/null || { echo "  control setup failed: $1"; fails=$((fails+1)); return; }
    expect red "$1"
    pg_exec "$live" -v ON_ERROR_STOP=1 -c "$3" >/dev/null || { echo "  control repair failed: $1"; fails=$((fails+1)); return; }
    expect green "$1 -- repaired"
}

cmd_selftest() {
    echo "== drift gate positive control (scratch server only; nothing hosted is touched) =="
    ensure_scratch
    echo "scratch: PostgreSQL $(podman exec "$CONTAINER" psql -U postgres -Atc 'show server_version')"
    "$HERE/db_migrate.sh" lint || fails=$((fails+1))
    build_db "$CONTAINER" expected >"$WORK/b1.log" 2>&1 || { cat "$WORK/b1.log"; echo "FATAL: build failed"; return 2; }
    build_db "$CONTAINER" live >"$WORK/b2.log" 2>&1 || { cat "$WORK/b2.log"; echo "FATAL: build failed"; return 2; }
    local L="scratch:$CONTAINER:live"
    snapshot "scratch:$CONTAINER:expected" > "$WORK/pinned.snap" || return 2
    echo "-- runner: idempotent re-run, ledger shape"
    if DB_TARGET="$L" "$HERE/db_migrate.sh" apply | grep -q '^nothing pending'; then echo "  GREEN as expected  re-run applies nothing"; else echo "  RED  re-run did something"; fails=$((fails+1)); fi
    DB_TARGET="$L" "$HERE/db_migrate.sh" status | sed 's/^/  /'
    if pg_exec "$L" -v ON_ERROR_STOP=1 -c "update ofc_meta.schema_migrations set checksum = repeat('0',64)" >/dev/null 2>&1; then
        echo "  FAIL ledger accepted an UPDATE"; fails=$((fails+1)); else echo "  GREEN as expected  ledger refuses UPDATE (append-only)"; fi
    echo "-- control 0: untouched live == expected"
    expect green "pristine"
    echo "-- controls: each drift must turn the gate RED, and its repair must turn it GREEN"
    drift "policy dropped" \
        "drop policy saved_strategies_own_rows on public.saved_strategies" \
        "create policy saved_strategies_own_rows on public.saved_strategies for all to ofc_app using (user_id = nullif(current_setting('app.current_user_id', true), '')::uuid) with check (user_id = nullif(current_setting('app.current_user_id', true), '')::uuid)"
    drift "role given BYPASSRLS" "alter role ofc_app bypassrls" "alter role ofc_app nobypassrls"
    drift "RLS FORCE switched off" "alter table public.users no force row level security" "alter table public.users force row level security"
    drift "RLS disabled" "alter table public.profiles disable row level security" "alter table public.profiles enable row level security"
    drift "column added by hand" "alter table public.users add column hand_edit int" "alter table public.users drop column hand_edit"
    drift "index added by hand" "create index hand_idx on public.users (email)" "drop index public.hand_idx"
    drift "grant widened to PUBLIC" "grant select on public.saved_strategies to public" "revoke select on public.saved_strategies from public"
    drift "guard row removed" "set app.allow_destructive = 'on'; delete from public.protected_relations where table_name = 'users' and schema_name = 'public'" \
        "insert into public.protected_relations select 'public','users',true,true,0.50,25,'restored'"
    drift "GoTrue default changed" "alter table auth.users alter column recovery_token drop default" "alter table auth.users alter column recovery_token set default ''::character varying"
    drift "event trigger disabled" "alter event trigger guard_protected_drop_trg disable" "alter event trigger guard_protected_drop_trg enable"
    echo "-- controls: roles that drift_ignore.txt accepts are pinned by attribute, not by name"
    # Roles are cluster-level and the snapshot was pinned before these exist, so each is
    # created, shown GREEN while it matches the accepted line exactly, then made to drift.
    pg_exec "$L" -v ON_ERROR_STOP=1 -c "create role service_role nosuperuser noinherit nocreaterole nocreatedb nologin noreplication bypassrls" >/dev/null \
        || { echo "  control setup failed: service_role"; fails=$((fails+1)); }
    expect green "service_role present with its accepted attributes is ignored"
    drift "accepted role service_role loses BYPASSRLS" "alter role service_role nobypassrls" "alter role service_role bypassrls"
    drift "accepted role service_role gains CREATEROLE" "alter role service_role createrole" "alter role service_role nocreaterole"
    drift "accepted role service_role gains SUPERUSER" "alter role service_role superuser" "alter role service_role nosuperuser"
    pg_exec "$L" -v ON_ERROR_STOP=1 -c "drop role service_role" >/dev/null || fails=$((fails+1))
    drift "a NEW role holding BYPASSRLS appears" "create role rogue_admin nologin bypassrls" "drop role rogue_admin"
    drift "a role that MIMICS an accepted name but is not the pinned one" "create role anon login" "drop role anon"
    echo "-- control: a recorded migration edited after it was applied"
    mkdir "$WORK/migs" && cp "$MIG_DIR"/* "$WORK/migs/" && echo "-- hand edit" >> "$WORK/migs/0001_baseline.sql"
    if MIG_DIR_OVERRIDE="$WORK/migs" DB_TARGET="$L" "$HERE/db_migrate.sh" verify >/dev/null 2>&1; then
        echo "  GREEN WANTED RED  an edited applied migration passed verify"; fails=$((fails+1)); else echo "  RED   as expected  edited applied migration is refused by verify"; fi
    if MIG_DIR_OVERRIDE="$WORK/migs" "$HERE/db_migrate.sh" lint >/dev/null 2>&1; then
        echo "  GREEN WANTED RED  an edited sealed migration passed lint"; fails=$((fails+1)); else echo "  RED   as expected  edited sealed migration is refused by lint"; fi
    echo "-- control: a failing migration rolls back whole and leaves the ledger alone"
    cp "$WORK/migs/0001_baseline.sql" /dev/null; cp "$MIG_DIR/0001_baseline.sql" "$WORK/migs/0001_baseline.sql"
    printf -- '-- bad\ncreate table public.half_applied(x int);\nselect 1/0;\n' > "$WORK/migs/9999_bad.sql"
    MIG_DIR_OVERRIDE="$WORK/migs" DB_TARGET="$L" "$HERE/db_migrate.sh" apply >/dev/null 2>&1   # lint refuses (unsealed): expected
    ( cd "$WORK/migs" && echo "9999 $(sha256sum 9999_bad.sql | cut -d' ' -f1) bad" >> MANIFEST.sha256 )
    if MIG_DIR_OVERRIDE="$WORK/migs" DB_TARGET="$L" "$HERE/db_migrate.sh" apply >/dev/null 2>&1; then
        echo "  GREEN WANTED RED  a failing migration reported success"; fails=$((fails+1))
    elif [[ "$(pg_scalar "$L" "select to_regclass('public.half_applied') is null and (select count(*) from ofc_meta.schema_migrations)=$(mig_files | wc -l)")" == t ]]; then
        echo "  RED   as expected  failing migration left no table and no ledger row"
    else echo "  FAIL  failing migration left debris"; fails=$((fails+1)); fi
    expect green "after the controls: live == expected again"
    echo
    if [[ $fails -eq 0 ]]; then echo "SELFTEST PASS: the gate went RED for every drift and GREEN when repaired"; return 0; fi
    echo "SELFTEST FAIL: $fails control(s) misbehaved"; return 1
}

case "${1:-}" in
    --live) cmd_live ;;
    --selftest) cmd_selftest ;;
    *) sed -n '3,9p' "$0"; exit 2 ;;
esac
