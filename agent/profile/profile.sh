#!/usr/bin/env bash
#
# Profiles the fine-tune with Nsight Systems and Nsight Compute, then reports
# the handful of numbers that actually decide whether the recipe is optimal.
#
# The two tools answer different questions and the order matters. nsys is a
# TIMELINE: it shows where wall-clock goes -- dataloader stalls, host-device
# copies, gaps between kernels, whether the GPU is idle waiting on Python. ncu
# is a KERNEL microscope: for one kernel it gives occupancy, memory throughput
# and whether it is compute- or bandwidth-bound.
#
# Run nsys first, always. Tuning a kernel that occupies 3% of the timeline is
# effort spent where the answer cannot be, and at 0.6B with short sequences the
# usual finding is that the GPU is starved rather than slow -- a data pipeline
# problem no kernel tuning can reach.
#
# ncu serializes and replays kernels, so it is 10-100x slower and is pointed at
# a few steps only. Never profile a full run with it.
set -uo pipefail

SCRATCH=${SCRATCH:-/scratch}
OUT=${OUT:-$SCRATCH/agents/profiles}
DATA=${DATA:-$(dirname "$0")/../dataset/data}
TRAIN=$(dirname "$0")/../train/train.py
STEPS=${STEPS:-40}

mkdir -p "$OUT"
cd "$(dirname "$0")"

echo "=== environment ==="
nvidia-smi --query-gpu=index,name,memory.total,persistence_mode,clocks.max.sm --format=csv
python3 -c "import torch;print('torch',torch.__version__,'cuda',torch.version.cuda,'gpus',torch.cuda.device_count())"
nsys --version 2>/dev/null | head -1 || echo "nsys NOT INSTALLED"
ncu --version 2>/dev/null | head -2 | tail -1 || echo "ncu NOT INSTALLED"

# ---------------------------------------------------------------------------
# 1. Baseline. Without a number to beat, every later change is a claim.
# ---------------------------------------------------------------------------
echo
echo "=== baseline: $STEPS steps, unprofiled ==="
python3 "$TRAIN" --data "$DATA" --out "$SCRATCH/agents/_bench" \
  --max-steps "$STEPS" 2>&1 | tail -4

# ---------------------------------------------------------------------------
# 2. nsys — where the wall-clock goes.
#
# --trace=cuda,nvtx,osrt,cudnn,cublas: osrt is what exposes a dataloader
# blocking on a file read or a lock, which is invisible with cuda tracing alone
# and is the most common cause of a starved GPU.
# --cuda-memory-usage=true costs a little overhead and is worth it: it shows
# allocator churn, which at short sequence lengths often dominates.
# ---------------------------------------------------------------------------
echo
echo "=== nsys: timeline ==="
nsys profile \
  --trace=cuda,nvtx,osrt,cudnn,cublas \
  --cuda-memory-usage=true \
  --sample=cpu \
  --force-overwrite=true \
  --output="$OUT/train_timeline" \
  python3 "$TRAIN" --data "$DATA" --out "$SCRATCH/agents/_bench" \
    --max-steps "$STEPS" --profile 2>&1 | tail -6

echo
echo "--- GPU kernel time, top 12 ---"
nsys stats --report cuda_gpu_kern_sum --format table "$OUT/train_timeline.nsys-rep" 2>/dev/null | head -20

echo
echo "--- host-device transfers (a busy column here means the input pipeline) ---"
nsys stats --report cuda_gpu_mem_time_sum --format table "$OUT/train_timeline.nsys-rep" 2>/dev/null | head -12

echo
echo "--- CUDA API blocking calls (cudaMemcpy/cudaStreamSynchronize = stalls) ---"
nsys stats --report cuda_api_sum --format table "$OUT/train_timeline.nsys-rep" 2>/dev/null | head -14

# GPU utilisation is the headline. Below ~85% on a 0.6B model with packing on
# means the answer is in the data path, not in the kernels.
echo
echo "--- utilisation ---"
nsys stats --report cuda_gpu_sum --format table "$OUT/train_timeline.nsys-rep" 2>/dev/null | head -10

# ---------------------------------------------------------------------------
# 3. ncu — the kernels nsys says are worth looking at.
#
# Scoped hard: --launch-skip past warmup and compilation, --launch-count small.
# The metrics chosen are the ones that decide the next action:
#   sm__throughput          how busy the SMs are
#   dram__throughput        how busy memory is
#   achieved_occupancy      whether there is enough parallelism to hide latency
#   smsp__..._tensor_op     whether tensor cores are being used at all
# A kernel high on DRAM and low on SM is bandwidth-bound: fuse it or raise
# arithmetic intensity. High SM, low DRAM is compute-bound: a bigger batch will
# not help. Both low means it is latency-bound and starved.
# ---------------------------------------------------------------------------
echo
echo "=== ncu: kernel detail (slow — a few launches only) ==="
ncu \
  --target-processes all \
  --launch-skip 200 \
  --launch-count 25 \
  --metrics \
sm__throughput.avg.pct_of_peak_sustained_elapsed,\
dram__throughput.avg.pct_of_peak_sustained_elapsed,\
sm__warps_active.avg.pct_of_peak_sustained_active,\
smsp__sass_thread_inst_executed_op_hfma_pred_on.sum,\
smsp__inst_executed_pipe_tensor.sum,\
launch__occupancy_limit_registers \
  --csv --log-file "$OUT/ncu_kernels.csv" \
  python3 "$TRAIN" --data "$DATA" --out "$SCRATCH/agents/_bench" \
    --max-steps 3 --profile 2>&1 | tail -4

echo
echo "--- ncu summary ---"
python3 - "$OUT/ncu_kernels.csv" <<'PY'
import csv, sys, collections
path = sys.argv[1]
try:
    rows = list(csv.DictReader(l for l in open(path) if not l.startswith('=')))
except FileNotFoundError:
    print("  no ncu output (did ncu need --privileged / CAP_SYS_ADMIN?)"); sys.exit()
agg = collections.defaultdict(dict)
for r in rows:
    k = r.get('Kernel Name', '')[:60]
    m, v = r.get('Metric Name'), r.get('Metric Value')
    if not k or not m: continue
    try: agg[k][m] = float(str(v).replace(',', ''))
    except ValueError: pass
print(f"  {'kernel':60} {'SM%':>7} {'DRAM%':>7} {'occ%':>7}  verdict")
for k, m in list(agg.items())[:15]:
    sm = m.get('sm__throughput.avg.pct_of_peak_sustained_elapsed', 0)
    dr = m.get('dram__throughput.avg.pct_of_peak_sustained_elapsed', 0)
    oc = m.get('sm__warps_active.avg.pct_of_peak_sustained_active', 0)
    verdict = ('bandwidth-bound' if dr > 60 and sm < 40 else
               'compute-bound'   if sm > 60 else
               'latency-bound / starved' if sm < 25 and dr < 25 else 'mixed')
    print(f"  {k:60} {sm:7.1f} {dr:7.1f} {oc:7.1f}  {verdict}")
PY

echo
echo "artifacts in $OUT"
echo "  train_timeline.nsys-rep   open in Nsight Systems UI"
echo "  ncu_kernels.csv           per-kernel metrics"
