#!/usr/bin/env bash
# Versioned, append-only database migrations with a ledger.
#
# @author Olumuyiwa Oluwasanmi
#
#   db_migrate.sh lint                 offline: file names, manifest, append-only
#   db_migrate.sh new NAME             create the next numbered migration
#   db_migrate.sh seal                 append new files' checksums to the manifest
#   db_migrate.sh status               ledger vs files: applied / PENDING / CHANGED
#   db_migrate.sh verify               exit 1 if a recorded migration was edited
#   db_migrate.sh apply [--dry-run]    apply pending migrations in order
#   db_migrate.sh adopt VERSION        record a version as applied WITHOUT running it
#
# Environment: OFC_DATABASE_URL (or DATABASE_URL) for the hosted database, or
# DB_TARGET=scratch:<container>:<db> for a scratch one. See
# docs/technical/DATABASE_MIGRATIONS.md.
#
# THE RULES THIS ENFORCES
#   1. Append-only. A file already in the ledger or the manifest is never edited;
#      its sha256 is recorded and a changed file is an error, not a re-run.
#   2. In order. A pending version lower than the highest applied one is
#      refused: it means two lanes numbered a migration the same, or one was
#      back-dated.
#   3. One transaction per migration (the ledger row commits with it), under an
#      advisory lock so two runners cannot interleave. A file whose first line
#      is `-- migrate:no-transaction` (CREATE INDEX CONCURRENTLY and the like)
#      runs outside one; its ledger row is written after, so a crash between
#      the two leaves it PENDING and it must be written to be re-runnable.
#   4. Idempotent re-run. `apply` with nothing pending changes nothing.
set -uo pipefail
# shellcheck source=db_common.sh
source "$(dirname "${BASH_SOURCE[0]}")/db_common.sh"

TARGET="${DB_TARGET:-url}"
MANIFEST="$MIG_DIR/MANIFEST.sha256"
LOCK_KEY=7267430001   # arbitrary; one number for every runner of this database
who() { printf '%s' "${OFC_MIGRATE_BY:-${USER:-unknown}@$(hostname -s 2>/dev/null || echo host)}" | tr -c 'A-Za-z0-9@._-' '_'; }

ensure_ledger() {
    pg_exec "$TARGET" -v ON_ERROR_STOP=1 >/dev/null <<'SQL'
CREATE SCHEMA IF NOT EXISTS ofc_meta;
REVOKE ALL ON SCHEMA ofc_meta FROM PUBLIC;
CREATE TABLE IF NOT EXISTS ofc_meta.schema_migrations (
    version       text PRIMARY KEY CHECK (version ~ '^[0-9]{4}$'),
    name          text NOT NULL,
    checksum      text NOT NULL CHECK (checksum ~ '^[0-9a-f]{64}$'),
    kind          text NOT NULL DEFAULT 'applied' CHECK (kind IN ('applied', 'adopted')),
    applied_at    timestamptz NOT NULL DEFAULT now(),
    applied_by    text NOT NULL DEFAULT session_user,
    execution_ms  integer,
    pg_version    text NOT NULL DEFAULT current_setting('server_version')
);
REVOKE ALL ON ofc_meta.schema_migrations FROM PUBLIC;
CREATE OR REPLACE FUNCTION ofc_meta.schema_migrations_append_only() RETURNS trigger
LANGUAGE plpgsql AS $f$
BEGIN
    RAISE EXCEPTION 'ofc_meta.schema_migrations is append-only (% refused)', tg_op
        USING HINT = 'a recorded migration is never edited: add a new one',
              ERRCODE = 'insufficient_privilege';
END;
$f$;
DO $t$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_trigger WHERE tgname = 'schema_migrations_append_only_trg'
                    AND tgrelid = 'ofc_meta.schema_migrations'::regclass) THEN
        CREATE TRIGGER schema_migrations_append_only_trg
            BEFORE UPDATE OR DELETE ON ofc_meta.schema_migrations
            FOR EACH ROW EXECUTE FUNCTION ofc_meta.schema_migrations_append_only();
    END IF;
END
$t$;
SQL
}

# Prints "version checksum kind" per ledger row.
ledger_rows() { pg_exec "$TARGET" -At -F' ' -v ON_ERROR_STOP=1 -c \
    "select version, checksum, kind from ofc_meta.schema_migrations order by version"; }

