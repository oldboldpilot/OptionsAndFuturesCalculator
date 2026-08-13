#!/usr/bin/env bash
#
# Set the per-node variables on the three SGEE queue services.
#
# @author Olumuyiwa Oluwasanmi
#
# Usage: deploy/queue-node/set-vars.sh
#
# Idempotent: run it again after adding a node or rotating the token.
#
# The token value is read from config/.env, which is gitignored and stays that
# way. It is generated here on first run and is NEVER echoed -- not to the
# terminal, not into a deploy log.

set -euo pipefail

PROJECT_ID="59242db9-5174-4722-aea3-4df2a8f54f99"
ENVIRONMENT="production"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ENV_FILE="${REPO_ROOT}/config/.env"

# The CONSENSUS port. This is the map the nodes use to reach each other for
# Raft and SWIM.
#
# THE BACKEND USES THE SAME VARIABLE NAME WITH A DIFFERENT PORT, and that is
# the single easiest thing to get wrong here. sgee_queue_node reads SGEE_PEERS
# and dials consensus (50052); backend/src/modules/sgee_queue_client.cpp reads
# SGEE_PEERS and dials the client-facing TaskQueue service (50053). Copying one
# service's value onto the other produces a process that connects to a real
# port, speaks the wrong protocol to it, and fails in a way that reads like a
# network problem.
CONSENSUS_PEERS="1=sgee-queue-1.railway.internal:50052,2=sgee-queue-2.railway.internal:50052,3=sgee-queue-3.railway.internal:50052"

# Raft timing. ALL-OR-NOTHING: sgee_queue_node refuses a half-set pair, because
# raising the election base alone leaves a heartbeat that no longer refreshes the
# deadline -- which is the churn this override exists to cure, not a milder form
# of it.
#
# Why these values rather than RaftNode's compiled-in 150/50 defaults: the
# deployed cluster was observed campaigning 20-23 times between consecutive
# status lines, with one node's term running 125 ahead of its peers and
# last_applied frozen. A 150 ms base randomised into [150, 300) cannot survive
# the scheduling jitter of shared containers on Railway's IPv6 overlay -- every
# follower times out before a heartbeat lands.
#
# 1500/300 randomises into [1500, 3000), a 5:1 heartbeat ratio, and a worst-case
# failover of ~3 s. That is entirely acceptable for a NON-AUTHORITATIVE mirror,
# where availability of the leader matters and latency of failover does not.
#
# The upper bound is not a matter of taste. The node derives its await budget as
# (2*base + heartbeat) * 4 milliseconds, and that budget MUST stay strictly under
# the broker's 30000 ms lease visibility window -- past it, a caller is still
# waiting on a lease the broker has already reclaimed, and completes against a
# stale fencing token. 1500/300 derives 13200 ms. The pair (3000, 1500) satisfies
# RaftNode::set_timing's own precondition and derives exactly 30000, which is why
# the node refuses it at startup and why P6 of tests/test_sgee_automated_reasoning
# discharges the bound through Z3 rather than trusting the arithmetic by eye.
ELECTION_TIMEOUT_MS="1500"
HEARTBEAT_MS="300"

# Mutual-TLS material, produced by deploy/queue-node/gen-tls.sh. Gitignored, and
# absent by default -- a deployment with no material here is a supported
# plaintext one, which is what the cluster ran as until certificates existed.
TLS_DIR="${REPO_ROOT}/config/keys/sgee-tls"
ENGINE_SERVICE="options-calculator-backend"

ensure_token() {
    if grep -q '^SGEE_QUEUE_TOKEN=' "${ENV_FILE}" 2>/dev/null; then
        return 0
    fi
    echo "[set-vars] generating SGEE_QUEUE_TOKEN into config/.env (value not shown)"
    {
        echo ""
        echo "# Shared secret for SGEE consensus frames (x-sgee-auth). Every node must"
        echo "# carry the SAME value; a cluster where only some nodes require it cannot"
        echo "# form a quorum. Guarded by tests/integration/queue_node_auth_test.sh."
        echo "SGEE_QUEUE_TOKEN=$(openssl rand -base64 36 | tr -d '\n/+=' | head -c 48)"
    } >> "${ENV_FILE}"
}

ensure_token
# shellcheck disable=SC2046
TOKEN="$(grep '^SGEE_QUEUE_TOKEN=' "${ENV_FILE}" | tail -1 | cut -d= -f2-)"
if [ -z "${TOKEN}" ]; then
    echo "FATAL: SGEE_QUEUE_TOKEN is empty in config/.env" >&2
    exit 1
