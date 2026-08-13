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
        "backend/queue-node-entrypoint.sh"
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

# Refuse to deploy a consensus node onto ephemeral storage.
#
# A node with no volume at /data starts, elects, accepts writes and answers
# /healthz exactly like a correct one. The difference appears only at the next
# restart, when its Raft log, currentTerm and votedFor -- the state Raft must
# fsync before replying to any RPC -- turn out to have been on the container
# filesystem and are gone. It then rejoins with an empty log and votes as if it
# had never seen the cluster. Nothing in the node's own output can tell you
# this; the mount is a property of the service, not of the process.
#
# So it is checked HERE, before an upload, rather than trusted. `railway volume
# list` prints one stanza per volume: the volume name, "Attached to: <service>"
# and "Mount path: <path>". Both facts must hold together for THIS service,
# which is why the check reads the stanza rather than grepping the whole output
# for the two strings independently -- another service's volume mounted at
# /data would otherwise satisfy a naive grep.
require_volume() {
    local svc="$1"
    local listing
    if ! listing="$(railway volume list 2>&1)"; then
        echo "FATAL: could not list volumes (is the project linked?):" >&2
        echo "${listing}" >&2
        exit 1
    fi
    # Collapse each stanza to one line so the pairing survives the match.
    if printf '%s\n' "${listing}" \
        | awk -v RS='' '{gsub(/\n/, " | "); print}' \
        | grep -q "Attached to: ${svc} .*Mount path: /data"; then
        echo "[deploy] ${svc}: volume attached at /data"
        return 0
    fi
    echo "FATAL: ${svc} has no volume mounted at /data." >&2
    echo "A consensus node without one runs, elects and looks healthy, and loses its" >&2
    echo "Raft log, currentTerm and votedFor at the next restart -- then rejoins and" >&2
    echo "votes as though it had never seen the cluster. Attach one before deploying:" >&2
    echo "    railway volume add --service ${svc} --mount-path /data" >&2
    echo "" >&2
    echo "Volumes currently in this project:" >&2
    printf '%s\n' "${listing}" >&2
    exit 1
}

# Wait until a node is genuinely back, and say what "back" means.
#
# THE GATE IS THE DEPLOYMENT'S OWN STATUS, NOT THE LOG TEXT, and the previous
# version of this function is why.
#
# It waited for a `Raft baseline:` line and its comment asserted that line was
# "ONLY the new binary emits" -- true on the day it was written, and false ever
# after, because that binary has since BEEN the deployed one. `railway logs`
# returns the last session's output when nothing is running, so the gate matched
# a leftover from the previous container and returned success for a node that
# never booted. On 2026-08-12 it reported "all three nodes rolled, one at a time,
# each proven up before the next" while every deployment was still BUILDING and
# the two before them had FAILED their healthcheck. Nothing was proven; the
# sentence was produced by a grep against a dead node's scrollback.
#
# That is the same trap this script's own header documents for
# `railway logs --build`, arriving through the runtime log instead. A marker
# chosen because "no previous binary could produce it" has a shelf life of
# exactly one deploy, so it cannot be the gate. The deployment id can: it is
# minted by THIS upload and its status is Railway's own answer about THIS
# rollout.
#
# Deliberately NOT gated on the node becoming leader. Followers are healthy
# participants; requiring leadership here would hang forever on two of three
# nodes and is the same mistake as putting leadership behind /healthz.
await_healthy() {
    local svc="$1"
    local waited=0
    local interval=20
    local deadline=2100   # 35 min: the image compiles gRPC from source
    local status=""

    echo "[deploy] waiting for ${svc}'s newest deployment to leave BUILDING"
    while [ "${waited}" -lt "${deadline}" ]; do
        # First data row of `deployment list` is the newest deployment.
        status="$(railway deployment list --service "${svc}" --environment "${ENVIRONMENT}" 2>/dev/null \
                  | grep -oE '\| (BUILDING|DEPLOYING|INITIALIZING|SUCCESS|FAILED|CRASHED|REMOVED|SLEEPING) \|' \
                  | head -1 | tr -d '| ' || true)"

        case "${status}" in
            SUCCESS)
                echo "[deploy] ${svc}: deployment SUCCESS"
                # Only NOW is the log worth reading, and only for the extra
                # detail -- never as the proof itself.
                local logs boots
                logs="$(railway logs --service "${svc}" 2>/dev/null || true)"
                printf '%s' "${logs}" | grep 'Raft baseline:' | tail -1 | sed 's/^/    /'
                boots="$(printf '%s' "${logs}" | grep -c 'Queue node initialized successfully' || true)"
                if [ "${boots}" -gt 1 ]; then
                    echo "WARNING: ${svc} shows ${boots} boots in its recent log -- it may be restarting." >&2
                fi
                return 0
                ;;
            FAILED|CRASHED)
                echo "FATAL: ${svc}'s newest deployment reported ${status}." >&2
                echo "  A healthcheck failure here means the container started and did not stay up." >&2
                echo "  Read the CONTAINER log, not the build log:" >&2
                echo "    railway logs --service ${svc}" >&2
                railway logs --service "${svc}" 2>/dev/null | grep -iE 'error|fatal|terminate called' \
                    | tail -5 | sed 's/^/    /' >&2 || true
                return 1
                ;;
            "")
                echo "WARNING: could not read a deployment status for ${svc}; retrying" >&2
                ;;
        esac

        sleep "${interval}"
        waited=$((waited + interval))
        [ $((waited % 120)) -eq 0 ] && echo "[deploy]   ...${waited}s (${status:-unknown})"
    done

    echo "FATAL: ${svc}'s deployment did not reach SUCCESS within ${deadline}s (last: ${status:-unknown})." >&2
    return 1
}

deploy_one() {
    local n="$1"
    local svc="sgee-queue-${n}"
    local dest
    dest="$(mktemp -d)"
    trap 'rm -rf "${dest}"' RETURN

    require_volume "${svc}"

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
        # ROLLING, and actually serialized. This branch used to fire three
        # uploads back to back and print "Confirm it is healthy before the
        # next" -- advice to a human, in a script nobody was reading while it
        # ran. `railway up --detach` returns when the UPLOAD is accepted, not
        # when the container is up, so all three builds started together and
        # all three containers restarted at roughly the same moment.
        #
        # For a stateless service that is a brief blip. For a three-node Raft
        # cluster it is the loss of quorum: two nodes down at once means no
        # leader can be elected and every write is refused until they come
        # back. The whole reason this is three separate services rather than
        # numReplicas: 3 is to be able to lose one at a time.
        #
        # await_healthy() below is the gate. One node is replaced, proven up,
        # and only then is the next touched -- so at most one node is ever
        # missing and the other two keep the quorum.
        for n in 1 2 3; do
            deploy_one "${n}"
            await_healthy "sgee-queue-${n}" || {
                echo "FATAL: sgee-queue-${n} did not come back. STOPPING the rollout here." >&2
                echo "The remaining nodes still run the previous binary, which is the safe" >&2
                echo "state -- do not continue until this node is understood." >&2
                exit 1
            }
        done
        echo "[deploy] all three nodes rolled, one at a time, each proven up before the next."
        ;;
    *) echo "usage: $0 <1|2|3|all>" >&2; exit 1 ;;
esac
