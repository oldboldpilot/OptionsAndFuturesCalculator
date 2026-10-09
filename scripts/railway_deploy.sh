#!/usr/bin/env bash
#
# Deploy the engine (options-calculator-backend) to Railway without `railway up`.
#
#     scripts/railway_deploy.sh [--dry-run] [--service-name NAME] [--keep-archive FILE]
#
# --dry-run builds and validates the archive without uploading.
# --service-name overrides the destination check. Say it out loud.
# --keep-archive leaves the verified .tar.gz at FILE.
#
# THE UPLOAD IS NOT HERE. It is backend/sensen/tools/railway_upload.sh, shared with every other
# repository that deploys a service by upload (the mortgage assistant is the second). It carries
# why `railway up` is not used (a fixed ~30 s deadline on the POST), the destination guard (the
# linked service is resolved to its NAME and must be the engine's -- on 2026-08-12 the CLI was
# linked to a queue node), the tracked-files-kept archive rule, the required-path check and the
# wait for Railway to pick the Dockerfile builder. This file is what is the ENGINE's alone:
#
#   * the parameters: .railwayignore, the paths the engine cannot build without, no weights;
#   * the PREFLIGHT, which has to run after the destination is known and before the archive is
#     built (hence the tool's --check-destination);
#   * the CUTOVER advice, which is about this service's one assistant.
#
# The tool is a submodule file, so a checkout without `git submodule update --init backend/sensen`
# has no deploy script. That is a refusal below and not a fallback to a second copy.

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

# The one service this script is allowed to deploy to. This archive is an engine archive; sending
# it anywhere else is a mistake, not a configuration.
EXPECTED_SERVICE_NAME="options-calculator-backend"

DRY_RUN=()
KEEP_ARCHIVE=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run)      DRY_RUN=(--dry-run); shift ;;
        --service-name) EXPECTED_SERVICE_NAME="${2:?--service-name needs a value}"; shift 2 ;;
        --keep-archive) KEEP_ARCHIVE=(--keep-archive "${2:?--keep-archive needs a value}"); shift 2 ;;
        *) echo "usage: $0 [--dry-run] [--service-name NAME] [--keep-archive FILE]" >&2; exit 2 ;;
    esac
done

UPLOAD="${REPO_ROOT}/backend/sensen/tools/railway_upload.sh"
[[ -f "$UPLOAD" ]] || {
    echo "ERROR: ${UPLOAD} not found -- run 'git submodule update --init backend/sensen'." >&2
    exit 1; }

# What the engine's image cannot be built without. railway.json is what selects the Dockerfile
# builder; without it Railway falls back to RAILPACK and the deploy fails with no build. The sensen
# modules are what the engine imports, and the logger's BMI guard is a TRACKED file a .gitignore
# matches (`*.cmake`), the one the tracked-files-kept rule exists for. No model weights ship: the
# engine fetches its encoders at image build time, and `**/*.gguf` in .railwayignore is the rule
# that keeps them out -- --forbid is the tripwire if that rule ever stops working.
UPLOAD_ARGS=(
    --expect-service "$EXPECTED_SERVICE_NAME"
    --ignore .railwayignore
    --require railway.json
    --require backend/Dockerfile
    --require backend/CMakeLists.txt
    --require backend/src/main.cpp
    --require backend/sensen/external/cpp23-logger/cmake/ToolchainBMIGuard.cmake
    --require backend/sensen/src/options.cppm
    --require backend/sensen/src/portfolio.cppm
    --forbid '*.gguf'
)

# The destination first: a wrong link must cost a message, not a configure or a 36 MB upload.
"$UPLOAD" "${UPLOAD_ARGS[@]}" --check-destination || {
    echo "       Queue nodes have their own path: deploy/queue-node/deploy.sh" >&2
    exit 1; }

