#!/usr/bin/env bash
#
# Deploy the assistant-worker: the replica that HOLDS the assistant weights and
# leases from the shared inference queue. See README.md in this directory.
#
# @author Olumuyiwa Oluwasanmi
#
# Usage: deploy/assistant-worker/deploy.sh [--dry-run]

set -euo pipefail

PROJECT="59242db9-5174-4722-aea3-4df2a8f54f99"          # fearless-amazement
ENVIRONMENT="273bbc5d-b59f-4d36-9ef4-69b5d79dabaf"      # production
SERVICE="f3161ac3-7bd4-4582-9e47-3421ad48b5a2"          # assistant-worker
SERVICE_NAME="assistant-worker"

# Variables this service needs, and the two that are easy to miss:
#
#   MODEL_URL / MODEL_SHA256 / MORTGAGE_MODEL_URL / MORTGAGE_MODEL_SHA256
#   INFERENCE_QUEUE (+ SGEE_PEERS and the three SGEE_TLS_*_B64, or DATABASE_URL)
#   MORTGAGE_GRAMMAR, SENSEN_QKV_FUSION
#   BUCKET_ACCESS_KEY_ID / BUCKET_SECRET_ACCESS_KEY   <-- see below
#
# The bucket credentials are not optional. backend/Dockerfile fetches both
# GGUFs from the private Railway bucket with SigV4 and, when they are empty,
# silently falls through to a plain wget the bucket rejects -- the build then
# fails MINUTES later inside a 60-line shell step whose error is about the
# checksum, not about credentials. Copy them from the engine.

DRY_RUN=0
[[ "${1:-}" == "--dry-run" ]] && DRY_RUN=1

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HERE="${REPO_ROOT}/deploy/assistant-worker"

# The destination is hardcoded above rather than taken from `railway link`,
# which is ambient state with no relationship to what is being deployed. The
# engine's own script learned this the hard way: the CLI was once linked to
# sgee-queue-3, and a bare deploy would have pushed the engine's config onto a
# queue node. Verify anyway -- a service can be renamed or recreated.
ACTUAL="$(railway status --json 2>/dev/null \
    | python3 -c "
import json,sys,os
want=os.environ['SVC']
d=json.load(sys.stdin)
for e in d.get('services',{}).get('edges',[]):
    if e['node']['id']==want: print(e['node']['name']); break
" SVC="$SERVICE" 2>/dev/null || true)"
if [[ -n "$ACTUAL" && "$ACTUAL" != "$SERVICE_NAME" ]]; then
    echo "ERROR: service id ${SERVICE} is now named '${ACTUAL}', not '${SERVICE_NAME}'." >&2
    echo "       Refusing to deploy into a service this script cannot identify." >&2
    exit 1
fi

# Stage the tree. Railway reads railway.json from the ROOT of the upload, and
# the one at the repository root describes the ENGINE (numReplicas 2, model
# URLs set). Uploading that here would give the worker the engine's replica
# count -- so this directory's railway.json is copied over it. The CLI's
# per-service escape hatches (railwayConfigFile, rootDirectory) are not
# reachable from the CLI, which is why this is a staged copy and not a flag.
STAGE="$(mktemp -d -t assistant-worker-XXXXXX)"
trap 'rm -rf "${STAGE}"' EXIT

# Same exclusions as the queue node's stage, and for the same reason: the
# untrimmed tree has already blown the upload deadline once. Not one excluded
# byte is compiled -- backend/CMakeLists.txt FORCES sensen's BUILD_TESTS,
# BUILD_PYTHON_BINDINGS, BUILD_BENCHMARKS and BUILD_EXAMPLES OFF.
rsync -a --quiet \
    --exclude 'build/' --exclude 'build-*/' --exclude '*.log' \
    --exclude 'models/*.gguf' --exclude 'node_modules/' --exclude '__pycache__/' \
    --exclude '.git' --exclude '.venv/' \
    --exclude 'external/SGEE/web-lsp/' --exclude 'external/SGEE/vscode-extension/' \
    --exclude 'BigBrotherAnalytics/' \
    --exclude 'sensen/external/CosyVoice/' --exclude 'sensen/eval_samples/' \
    --exclude 'sensen/demo_qwen3/' --exclude 'sensen/benchmarks/' \
    --exclude 'sensen/tests/' --exclude 'sensen/python/' \
    --exclude 'sensen/examples/' --exclude 'sensen/docs/' \
    "${REPO_ROOT}/backend/" "${STAGE}/backend/"
