#!/usr/bin/env bash
# What the shared queue ADDS to an encoder parse: p50/p95 at concurrency 1/8/24 for three routings,
# optionally across Raft heartbeats:
#
#   L  INFERENCE_QUEUE=local: the chain in-process, no queue anywhere (the floor). Its in-proc column is
#      by construction (every request sent): local mode logs no per-request line to count
#   A  INFERENCE_QUEUE=sgee at the DEFAULT bounds: in-process first, spill to the queue only when busy
#   Q  INFERENCE_QUEUE=sgee with ENCODER_LOCAL_MAX_IN_FLIGHT=0: every request goes to the queue first,
#      the routing the service had before 2026-10-07 and the one that cost ~570 ms at a 300 ms heartbeat
#
# @author Olumuyiwa Oluwasanmi
#
#   encoder_queue_bench.sh <sgee_queue_node> <calculator_engine> <out-dir>
#
# Boots a three-node SGEE cluster and four engines holding the real encoder weights -- L, A, Q and B
# (the executor the others spill to) -- and drives the SAME request stream at L, A and Q, interleaved
# over several rounds, because this repository has already paid for a throughput claim made from one
# run (the spread across runs exceeded the effect).
#
# Environment:
#   EQB_ROUNDS         rounds per cell (default 3)
#   EQB_SURFACES       surfaces to drive (default "mortgage strategy")
#   EQB_HEARTBEATS     Raft heartbeats in ms, one fresh cluster each (default: the node default). A
#                      queue operation costs about a heartbeat, so this is THE parameter; the
#                      election base is set to 5x. 300 is the deployed value.
#   EQB_REQUESTS_SINGLE / EQB_REQUESTS   requests per cell at c=1 / c>1 (default 200 / 480; use
#                      30 / 96 at a 300 ms heartbeat, where the queue serves ~3 per second)
#   EQB_ENGINE_ENV     extra "K=V ..." for the queue-mode engines only, e.g.
#                      ENCODER_QUEUE_MAX_IN_FLIGHT=64 to measure the queue unbounded, or
#                      ENCODER_LOCAL_MAX_IN_FLIGHT=2 to make A busy at a lower concurrency
#
# Runs under the repository's lock (it binds ports): flock /home/muyiwa/.cache/lanes/test_ofc.lock <this>.
set -uo pipefail
NODE_BIN="${1:?usage: $0 <sgee_queue_node> <calculator_engine> <out-dir>}"
ENGINE_BIN="${2:?}"; OUT="${3:?}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROUNDS="${EQB_ROUNDS:-3}"
SURFACES="${EQB_SURFACES:-mortgage strategy}"
HEARTBEATS="${EQB_HEARTBEATS:-0}"
N_SINGLE="${EQB_REQUESTS_SINGLE:-200}"
N_MANY="${EQB_REQUESTS:-480}"
WARMUP=3
mkdir -p "$OUT"
: > "$OUT/samples.jsonl"

# In-process answers in one cell: <arm> <label> <count before> <requests ANSWERED, warm-up included>.
# Arms A and Q are READ from the engine log, which carries one line per request. Arm L runs
# INFERENCE_QUEUE=local, where there is no queue to route around and no per-request line is written
# (nothing to account for), so its count is BY CONSTRUCTION every request answered -- the probe's own
# `answered`, which leaves out a request that errored. Reading its log would report 0 in-process for an
# engine that answered all of them in-process.
in_process_in_cell() {
  if [ "$1" = L ]; then echo "$4"; else echo $(( $(eqc_inprocess "$1" "$2") - $3 )); fi
}

