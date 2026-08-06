#!/bin/bash
#
# Container entrypoint: the C++ engine on :50051 with Envoy in front of it.
#
# Envoy presents the engine three ways, because no single one reaches every
# caller:
#
#   :8080               gRPC-Web -- browsers (the frontend)
#   :8080               JSON     -- server-side callers with no proto toolchain
#   :GRPC_NATIVE_PORT   native gRPC, behind a Railway TCP proxy (opt-in)
#
# The third exists because Railway's HTTP edge terminates HTTP/2 and speaks
# HTTP/1.1 to the container, so HTTP/2 trailers are dropped. gRPC carries its
# status ENTIRELY in trailers, including on success, so a native gRPC call
# through :8080 returns the right answer and then hangs waiting for a
# `grpc-status` that no longer exists; the client reports "Stream removed",
# which reads like the backend crashed. Railway's documented answer is their TCP
# proxy, which forwards bytes without interpreting HTTP, so trailers survive.
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

# ---------------------------------------------------------------------------
# Assemble the Envoy configuration.
#
# Copied rather than edited in place, so every restart starts from the
# checked-in file. Appending to /etc/envoy/envoy.yaml directly would accumulate
# another listener on each restart and then fail to bind.
# ---------------------------------------------------------------------------
ENVOY_CONFIG=/tmp/envoy.runtime.yaml
cp /etc/envoy/envoy.yaml "${ENVOY_CONFIG}"

if [[ -n "${GRPC_NATIVE_PORT:-}" ]]; then
    TLS_BLOCK=""

    if [[ -n "${GRPC_TLS_CERT:-}" && -n "${GRPC_TLS_KEY:-}" ]]; then
        # Certificate material arrives as environment content rather than as a
        # mounted file because Railway has no file mounts for secrets.
        mkdir -p /etc/envoy/tls
        printf '%s' "${GRPC_TLS_CERT}" > /etc/envoy/tls/tls.crt
        printf '%s' "${GRPC_TLS_KEY}"  > /etc/envoy/tls/tls.key
        chmod 600 /etc/envoy/tls/tls.key
        TLS_BLOCK=$(cat <<'TLSEOF'
          transport_socket:
            name: envoy.transport_sockets.tls
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
              common_tls_context:
                # gRPC clients select HTTP/2 by ALPN. Without advertising h2
                # they negotiate http/1.1 and fail in a way that looks like a
                # protocol bug rather than a missing advertisement.
                alpn_protocols: ["h2"]
                tls_certificates:
                  - certificate_chain: { filename: /etc/envoy/tls/tls.crt }
                    private_key: { filename: /etc/envoy/tls/tls.key }
TLSEOF
)
        echo "Native gRPC on :${GRPC_NATIVE_PORT} with TLS."
    elif [[ "${GRPC_ALLOW_PLAINTEXT:-}" == "1" ]]; then
        # Deliberately loud. A Railway TCP proxy is reachable from the public
        # internet, and this endpoint authenticates with an API key sent as a
        # header: in plaintext that key is readable by anything on the path, and
        # it is long-lived, so one capture stays valid until somebody notices.
        echo "WARNING: native gRPC on :${GRPC_NATIVE_PORT} is PLAINTEXT." >&2
        echo "         API keys will cross the network unencrypted. Use this" >&2
        echo "         only where the port is genuinely private." >&2
    else
        # Fail closed. Silently downgrading to plaintext is how a development
        # convenience becomes a production credential leak.
        echo "FATAL: GRPC_NATIVE_PORT is set but no TLS material was provided." >&2
        echo "       Set GRPC_TLS_CERT and GRPC_TLS_KEY (PEM contents), or set" >&2
        echo "       GRPC_ALLOW_PLAINTEXT=1 if this port is genuinely private." >&2
        exit 1
    fi

    cat >> "${ENVOY_CONFIG}" <<NATIVEEOF
    - name: listener_grpc_native
      address:
        socket_address: { address: 0.0.0.0, port_value: ${GRPC_NATIVE_PORT} }
      filter_chains:
        -