cp "${HERE}/railway.json" "${STAGE}/railway.json"

# Dangling symlinks abort the Railway indexer outright, with a message naming a
# vendored path and no hint that the upload never started. Prune them from the
# STAGE only; the working tree is untouched.
pruned=$(find "${STAGE}" -xtype l -print -delete | wc -l)
[[ "${pruned}" -gt 0 ]] && echo "[deploy] pruned ${pruned} dangling symlink(s)"

# backend/models/ must EXIST in the stage: backend/Dockerfile does
# `COPY backend/models/ /model-staged/`, which fails the build outright if the
# directory is absent ("/backend/models: not found", several minutes in, with
# nothing pointing at the exclusion list that ate it). Only the .gguf files are
# excluded -- they are 639 MB each and the image fetches the real weights from
# MODEL_URL at build time; the staging path exists so a LOCAL docker build can
# use a locally held checkpoint.
for required in backend/Dockerfile backend/src/main.cpp backend/models railway.json; do
    [[ -e "${STAGE}/${required}" ]] || { echo "ERROR: ${required} missing from stage" >&2; exit 1; }
done
grep -q '"numReplicas": 1' "${STAGE}/railway.json" || {
    echo "ERROR: staged railway.json is not the worker's (expected numReplicas 1)" >&2; exit 1; }
echo "[deploy] stage verified: worker railway.json, Dockerfile, sources present"

ARCHIVE="$(mktemp -t assistant-worker-upload-XXXXXX.tar.gz)"
trap 'rm -rf "${STAGE}"; rm -f "${ARCHIVE}"' EXIT
( cd "${STAGE}" && tar -czf "${ARCHIVE}" . )
echo "[deploy] archive $(du -h "${ARCHIVE}" | cut -f1)"

if [[ $DRY_RUN -eq 1 ]]; then echo "dry run — not uploading."; exit 0; fi

# curl, not `railway up`: the CLI enforces a ~30s client deadline that
# Railway's own /up endpoint has been exceeding for any real payload. A
# timeout there is a statement about the CLIENT -- Railway can accept the
# upload and create a deployment while the CLI reports failure.
TOKEN="$(python3 -c "import json;print(json.load(open('$HOME/.railway/config.json'))['user']['accessToken'])")"
URL="https://backboard.railway.com/project/${PROJECT}/environment/${ENVIRONMENT}/up?serviceId=${SERVICE}"
echo "[deploy] uploading (no client deadline)..."
RESPONSE="$(curl -sS -X POST "$URL" \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Content-Type: multipart/form-data" \
    --data-binary "@${ARCHIVE}" \
    -w '\n%{http_code}')"
CODE="$(tail -1 <<<"$RESPONSE")"
BODY="$(sed '$d' <<<"$RESPONSE")"
echo "[deploy] http=${CODE}"
[[ -n "$BODY" ]] && echo "[deploy] ${BODY:0:300}"
[[ "$CODE" == "200" ]] || { echo "ERROR: upload failed" >&2; exit 1; }

echo
echo "Gate on the deployment list, never on log text:"
echo "    railway deployment list --service ${SERVICE_NAME}"
echo
echo "Then confirm this replica HOLDS the weights (two lines, one per assistant):"
echo "    railway logs --service ${SERVICE_NAME} --deployment <id> | grep 'model is LOADED'"