# --------------------------------------------------------------------------
# PREFLIGHT: does this repository still CONFIGURE without the trees the upload
# excludes?
#
# WHY THIS EXISTS. Deployment 797424f2 (2026-09-25) FAILED at CMake generate,
# before one file compiled, on
#     Cannot find source file: .../backend/sensen/tests/test_ring_completion_contract.cpp
# Nothing local was wrong and no local build could see it. `.railwayignore` and
# `.dockerignore` both drop `backend/sensen/tests`, on the stated grounds that
# backend/CMakeLists.txt FORCEs BUILD_TESTS off -- an invariant whose own comment
# records it as "verified by parking all four and re-configuring". That is a
# hand-check, it was true when it was made, and a submodule bump invalidated it
# by adding two test targets declared OUTSIDE `if(BUILD_TESTS)`.
#
# So the check is now performed rather than remembered: park the excluded trees
# and re-run configure, which is what the upload's builder will do. It is nearly
# free when backend/build is already configured -- the try_compile probes are
# cached, so it is a generate step of under a second.
#
# It SKIPS LOUDLY, never silently: an unconfigured build directory means the
# check did not run, and a skip that reads like a pass is the failure mode this
# whole script is written against.
#
# A SUBSHELL, so its EXIT trap (restore the parked trees) is its own.
# --------------------------------------------------------------------------
(
    # DERIVED FROM .railwayignore, not written out here. A hand-kept list beside a
    # hand-kept exclude list is two things to forget, and forgetting one of them is
    # what this preflight exists to catch. Every excluded path that is a directory
    # INSIDE the build's source root is parked; a path that no longer exists, or
    # that names a file, is skipped.
    mapfile -t PARKED_TREES < <(
        grep -vE '^\s*(#|$)' .railwayignore \
        | sed -E 's#/+$##' \
        | grep -E '^backend/' \
        | grep -vE '[*?\[]' \
        | while read -r cand; do [[ -d "$cand" ]] && printf '%s\n' "$cand"; done
    )
    if [[ ! -f backend/build/CMakeCache.txt ]]; then
        echo "PREFLIGHT SKIPPED: backend/build is not configured, so the"
        echo "  'does it configure without the excluded trees' check did NOT run."
        echo "  Configure it once (cmake -B backend/build -S backend) to enable this."
        exit 0
    fi
    PARK_DIR="$(mktemp -d -t railway-parked-XXXXXX)"
    # THE KEY IS THE WHOLE PATH, NOT THE BASENAME, AND THAT IS NOT A TIDY-UP.
    #
    # `.railwayignore` parks TWO trees called `docs` -- backend/sensen/docs and
    # backend/external/SGEE/docs. Keyed by basename, the first parks as
    # PARK/docs and the second `mv`s INTO it, becoming PARK/docs/docs. Restore
    # then moves the whole nest back to whichever path it tries first, and the
    # other tree is never restored at all: SGEE's docs/ silently ended up inside
    # sensen's, and SGEE's own was gone. That happened three times before it was
    # attributed -- it looks like an unrelated "docs moved" mystery because the
    # deploy that caused it had already exited and restored its trap.
    #
    # A path-derived key cannot collide, and the guard below turns any future
    # collision into a LOUD failure instead of a silent nesting.
    park_key() { local p="${1#./}"; printf '%s' "${p//\//__}"; }
    restore_parked() {
        local t k
        for t in "${PARKED_TREES[@]}"; do
            k="${PARK_DIR}/$(park_key "$t")"
            if [[ -d "$k" && ! -d "$t" ]]; then
                mkdir -p "$(dirname "$t")"
                mv "$k" "$t"
            fi
        done
        rm -rf "$PARK_DIR"
    }
    trap restore_parked EXIT
    for t in "${PARKED_TREES[@]}"; do
        [[ -d "$t" ]] || continue
        PARK_TARGET="${PARK_DIR}/$(park_key "$t")"
        if [[ -e "$PARK_TARGET" ]]; then
            echo "PARK COLLISION: '$t' would overwrite '$PARK_TARGET'." >&2
            echo "  Two excluded trees derived the same park key. Refusing rather" >&2
            echo "  than nesting one inside the other -- that is how a tree goes" >&2
            echo "  missing. Nothing has been uploaded." >&2
            exit 1
        fi
        mv "$t" "$PARK_TARGET"
    done
    PREFLIGHT_LOG="$(mktemp -t railway-preflight-XXXXXX.log)"
    set +e
    cmake -B backend/build -S backend -DCMAKE_BUILD_TYPE=Release > "$PREFLIGHT_LOG" 2>&1
    PREFLIGHT_RC=$?
    set -e
    restore_parked
    trap - EXIT
    if [[ $PREFLIGHT_RC -ne 0 ]]; then
        echo "PREFLIGHT FAILED: this tree does not CONFIGURE without the trees the" >&2
        echo "  upload excludes, so the image build would die at CMake generate" >&2
        echo "  exactly as deployment 797424f2 did. Nothing has been uploaded." >&2
        echo >&2
        grep -E 'CMake Error|Cannot find source file|^ +/' "$PREFLIGHT_LOG" | head -20 >&2
        echo >&2
        echo "  Full log: ${PREFLIGHT_LOG}" >&2
        echo "  Usual cause: a vendored CMakeLists declares a target whose source" >&2
        echo "  lives in an excluded tree. Guard it on EXISTS rather than on" >&2
        echo "  BUILD_TESTS -- a derived condition cannot go stale." >&2
        # leave backend/build correct for the next local build before leaving
        cmake -B backend/build -S backend -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 || true
        exit 1
    fi
    rm -f "$PREFLIGHT_LOG"
    # Put the build directory back the way a local build expects it.
    cmake -B backend/build -S backend -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
    echo "preflight: configures cleanly without the excluded trees (${#PARKED_TREES[@]} parked)."
)

