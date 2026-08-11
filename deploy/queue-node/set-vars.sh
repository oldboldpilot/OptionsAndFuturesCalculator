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
    # --skip-deploys on every call but the last: each `variable set` otherwise
    # triggers its own deploy, and a node restarting three times in a row while
    # its peers are mid-election is noise that looks like instability.
    railway variable set \
        --environment "${ENVIRONMENT}" --service "${svc}" \
        --skip-deploys \
        "SGEE_NODE_ID=${n}" "SGEE_PEERS=${CONSENSUS_PEERS}" > /dev/null

    # Passed on stdin so the token never appears in an argv the process table
    # can be read for.
    printf '%s' "${TOKEN}" | railway variable set \
        --environment "${ENVIRONMENT}" --service "${svc}" \
        --skip-deploys --stdin "SGEE_QUEUE_TOKEN" > /dev/null
done

echo "[set-vars] done. Variables set with --skip-deploys; run deploy.sh to roll them out."
