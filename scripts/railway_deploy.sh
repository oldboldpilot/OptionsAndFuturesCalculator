#!/usr/bin/env bash
#
# Deploy the backend to Railway without `railway up`.
#
# WHY THIS EXISTS
#
# `railway up` enforces a fixed ~30 s deadline on the POST that uploads the
# source archive. Railway's /up endpoint has been answering in ~25 s even for a
# 1-byte body (measured), so any real payload pushes the request past the
# deadline. The CLI then aborts mid-transfer and prints:
#
#     error sending request for url (.../up?serviceId=...)
#     Caused by: operation timed out
#
# which is misleading twice over. First, the request has already reached
# Railway — a deployment record exists. Second, the outcome is a coin flip: if
# enough of the archive arrived, the deploy proceeds and SUCCEEDS despite the
# error (deployment 30647b56 did exactly that); if it did not, Railway finds no
# railway.json, silently falls back to its RAILPACK auto-detector, finds nothing
# it can build, and the deployment FAILS with no build attached.
#
# That fallback is the reliable diagnostic, with one wrinkle worth stating
# because it is easy to misread: `builder` reads RAILPACK on EVERY deployment at
# first. It is the placeholder Railway shows while the archive is still being
# extracted, and it flips to DOCKERFILE once railway.json is found. So RAILPACK
# is only evidence of a truncated upload once the deployment has stopped moving
# — a deployment that sits at INITIALIZING/RAILPACK and then FAILS never
# received a complete archive. This script waits for the transition rather than
# sampling once, and trusts that over any exit code.
#
# Three hypotheses were tested and disproved before landing here: slow indexing
# of the 16 GB tree (parked the 13 GB backend/build — no change), an expired
# credential (re-authenticated — no change), and payload size (trimmed 62 MB to
# 36 MB — no change, still 32 s). The latency is server-side and not ours to fix.
#
# THE FIX
#
# Upload the same archive to the same endpoint with curl, which has no such
# deadline. This is what `railway up` does minus the timeout, so it is not a
# workaround of Railway's API — it is the same call, allowed to finish.
#
#     scripts/railway_deploy.sh [--dry-run] [--service-name NAME]
#
# --dry-run builds and validates the archive without uploading.
# --service-name overrides the destination check below. Say it out loud.
#
# THE DESTINATION IS CHECKED, NOT ASSUMED
#
# Everything below verifies the ARCHIVE — railway.json, the Dockerfile, the
# sensen closure — and until 2026-08-12 nothing verified WHERE it was going. The
# service came from `~/.railway/config.json`, i.e. from whatever `railway link`
# last pointed at, which is ambient state with no relationship to what is being
# deployed. This project has four services in one environment: the engine and
# three SGEE queue nodes. Running this while linked to `sgee-queue-3` — which is
# what it was linked to during the 2026-08-12 session — would have uploaded a
# verified, perfectly-formed ENGINE archive onto a queue node, and every check
# in this file would have passed while doing it.
#
# The engine's railway.json at the repo root would then have replaced that
# node's image and `numReplicas`, and Railway forbids volumes on a service with
# replicas, so the failure would not even have been a clean one.
#
# So: resolve the linked service id to its NAME and require it to be the
# engine's. `deploy/queue-node/deploy.sh` is the other direction of the same
# rule — it stages its own tree and passes `--service` explicitly, precisely
# because the root railway.json describes the engine.

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

# The one service this script is allowed to deploy to. This archive is an engine
# archive; sending it anywhere else is a mistake, not a configuration.
EXPECTED_SERVICE_NAME="options-calculator-backend"

DRY_RUN=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run)      DRY_RUN=1; shift ;;
        --service-name) EXPECTED_SERVICE_NAME="${2:?--service-name needs a value}"; shift 2 ;;
        *) echo "usage: $0 [--dry-run] [--service-name NAME]" >&2; exit 2 ;;
    esac
done

CONFIG="${HOME}/.railway/config.json"
[[ -f "$CONFIG" ]] || { echo "ERROR: $CONFIG not found — run 'railway login'." >&2; exit 1; }

