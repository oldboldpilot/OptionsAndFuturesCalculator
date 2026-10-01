#!/usr/bin/env bash
# Mutation arms for the 16-bit (bf16) LLQ weight-store tier.
#
# Each arm removes ONE thing the tier claims to rely on, rebuilds, and requires
# `test_llq_weight_store` to FAIL. An arm that still passes means the check is not
# proving what it claims, so a passing arm is reported as a FAILURE of the arm.
#
# CCACHE_DISABLE=1 IS REQUIRED and is not a precaution. This build tree sets
# CMAKE_CXX_COMPILER_LAUNCHER=ccache, and ccache hashes a translation unit's own
# source text, never the BMIs it imports. Functions defined in a `.cppm` are
# inlined into consumers, so editing a module and rebuilding hands an unchanged
# consumer a cache HIT carrying the OLD inlined body -- this repository has
# already had three mutation runs "pass" against a deleted guard that way.
#
# RESTORE IS ON A TRAP, because an arm that hangs or is interrupted must not leave
# the tree mutated: a mutated tree that then gets committed is worse than a missing
# arm. Every file is restored from a pristine copy and its sha256 re-checked.
#
# WHICH ARMS NEED TO RUN, AND WHY THREE OF THE FOUR DO NOT. An arm earns its cost
# only where no continuous gate covers the property. Re-triaged 2026-09-30:
#
#   2 exponent-centre dropped  COVERED by the bit-identity gate in
#   3 tile written row-major   test_llq_weight_store: both make every joined word
#                              move, and the gate runs matvecQuantizedBatch with
#                              the dense buffer POISONED, so it fails loudly.
#   4 front-door ignores the   COVERED TWICE: by the same identity gate (the
#     registry                 poisoned buffer answers), and by the kernel
#                              counters -- source_calls and dense_calls both go to
#                              zero, llq_used is false, and the probe exits 3.
#                              That is an assertion on every run, not a one-off.
#
#   1 F16 adopted as bf16      NOT COVERED BY ANY IDENTITY GATE, and it is the
#                              dangerous one. An F16 word handed to
#                              LlqBf16Matrix::fromBits encodes WITHOUT ERROR and
#                              round-trips EXACTLY -- the codec splits bits 7..14
#                              and rejoins them whatever they mean -- so a wrongly
#                              adopted F16 tensor is bit-identical through the
#                              codec AND through the kernel, and every identity
#                              gate PASSES on it. The only thing between that and
#                              a model served on a field split the format does not
#                              have is the qtype check at adoption. RUN THIS ONE.
#
# Default is arm 1 for that reason. Pass arm numbers to run others:
#   llq_bf16_mutation_arms.sh          -> arm 1
#   llq_bf16_mutation_arms.sh 1 2 3 4  -> all four
set -uo pipefail
ARMS=("$@"); [ ${#ARMS[@]} -eq 0 ] && ARMS=(1)
want() { for a in "${ARMS[@]}"; do [ "$a" = "$1" ] && return 0; done; return 1; }
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

STORE=backend/src/modules/llq_weight_store.cppm
GEMM=backend/sensen/src/gemm_kernels.cppm
PROBE=backend/src/llq_throughput_probe.cpp
FILES=("$STORE" "$GEMM" "$PROBE")

stash=$(mktemp -d)
for f in "${FILES[@]}"; do cp "$f" "$stash/$(basename "$f")"; done
before=$(sha256sum "${FILES[@]}")

restore() {
  for f in "${FILES[@]}"; do cp "$stash/$(basename "$f")" "$f"; done
  local after
  after=$(sha256sum "${FILES[@]}")
  if [ "$before" = "$after" ]; then
    echo "RESTORED: all files byte-identical to pristine"
  else
    echo "RESTORE MISMATCH -- the tree is NOT pristine:"; diff <(echo "$before") <(echo "$after")
  fi
  rm -rf "$stash"
  # RESTORING THE SOURCE IS NOT ENOUGH: the binaries on disk are still the LAST
  # ARM's. Anything measured afterwards -- a throughput run, an engine boot --
  # would be measuring a deliberately broken build from a pristine tree, which is
  # the stale-binary trap with the evidence already cleaned up. Rebuild here, with
  # ccache off for the reason in this file's header.
  echo "rebuilding pristine binaries (ccache off)"
  CCACHE_DISABLE=1 ninja -C backend/build calculator_engine llq_throughput_probe \
    test_llq_weight_store >/tmp/arm_restore_build.log 2>&1 \
    && echo "REBUILT: binaries match the restored tree" \
    || { echo "*** REBUILD FAILED -- binaries do NOT match the tree ***"; tail -5 /tmp/arm_restore_build.log; }
}
trap restore EXIT

# mutate <file> <python-expression-file> -- applies an exact substitution.
apply() { # apply <file> <old> <new>
  python3 - "$1" "$2" "$3" <<'PY'
import sys
p, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(p).read()
n = s.count(old)
if n != 1:
    print(f"MUTATION DID NOT APPLY: {n} occurrences in {p}", file=sys.stderr)
    sys.exit(1)
open(p, "w").write(s.replace(old, new))
PY
}

arm() { # arm <name> <expected symptom>
  echo
  echo "================ ARM: $1"
  echo "expected symptom: $2"
  if ! CCACHE_DISABLE=1 ninja -C backend/build test_llq_weight_store >/tmp/arm_build.log 2>&1; then
    echo "ARM RESULT: build FAILED (that is a valid symptom for a type-level guard)"
    grep -m3 "error:" /tmp/arm_build.log || true
    return 0
  fi
  if ./backend/build/test_llq_weight_store >/tmp/arm_test.log 2>&1; then
    echo "ARM RESULT: *** TEST STILL PASSED -- THE CHECK DOES NOT PROVE WHAT IT CLAIMS ***"
    tail -3 /tmp/arm_test.log
    return 1
  fi
  echo "ARM RESULT: test FAILED as required"
  grep -c "FAIL:" /tmp/arm_test.log | sed 's/^/  failing checks: /'
  grep -m6 "FAIL:" /tmp/arm_test.log
  tail -2 /tmp/arm_test.log
  return 0
}

rc=0

if want 1; then
# ── Arm 1: F16 is adopted as bf16 ────────────────────────────────────────────
# The tier's sharpest claim: an F16 word read with bf16 field widths encodes
# WITHOUT ERROR and describes nothing. Let F16 through and the F16 section must
# stop reporting it left-dense.
apply "$STORE" \
  'if (qtype != sensen::GEMM::QType::Q8_0 && qtype != sensen::GEMM::QType::BF16) {' \
  'if (qtype != sensen::GEMM::QType::Q8_0 && qtype != sensen::GEMM::QType::BF16 && qtype != sensen::GEMM::QType::F16) {' || rc=1
apply "$STORE" \
  'if (qtype == sensen::GEMM::QType::BF16) {
                adopt_bf16(src_row_major, dense_key, n_rows, k);' \
  'if (qtype == sensen::GEMM::QType::BF16 || qtype == sensen::GEMM::QType::F16) {
                adopt_bf16(src_row_major, dense_key, n_rows, k);' || rc=1
arm "F16 adopted as bf16" \
    "an F16 weight is counted left-dense / an LLQ store REFUSES an F16 model" || rc=1
for f in "${FILES[@]}"; do cp "$stash/$(basename "$f")" "$f"; done
fi

if want 2; then
# ── Arm 2: the exponent centre is dropped on the rejoin ──────────────────────
# LlqBf16Matrix stores exponents as signed codes relative to a per-panel centre.
# Forget the centre and the words are wrong by a power of two.
apply "$STORE" \
  'sm[c], static_cast<std::uint8_t>(static_cast<std::int32_t>(codes[c]) + centre));' \
  'sm[c], static_cast<std::uint8_t>(static_cast<std::int32_t>(codes[c])));' || rc=1
arm "exponent centre dropped" \
    "bf16 rows [..) are bit-identical to the dense buffer / LLQ output is bit-identical" || rc=1
for f in "${FILES[@]}"; do cp "$stash/$(basename "$f")" "$f"; done
fi

if want 3; then
# ── Arm 3: the tile is written row-major ─────────────────────────────────────
# The kernel reads column-major. A row-major tile is the same BYTES in the wrong
# places, which is the failure a size check cannot see.
apply "$STORE" \
  'std::memcpy(dst.data() + (((c * w) + t) * kBf16Bytes), &word, sizeof(word));' \
  'std::memcpy(dst.data() + (((t * k_) + c) * kBf16Bytes), &word, sizeof(word));' || rc=1
arm "tile written row-major" \
    "bf16 rows [..) are bit-identical to the dense buffer" || rc=1
for f in "${FILES[@]}"; do cp "$stash/$(basename "$f")" "$f"; done
fi

if want 4; then
# ── Arm 4: sensen's front door never consults the bf16 registry ──────────────
# This is the arm that proves the SEAM serves, not the source object. With the
# dense buffer poisoned, ignoring the registry computes from 0xA5A5.
apply "$GEMM" \
  '        if (qtype == QType::BF16) [[unlikely]] {
            const auto* src = findBf16WeightSource(b_T);' \
  '        if (false) [[unlikely]] {
            const auto* src = findBf16WeightSource(b_T);' || rc=1
arm "front door ignores the bf16 registry" \
    "bf16 m = N: LLQ output is bit-identical to dense (the poisoned buffer answered)" || rc=1
for f in "${FILES[@]}"; do cp "$stash/$(basename "$f")" "$f"; done
fi

echo
echo "================ arms complete, rc=$rc"
exit $rc
