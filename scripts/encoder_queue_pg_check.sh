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
eqc_start_engine A "$PORT_A" postgres "DATABASE_URL=$DBURL" PGSSLMODE_OVERRIDE=prefer || exit 1
eqc_start_engine B "$PORT_B" postgres "DATABASE_URL=$DBURL" PGSSLMODE_OVERRIDE=prefer || exit 1
sleep 1
for e in A B; do
    check_eq "engine $e: both assistants announce INFERENCE_QUEUE=postgres" "$(grep -c 'INFERENCE_QUEUE=postgres' "$WORK/$e.log")" 2
done

eqc_burst mortgage "mortgage encoder" "strategy encoder"
eqc_burst strategy "strategy encoder" "mortgage encoder"
eqc_replay mortgage "mortgage encoder" 272
eqc_replay strategy "strategy encoder" 300

echo; [ "$FAILURES" -eq 0 ] && echo "encoder_queue_pg_check: all checks passed" || echo "encoder_queue_pg_check: $FAILURES FAILED"
exit $(( FAILURES != 0 ))