cmd_lint() {
    local rc=0 f v seen="" line mv ms
    while IFS= read -r f; do
        [[ -z "$f" ]] && continue
        v="$(mig_version "$f")"
        if [[ ! "$(basename "$f")" =~ ^[0-9]{4}_[a-z0-9_]+\.sql$ ]]; then
            echo "LINT FAIL  bad file name: $(basename "$f") (want NNNN_snake_case.sql)"; rc=1
        fi
        if [[ " $seen " == *" $v "* ]]; then echo "LINT FAIL  duplicate version $v"; rc=1; fi
        seen="$seen $v"
    done < <(mig_files)
    [[ -z "$seen" ]] && { echo "LINT FAIL  no migrations found in $MIG_DIR"; return 1; }
    if [[ ! -f "$MANIFEST" ]]; then echo "LINT FAIL  $MANIFEST missing (run: db_migrate.sh seal)"; return 1; fi
    # every file is sealed and unchanged
    while IFS= read -r f; do
        [[ -z "$f" ]] && continue
        v="$(mig_version "$f")"
        line="$(awk -v v="$v" '$1==v {print $2}' "$MANIFEST")"
        if [[ -z "$line" ]]; then echo "LINT FAIL  $v is not in the manifest (run: db_migrate.sh seal)"; rc=1
        elif [[ "$line" != "$(mig_sum "$f")" ]]; then
            echo "LINT FAIL  $v was EDITED after it was sealed: add a new migration instead"; rc=1
        fi
    done < <(mig_files)
    # every manifest line still has its file
    while read -r mv ms _; do
        [[ -z "$mv" || "$mv" == \#* ]] && continue
        ls "$MIG_DIR/${mv}_"*.sql >/dev/null 2>&1 || { echo "LINT FAIL  $mv is in the manifest but its file is gone"; rc=1; }
    done < "$MANIFEST"
    # git: no line of the committed manifest may change or disappear
    if git -C "$REPO" rev-parse --git-dir >/dev/null 2>&1 \
       && git -C "$REPO" cat-file -e "HEAD:backend/db/migrations/MANIFEST.sha256" 2>/dev/null; then
        while read -r l; do
            grep -qxF -- "$l" "$MANIFEST" || { echo "LINT FAIL  committed manifest line changed or removed: ${l%% *}"; rc=1; }
        done < <(git -C "$REPO" show "HEAD:backend/db/migrations/MANIFEST.sha256" | grep -v '^#')
    fi
    [[ $rc -eq 0 ]] && echo "lint OK: $(mig_files | wc -l) migration(s), manifest consistent"
    return $rc
}

cmd_new() {
    local name="${1:-}"
    [[ "$name" =~ ^[a-z0-9_]+$ ]] || { echo "usage: db_migrate.sh new snake_case_name" >&2; return 2; }
    local last next
    last="$(mig_files | tail -1)"; last="$(mig_version "${last:-0000_x}")"
    next="$(printf '%04d' $((10#$last + 1)))"
    local path="$MIG_DIR/${next}_${name}.sql"
    cat > "$path" <<EOT
-- ${next}_${name}
--
-- @author $(git -C "$REPO" config user.name 2>/dev/null || echo "Olumuyiwa Oluwasanmi")
--
-- WHY: <the defect or feature this changes the schema for>
--
-- Runs in ONE transaction together with its ledger row. Add
-- "-- migrate:no-transaction" as the FIRST line for CREATE INDEX CONCURRENTLY.
-- Roles, grants and RLS policies are schema: put them here, or the drift gate
-- (scripts/check_schema_drift.sh) will say the database moved without a record.
-- Never edit this file once it is sealed or applied; add a newer migration.

EOT
    echo "created $path"
    echo "next: write the SQL, then db_migrate.sh seal && scripts/check_schema_drift.sh --selftest"
}

cmd_seal() {
    [[ -f "$MANIFEST" ]] || printf '# version sha256 name -- APPEND-ONLY. Written by db_migrate.sh seal; never edit a line.\n' > "$MANIFEST"
    local f v added=0
    while IFS= read -r f; do
        [[ -z "$f" ]] && continue
        v="$(mig_version "$f")"
        if ! awk -v v="$v" '$1==v {found=1} END{exit !found}' "$MANIFEST"; then
            echo "$v $(mig_sum "$f") $(mig_name "$f")" >> "$MANIFEST"; echo "sealed $v $(mig_name "$f")"; added=1
        fi
    done < <(mig_files)
    [[ $added -eq 0 ]] && echo "nothing new to seal"
    cmd_lint
}

# Computes: state per file into the global array STATE (version -> text) and
# returns 1 if anything is wrong. Prints a table when $1 = print.
analyse() {
    local mode="${1:-quiet}" rc=0 v sum k ledger f max=""
    declare -gA L_SUM=() L_KIND=() F_SUM=() F_NAME=()
    declare -ga PENDING=()
    ledger="$(ledger_rows)" || { echo "FATAL: cannot read the ledger" >&2; return 2; }
    while read -r v sum k; do [[ -n "$v" ]] && { L_SUM[$v]="$sum"; L_KIND[$v]="$k"; max="$v"; }; done <<< "$ledger"
    while IFS= read -r f; do
        [[ -z "$f" ]] && continue
        v="$(mig_version "$f")"
        [[ -n "${MIG_UPTO:-}" && "$v" > "$MIG_UPTO" ]] && continue   # drift gate: "the schema as of version N"
        F_SUM[$v]="$(mig_sum "$f")"; F_NAME[$v]="$(mig_name "$f")"
    done < <(mig_files)
    local all; all="$(printf '%s\n' "${!L_SUM[@]}" "${!F_SUM[@]}" | LC_ALL=C sort -u | grep -v '^$')"
    [[ "$mode" == print ]] && printf '%-6s %-9s %s\n' VERSION STATE NAME
    for v in $all; do
        local st
        if [[ -n "${L_SUM[$v]:-}" && -z "${F_SUM[$v]:-}" ]]; then st="MISSING-FILE"; rc=1
        elif [[ -n "${L_SUM[$v]:-}" && "${L_SUM[$v]}" != "${F_SUM[$v]}" ]]; then st="CHANGED"; rc=1
        elif [[ -n "${L_SUM[$v]:-}" ]]; then st="${L_KIND[$v]}"
        else
            st="PENDING"; PENDING+=("$v")
            if [[ -n "$max" && "$v" < "$max" ]]; then st="OUT-OF-ORDER"; rc=1; fi
        fi
        [[ "$mode" == print ]] && printf '%-6s %-9s %s\n' "$v" "$st" "${F_NAME[$v]:-?}"
    done
    return $rc
}

cmd_status() {
    ensure_ledger || return 2
    analyse print; local rc=$?
    echo "pending: ${#PENDING[@]}"
    [[ $rc -ne 0 ]] && echo "PROBLEM: a recorded migration changed, vanished, or a pending one is out of order"
    return $rc
}

cmd_verify() { ensure_ledger || return 2; analyse quiet; local rc=$?
    [[ $rc -eq 0 ]] && echo "verify OK: every recorded migration matches its file" || { analyse print; echo "verify FAILED"; }
    return $rc; }

apply_one() {
    local v="$1" f sum name notx=0 by sql
    f="$(ls "$MIG_DIR/${v}_"*.sql)"; sum="$(mig_sum "$f")"; name="$(mig_name "$f")"; by="$(who)"
    head -1 "$f" | grep -q '^-- migrate:no-transaction' && notx=1
    sql="INSERT INTO ofc_meta.schema_migrations(version, name, checksum, applied_by, execution_ms)
         VALUES ('$v', '$name', '$sum', '$by',
                 (extract(epoch FROM clock_timestamp() - now()) * 1000)::int)"
    if [[ $notx -eq 1 ]]; then
        pg_exec "$TARGET" -v ON_ERROR_STOP=1 -c "select pg_advisory_lock($LOCK_KEY)" -f - -c "$sql" < "$f" >/dev/null
    else
        pg_exec "$TARGET" -v ON_ERROR_STOP=1 -1 -c "select pg_advisory_xact_lock($LOCK_KEY)" -f - -c "$sql" < "$f" >/dev/null
    fi
}

cmd_apply() {
    local dry=0; [[ "${1:-}" == "--dry-run" ]] && dry=1
    cmd_lint >/dev/null || { cmd_lint; echo "refusing to apply: lint failed"; return 1; }
    ensure_ledger || return 2
    analyse quiet || { analyse print; echo "refusing to apply: ledger and files disagree"; return 1; }
    if [[ ${#PENDING[@]} -eq 0 ]]; then echo "nothing pending: database is at $(printf '%s\n' "${!L_SUM[@]}" | sort | tail -1)"; return 0; fi
    echo "plan: ${PENDING[*]}"
    [[ $dry -eq 1 ]] && { echo "(dry run: nothing applied)"; return 0; }
    local v
    for v in "${PENDING[@]}"; do
        if apply_one "$v"; then echo "applied $v ${F_NAME[$v]}"
        else echo "FAILED $v ${F_NAME[$v]}: rolled back, ledger unchanged; later migrations NOT attempted"; return 1; fi
    done
}

cmd_adopt() {
    local v="${1:-}"
    [[ "$v" =~ ^[0-9]{4}$ ]] || { echo "usage: db_migrate.sh adopt NNNN" >&2; return 2; }
    local f; f="$(ls "$MIG_DIR/${v}_"*.sql 2>/dev/null)" || { echo "no such migration $v" >&2; return 2; }
    ensure_ledger || return 2
    if [[ "${SKIP_DRIFT_GATE:-0}" != 1 ]]; then
        echo "adopt records that the database ALREADY IS at $v. Proving it first (drift gate)..."
        DB_TARGET_LIVE="$TARGET" DRIFT_UPTO="$v" "$(dirname "${BASH_SOURCE[0]}")/check_schema_drift.sh" --live || { echo "refusing to adopt: the live schema is not what $v describes"; return 1; }
    fi
    pg_exec "$TARGET" -v ON_ERROR_STOP=1 -c \
        "INSERT INTO ofc_meta.schema_migrations(version, name, checksum, kind, applied_by)
         VALUES ('$v', '$(mig_name "$f")', '$(mig_sum "$f")', 'adopted', '$(who)')" >/dev/null \
        && echo "adopted $v (recorded, not executed)"
}

case "${1:-}" in
    lint) cmd_lint ;;
    new) shift; cmd_new "$@" ;;
    seal) cmd_seal ;;
    status) cmd_status ;;
    verify) cmd_verify ;;
    apply) shift; cmd_apply "$@" ;;
    adopt) shift; cmd_adopt "$@" ;;
    *) sed -n '3,12p' "$0"; exit 2 ;;
esac