ID_FILE="$(mktemp -t railway-deployment-id-XXXXXX)"
trap 'rm -f "$ID_FILE"' EXIT
"$UPLOAD" "${UPLOAD_ARGS[@]}" "${DRY_RUN[@]}" "${KEEP_ARCHIVE[@]}" --deployment-id-file "$ID_FILE"

DEPLOY_ID="$(cat "$ID_FILE")"
[[ -n "$DEPLOY_ID" ]] || exit 0     # a dry run uploaded nothing, so there is nothing to confirm

# Derive the expected count from railway.json rather than stating a number here. This line once said
# "numReplicas 3 => 3 mortgage + 3 strategy" for four days after numReplicas dropped to 2, which
# made the gate pass at 4 while telling the reader to expect 6. It is one line per replica now that
# the mortgage assistant runs in its own service.
_replicas="$(sed -n 's/.*"numReplicas"[[:space:]]*:[[:space:]]*\([0-9]\+\).*/\1/p' \
              "${REPO_ROOT}/railway.json" 2>/dev/null | head -1)"
_replicas="${_replicas:-1}"
echo ""
echo "  Expect one 'model is LOADED' line per replica (the strategy assistant) --"
echo "  numReplicas ${_replicas} => ${_replicas} lines --"
echo "  timestamped after this upload:"
echo "      railway logs --deployment ${DEPLOY_ID} | grep -c 'model is LOADED'"
echo ""
# 'model is LOADED' COUNTS weights and does not say WHICH model, which is the distinction
# `local_model_loaded()` exists for -- it is true of a decoder and of an ENCODER alike, deliberately,
# so the count above stays correct across a backend change. It is therefore blind to the one thing a
# backend flip needs to confirm. This second line names the model, and it is the check that would
# have caught serving a decoder from an image whose variables asked for an encoder:
echo "  Then confirm WHICH model answered -- the count above cannot:"
echo "      railway logs --deployment ${DEPLOY_ID} | grep 'assistant ready'"
echo "  An ENCODER prints 'ENCODER assistant ready: ... N operations, vocab N'"
echo "  beside its LOADED line; a decoder prints neither of those fields."
echo ""
echo "  And assert the NEGATIVE: 0 '[ERROR' lines. An assistant whose weights"
echo "  are missing reports itself UNAVAILABLE while the other two services"
echo "  serve perfectly, so a half-loaded fleet looks green. Note the logger"
echo "  PADS the level, so the text is '[WARN ]' -- grep '\[WARN *\]' and"
echo "  assert a positive control on the same pattern family before quoting a"
echo "  zero, which this repo has already had read wrongly once."
