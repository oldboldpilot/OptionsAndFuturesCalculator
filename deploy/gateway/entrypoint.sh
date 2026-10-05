#!/bin/sh
# sensen-gateway container entrypoint.
#
# @author Olumuyiwa Oluwasanmi
#
# WHY A SCRIPT AT ALL. The gateway takes its TLS material and its ticket master
# key as FILE PATHS, and a container has no files but the ones the image baked
# in. Baking a private key into an image means every copy of that image carries
# it and the key cannot be rotated without a rebuild. So the material arrives as
# environment variables and is written here, mode 0600, to a tmpfs-backed path.
#
# THIS IS THE PATTERN `backend/start.sh` ALREADY USES for the engine's native
# gRPC listener (GRPC_TLS_CERT / GRPC_TLS_KEY -> /etc/envoy/tls/*, chmod 600),
# deliberately rather than a new one: one mechanism for one problem in one
# project, and that one is already in production.
#
# IT REFUSES RATHER THAN DEGRADING, three times, and each refusal is a
# precondition the gateway itself cannot state as clearly from inside:
#
#   * no certificate or no key -> exit. The gateway has NO CLEARTEXT MODE, so
#     without them it would refuse to bind anyway -- but it would refuse with a
#     message about a missing FILE, sending the reader to look for a path that
#     was never going to exist. Naming the missing VARIABLE is the useful error.
#   * no ticket key -> exit. The gateway's own words: "an admitted request
#     cannot be ticketed, and a leaf must never be reachable without one."
#   * no store -> exit. A gateway with no store answers 401 to every /v1/*
#     request while /healthz returns 200, which is the "green healthcheck over a
#     service that serves nothing" shape this project records against Railway's
#     own SUCCESS. Better to not start.
set -eu

RUN_DIR="${SENSEN_GATEWAY_RUN_DIR:-/tmp/sensen-gateway}"
mkdir -p "$RUN_DIR"
chmod 700 "$RUN_DIR"

die() { echo "sensen-gateway entrypoint: FATAL: $1" >&2; exit 1; }

[ -n "${SENSEN_GATEWAY_CERT_PEM:-}" ] || die \
  "SENSEN_GATEWAY_CERT_PEM is unset. The gateway has no cleartext mode; set the certificate PEM on the service."
[ -n "${SENSEN_GATEWAY_KEY_PEM:-}" ] || die \
  "SENSEN_GATEWAY_KEY_PEM is unset. The gateway has no cleartext mode; set the private key PEM on the service."
[ -n "${SENSEN_GATEWAY_TICKET_KEY_HEX:-}" ] || die \
  "SENSEN_GATEWAY_TICKET_KEY_HEX is unset. An admitted request cannot be ticketed and a leaf must never be reachable without one; generate it with 'sensen-gateway admin gen-ticket-key'."
[ -n "${SENSEN_GATEWAY_STORE:-}" ] || die \
  "SENSEN_GATEWAY_STORE is unset. With no store every /v1/* request is 401 while /healthz stays 200, which is a healthy-looking service that serves nothing."

printf '%s' "$SENSEN_GATEWAY_CERT_PEM" > "$RUN_DIR/tls.crt"
printf '%s' "$SENSEN_GATEWAY_KEY_PEM"  > "$RUN_DIR/tls.key"
printf '%s' "$SENSEN_GATEWAY_TICKET_KEY_HEX" > "$RUN_DIR/ticket.key"
chmod 600 "$RUN_DIR/tls.key" "$RUN_DIR/ticket.key"
chmod 644 "$RUN_DIR/tls.crt"

# The PEM text is no longer needed by anything downstream, and the gateway logs
# its own configuration -- so the SECRET is dropped from the environment that
# `/proc/<pid>/environ` would otherwise carry for the life of the process.
unset SENSEN_GATEWAY_KEY_PEM SENSEN_GATEWAY_CERT_PEM SENSEN_GATEWAY_TICKET_KEY_HEX

export SENSEN_GATEWAY_CERT="$RUN_DIR/tls.crt"
export SENSEN_GATEWAY_KEY="$RUN_DIR/tls.key"
export SENSEN_GATEWAY_TICKET_KEY_FILE="$RUN_DIR/ticket.key"

# RAILWAY INJECTS `PORT`, AND IT IS THE RIGHT PORT HERE -- unlike the engine,
# where `PORT=8080` is ENVOY's listener and reading it would have put the engine
# on Envoy's socket. This container runs exactly one listener, so `PORT` is it.
# An explicit SENSEN_GATEWAY_PORT still wins, for a local run.
if [ -z "${SENSEN_GATEWAY_PORT:-}" ] && [ -n "${PORT:-}" ]; then
    export SENSEN_GATEWAY_PORT="$PORT"
fi

echo "sensen-gateway entrypoint: TLS + ticket material written to $RUN_DIR, port ${SENSEN_GATEWAY_PORT:-unset}"
exec /usr/local/bin/sensen-gateway "$@"
