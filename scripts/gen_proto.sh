#!/usr/bin/env bash
#
# Single-source protobuf codegen.
#
# Generates the gRPC-Web TypeScript client from the canonical .proto and writes
# it into frontend/src/grpc/. The C++ side is generated at build time by
# backend/CMakeLists.txt from the same file.
#
# This script exists because the frontend stubs previously drifted: the .d.ts
# and client stub were regenerated from a newer proto while the runtime
# calculator_pb.js was not, so TypeScript compiled cleanly against declarations
# the runtime could not satisfy and getMarketQuote/getMarketChain threw
# "Cannot read properties of undefined (reading 'deserializeBinary')" in the
# browser. Always regenerate through this script — never by hand.
#
# Generated output is committed, because Cloudflare Pages has no protoc at
# build time.

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
# The canonical proto lives with the backend, which generates its C++ from the
# same file at build time. The old root-level proto/ copy had a different
# package and service name and has been deleted.
PROTO_DIR="${REPO_ROOT}/backend/proto"
PROTO_FILE="${PROTO_DIR}/calculator.proto"
OUT_DIR="${REPO_ROOT}/frontend/src/grpc"

PROTOC="${REPO_ROOT}/frontend/node_modules/grpc-tools/bin/protoc"
GRPC_WEB_PLUGIN="${PROTOC_GEN_GRPC_WEB:-$(command -v protoc-gen-grpc-web || true)}"

if [[ ! -x "$PROTOC" ]]; then
    echo "ERROR: protoc not found at ${PROTOC}" >&2
    echo "       run: (cd frontend && npm install --no-save grpc-tools)" >&2
    exit 1
fi

if [[ -z "$GRPC_WEB_PLUGIN" || ! -x "$GRPC_WEB_PLUGIN" ]]; then
    echo "ERROR: protoc-gen-grpc-web not found on PATH" >&2
    exit 1
fi

if [[ ! -f "$PROTO_FILE" ]]; then
    echo "ERROR: ${PROTO_FILE} not found" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

echo "Generating gRPC-Web client"
echo "  proto : ${PROTO_FILE}"
echo "  out   : ${OUT_DIR}"

# CommonJS message classes (the runtime) + TypeScript service client and .d.ts.
# import_style=commonjs+dts emits both calculator_pb.js and calculator_pb.d.ts
# from one invocation, which is what keeps them from drifting apart.
"$PROTOC" \
    --proto_path="$PROTO_DIR" \
    --plugin=protoc-gen-grpc-web="$GRPC_WEB_PLUGIN" \
    --js_out="import_style=commonjs,binary:${OUT_DIR}" \
    --grpc-web_out="import_style=typescript,mode=grpcwebtext:${OUT_DIR}" \
    "$PROTO_FILE"

echo "Done. Generated:"
ls -1 "$OUT_DIR"

# Guard against the exact drift that caused the browser crash: every message the
# client stub references must actually exist in the generated runtime.
echo
echo "Verifying client references resolve against the generated runtime..."
missing=0
for msg in $(grep -oE 'calculator_pb\.[A-Za-z]+' "${OUT_DIR}/CalculatorServiceClientPb.ts" \
             | sed 's/calculator_pb\.//' | sort -u); do
    if ! grep -q "proto\.calculator\.${msg}'" "${OUT_DIR}/calculator_pb.js"; then
        echo "  MISSING in runtime: ${msg}" >&2
        missing=1
    fi
done

if [[ $missing -ne 0 ]]; then
    echo "ERROR: generated client references messages absent from calculator_pb.js" >&2
    exit 1
fi
echo "  all client message references resolve."
