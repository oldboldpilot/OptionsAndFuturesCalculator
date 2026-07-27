#!/bin/bash
#
# Container entrypoint: the C++ engine on :50051 with Envoy translating
# gRPC-Web in front of it on :8080.
#
# If either process dies the container must exit so the platform restarts it —
# an Envoy still answering health checks while the engine is dead is worse than
# being down, because every request returns a confusing 503 instead.
set -euo pipefail

export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/app/lib}"

if [[ -z "${ALPACA_API_KEY:-}" || -z "${ALPACA_API_SECRET:-}" ]]; then
    echo "WARNING: ALPACA_API_KEY / ALPACA_API_SECRET are not set." >&2
    echo "         Quote and chain requests will fail with NotConfigured." >&2
fi

echo "Starting OptionsCalculatorEngine on :50051..."
/app/calculator_engine &
BACKEND_PID=$!

echo "Starting Envoy on :8080..."
envoy -c /etc/envoy/envoy.yaml --log-level "${ENVOY_LOG_LEVEL:-info}" &
ENVOY_PID=$!

terminate() {
    echo "Shutting down..."
    kill -TERM "${BACKEND_PID}" "${ENVOY_PID}" 2>/dev/null || true
    wait
}
trap terminate SIGTERM SIGINT

# Exit as soon as either process does, propagating its status.
wait -n "${BACKEND_PID}" "${ENVOY_PID}"
STATUS=$?
echo "A process exited with status ${STATUS}; stopping the container."
terminate
exit "${STATUS}"
