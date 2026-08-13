#!/bin/sh
#
# Queue-node entrypoint: materialise TLS material, then exec the node.
#
# @author Olumuyiwa Oluwasanmi
#
# WHY THIS EXISTS AT ALL: sgee_queue_node reads SGEE_TLS_CA_CERT / SGEE_TLS_CERT
# / SGEE_TLS_KEY as FILE PATHS, and a Railway environment variable is a value,
# not a file. Without this shim there is nowhere for a certificate to live in the
# container short of baking it into the image, which would put a private key in
# every layer of a registry.
#
# The three *_B64 variables below carry base64 of the PEM. Base64 rather than the
# PEM itself because the value travels through `railway variables --set`, a shell,
# and a JSON API on the way here, and a multi-line value with embedded newlines is
# mangled by at least one of them depending on quoting. One line survives all
# three.
#
# ALL-OR-NOTHING, and this shim enforces it one layer EARLIER than the node does.
# sgee_queue_node already refuses to boot on a half-set trio (ConfigError::
# PartialTls) precisely because a node holding a certificate and no key would
# otherwise serve plaintext while whoever set the certificate believed the port
# was protected. That check cannot see a half-set trio of *_B64 vars, because it
# only ever sees the paths this script exports -- so the same rule is applied to
# the inputs here. Setting none of the three is a supported, plaintext
# deployment; setting some is an error, never a downgrade.
set -eu

TLS_DIR=/run/sgee-tls

count_set=0
for v in SGEE_TLS_CA_CERT_B64 SGEE_TLS_CERT_B64 SGEE_TLS_KEY_B64; do
    eval "val=\${$v:-}"
    [ -n "$val" ] && count_set=$((count_set + 1))
done

if [ "$count_set" -eq 0 ]; then
    echo "[entrypoint] no TLS material supplied; starting with both ports in plaintext" >&2
elif [ "$count_set" -ne 3 ]; then
    echo "[entrypoint] FATAL: ${count_set} of 3 TLS variables are set." >&2
    echo "[entrypoint] SGEE_TLS_CA_CERT_B64, SGEE_TLS_CERT_B64 and SGEE_TLS_KEY_B64 are" >&2
    echo "[entrypoint] all-or-nothing. Refusing to start: the dangerous outcome here is not" >&2
    echo "[entrypoint] a node that fails loudly, it is one that quietly serves plaintext" >&2
    echo "[entrypoint] while its operator believes the port is protected." >&2
    exit 1
else
    mkdir -p "$TLS_DIR"
    chmod 700 "$TLS_DIR"
    # Decode failures must be fatal for the same reason a half-set trio is: a
    # truncated or corrupt value would otherwise leave an unreadable file, and the
    # node's own read_pem() failure is a second chance to get this wrong quietly.
    printf '%s' "$SGEE_TLS_CA_CERT_B64" | base64 -d > "$TLS_DIR/ca.pem"
    printf '%s' "$SGEE_TLS_CERT_B64"    | base64 -d > "$TLS_DIR/cert.pem"
    printf '%s' "$SGEE_TLS_KEY_B64"     | base64 -d > "$TLS_DIR/key.pem"
    chmod 600 "$TLS_DIR"/*.pem

    for f in ca.pem cert.pem key.pem; do
        if [ ! -s "$TLS_DIR/$f" ]; then
            echo "[entrypoint] FATAL: $f decoded to an empty file." >&2
            exit 1
        fi
    done

    SGEE_TLS_CA_CERT="$TLS_DIR/ca.pem"
    SGEE_TLS_CERT="$TLS_DIR/cert.pem"
    SGEE_TLS_KEY="$TLS_DIR/key.pem"
    export SGEE_TLS_CA_CERT SGEE_TLS_CERT SGEE_TLS_KEY
    echo "[entrypoint] TLS material staged in ${TLS_DIR}; mutual TLS will be required on both ports" >&2
fi

exec /app/sgee_queue_node "$@"