for hb in $HEARTBEATS; do
  (
    if [ "$hb" != 0 ]; then export EQC_NODE_ENV="SGEE_HEARTBEAT_MS=$hb SGEE_ELECTION_TIMEOUT_MS=$(( hb * 5 ))"; fi
    # shellcheck source=/dev/null
    . "$REPO/backend/tests/integration/lib/encoder_cluster.sh"
    eqc_init || exit $?
    eqc_start_cluster || exit 1
    eqc_start_engine L "$PORT_L" local || exit 1
    # shellcheck disable=SC2086
    eqc_start_engine A "$PORT_A" sgee ${EQB_ENGINE_ENV:-} || exit 1
    # shellcheck disable=SC2086
    eqc_start_engine B "$PORT_B" sgee ${EQB_ENGINE_ENV:-} || exit 1
    PORT_Q="$PORT_S"
    # shellcheck disable=SC2086
    eqc_start_engine Q "$PORT_Q" sgee ${EQB_ENGINE_ENV:-} ENCODER_LOCAL_MAX_IN_FLIGHT=0 || exit 1
    sleep 1

    # What the polling costs when nobody is asking: CPU of the nodes and of engines A + B over an
    # idle window, as a percentage of one core. The idle tick is the only cost of a short poll
    # interval, so it has to be a number rather than a worry.
    cpu_jiffies() { awk '{print $14+$15}' "/proc/$1/stat" 2>/dev/null || echo 0; }
    sum_jiffies() { local t=0 p; for p in "$@"; do t=$(( t + $(cpu_jiffies "$p") )); done; echo "$t"; }
    node_pids=$(qn_self_pids | tr '\n' ' ')
    idle_s=10; hz=$(getconf CLK_TCK)
    n0=$(sum_jiffies $node_pids); e0=$(sum_jiffies "${ENGINE_PIDS[1]}" "${ENGINE_PIDS[2]}")
    sleep "$idle_s"
    n1=$(sum_jiffies $node_pids); e1=$(sum_jiffies "${ENGINE_PIDS[1]}" "${ENGINE_PIDS[2]}")
    printf 'heartbeat %s ms: idle CPU over %ss (percent of one core): 3 queue nodes %.2f, engines A+B %.2f\n' "$hb" "$idle_s" \
      "$(echo "($n1-$n0)*100/($hz*$idle_s)" | bc -l)" "$(echo "($e1-$e0)*100/($hz*$idle_s)" | bc -l)"

    for surface in $SURFACES; do
      for round in $(seq 1 "$ROUNDS"); do
        for c in 1 8 24; do
          n=$(( c == 1 ? N_SINGLE : N_MANY ))
          for arm in L A Q; do
            port="PORT_$arm"; label="$surface encoder"
            s0="$(eqc_queued "$arm" "$label")"; i0="$(eqc_inprocess "$arm" "$label")"
            python3 -P "$PROBE" latency --target "127.0.0.1:${!port}" --surface "$surface" -c "$c" -n "$n" --warmup "$WARMUP" \
              > "$OUT/cell.json"
            # The log counts are read AFTER the cell has finished: expanded inside the pipeline that runs
            # the probe they would be read before it, and every cell would report zero spills.
            python3 -P -c 'import json,sys; d=json.load(open(sys.argv[1])); d["arm"],d["round"],d["heartbeat_ms"],d["fallbacks"],d["spilled"],d["in_process"]=sys.argv[2],int(sys.argv[3]),int(sys.argv[4]),int(sys.argv[5]),int(sys.argv[6]),int(sys.argv[7]); print(json.dumps(d))' \
                "$OUT/cell.json" "$arm" "$round" "$hb" "$(eqc_fallbacks "$arm")" "$(( $(eqc_queued "$arm" "$label") - s0 ))" "$(in_process_in_cell "$arm" "$label" "$i0" "$(python3 -P -c 'import json,sys; print(json.load(open(sys.argv[1]))["answered"])' "$OUT/cell.json")")" >> "$OUT/samples.jsonl"
          done
        done
      done
    done
    echo "executed per engine: A mortgage=$(eqc_executed A 'mortgage encoder') B mortgage=$(eqc_executed B 'mortgage encoder') Q mortgage=$(eqc_executed Q 'mortgage encoder') A strategy=$(eqc_executed A 'strategy encoder') B strategy=$(eqc_executed B 'strategy encoder') Q strategy=$(eqc_executed Q 'strategy encoder')"
  )
done

python3 -P - "$OUT/samples.jsonl" <<'PY'
import json, statistics, sys, collections
rows = [json.loads(l) for l in open(sys.argv[1])]
g = collections.defaultdict(list)
for r in rows:
    g[(r['heartbeat_ms'], r['surface'], r['concurrency'], r['arm'])].append(r)
names = {'L': 'L local', 'A': 'A default', 'Q': 'Q queue-1st'}
print(f"{'hb ms':>6} {'surface':9} {'c':>3} {'arm':12} {'p50 ms':>9} {'p95 ms':>9} {'max ms':>9} {'rps':>8} {'errors':>6} {'spilled':>8} {'in-proc':>8}   per-round p50/p95")
for k in sorted(g):
    rs = g[k]
    p50 = statistics.median(r['p50_ms'] for r in rs); p95 = statistics.median(r['p95_ms'] for r in rs)
    rps = statistics.median(r['throughput_rps'] for r in rs); err = sum(r['errors'] for r in rs)
    per = ' '.join(f"{r['p50_ms']}/{r['p95_ms']}" for r in rs)
    sp = statistics.median(r.get('spilled', 0) for r in rs); ip = statistics.median(r.get('in_process', 0) for r in rs)
    mx = statistics.median(r['max_ms'] for r in rs)
    print(f"{k[0]:>6} {k[1]:9} {k[2]:>3} {names[k[3]]:12} {p50:9.2f} {p95:9.2f} {mx:9.2f} {rps:8.1f} {err:6d} {sp:8.0f} {ip:8.0f}   {per}")
PY
