#!/usr/bin/env bash
# Score ONE strategy-assistant weight file through the real ParseStrategy RPC.
#
# The strategy surface refuses anonymously, so PRO_GATE_MODE=off is a precondition
# rather than a convenience. (A mortgage twin of this harness existed while the
# mortgage decoder was served from this engine; it left with that assistant.)
#
# WHAT THIS HARNESS CANNOT DO: eval_grpc.py
# has no --json-out and no --assert-disjoint-from, so there is no per-row record
# to pair on and no contamination check. A difference of totals is all this side
# can report until that is fixed, and a difference of totals is exactly what this
# repository has twice been wrong to trust.
#
# Usage: measure_strategy_arm.sh <label> <gguf> <port> <engine-copy> <outdir>
set -euo pipefail
label=$1 gguf=$2 port=$3 engine=$4 out=$5
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$out"
log=$out/$label.engine.log
sha=$(sha256sum "$gguf" | cut -d' ' -f1)
echo "$label sha256 $sha" | tee "$out/$label.sha"

extra=()
for kv in ${EXTRA_SET:-}; do extra+=(--set "$kv"); done

python3 "$root/scripts/run_with_env.py" \
  --set MODEL_PATH="$gguf" --set ENGINE_GRPC_PORT="$port" \
  --set INFERENCE_QUEUE=local --set DATABASE_URL= --set PRO_GATE_MODE=off --set QUOTA_POLICY= \
  "${extra[@]}" -- "$engine" >"$log" 2>&1 &
wrapper=$!
trap 'pkill -f -- "^$engine\$" 2>/dev/null || true; kill $wrapper 2>/dev/null || true' EXIT

for _ in $(seq 1 180); do
  grep -q 'Strategy assistant model is LOADED' "$log" && break
  sleep 1
done
grep -q 'Strategy assistant model is LOADED' "$log" || { echo "model did not load"; tail -20 "$log"; exit 1; }

n_bin=$(pgrep -fc -- "^$engine\$" || true)
n_listen=$(ss -ltnH "sport = :$port" | wc -l)
echo "one-engine: copies_running=$n_bin listeners_on_$port=$n_listen" | tee "$out/$label.oneengine"
[ "$n_bin" = 1 ] && [ "$n_listen" = 1 ] || { echo "ONE-ENGINE ASSERTION FAILED"; exit 1; }

# Requested variables asserted against what the PROCESS holds, not the command
# line: config/.env is applied after the inherited environment, and the engine
# does not log most of these, so a log grep is a check that cannot fail.
pid=$(pgrep -f -- "^$engine\$" | head -1)
for kv in MODEL_PATH="$gguf" PRO_GATE_MODE=off ${EXTRA_SET:-}; do
  tr '\0' '\n' < "/proc/$pid/environ" | grep -qxF -- "$kv" || {
    echo "ENV ASSERTION FAILED: $kv not in /proc/$pid/environ"; exit 1; }
done
echo "env-in-force: MODEL_PATH=$gguf PRO_GATE_MODE=off ${EXTRA_SET:-}" | tee "$out/$label.envinforce"

python3 "$root/agent/train/eval_grpc.py" --addr "localhost:$port" \
  --val "$root/agent/dataset/data/val.jsonl" ${EVAL_ARGS:-} --label "$label" \
  | tee "$out/$label.eval.txt"
