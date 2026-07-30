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
#     scripts/railway_deploy.sh [--dry-run]
#
# --dry-run builds and validates the archive without uploading.

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

DRY_RUN=0
[[ "${1:-}" == "--dry-run" ]] && DRY_RUN=1

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
        *DOCKERFILE*) echo "upload landed intact — Railway is building."; exit 0 ;;
    esac
    sleep 10
done

echo "WARNING: newest deployment is not using the Dockerfile builder." >&2
echo "         That means the archive did not arrive intact. Re-run." >&2
exit 1
