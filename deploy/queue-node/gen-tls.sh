#!/usr/bin/env bash
#
# Generate the mutual-TLS material for the SGEE queue cluster.
#
# @author Olumuyiwa Oluwasanmi
#
# Usage: deploy/queue-node/gen-tls.sh
#
# Writes a private CA, one node certificate and one client certificate into
# config/keys/sgee-tls/, which is gitignored. Idempotent: it refuses to overwrite
# existing material, because regenerating the CA invalidates every certificate
# already deployed and the cluster would lose quorum until all three nodes were
# rolled with the new one.
#
# TWO certificates, not one. Both are signed by the same CA and the nodes verify
# against that CA, so a single cert would work -- and would also mean the engine's
# mirror client holds material that lets it present itself AS a queue node. The
# node cert carries serverAuth+clientAuth (nodes are both, to each other); the
# client cert carries clientAuth only.
#
# The node cert's SANs cover all three internal hostnames plus loopback, so one
# cert serves every node. Per-node certificates would be stricter, but Railway's
# internal DNS is the only naming here and a node's identity is already fenced by
# SGEE_NODE_ID and node.id on its volume -- see docs/SGEE_QUEUE_CLUSTER.md.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${REPO_ROOT}/config/keys/sgee-tls"
DAYS=3650

if [ -d "${OUT}" ] && [ -n "$(ls -A "${OUT}" 2>/dev/null)" ]; then
    echo "[gen-tls] ${OUT} already contains material; refusing to overwrite." >&2
    echo "[gen-tls] Regenerating the CA invalidates every deployed certificate." >&2
    echo "[gen-tls] To rotate deliberately: move the directory aside, re-run, then roll" >&2
    echo "[gen-tls] ALL THREE nodes and the engine before the old material is removed." >&2
    exit 1
fi

mkdir -p "${OUT}"
chmod 700 "${OUT}"
cd "${OUT}"

echo "[gen-tls] CA"
openssl req -x509 -newkey rsa:4096 -sha256 -days "${DAYS}" -nodes \
    -keyout ca.key -out ca.pem \
    -subj "/O=OptionsAndFuturesCalculator/CN=sgee-queue-ca" 2>/dev/null

cat > node.cnf <<'CNF'
[req]
distinguished_name = dn
req_extensions     = ext
prompt             = no
[dn]
O  = OptionsAndFuturesCalculator
CN = sgee-queue-node
[ext]
basicConstraints = CA:FALSE
keyUsage         = critical, digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth, clientAuth
subjectAltName   = @alt
[alt]
DNS.1 = sgee-queue-1.railway.internal
DNS.2 = sgee-queue-2.railway.internal
DNS.3 = sgee-queue-3.railway.internal
DNS.4 = localhost
IP.1  = ::1
IP.2  = 127.0.0.1
CNF

echo "[gen-tls] node certificate"
openssl req -newkey rsa:4096 -nodes -keyout node.key -out node.csr -config node.cnf 2>/dev/null
openssl x509 -req -in node.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
    -out node.pem -days "${DAYS}" -sha256 \
    -extfile node.cnf -extensions ext 2>/dev/null

cat > client.cnf <<'CNF'
[req]
distinguished_name = dn
req_extensions     = ext
prompt             = no
[dn]
O  = OptionsAndFuturesCalculator
CN = sgee-queue-client
[ext]
basicConstraints = CA:FALSE
keyUsage         = critical, digitalSignature, keyEncipherment
extendedKeyUsage = clientAuth
CNF

echo "[gen-tls] client certificate (engine mirror writer)"
openssl req -newkey rsa:4096 -nodes -keyout client.key -out client.csr -config client.cnf 2>/dev/null
openssl x509 -req -in client.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
    -out client.pem -days "${DAYS}" -sha256 \
    -extfile client.cnf -extensions ext 2>/dev/null

rm -f node.csr client.csr
chmod 600 ./*.key
chmod 644 ./*.pem

echo "[gen-tls] verifying the chain"
openssl verify -CAfile ca.pem node.pem
openssl verify -CAfile ca.pem client.pem

echo "[gen-tls] done. Material in ${OUT} (gitignored). Keys are NOT printed."
