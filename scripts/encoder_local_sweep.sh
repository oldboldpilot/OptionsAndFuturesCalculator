#!/usr/bin/env bash
# How far does ONE replica answer an encoder request in about a millisecond? p50/p95 and throughput of
# the in-process chain against client concurrency, on a single engine with no queue anywhere.
#
# @author Olumuyiwa Oluwasanmi
#
#   encoder_local_sweep.sh <calculator_engine> <out.jsonl>
#
# This is the measurement behind `encoder_queue::kDefaultLocalMaxInFlight`, the point at which a
# replica counts as BUSY and starts spilling requests to the shared queue. The default is where
# throughput stops growing with concurrency; it is a property of the HOST, so re-run this on the
# machine the engine will serve from before trusting the number there, and set
# ENCODER_LOCAL_MAX_IN_FLIGHT if it differs.
#
# One engine (INFERENCE_QUEUE=local), concurrency 1 2 4 8 16 32 64, the strategy surface, three rounds
# interleaved -- the order of the cells changes between rounds so a slow minute on a shared host is
# not mistaken for a property of one concurrency. Prints a median table.
#
# Runs under the repository's lock (it binds a port):
#   flock /home/muyiwa/.cache/lanes/test_ofc.lock scripts/encoder_local_sweep.sh <engine> <out>
set -uo pipefail
ENGINE_BIN="${1:?usage: $0 <calculator_engine> <out.jsonl>}"
OUT="${2:?usage: $0 <calculator_engine> <out.jsonl>}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NODE_BIN=/bin/true   # no queue node is started; the shared library only needs a path
# shellcheck source=/dev/null
. "$REPO/backend/tests/integration/lib/encoder_cluster.sh"
eqc_init || exit $?
eqc_start_engine L "$PORT_L" local || exit 1
: > "$OUT"
for round in 1 2 3; do
  for surface in strategy; do
    if [ $(( round % 2 )) -eq 1 ]; then cs="1 2 4 8 16 32 64"; else cs="64 32 16 8 4 2 1"; fi
    for c in $cs; do
      n=$(( c < 8 ? 300 : 600 ))
      python3 -P "$PROBE" latency --target "127.0.0.1:$PORT_L" -c "$c" -n "$n" --warmup 5 \
        | python3 -P -c 'import json,sys; d=json.loads(sys.stdin.read()); d["round"]=int(sys.argv[1]); print(json.dumps(d))' "$round" >> "$OUT"
    done
  done
done
python3 -P - "$OUT" <<'PY'
import json, statistics, sys, collections
g = collections.defaultdict(list)
for l in open(sys.argv[1]):
    r = json.loads(l); g[(r['surface'], r['concurrency'])].append(r)
print(f"{'surface':9} {'c':>3} {'p50 ms':>8} {'p95 ms':>8} {'rps':>8} errors  per-round p50")
for k in sorted(g):
    rs = g[k]
    print(f"{k[0]:9} {k[1]:>3} {statistics.median(r['p50_ms'] for r in rs):8.2f} {statistics.median(r['p95_ms'] for r in rs):8.2f} "
          f"{statistics.median(r['throughput_rps'] for r in rs):8.1f} {sum(r['errors'] for r in rs):6d}  {' '.join(str(r['p50_ms']) for r in rs)}")
PY