read -r PROJECT ENVIRONMENT SERVICE < <(python3 -c "
import json, os, sys
d = json.load(open(os.path.expanduser('~/.railway/config.json')))
p = d.get('projects', {}).get(os.getcwd())
if not p:
    sys.exit('ERROR: this directory is not linked to a Railway project.')
print(p['project'], p['environment'], p['service'])
")

# --------------------------------------------------------------------------
# Destination check. See the header: this runs BEFORE the archive is built, so
# a wrong link costs a message rather than a 36 MB upload onto a queue node.
# --------------------------------------------------------------------------
LINKED_NAME="$(railway status --json 2>/dev/null | SERVICE_ID="$SERVICE" python3 -c "
import json, os, sys
try:
    d = json.load(sys.stdin)
except Exception:
    sys.exit(0)          # print nothing; the check below reports it
want = os.environ['SERVICE_ID']
for e in d.get('services', {}).get('edges', []):
    n = e.get('node', {})
    if n.get('id') == want:
        print(n.get('name', ''))
        break
")"

if [[ -z "$LINKED_NAME" ]]; then
    echo "ERROR: could not resolve the linked service id to a name." >&2
    echo "       Refusing to upload an engine archive to an unidentified service." >&2
    echo "       Check 'railway status', then 'railway link'." >&2
    exit 1
fi

if [[ "$LINKED_NAME" != "$EXPECTED_SERVICE_NAME" ]]; then
    echo "ERROR: linked service is '${LINKED_NAME}', expected '${EXPECTED_SERVICE_NAME}'." >&2
    echo "" >&2
    echo "       This builds an ENGINE archive: the repo-root railway.json names" >&2
    echo "       backend/Dockerfile and numReplicas. Deploying it to any other" >&2
    echo "       service replaces that service's image with the engine's." >&2
    echo "" >&2
    echo "       Queue nodes have their own path: deploy/queue-node/deploy.sh" >&2
    echo "       Otherwise: railway link   (or pass --service-name to override)" >&2
    exit 1
fi

echo "destination: ${LINKED_NAME} (environment ${ENVIRONMENT})"

# The CLI refreshes this token lazily and only on demand, so poke it first: a
# read command forces a refresh if the access token has expired, and the token
# we then read from the config is current.
railway whoami >/dev/null 2>&1 || true
TOKEN="$(python3 -c "
import json, os, sys
u = json.load(open(os.path.expanduser('~/.railway/config.json')))['user']
t = u.get('accessToken')
if not t:
    sys.exit('ERROR: no accessToken in ~/.railway/config.json — run railway login.')
print(t)
")"

# --------------------------------------------------------------------------
# Build the archive, honouring .railwayignore.
# --------------------------------------------------------------------------
ARCHIVE="$(mktemp -t railway-upload-XXXXXX.tar.gz)"
EXCLUDES="$(mktemp -t railway-excludes-XXXXXX)"
trap 'rm -f "$ARCHIVE" "$EXCLUDES"' EXIT

grep -vE '^\s*(#|$)' .railwayignore > "$EXCLUDES"

tar --exclude-from="$EXCLUDES" --exclude-vcs-ignores -czf "$ARCHIVE" . 2>/dev/null || \
tar --exclude-from="$EXCLUDES" -czf "$ARCHIVE" .

SIZE=$(stat -c %s "$ARCHIVE")
printf 'archive: %.1f MB\n' "$(echo "$SIZE/1048576" | bc -l)"

# railway.json is what selects the Dockerfile builder. If it is not in the
# archive, Railway falls back to RAILPACK and the deploy fails with no build —
# so verify it is present before spending the upload.
#
# The listing is materialised once rather than piped per check. `tar … | grep -q`
# looks obvious and is wrong here: grep -q exits at the first match, tar takes
# SIGPIPE, and under `set -o pipefail` the pipeline reports failure even though
# the match succeeded. Whether it bites depends on whether tar has finished
# writing, so it fails intermittently and on a different entry each time.
LIST="$(mktemp -t railway-list-XXXXXX)"
trap 'rm -f "$ARCHIVE" "$EXCLUDES" "$LIST"' EXIT
tar -tzf "$ARCHIVE" > "$LIST"

for required in ./railway.json ./backend/Dockerfile ./backend/CMakeLists.txt ./backend/src/main.cpp; do
    grep -qxF "$required" "$LIST" || {
        echo "ERROR: $required missing from the archive." >&2; exit 1; }
done

# The engine cannot build without the sensen modules it imports.
for required in ./backend/sensen/src/options.cppm ./backend/sensen/src/portfolio.cppm; do
    grep -qxF "$required" "$LIST" || {
        echo "ERROR: $required missing from the archive." >&2; exit 1; }
done

echo "archive verified: $(wc -l < "$LIST") entries, railway.json + Dockerfile + sensen closure present"

if [[ $DRY_RUN -eq 1 ]]; then
    echo "dry run — not uploading."
    exit 0
fi

# --------------------------------------------------------------------------
# Upload. No --max-time: letting it finish is the entire point.
# --------------------------------------------------------------------------
URL="https://backboard.railway.com/project/${PROJECT}/environment/${ENVIRONMENT}/up?serviceId=${SERVICE}"
echo "uploading to Railway (no client deadline)..."

RESPONSE="$(curl -sS -X POST "$URL" \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Content-Type: multipart/form-data" \
    --data-binary "@${ARCHIVE}" \
    -w '\n%{http_code} %{time_total}')"

