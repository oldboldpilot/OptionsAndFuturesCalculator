#!/usr/bin/env bash
# Score ONE mortgage-assistant weight file: accuracy through the real
# ParseOperation RPC plus decode throughput from the engine's own timing lines.
#
# Optional env: EXTRA_SET=K=V (one more engine variable), EVAL_ARGS="--n 120".
# Usage: measure_bit_width_arm.sh <label> <gguf> <port> <engine-binary-copy> <outdir>
#
# Runs a PRIVATE copy of the engine on <port> (ENGINE_GRPC_PORT, never PORT),
# loaded through scripts/run_with_env.py so the bare-JSON keys in config/.env
# survive. INFERENCE_QUEUE=local and an empty DATABASE_URL so nothing touches
# the production Postgres the ambient config points at. QUOTA_POLICY is emptied
# (quotas DISABLED): the anonymous compute budget otherwise refuses ~95% of a
# 600-row sweep with RESOURCE_EXHAUSTED, which the harness counts as "errors".
set -euo pipefail
label=$1 gguf=$2 port=$3 engine=$4 out=$5
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$out"
log=$out/$label.engine.log
sha=$(sha256sum "$gguf" | cut -d' ' -f1)
echo "$label sha256 $sha" | tee "$out/$label.sha"

python3 "$root/scripts/run_with_env.py" \
  --set MORTGAGE_MODEL_PATH="$gguf" --set ENGINE_GRPC_PORT="$port" \
  --set INFERENCE_QUEUE=local --set DATABASE_URL= --set PRO_GATE_MODE=off --set QUOTA_POLICY= ${EXTRA_SET:+--set $EXTRA_SET} \
  -- "$engine" >"$log" 2>&1 &
wrapper=$!
trap 'pkill -f -- "^$engine\$" 2>/dev/null || true; kill $wrapper 2>/dev/null || true' EXIT

for _ in $(seq 1 120); do
  grep -q 'Mortgage assistant model is LOADED' "$log" && break
  sleep 1
done
grep -q 'Mortgage assistant model is LOADED' "$log" || { echo "model did not load"; tail -20 "$log"; exit 1; }

# One-engine assertion: exactly one process has THIS binary copy, and exactly
# one listener on the port (SO_REUSEPORT would hide a second).
n_bin=$(pgrep -fc -- "^$engine\$" || true)
n_listen=$(ss -ltnH "sport = :$port" | wc -l)
echo "one-engine: copies_running=$n_bin listeners_on_$port=$n_listen" | tee "$out/$label.oneengine"
[ "$n_bin" = 1 ] && [ "$n_listen" = 1 ] || { echo "ONE-ENGINE ASSERTION FAILED"; exit 1; }

python3 "$root/agent/train/eval_grpc_mortgage.py" --addr "localhost:$port" \
  --val "$root/agent/dataset/data_mortgage/val.jsonl" --engine-log "$log" \
  ${EVAL_ARGS:-} --label "$label" --json-out "$out/$label.json" \
  --assert-disjoint-from "$root/agent/dataset/data_mortgage/train.jsonl" \
  | tee "$out/$label.eval.txt"
