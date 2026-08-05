#!/usr/bin/env bash
#
# Regenerate the gRPC-Web TypeScript client for sensen.finance.Finance from
# the vendored proto/finance.proto.
#
# Cloned from OptionsAndFuturesCalculator's scripts/gen_proto.sh (the server
# repo that owns the canonical .proto), because that script exists to prevent
# a specific, previously-real bug: generating the .d.ts / client stub from a
# newer proto while the runtime *_pb.js lags behind, so TypeScript compiles
# against declarations the runtime cannot satisfy and calls throw at runtime
# ("Cannot read properties of undefined (reading 'deserializeBinary')"). The
# fix there -- and here -- is to always regenerate both files from one
# protoc invocation, and verify class references resolve before calling the
# job done. Never hand-edit the generated output.
#
# Generated output (src/grpc/) is committed, because this is a static site
# with no protoc at build time -- same reasoning as the server repo.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROTO_DIR="${PKG_ROOT}/proto"
PROTO_FILE="${PROTO_DIR}/finance.proto"
OUT_DIR="${PKG_ROOT}/src/grpc"

# protoc: prefer a local grpc-tools install (npm i -D grpc-tools), fall back
# to whatever protoc is on PATH. Pinned upstream to libprotoc 3.19.1 via
# grpc-tools 1.13.1 -- an unrelated protoc on PATH may emit different output.
if [[ -x "${PKG_ROOT}/node_modules/grpc-tools/bin/protoc" ]]; then
    PROTOC="${PKG_ROOT}/node_modules/grpc-tools/bin/protoc"
elif command -v protoc >/dev/null; then
    PROTOC="$(command -v protoc)"
    echo "WARNING: using system protoc ($("$PROTOC" --version)); the" >&2
    echo "         canonical toolchain is grpc-tools 1.13.1 (libprotoc 3.19.1)." >&2
    echo "         Run: npm install --no-save grpc-tools@1.13.1 for parity." >&2
else
    echo "ERROR: no protoc found. Run: npm install --no-save grpc-tools@1.13.1" >&2
    exit 1
fi

# protoc-gen-grpc-web plugin, pinned to v1.5.0 (same as the server repo).
GRPC_WEB_PLUGIN="${PROTOC_GEN_GRPC_WEB:-$(command -v protoc-gen-grpc-web || true)}"
if [[ -z "$GRPC_WEB_PLUGIN" || ! -x "$GRPC_WEB_PLUGIN" ]]; then
    echo "ERROR: protoc-gen-grpc-web not found on PATH." >&2
    echo "       Install v1.5.0: https://github.com/grpc/grpc-web/releases" >&2
    exit 1
fi

if [[ ! -f "$PROTO_FILE" ]]; then
    echo "ERROR: ${PROTO_FILE} not found" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

echo "Generating gRPC-Web client for sensen.finance.Finance"
echo "  protoc  : ${PROTOC}"
echo "  plugin  : ${GRPC_WEB_PLUGIN}"
echo "  proto   : ${PROTO_FILE}"
echo "  out     : ${OUT_DIR}"

# CommonJS message classes (the runtime) + TypeScript service client and
# .d.ts, in one invocation -- what keeps them from drifting apart.
"$PROTOC" \
    --proto_path="$PROTO_DIR" \
    --plugin=protoc-gen-grpc-web="$GRPC_WEB_PLUGIN" \
    --js_out="import_style=commonjs,binary:${OUT_DIR}" \
    --grpc-web_out="import_style=typescript,mode=grpcwebtext:${OUT_DIR}" \
    "$PROTO_FILE"

echo "Done. Generated:"
ls -1 "$OUT_DIR"

# Guard against the exact drift described above: every message the client
# stub references must actually exist in the generated runtime.
echo
echo "Verifying client references resolve against the generated runtime..."
missing=0
for msg in $(grep -oE 'finance_pb\.[A-Za-z]+' "${OUT_DIR}/FinanceServiceClientPb.ts" \
             | sed 's/finance_pb\.//' | sort -u); do
    if ! grep -q "proto\.sensen\.finance\.${msg}'" "${OUT_DIR}/finance_pb.js"; then
        echo "  MISSING in runtime: ${msg}" >&2
        missing=1
    fi
done

if [[ $missing -ne 0 ]]; then
    echo "ERROR: generated client references messages absent from finance_pb.js" >&2
    exit 1
fi
echo "  all client message references resolve."