HTTP_CODE="$(tail -1 <<<"$RESPONSE" | cut -d' ' -f1)"
ELAPSED="$(tail -1 <<<"$RESPONSE" | cut -d' ' -f2)"
BODY="$(sed '$d' <<<"$RESPONSE")"

echo "http=${HTTP_CODE} elapsed=${ELAPSED}s"
[[ -n "$BODY" ]] && echo "response: ${BODY:0:400}"

if [[ "$HTTP_CODE" != "200" ]]; then
    echo "ERROR: upload rejected." >&2
    exit 1
fi

# --------------------------------------------------------------------------
# Confirm the archive actually landed. RAILPACK here means truncated upload.
# --------------------------------------------------------------------------
echo "confirming the deployment picked up the Dockerfile builder..."
for _ in $(seq 1 20); do
    INFO="$(railway deployment list --json 2>/dev/null | python3 -c "
import json, sys
try:
    d = json.load(sys.stdin)[0]
except Exception:
    sys.exit(0)
m = d.get('meta', {})
b = (m.get('serviceManifest') or m.get('fileServiceManifest') or {}).get('build', {}).get('builder', '?')
print(d['id'], d['status'], b)
")"
    [[ -n "$INFO" ]] && { echo "  $INFO"; }
    case "$INFO" in
        *DOCKERFILE*)
            DEPLOY_ID="${INFO%% *}"
            echo "upload landed intact — Railway is building."
            echo ""
            echo "Deployment id: ${DEPLOY_ID}"
            echo ""
            echo "  Follow THIS build (the id is not optional):"
            echo "      railway logs --service ${LINKED_NAME} --build ${DEPLOY_ID}"
            echo ""
            # Said here because it has already cost 25 minutes of believing a
            # deploy had landed. `railway logs --build` WITHOUT a deployment id
            # shows the newest deployment Railway has a build log for, which
            # while a new build is running is the PREVIOUS one -- so it prints
            # "[3/3] Healthcheck succeeded!" describing the deploy you are
            # replacing. It looks exactly like success.
            # Derive the expected count from railway.json rather than stating a
            # number here. This line said "numReplicas 3 => 3 mortgage + 3
            # strategy" for four days after numReplicas dropped to 2, which
            # makes the gate pass at 4 while telling the reader to expect 6.
            _replicas="$(sed -n 's/.*"numReplicas"[[:space:]]*:[[:space:]]*\([0-9]\+\).*/\1/p' \
                          "${REPO_ROOT}/railway.json" 2>/dev/null | head -1)"
            _replicas="${_replicas:-1}"
            echo "  Then confirm the CUTOVER, which the build log cannot tell you."
            echo "  Name the deployment -- 'railway logs --service' REPLAYS THE LAST"
            echo "  DEAD SESSION'S SCROLLBACK when nothing is running, so grepping it"
            echo "  passes against the container you are replacing:"
            echo "      railway logs --deployment ${DEPLOY_ID} | grep -c 'model is LOADED'"
            echo ""
            echo "  A green healthcheck is not evidence the new image is serving."
            echo "  A fresh boot sequence is. Expect one 'model is LOADED' line per"
            echo "  replica per assistant -- numReplicas ${_replicas} => $((_replicas * 2)) lines"
            echo "  (${_replicas} mortgage + ${_replicas} strategy) -- timestamped after this upload."
            exit 0 ;;
    esac
    sleep 10
done

echo "WARNING: newest deployment is not using the Dockerfile builder." >&2
echo "         That means the archive did not arrive intact. Re-run." >&2
exit 1
