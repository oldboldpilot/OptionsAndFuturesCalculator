#!/usr/bin/env bash
# What the shared queue ADDS to an encoder parse: p50/p95 at concurrency 1/8/24, local vs queue,
# optionally across Raft heartbeats.
#
# @author Olumuyiwa Oluwasanmi
#
#   encoder_queue_bench.sh <sgee_queue_node> <calculator_engine> <out-dir>
#
# Boots a three-node SGEE cluster and three engines holding the real encoder weights -- L
# (INFERENCE_QUEUE=local), A and B (INFERENCE_QUEUE=sgee) -- and drives the SAME request stream at L
# and at A, interleaved over several rounds, because this repository has already paid for a
# throughput claim made from one run (the spread across runs exceeded the effect).
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
#                      ENCODER_QUEUE_MAX_IN_FLIGHT=64 to measure the queue unbounded
#
# Runs under the shared lock (it binds ports): flock /home/muyiwa/.cache/lanes/test.lock <this>.
set -uo pipefail
NODE_BIN="${1:?usage: $0 <sgee_queue_node> <calculator_engine> <out-dir>}"
ENGINE_BIN="${2:?}"; OUT="${3:?}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROUNDS="${EQB_ROUNDS:-3}"
SURFACES="${EQB_SURFACES:-mortgage strategy}"
HEARTBEATS="${EQB_HEARTBEATS:-0}"
N_SINGLE="${EQB_REQUESTS_SINGLE:-200}"
N_MANY="${EQB_REQUESTS:-480}"
mkdir -p "$OUT"
: > "$OUT/samples.jsonl"

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
          for arm in L A; do
            port="PORT_$arm"
            python3 -P "$PROBE" latency --target "127.0.0.1:${!port}" --surface "$surface" -c "$c" -n "$n" --warmup 3 \
              | python3 -P -c 'import json,sys; d=json.loads(sys.stdin.read()); d["arm"],d["round"],d["heartbeat_ms"],d["fallbacks"]=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]),int(sys.argv[4]); print(json.dumps(d))' \
                "$arm" "$round" "$hb" "$(eqc_fallbacks A)" >> "$OUT/samples.jsonl"
          done
        done
      done
    done
    echo "executed per engine: A mortgage=$(eqc_executed A 'mortgage encoder') B mortgage=$(eqc_executed B 'mortgage encoder') A strategy=$(eqc_executed A 'strategy encoder') B strategy=$(eqc_executed B 'strategy encoder')"
  )
done

python3 -P - "$OUT/samples.jsonl" <<'PY'
import json, statistics, sys, collections
rows = [json.loads(l) for l in open(sys.argv[1])]
g = collections.defaultdict(list)
for r in rows:
    g[(r['heartbeat_ms'], r['surface'], r['concurrency'], r['arm'])].append(r)
print(f"{'hb ms':>6} {'surface':9} {'c':>3} {'arm':>4} {'p50 ms':>9} {'p95 ms':>9} {'rps':>8} {'errors':>6} {'A fallbacks so far':>19}   per-round p50 / p95")
for k in sorted(g):
    rs = g[k]
    p50 = statistics.median(r['p50_ms'] for r in rs); p95 = statistics.median(r['p95_ms'] for r in rs)
    rps = statistics.median(r['throughput_rps'] for r in rs); err = sum(r['errors'] for r in rs)
    per = ' '.join(f"{r['p50_ms']}/{r['p95_ms']}" for r in rs)
    print(f"{k[0]:>6} {k[1]:9} {k[2]:>3} {'L' if k[3]=='L' else 'A(q)':>4} {p50:9.2f} {p95:9.2f} {rps:8.1f} {err:6d} {rs[-1]['fallbacks']:>19}   {per}")
PY
