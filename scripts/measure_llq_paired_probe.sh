#!/usr/bin/env bash
# Paired, alternating comparison of ONE weight file served dense and served from
# an LLQ image, driven through `llq_throughput_probe`.
#
# WHY THE PROBE AND NOT AN ENGINE-DRIVEN RPC SWEEP. The Q8_0 tier was measured by
# driving the real engine through the mortgage assistant's RPC harness (since removed
# with that assistant, 2026-10), the stronger instrument. It cannot be used at 16 bits: sensen's 16-bit weight
# path decodes through `resolveDotKernel` with block_values == 1, i.e. one
# function-pointer call PER ELEMENT, so a bf16 model decodes at single-digit
# tok/s and a 100-row RPC sweep does not finish in a usable time. The probe
# measures the same two things that matter here -- an aggregate token sha256 over
# every generated token, and decode/prefill rates from the engine's own timing --
# in one process per arm, and it FAILS (exit 3) unless the kernel counters say an
# image answered every slice. Use the RPC harness for Q8_0; use this at 16 bits.
#
# ALTERNATING AND PAIRED because this host is never idle: a per-arm run measured
# minutes apart reports the load it happened to meet, not the arm. The load
# average is recorded at every boundary so a round taken under a different load is
# visible rather than averaged in.
#
# A FRESH PROCESS PER ARM: the weight store is chosen before any model loads,
# because sensen reads SENSEN_QKV_FUSION into a static. There is no way to switch
# stores inside one process.
#
# SENSEN_QKV_FUSION=0 IS EXPORTED FOR BOTH ARMS, AND THE FIRST RUN OF THIS SCRIPT
# WAS INVALID WITHOUT IT. An LLQ store REQUIRES fusion off (with it on, Q, K and V
# are copied into a second buffer no source is registered for), and
# `prepare_process_environment` sets it for an LLQ mode -- but it returns EARLY for
# Dense, so the dense arm ran with fusion ON by default. That is a different amount
# of work, not a different weight store: fusion computes Q+K+V in one matvec, and
# the measured front-door call counts gave it away at 170,072 against the LLQ arm's
# 198,744 on byte-identical output. A dense arm with fusion on is the arm this
# repository's own LLQ notes already say is not comparable; exporting the variable
# here is what makes the two columns the same experiment with one variable changed.
#
# THREE ARMS, not two: dense, llq and llq-fused. `llq` materialises the image back
# into the dense layout and runs the ordinary kernel, so it does strictly more work
# for the same bytes read and can only be slower -- it is the identity reference,
# not the tier to serve. Only `llq-fused` consumes the packed image directly, so it
# is the only arm where fewer bytes can become fewer bytes READ. Quoting a
# materialise arm as "LLQ throughput" measures the wrong thing.
#
# Usage: measure_llq_paired_probe.sh <gguf> <outdir> [rounds] [prompts] [tokens] [threads] [arms]
set -euo pipefail
export SENSEN_QKV_FUSION=0
gguf=$1 out=$2 rounds=${3:-3} prompts=${4:-6} tokens=${5:-16} threads=${6:-8}
arms=${7:-"dense llq llq-fused"}
root=$(cd "$(dirname "$0")/.." && pwd)
probe=$root/backend/build/llq_throughput_probe
mkdir -p "$out"
log=$out/paired_rounds.log
: >"$log"

[ -x "$probe" ] || { echo "no llq_throughput_probe at $probe -- build it first"; exit 1; }

for r in $(seq 1 "$rounds"); do
  echo "$(date +%T) round $r start load $(cut -d' ' -f1-3 /proc/loadavg)" | tee -a "$log"
  for arm in $arms; do
    f=$out/probe_${arm}_r${r}
    echo "$(date +%T) round $r $arm begin load $(cut -d' ' -f1-3 /proc/loadavg)" | tee -a "$log"
    # The probe exits non-zero when an LLQ arm did not actually measure LLQ, so
    # `set -e` is the assertion: a run whose slices were answered by the dense
    # kernel must not reach the table at all.
    # --count-coverage on EVERY arm, so the dense arm carries a positive control:
    # dense_calls > 0 proves the dense kernel really ran there. Without it the
    # dense arm reports 0/0, which is indistinguishable from counting being off.
    "$probe" "$gguf" --store "$arm" --threads "$threads" --prompts "$prompts" \
      --timed "$prompts" --tokens "$tokens" --reps 1 --count-coverage \
      --label "${arm}_r${r}" --val "$root/agent/dataset/data_mortgage/val.jsonl" \
      >"$f.stdout" 2>"$f.stderr"
    grep -m1 '^RESULT ' "$f.stdout" >"$f.json"
    echo "$(date +%T) round $r $arm end   load $(cut -d' ' -f1-3 /proc/loadavg)" | tee -a "$log"
    # The MEASURED store, not the requested one. `llq_used` is derived from the
    # kernel counters inside the probe, so asserting it here is asserting that an
    # image answered every slice -- and asserting it on the DENSE arm too, which
    # is what stops a "dense" row having quietly been LLQ.
    python3 - "$f.json" "$arm" <<'PY'
import json, sys
row = json.loads(open(sys.argv[1]).read()[len("RESULT "):])
arm = sys.argv[2]
assert row["store"] == arm, f'store {row["store"]!r} != {arm!r}'
assert row["llq_used"] is (arm != "dense"), f'llq_used {row["llq_used"]} wrong for {arm}'
# Both arms must agree on fusion or they are not the same experiment. The probe
# reports what the process actually installed, so this is measured, not assumed.
assert row["qkv_fusion"] is False, f'qkv_fusion {row["qkv_fusion"]} -- arms are not comparable'
if arm == "dense":
    assert row["coverage_source_calls"] == 0, "the dense arm was answered from an image"
    assert row["coverage_dense_calls"] > 0, "the dense arm counted no slice at all"
else:
    assert row["coverage_dense_calls"] == 0, "an LLQ arm ran the dense kernel"
    assert row["coverage_source_calls"] > 0, "no slice was answered from an image"
print(f'{arm} r: store={row["store"]} llq_used={row["llq_used"]} '
      f'decode={row["decode_tok_s"]:.2f} prefill={row["prefill_tok_s"]:.1f} '
      f'sha={row["token_sha"][:16]} src_calls={row["coverage_source_calls"]} '
      f'dense_calls={row["coverage_dense_calls"]}')
PY
  done
done

echo
echo "=== paired table ==="
python3 "$root/scripts/llq_paired_summary.py" "$out" "$rounds" "$arms"
