#!/usr/bin/env bash
#
# Converts the merged 16-bit checkpoint train.py produced into the Q8_0 GGUF that
# production serves, using SENSEN's own converter.
#
#   scripts/convert_to_gguf.sh <merged_dir> <out_file.gguf> [dtype]
#
# dtype: q8_0 (default, what production serves) | f16 | q6_k | q4_k | q5_k
#
# ---------------------------------------------------------------------------
# WHY SENSEN AND NOT llama.cpp
#
# sensen is the standard for both inference and conversion in this project, and
# this step used llama.cpp until 2026-08-03 for no reason other than habit.
# `sensen::convert_safetensors_to_gguf` (backend/sensen/src/model_converter.cppm)
# writes a Q8_0 GGUF DIRECTLY from the merged safetensors, so the llama.cpp
# two-step -- `convert_hf_to_gguf.py --outtype f16` then `llama-quantize ... Q8_0`
# -- is replaced by one call with no 1.2 GB f16 intermediate:
#
#   llama.cpp : two processes, an f16 intermediate, minutes
#   sensen    : one call, no intermediate, 0.765 s (310 tensors, 639,447,136 B)
#
# Measured 2026-08-03 on the model production serves: both outputs score 16/16 on
# the defect holdout through the real serving path. They are NOT byte-identical
# (~480 bytes of metadata differ, weights do not) so a checksum comparison across
# the two writers is meaningless -- compare BEHAVIOUR, per
# docs/guides/ASSISTANT_EVALUATION.md.
#
# llama.cpp is deliberately still vendored, for two things this does not replace:
# the parity probes under backend/src/*_probe.cpp, which need an INDEPENDENT
# implementation to check sensen against (a reference that shares sensen's code
# would prove nothing), and the opt-in ASSISTANT_BACKEND=llamacpp second backend.
# Neither is a conversion or serving default.
#
# @author Olumuyiwa Oluwasanmi
set -euo pipefail

MERGED_DIR=${1:?usage: convert_to_gguf.sh <merged_dir> <out_file.gguf> [dtype]}
OUT_FILE=${2:?usage: convert_to_gguf.sh <merged_dir> <out_file.gguf> [dtype]}
DTYPE=${3:-q8_0}

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONVERTER="${SENSEN_CONVERTER:-$REPO/backend/sensen/build/bin/convert_safetensors_to_gguf}"
VALIDATOR="${SENSEN_VALIDATOR:-$REPO/backend/sensen/build/bin/validate_gguf}"

die() { echo "convert_to_gguf: $*" >&2; exit 2; }

[ -d "$MERGED_DIR" ] || die "no such merged checkpoint directory: $MERGED_DIR"
[ -f "$MERGED_DIR/config.json" ] || die "$MERGED_DIR has no config.json -- is it a merged HF checkpoint?"

if [ ! -x "$CONVERTER" ]; then
    die "converter not built at $CONVERTER
  Build it with:  ninja -C backend/sensen/build convert_safetensors_to_gguf validate_gguf
  Or set SENSEN_CONVERTER to an existing binary."
fi

# The converter supports qwen3 and qwen2 only, and says so loudly rather than
# writing a GGUF that loads and then generates confident nonsense. Check here too
# so the failure names the file rather than surfacing from inside the converter.
ARCH=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get('model_type',''))" \
        "$MERGED_DIR/config.json" 2>/dev/null || echo "")
case "$ARCH" in
    qwen3|qwen2) ;;
    "") die "could not read model_type from $MERGED_DIR/config.json" ;;
    *) die "model_type '$ARCH' is not supported by sensen's converter (qwen3|qwen2 only)" ;;
esac

mkdir -p "$(dirname "$OUT_FILE")"

echo "=== sensen convert_safetensors_to_gguf ($ARCH -> $DTYPE) ==="
"$CONVERTER" --model "$MERGED_DIR" --out "$OUT_FILE" --dtype "$DTYPE"

if [ -x "$VALIDATOR" ]; then
    echo "=== validate_gguf ==="
    "$VALIDATOR" "$OUT_FILE" | tail -3
else
    echo "note: validate_gguf not built; skipping structural validation" >&2
fi

echo "=== checksum ==="
sha256sum "$OUT_FILE"
ls -la "$OUT_FILE"

cat <<'NEXT'

Next: this checksum is what MODEL_SHA256 must be set to, and the GGUF must be
uploaded to the HF repo before repointing MODEL_URL. Do NOT assume the upload was
faithful -- re-download and re-checksum. See docs/STRATEGY_ASSISTANT_PIPELINE.md
section 4, "Swapping the served model".

Score it before shipping it: docs/guides/ASSISTANT_EVALUATION.md. A GGUF that
loads is not a GGUF that works.
NEXT