${TLS_BLOCK}
          filters:
          - name: envoy.filters.network.http_connection_manager
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
              # http2 rather than auto: this listener exists to carry native
              # gRPC, which is HTTP/2 only. Accepting HTTP/1.1 here would let a
              # misconfigured client connect and then fail obscurely.
              codec_type: http2
              stat_prefix: grpc_native
              route_config:
                name: native_route
                virtual_hosts:
                  - name: native_service
                    domains: ["*"]
                    routes:
                      # Mirrors the timeout on the gRPC-Web listener in
                      # envoy.yaml: Envoy's route default is 15s, and
                      # calculator.assistant.StrategyAssistant (a fine-tuned
                      # Qwen3-0.6B running on CPU) can legitimately exceed that
                      # once a cold model load stacks on top of generation,
                      # even though a warm call finishes in about 1.1s. The two
                      # listeners must agree, or a native gRPC caller sees a
                      # request that would have succeeded over gRPC-Web get
                      # killed here instead, with no obvious reason why.
                      - match: { prefix: "/" }
                        route:
                          cluster: backend_grpc_service
                          timeout: 120s
                          max_stream_duration:
                            grpc_timeout_header_max: 0s
              http_filters:
              # No grpc_web filter and no transcoder: this path is native gRPC
              # end to end, so the response passes through with its trailers
              # intact, which is the entire reason this listener exists.
              - name: envoy.filters.http.router
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
NATIVEEOF
else
    echo "Native gRPC listener disabled (GRPC_NATIVE_PORT unset)."
fi

echo "Starting OptionsCalculatorEngine on :50051..."

# A model path that names a file which is not there is worse than an unset one:
# the engine's own contract is "unset means no model, answer MODEL_UNAVAILABLE",
# and a dangling path takes some less legible failure route instead. The image
# now sets both paths UNCONDITIONALLY (a model can arrive either by URL fetch or
# by being staged into the build context, and the Dockerfile cannot test for a
# file's existence when it sets ENV), so the existence check belongs here.
for _mv in MODEL_PATH MORTGAGE_MODEL_PATH; do
    eval "_mp=\${$_mv:-}"
    if [ -n "${_mp}" ] && [ ! -f "${_mp}" ]; then
        echo "start.sh: ${_mv}=${_mp} does not exist -- unsetting it so the service reports its model unavailable rather than failing on a dangling path."
        unset "${_mv}"
    fi
done

# Line-buffered, because the engine's stdout here is a PIPE, not a terminal, and
# libc therefore block-buffers it in 4 KB chunks. Startup is verbose enough to
# flush those chunks, so the early lines arrive and the LAST few sit in the
# buffer indefinitely -- and the last few are exactly the ones that say whether
# an optional subsystem came up.
#
# Concretely: "constrained decoding armed on '<params>'" never appeared in
# `railway logs` while the feature was demonstrably working, and its absence
# reads identically to the feature having failed to start. It cost an hour of
# chasing a phantom locally before the same trick (stdbuf -oL) showed the line
# had been there all along. A log you cannot trust for absence is worse than no
# log, because it invites exactly the wrong conclusion.
#
# `stdbuf` is coreutils and is already in the image. If it were ever missing,
# fall through to the unbuffered-but-present binary rather than not starting.
if command -v stdbuf >/dev/null 2>&1; then
    stdbuf -oL -eL /app/calculator_engine &
else
    echo "start.sh: stdbuf not found -- engine logs will be block-buffered and may appear truncated."
    /app/calculator_engine &
fi
BACKEND_PID=$!

echo "Starting Envoy on :8080..."
envoy -c "${ENVOY_CONFIG}" --log-level "${ENVOY_LOG_LEVEL:-info}" &
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
