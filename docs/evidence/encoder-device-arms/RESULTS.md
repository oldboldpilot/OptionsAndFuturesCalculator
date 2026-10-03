# The encoder's DEVICE arms, built and run on real GPUs, 2026-10-03

Host: multi-GPU server -- RTX PRO 6000 Blackwell Workstation (97,887 MiB) + RTX 5090
      nvcc 13.4, clang++-23 (/usr/lib/llvm-23/bin/clang++), ENABLE_CUDA=ON
Tree: isolated worktree at sensen f4d5749f, 0 dirty, SENSEN_USE_SGEE=OFF, SENSEN_TBQWF=OFF
      [PASS] policy_float_types_no_intrinsics

## FIRST TIME THESE HAVE EVER BEEN LINKED OR EXECUTED
test_encoder_dispatch       107 passed, 0 failed
test_text_encoder_device     25 passed, 1 FAILED   (real exit code 1)

By arm:
  CUDA   12 checks, ALL PASS  (3 CUDA + 5 CUDA BF16 + 4 CUDA F32)
  Triton 11 of 12

## THE ONE FAILURE IS A TOLERANCE, NOT A DECISION
  [FAIL] Triton F32: the device trunk agrees with the CPU trunk to 1e-03  (3.060e-03)
         worst |device - CPU| over 8 utterances 3.060e-03 (bound 1e-03)
         argmax decisions scored 35, identical 35, inside the noise band and not scored 1

3.06e-3 is the figure CLAUDE.md ALREADY RECORDS as an open item, so it is pre-existing
and not introduced here. Every argmax decision is the CPU's, so no answer changes.

The BF16 arm passes comfortably and its own numbers show why the F32 one is suspicious:
  [PASS] Triton BF16: agrees to 8e-02  (1.630e-02); 34 of 34 argmax decisions identical
  Triton BF16: |device - F32 cell on bf16 weights| 1.993e-02; the CPU bf16 cell's own
               distance from it 1.644e-02

3e-3 on an F32 path against 1.6e-2 on a bf16 one is the signature of TF32 tensor cores.
This project already records the CPU analogue -- 'GEMM::PrecisionScope and
SENSEN_PRECISION_MODE=FP32 DO NOT GIVE FP32' on an AMX host, fp32 below 64 rows and
bf16-level error at 64 and above. NOT CONFIRMED: the Triton trunk goes through
src/cuda/triton_worker_bridge.h rather than a Python kernel that could be read for an
allow_tf32 setting, so this is a hypothesis with a mechanism, not a finding.

## WHAT BLOCKED THIS, AND THE THREE WRONG ANSWERS FIRST
A CUDA build of the branch failed on src/qwen38.cppm -- a file neither the encoder nor
the device arms touch:
    avx512fintrin.h:310: definition with same mangled name '_ZL17_mm512_set1_epi32i'

Eliminated by crossing ONE variable each:
  - SENSEN_TBQWF AUTO vs OFF, the only CMakeCache difference against a tree that
    builds: identical failure, 3 seconds.
  - a stale object: the working tree's .o was built the SAME DAY, AFTER its source, by
    the same clang 23.1.2 (recorded in the object), and ninja -n reports it current.
  - the encoder modules themselves: removing encoder_dispatch.cppm and text_encoder.cppm
    from the module list reproduces the failure exactly.

The cause: the branch sits 92 commits behind a master that fixed it in 5c325105,
'no _mm* intrinsic in the interface, so importers compile under clang-23 CUDA' -- whose
gate script names src/qwen38.cppm under CUDA and this exact mangled name. Adopted byte
for byte as sensen f4d5749f.

## A REPORTING DEFECT OF MINE, recorded because it nearly shipped as a pass
The first run printed 'exit=0' with a failure present: $? after a pipe is the PIPE's
status, not the binary's. The real exit code is 1. A harness that cannot report a
failure is worse than no harness.

## STILL ABSENT: cuBLAS, cuTile, CUDA-graph
No implementation exists for any of the three. The dispatch surface reports them as gaps
with a stated reason, which is the honest answer and not coverage.