fi

for n in 1 2 3; do
    svc="sgee-queue-${n}"
    echo "[set-vars] ${svc}"
    # --skip-deploys on EVERY call, including the last, and deploy.sh is what
    # rolls them out afterwards.
    #
    # Each `variable set` otherwise triggers its own deploy, so setting three
    # variables on three services would restart every node up to six times
    # while its peers are mid-election -- noise indistinguishable from
    # instability. Deploying from here would also roll out in whatever order
    # the loop happens to reach, while deploy.sh serialises on /statusz.
    #
    # (An earlier version of this comment said "every call but the last",
    # describing an intent the code never had: the flag was always passed
    # unconditionally. Reading the comment rather than the code would leave an
    # operator waiting for a rollout that is not coming.)
    railway variable set \
        --environment "${ENVIRONMENT}" --service "${svc}" \
        --skip-deploys \
        "SGEE_NODE_ID=${n}" "SGEE_PEERS=${CONSENSUS_PEERS}" \
        "SGEE_ELECTION_TIMEOUT_MS=${ELECTION_TIMEOUT_MS}" \
        "SGEE_HEARTBEAT_MS=${HEARTBEAT_MS}" > /dev/null

    # Passed on stdin so the token never appears in an argv the process table
    # can be read for.
    printf '%s' "${TOKEN}" | railway variable set \
        --environment "${ENVIRONMENT}" --service "${svc}" \
        --skip-deploys --stdin "SGEE_QUEUE_TOKEN" > /dev/null

    # Mutual TLS, when the material exists. Base64 of the PEM, because the value
    # crosses a shell, the CLI and a JSON API on the way here and a multi-line
    # PEM does not survive all three intact. The node's entrypoint decodes these
    # back to files, since sgee_queue_node's config takes paths -- see
    # backend/queue-node-entrypoint.sh.
    #
    # ALL THREE OR NONE, and the node refuses to boot on a half-set trio. Every
    # key goes over stdin, never argv.
    if [ -f "${TLS_DIR}/ca.pem" ]; then
        printf '%s' "$(base64 -w0 "${TLS_DIR}/ca.pem")" | railway variable set \
            --environment "${ENVIRONMENT}" --service "${svc}" \
            --skip-deploys --stdin "SGEE_TLS_CA_CERT_B64" > /dev/null
        printf '%s' "$(base64 -w0 "${TLS_DIR}/node.pem")" | railway variable set \
            --environment "${ENVIRONMENT}" --service "${svc}" \
            --skip-deploys --stdin "SGEE_TLS_CERT_B64" > /dev/null
        printf '%s' "$(base64 -w0 "${TLS_DIR}/node.key")" | railway variable set \
            --environment "${ENVIRONMENT}" --service "${svc}" \
            --skip-deploys --stdin "SGEE_TLS_KEY_B64" > /dev/null
        echo "[set-vars]   mTLS material set on ${svc}"
    fi
done

# The engine is the other end of port 50053. Once the nodes REQUIRE a client
# certificate, a mirror writer without one fails every write -- and mirror writes
# are dropped by design, so the symptom is a mirror that silently stops
# mirroring rather than anything that raises an alarm. It gets the CLIENT
# certificate, not the node one, so it cannot present itself as a queue node.
if [ -f "${TLS_DIR}/client.pem" ]; then
    echo "[set-vars] ${ENGINE_SERVICE} (mirror client credentials)"
    printf '%s' "$(base64 -w0 "${TLS_DIR}/ca.pem")" | railway variable set \
        --environment "${ENVIRONMENT}" --service "${ENGINE_SERVICE}" \
        --skip-deploys --stdin "SGEE_TLS_CA_CERT_B64" > /dev/null
    printf '%s' "$(base64 -w0 "${TLS_DIR}/client.pem")" | railway variable set \
        --environment "${ENVIRONMENT}" --service "${ENGINE_SERVICE}" \
        --skip-deploys --stdin "SGEE_TLS_CERT_B64" > /dev/null
    printf '%s' "$(base64 -w0 "${TLS_DIR}/client.key")" | railway variable set \
        --environment "${ENVIRONMENT}" --service "${ENGINE_SERVICE}" \
        --skip-deploys --stdin "SGEE_TLS_KEY_B64" > /dev/null
fi

echo "[set-vars] done. Variables set with --skip-deploys; run deploy.sh to roll them out."
