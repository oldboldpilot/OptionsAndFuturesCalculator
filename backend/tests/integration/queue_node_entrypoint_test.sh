#!/usr/bin/env bash
#
# Gate for backend/queue-node-entrypoint.sh.
#
# @author Olumuyiwa Oluwasanmi
#
# The entrypoint decides whether three queue nodes come up speaking mutual TLS,
# plaintext, or not at all. The dangerous outcome is not a node that refuses --
# that is loud -- it is a node that quietly serves plaintext while its operator
# believes the port is protected, which is exactly what a half-set trio of
# variables would produce without the check this test pins.
#
# No node, no container and no network: the script is exercised directly with its
# exec line replaced, because what is under test is the DECISION, not the binary
# it hands off to.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
ENTRYPOINT="${REPO_ROOT}/backend/queue-node-entrypoint.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

fail=0
check() {
    local name="$1" want_rc="$2" got_rc="$3"
    if [ "${want_rc}" = "${got_rc}" ]; then
        echo "  PASS  ${name}"
    else
        echo "  FAIL  ${name}: expected rc=${want_rc}, got rc=${got_rc}"
        fail=1
    fi
}

# A stand-in for the node that reports only that it was reached. It must NOT
# reference the SGEE_TLS_* variables: under `set -u` an unset one would abort,
# and the plaintext case would then look like a refusal for a reason that has
# nothing to do with the entrypoint. (That is not hypothetical -- the first
# version of this harness did exactly that and reported a false failure.)
# TLS_DIR is redirected too: the real one is /run/sgee-tls, which the container
# writes as root and a CI or developer host does not. Rewriting it here keeps the
# test hermetic instead of requiring privilege to assert a decision that has
# nothing to do with privilege.
STAGE="${WORK}/staged"
sed -e 's#exec /app/sgee_queue_node "$@"#echo REACHED_NODE#' \
    -e "s#^TLS_DIR=/run/sgee-tls#TLS_DIR=${STAGE}#" "${ENTRYPOINT}" > "${WORK}/ep.sh"
chmod +x "${WORK}/ep.sh"

# Self-material, so the test needs nothing from config/keys and runs anywhere.
openssl req -x509 -newkey rsa:2048 -sha256 -days 1 -nodes \
    -keyout "${WORK}/t.key" -out "${WORK}/t.pem" \
    -subj "/CN=entrypoint-test" >/dev/null 2>&1
CA_B64="$(base64 -w0 "${WORK}/t.pem")"
KEY_B64="$(base64 -w0 "${WORK}/t.key")"

cd "${WORK}"

echo "[entrypoint-test] no TLS material -> plaintext, node reached"
out=$(env -u SGEE_TLS_CA_CERT_B64 -u SGEE_TLS_CERT_B64 -u SGEE_TLS_KEY_B64 \
      ./ep.sh 2>&1); rc=$?
check "unset trio starts" 0 "${rc}"
grep -q "REACHED_NODE" <<<"${out}" || { echo "  FAIL  node was not reached"; fail=1; }
grep -qi "plaintext" <<<"${out}" || { echo "  FAIL  plaintext was not announced"; fail=1; }

# Every partial combination, not just one: a check that only covers "two of
# three" would pass an entrypoint that admitted exactly one.
echo "[entrypoint-test] partial trios -> refuse, node NOT reached"
for combo in "CA" "CERT" "KEY" "CA CERT" "CA KEY" "CERT KEY"; do
    envargs=()
    for which in ${combo}; do
        case "${which}" in
            CA)   envargs+=("SGEE_TLS_CA_CERT_B64=${CA_B64}") ;;
            CERT) envargs+=("SGEE_TLS_CERT_B64=${CA_B64}") ;;
            KEY)  envargs+=("SGEE_TLS_KEY_B64=${KEY_B64}") ;;
        esac
    done
    out=$(env -u SGEE_TLS_CA_CERT_B64 -u SGEE_TLS_CERT_B64 -u SGEE_TLS_KEY_B64 \
          "${envargs[@]}" ./ep.sh 2>&1); rc=$?
    check "partial(${combo}) refuses" 1 "${rc}"
    if grep -q "REACHED_NODE" <<<"${out}"; then
        echo "  FAIL  partial(${combo}) started the node anyway"; fail=1
    fi
done

echo "[entrypoint-test] full trio -> stage files and start"
rm -rf "${STAGE}"
out=$(SGEE_TLS_CA_CERT_B64="${CA_B64}" SGEE_TLS_CERT_B64="${CA_B64}" \
      SGEE_TLS_KEY_B64="${KEY_B64}" ./ep.sh 2>&1); rc=$?
check "full trio starts" 0 "${rc}"
grep -q "REACHED_NODE" <<<"${out}" || { echo "  FAIL  node was not reached"; fail=1; }
for f in ca.pem cert.pem key.pem; do
    if [ ! -s "${STAGE}/${f}" ]; then
        echo "  FAIL  ${f} was not staged"; fail=1
    fi
done
# The private key must not be world- or group-readable.
if [ -f "${STAGE}/key.pem" ]; then
    mode=$(stat -c '%a' "${STAGE}/key.pem")
    [ "${mode}" = "600" ] || { echo "  FAIL  key.pem mode is ${mode}, expected 600"; fail=1; }
fi

echo "[entrypoint-test] corrupt base64 -> refuse rather than stage garbage"
out=$(SGEE_TLS_CA_CERT_B64='!!!not base64!!!' SGEE_TLS_CERT_B64="${CA_B64}" \
      SGEE_TLS_KEY_B64="${KEY_B64}" ./ep.sh 2>&1); rc=$?
check "corrupt base64 refuses" 1 "${rc}"
if grep -q "REACHED_NODE" <<<"${out}"; then
    echo "  FAIL  corrupt material started the node anyway"; fail=1
fi

if [ "${fail}" -eq 0 ]; then
    echo "[entrypoint-test] OK"
else
    echo "[entrypoint-test] FAILED"
fi
exit "${fail}"
