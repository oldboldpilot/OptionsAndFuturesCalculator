#!/usr/bin/env bash
# Paired, alternating decode-throughput comparison of the SAME Q8_0 weight file
# served dense and served from an LLQ image.
#
# Alternating and paired because this host is never idle: a per-arm run measured
# hours apart reports the load it happened to meet, not the arm. Each round
# exposes both arms to the same conditions, and the load average is recorded at
# every boundary so a round taken under a different load is visible rather than
# averaged in.
#
# A FRESH ENGINE PER ARM: the weight store is chosen once, before any model
# loads, because sensen reads SENSEN_QKV_FUSION into a static. There is no way
# to switch stores inside one process, and pretending otherwise would measure
# whichever store won the race.
#
# ONE BINARY FOR EVERY ARM, AND IT IS NAMED RATHER THAN TAKEN FROM THE BUILD
# DIRECTORY. Copying `backend/build/calculator_engine` at each arm reads whatever
# the tree happens to hold, so a concurrent rebuild silently splits a paired run
# across two engines -- measured: a 17:06 run picked up a 16:32 relink whose
# weight store refused the dense arm outright. Default to the build output for an
# ordinary run; pass <engine> to pin a staged copy.
#
# Usage: measure_llq_paired_throughput.sh <gguf> <outdir> [rounds] [n_rows] [engine]
set -euo pipefail
gguf=$1 out=$2 rounds=${3:-3} n=${4:-25}
root=$(cd "$(dirname "$0")/.." && pwd)
engine_src=${5:-$root/backend/build/calculator_engine}
mkdir -p "$out"
# Recorded, so the table says which binary produced it.
{ echo "engine: $engine_src"; sha256sum "$engine_src"; } | tee "$out/tp_engine.txt"
cp "$engine_src" /tmp/bw/engine-tp-dense
cp "$engine_src" /tmp/bw/engine-tp-llq
log=$out/tp_rounds.log
: >"$log"
for r in $(seq 1 "$rounds"); do
  echo "$(date +%T) round $r load $(cut -d' ' -f1-3 /proc/loadavg)" | tee -a "$log"
  for arm in dense llq; do
    port=$((50080 + r * 2 + $([ "$arm" = llq ] && echo 1 || echo 0)))
    # SENSEN_QKV_FUSION=0 ON BOTH ARMS, and that is the whole validity of the
    # comparison. An LLQ store requires fusion off, and
    # prepare_process_environment returns EARLY for Dense, so it never sets the
    # variable -- leaving the dense arm fused and the LLQ arm not. The two arms
    # then differ in two things at once and the table reads as an LLQ cost.
    # The tell is a COUNT, not a rate: byte-identical output with 170,072
    # against 198,744 front-door slice calls. A rate-only reading shows
    # "1.8x slower" and is believed.
    EXTRA_SET="MORTGAGE_WEIGHT_STORE=$arm SENSEN_QKV_FUSION=0" EVAL_ARGS="--n $n" \
      bash "$root/scripts/measure_bit_width_arm.sh" "tp_${arm}_r${r}" \
      "$gguf" "$port" "/tmp/bw/engine-tp-$arm" "$out" >"$out/tp_${arm}_r${r}.stdout" 2>&1
    python3 "$root/scripts/parse_engine_timing.py" "$out/tp_${arm}_r${r}.engine.log" \
      | tee "$out/tp_${arm}_r${r}.timing.txt"
    # The measured flag, not the requested one: main.cpp prints this
    # unconditionally, before any model loads, so it names the store the process
    # actually installed. A run whose store fell back would otherwise be quoted
    # as an LLQ number. Asserted, not merely recorded -- both arms are checked,
    # so the DENSE arm cannot silently have been LLQ either.
    got=$(grep -m1 -o 'MORTGAGE_WEIGHT_STORE=[a-z]*' "$out/tp_${arm}_r${r}.engine.log")
    echo "store-in-force: $got" | tee -a "$out/tp_${arm}_r${r}.timing.txt"
    [ "$got" = "MORTGAGE_WEIGHT_STORE=$arm" ] || { echo "STORE ASSERTION FAILED: wanted $arm"; exit 1; }
    # The fusion setting is asserted by measure_bit_width_arm.sh against
    # /proc/<pid>/environ, which is the only place that can say it is IN FORCE
    # rather than requested; it writes <label>.envinforce and exits non-zero
    # otherwise, so reaching here means both arms agreed.
    cat "$out/tp_${arm}_r${r}.envinforce" >> "$out/tp_${arm}_r${r}.timing.txt"
    echo "$(date +%T) round $r $arm end load $(cut -d' ' -f1-3 /proc/loadavg)" | tee -a "$log"
  done
done
