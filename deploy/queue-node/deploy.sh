#!/usr/bin/env bash
#
# Deploy one SGEE queue node, or all three in order.
#
# @author Olumuyiwa Oluwasanmi
#
# Usage: deploy/queue-node/deploy.sh <1|2|3|all>
#
# See README.md in this directory for why the upload is staged rather than run
# from the repository root.

set -euo pipefail

PROJECT_ID="59242db9-5174-4722-aea3-4df2a8f54f99"   # fearless-amazement
ENVIRONMENT="production"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HERE="${REPO_ROOT}/deploy/queue-node"

# `railway up` has failed on this repository before with "operation timed out",
# and the cause was payload size: local build directories travel with the
# upload because the CLI archives the WORKING TREE, not git. backend/build and
# backend/build-int8 alone are 17 GB. Excluding by shape rather than by literal
# path is deliberate -- a new build directory appearing locally must not be
# able to turn into a deploy outage.
stage() {
    local dest="$1"
    mkdir -p "${dest}/backend"
    # These patterns are EXACT on purpose. An earlier version used 'build*/',
    # which also matched `builder/` and silently removed
    # src/builder/fluent.cppm and tests/builder/*.cpp from the upload. The
    # container then failed to CONFIGURE, twenty minutes in, with "Cannot find
    # source file" -- a message that names the file and says nothing about the
    # exclude list that ate it. Do not reintroduce a glob here.
    #
    # The second block of exclusions is not a guess and not an optimisation:
    # it MIRRORS the backend/ entries of the repository's own .dockerignore,
    # which is the list the production engine image already builds this same
    # CMake project without. If configuring needed any of them, that image
    # would not build either.
    #
    # It is also load-bearing. `railway up` TIMED OUT uploading the untrimmed
    # 144 MB to the second service after succeeding on the first -- the
    # upload-deadline trap this repository has met before. Trimmed, the payload
    # is 59 MB and not one excluded byte is compiled: backend/CMakeLists.txt
    # FORCES sensen's BUILD_TESTS, BUILD_PYTHON_BINDINGS, BUILD_BENCHMARKS and
    # BUILD_EXAMPLES OFF (lines 406-457), so those add_subdirectory calls never
    # run, and docs/ is never added at all.
    #
    # Excluding CosyVoice also removes the dangling symlinks at their source
    # rather than only pruning them afterwards.
    rsync -a --quiet \
        --exclude 'build/' --exclude 'build-*/' --exclude '*.log' \
        --exclude 'models/' --exclude 'node_modules/' --exclude '__pycache__/' \
        --exclude '.git' --exclude '.venv/' \
        --exclude 'external/SGEE/web-lsp/' --exclude 'external/SGEE/vscode-extension/' \
        --exclude 'BigBrotherAnalytics/' \
        --exclude 'sensen/external/CosyVoice/' --exclude 'sensen/eval_samples/' \
        --exclude 'sensen/demo_qwen3/' --exclude 'sensen/benchmarks/' \
        --exclude 'sensen/tests/' --exclude 'sensen/python/' \
        --exclude 'sensen/examples/' --exclude 'sensen/docs/' \
        "${REPO_ROOT}/backend/" "${dest}/backend/"
    cp "${HERE}/railway.json" "${dest}/railway.json"

    # Dangling symlinks abort the Railway CLI's indexer outright:
    #   IO error for operation on .../Matcha-TTS/data: No such file or directory
    # and the upload never starts. The tree genuinely contains some -- vendored
    # third-party checkouts under backend/sensen/external point at paths that
    # were never populated -- and `rsync -a` faithfully reproduces them, broken
    # target and all. Nothing in this image's build follows them, so they are
    # removed from the STAGE only; the working tree is untouched.
    local dangling
    dangling=$(find "${dest}" -xtype l -print -delete | wc -l)
    if [ "${dangling}" -gt 0 ]; then
        echo "[deploy] pruned ${dangling} dangling symlink(s) from the staged upload"
    fi

    # A staged tree that is missing the Dockerfile or the SGEE sources produces
    # a Railway build failure several minutes later, with a message about a
    # missing file and no hint that the exclusion list ate it. Check here.
    # Includes files that an over-broad exclude has ALREADY removed once, not
    # only the obvious entry points. The check is worth little if it only
    # confirms the files nobody was going to lose.
    local required=(
        "backend/Dockerfile.queue-node"
        "backend/CMakeLists.txt"
        "backend/external/SGEE/CMakeLists.txt"
        "backend/external/SGEE/tools/sgee_queue_node.cpp"
        "backend/external/SGEE/src/builder/fluent.cppm"
        "backend/external/SGEE/tests/builder/builder_test.cpp"
        "backend/external/SGEE/tests/integration/queue_node_client.cpp"
        "backend/sensen/src/gp_ara_interfaces.cppm"
        "railway.json"
    )
    local missing=0
    for f in "${required[@]}"; do
        if [ ! -f "${dest}/${f}" ]; then
            echo "FATAL: staged upload is missing ${f}" >&2
            missing=1
        fi
    done
    [ "${missing}" -eq 0 ] || exit 1

    local mb
    mb=$(du -sm "${dest}" | awk '{print $1}')
    echo "[deploy] staged upload: ${mb} MB"
    if [ "${mb}" -gt 400 ]; then
        echo "FATAL: staged upload is ${mb} MB. The CLI has timed out on this" >&2
        echo "repository at far less. Something large slipped past the exclude" >&2
        echo "list -- find it before deploying, do not raise this ceiling." >&2
        exit 1
    fi
}

