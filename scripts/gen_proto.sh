#!/usr/bin/env bash
#
# Single-source protobuf codegen.
#
# Generates the gRPC-Web TypeScript clients from the canonical .protos and
# writes them into frontend/src/grpc/. The C++ side is generated at build time
# by backend/CMakeLists.txt from the same files.
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
# The canonical protos live with the backend, which generates its C++ from the
# same files at build time. The old root-level proto/ copy had a different
# package and service name and has been deleted.
PROTO_DIR="${REPO_ROOT}/backend/proto"
OUT_DIR="${REPO_ROOT}/frontend/src/grpc"

# One entry per service contract to generate a gRPC-Web client for. Each entry
# is a proto file basename (without .proto) under PROTO_DIR; the generated
# filenames (e.g. calculator_pb.js, CalculatorServiceClientPb.ts) are derived
# from it by the plugin, not chosen by this script.
PROTOS=(
    calculator
    finance
)

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

for name in "${PROTOS[@]}"; do
    if [[ ! -f "${PROTO_DIR}/${name}.proto" ]]; then
        echo "ERROR: ${PROTO_DIR}/${name}.proto not found" >&2
        exit 1
    fi
done

mkdir -p "$OUT_DIR"

# CommonJS message classes (the runtime) + TypeScript service client and .d.ts.
# import_style=commonjs+dts emits both <name>_pb.js and <name>_pb.d.ts from one
# invocation, which is what keeps them from drifting apart. Each proto is
# compiled in its own protoc invocation (rather than all at once) so one
# file's output can never leak identifiers into another's.
for name in "${PROTOS[@]}"; do
    proto_file="${PROTO_DIR}/${name}.proto"
    echo "Generating gRPC-Web client"
    echo "  proto : ${proto_file}"
    echo "  out   : ${OUT_DIR}"

    "$PROTOC" \
        --proto_path="$PROTO_DIR" \
        --plugin=protoc-gen-grpc-web="$GRPC_WEB_PLUGIN" \
        --js_out="import_style=commonjs,binary:${OUT_DIR}" \
        --grpc-web_out="import_style=typescript,mode=grpcwebtext:${OUT_DIR}" \
        "$proto_file"
done

echo "Done. Generated:"
ls -1 "$OUT_DIR"

# Guard against the exact drift that caused the browser crash: every message
# each client stub references must actually exist in that proto's generated
# runtime. Checked per-proto so a finance identifier can never be satisfied by
# a calculator message of the same name, or vice versa.
echo
echo "Verifying client references resolve against the generated runtime..."
missing=0
for name in "${PROTOS[@]}"; do
    proto_file="${PROTO_DIR}/${name}.proto"
    pb_js="${OUT_DIR}/${name}_pb.js"
    # protoc-gen-grpc-web names the client stub after the .proto file's
    # basename, title-cased, not after the service name inside it (e.g.
    # calculator.proto's `service OptionsCalculator` still yields
    # CalculatorServiceClientPb.ts).
    client_ts="${OUT_DIR}/${name^}ServiceClientPb.ts"
    pb_prefix="${name}_pb"
    package="$(grep -m1 -E '^package ' "$proto_file" | sed -E 's/^package ([^;]+);/\1/')"
    package_dotted="${package//./\\.}"

    if [[ ! -f "$client_ts" ]]; then
        echo "  MISSING client stub: ${client_ts}" >&2
        missing=1
        continue
    fi

    for msg in $(grep -oE "${pb_prefix}\.[A-Za-z]+" "$client_ts" \
                 | sed "s/${pb_prefix}\.//" | sort -u); do
        if ! grep -q "proto\.${package_dotted}\.${msg}'" "$pb_js"; then
            echo "  MISSING in runtime (${name}): ${msg}" >&2
            missing=1
        fi
    done
done

if [[ $missing -ne 0 ]]; then
    echo "ERROR: a generated client references messages absent from its runtime" >&2
    exit 1
fi
echo "  all client message references resolve."
