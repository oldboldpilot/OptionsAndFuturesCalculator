#!/usr/bin/env bash
# The encoders on the POSTGRES queue (INFERENCE_QUEUE=postgres): the same accounting and the same
# byte-identity claims the SGEE gate makes (they are the SAME functions, eqc_burst and eqc_replay in
# the shared library), against a scratch database. Not a ctest: it needs the sensen-postgres container.
#
# @author Olumuyiwa Oluwasanmi
#
#   flock /home/muyiwa/.cache/lanes/test_ofc.lock scripts/encoder_queue_pg_check.sh <calculator_engine>
#
# Only the scratch database (EQC_PG_DB, default `sensen_gw_gate_qenc`) is created and dropped here; the live
# `sensen_gw` is never named. The connection string carries a password and is built in this shell
# and handed to the engines by environment; it is never printed.
set -uo pipefail
ENGINE_BIN="${1:?usage: $0 <calculator_engine>}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NODE_BIN="${EQC_NODE_BIN:-/bin/true}"       # no SGEE nodes are started; the library only needs a path
DB="${EQC_PG_DB:-sensen_gw_gate_qenc}"
EQC_QUEUE=postgres
EQC_BURST=96

# shellcheck source=/dev/null
. "$REPO/backend/tests/integration/lib/encoder_cluster.sh"
eqc_init || exit $?

drop_db() { podman exec sensen-postgres psql -U postgres -qc "drop database if exists $DB" >/dev/null 2>&1; }
trap 'drop_db; eqc_cleanup' EXIT
drop_db
podman exec sensen-postgres psql -U postgres -v ON_ERROR_STOP=1 -qc "create database $DB" || { echo "cannot create scratch db"; exit 3; }
podman exec -i sensen-postgres psql -U postgres -d "$DB" -v ON_ERROR_STOP=1 -q < "$REPO/backend/migrations/02_inference_queue.sql" >/dev/null \
    || { echo "cannot apply the inference_queue migration"; exit 3; }
PGPW=$(podman inspect sensen-postgres --format '{{range .Config.Env}}{{println .}}{{end}}' | sed -n 's/^POSTGRES_PASSWORD=//p')
DBURL="host=127.0.0.1 port=54329 dbname=$DB user=postgres password=$PGPW"; unset PGPW

eqc_start_engine L "$PORT_L" local || exit 1
eqc_start_engine A "$PORT_A" postgres ENCODER_LOCAL_MAX_IN_FLIGHT=0 "DATABASE_URL=$DBURL" PGSSLMODE_OVERRIDE=prefer || exit 1
eqc_start_engine B "$PORT_B" postgres ENCODER_LOCAL_MAX_IN_FLIGHT=0 "DATABASE_URL=$DBURL" PGSSLMODE_OVERRIDE=prefer || exit 1
sleep 1
for e in A B; do
    check_eq "engine $e: both assistants announce INFERENCE_QUEUE=postgres" "$(grep -c 'INFERENCE_QUEUE=postgres' "$WORK/$e.log")" 2
done

# A and B send every request to the queue first (the lever that exercises the queue path at one caller).
# The DEFAULT routing on this substrate: an engine at the defaults answers in-process and writes no
# job at all at a concurrency of one.
eqc_start_engine D "$PORT_D" postgres "DATABASE_URL=$DBURL" PGSSLMODE_OVERRIDE=prefer || exit 1
jobs0="$(podman exec sensen-postgres psql -U postgres -d "$DB" -Atc 'select count(*) from inference_jobs')"
for surface in mortgage strategy; do
    label="$surface encoder"; ip0="$(eqc_inprocess D "$label")"; sp0="$(eqc_queued D "$label")"
    python3 -P "$PROBE" latency --target "127.0.0.1:$PORT_D" --surface "$surface" -c 1 -n 48 --warmup 0 > "$WORK/pg_idle_$surface.json" 2>&1 \
        || { fail "$surface idle run on postgres reported errors"; cat "$WORK/pg_idle_$surface.json"; }
    check_eq "postgres idle $surface: all 48 answered in-process" "$(( $(eqc_inprocess D "$label") - ip0 ))" 48
    check_eq "postgres idle $surface: none spilled" "$(( $(eqc_queued D "$label") - sp0 ))" 0
done
jobs1="$(podman exec sensen-postgres psql -U postgres -d "$DB" -Atc 'select count(*) from inference_jobs')"
check_eq "postgres idle: the jobs table did not gain a row" "$(( jobs1 - jobs0 ))" 0
kill -TERM "${ENGINE_PIDS[-1]}" 2>/dev/null || true   # the pid this script captured

eqc_burst mortgage "mortgage encoder" "strategy encoder"
eqc_burst strategy "strategy encoder" "mortgage encoder"
eqc_replay mortgage "mortgage encoder" 272
eqc_replay strategy "strategy encoder" 300

echo; [ "$FAILURES" -eq 0 ] && echo "encoder_queue_pg_check: all checks passed" || echo "encoder_queue_pg_check: $FAILURES FAILED"
exit $(( FAILURES != 0 ))