deploy_one() {
    local n="$1"
    local svc="sgee-queue-${n}"
    local dest
    dest="$(mktemp -d)"
    trap 'rm -rf "${dest}"' RETURN

    echo "[deploy] staging for ${svc}"
    stage "${dest}"

    # --detach, not --ci. The image compiles gRPC from source, so a build is
    # tens of minutes; streaming it holds a terminal open for the whole time
    # and tells you nothing you cannot get from `railway logs --build`. Poll
    # /healthz and /statusz instead -- those answer the question that matters,
    # which is whether the node joined, not whether it compiled.
    # Retried, because the upload times out intermittently and succeeds on the
    # next attempt with a byte-identical payload. Observed at 144 MB (every
    # time) and still at 59 MB (about one attempt in three), so it is not
    # purely size -- `railway up` has a fixed deadline that a slow leg of the
    # transfer can exceed. Failing the whole deploy on the first timeout means
    # a human retries by hand and learns nothing.
    echo "[deploy] uploading to ${svc}"
    local attempt
    for attempt in 1 2 3; do
        if ( cd "${dest}" && railway up \
                --project "${PROJECT_ID}" \
                --environment "${ENVIRONMENT}" \
                --service "${svc}" \
                --detach \
                --message "sgee queue node ${n}" ); then
            return 0
        fi
        echo "[deploy] upload attempt ${attempt} failed; retrying"
        sleep 10
    done
    echo "FATAL: three upload attempts to ${svc} all failed" >&2
    return 1
}

case "${1:?usage: $0 <1|2|3|all|stage DIR>}" in
    # Staging is exposed on its own so the image can be built locally against
    # the EXACT tree Railway will receive. A Dockerfile that builds from the
    # repository root and fails from the staged upload is a difference worth
    # finding on this machine rather than in a build log.
    stage) stage "${2:?usage: $0 stage <dir>}" ;;
    1|2|3) deploy_one "$1" ;;
    all)
        for n in 1 2 3; do
            deploy_one "${n}"
            echo "[deploy] node ${n} uploaded. Confirm it is healthy before the next."
        done
        ;;
    *) echo "usage: $0 <1|2|3|all>" >&2; exit 1 ;;
esac
