#!/usr/bin/env bash
#
# Integrated validation across every branch landed in this cycle.
#
# @author Olumuyiwa Oluwasanmi
#
# WHY THIS EXISTS, rather than trusting the per-branch results.
#
# Four SGEE branches were developed concurrently and each was validated ALONE:
# the import-std guards, the task-queue gRPC service, the ReplicatedQueueRuntime
# driver, and the Raft snapshot fix. Every one reported green on its own branch.
# That is not the same claim as "they work together", and there is a specific,
# known reason to doubt it:
#
#   The gRPC queue service takes a mutex to serialise RPC calls into the broker.
#   The driver runs its OWN thread ticking that SAME broker. TaskBroker is
#   single-threaded BY CONTRACT -- `FeedForwardNetwork`-style mutable scratch
#   and an in-memory index behind no lock of its own. If those two paths do not
#   share one lock, the failure is silent state corruption, not a crash, and no
#   test on either branch alone can see it.
#
# So this script merges everything first and validates the MERGE. A green run
# here is the only evidence that means anything about what would actually ship.
#
# It is also deliberately paranoid about two traps this repository has been
# bitten by more than once:
#
#   ccache. `clang++` on PATH is /usr/bin/ccache, and it HAS served stale
#   objects for edited .cppm files -- which fakes a passing discriminating test,
#   the single worst failure mode available to us. We pin the real compiler and
#   disable ccache outright.
#
#   Ninja. C++23 modules cannot be built by Unix Makefiles at all. If cmake ever
#   picks the default generator here, the failure is confusing and unrelated to
#   the code.
#
# Usage:  scripts/validate_integration.sh [--keep-build]
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SGEE="${REPO}/backend/external/SGEE"
SCRATCH="${TMPDIR:-/tmp}/ofc-integration-validation"
KEEP_BUILD=0
[[ "${1:-}" == "--keep-build" ]] && KEEP_BUILD=1

# Never inside the repo: `railway up` uploads the WORKING TREE, not git, so a
# stray build directory is a deploy outage waiting to happen.
BUILD="${SCRATCH}/build"

export CCACHE_DISABLE=1
CXX_REAL="/usr/local/bin/clang++"
C_REAL="/usr/local/bin/clang"

red()   { printf '\033[31m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
info()  { printf '\033[36m==> %s\033[0m\n' "$*"; }

fail() { red "FAIL: $*"; exit 1; }

info "Preflight"
[[ -x "$CXX_REAL" ]] || fail "$CXX_REAL missing -- ccache would be used instead, and it has served stale .cppm objects before"
command -v ninja >/dev/null || fail "ninja missing -- C++23 modules CANNOT be built with Unix Makefiles"

# Disk. The 24.7 GB /tmp quota has been exhausted twice in one day by redundant
# build trees; a build that dies halfway through looks exactly like a real
# compile failure and wastes a debugging cycle.
avail_kb=$(df -Pk "${TMPDIR:-/tmp}" | awk 'NR==2{print $4}')
if (( avail_kb < 8 * 1024 * 1024 )); then
    fail "under 8 GiB free on ${TMPDIR:-/tmp} ($(( avail_kb / 1024 )) MiB) -- clean scratch build dirs first"
fi
green "preflight ok ($(( avail_kb / 1024 / 1024 )) GiB free)"

info "SGEE branch inventory"
git -C "$SGEE" --no-pager branch -v | sed 's/^/    /'

# ---------------------------------------------------------------------------
# Stage 1 -- integration merge, in a throwaway worktree.
#
# We merge into a detached integration branch rather than into main, so a
# conflict here is information rather than damage. Nothing is pushed.
# ---------------------------------------------------------------------------
info "Stage 1: merging SGEE branches into an integration branch"
INT_WT="${SCRATCH}/sgee-integration"
rm -rf "$INT_WT"
git -C "$SGEE" worktree prune
git -C "$SGEE" branch -D integration/validate 2>/dev/null || true
git -C "$SGEE" worktree add -b integration/validate "$INT_WT" main >/dev/null

MERGED=()
# Order matters: the snapshot branch was cut FROM the driver branch, so merging
# the driver first keeps that merge a fast-forward-ish no-op rather than a
# spurious three-way conflict over the same runtime file.
for br in test/guard-import-std feature/task-queue-grpc-service \
          driver/replicated-queue-runtime-thread feature/raft-snapshot-restore; do
    if git -C "$SGEE" rev-parse --verify "$br" >/dev/null 2>&1; then
        info "  merging $br"
        if git -C "$INT_WT" merge --no-edit "$br" >/dev/null 2>&1; then
            MERGED+=("$br")
        else
            git -C "$INT_WT" merge --abort 2>/dev/null || true
            fail "merge conflict on $br -- resolve before validating; per-branch green means nothing if they do not compose"
        fi
    else
        red "  branch $br not found -- skipped"
    fi
done
green "merged: ${MERGED[*]:-none}"

# ---------------------------------------------------------------------------
# Stage 2 -- SGEE standalone.
#
# This is the configuration SGEE's own 70/71 figure was measured in, so it is
# the only apples-to-apples comparison against the documented baseline.
# SGEE_NO_IMPORT_STD stays at its OFF default here: real `import std;`.
# ---------------------------------------------------------------------------
info "Stage 2: SGEE standalone (real import std)"
cmake -S "$INT_WT" -B "${BUILD}/sgee-standalone" -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_COMPILER="$CXX_REAL" -DCMAKE_C_COMPILER="$C_REAL" \
      -DSGEE_BUILD_EXAMPLES=OFF -DSGEE_BUILD_BENCHMARKS=OFF >/dev/null \
    || fail "SGEE standalone configure"
cmake --build "${BUILD}/sgee-standalone" -j"$(nproc)" >/dev/null \
    || fail "SGEE standalone build"
( cd "${BUILD}/sgee-standalone" && ctest --output-on-failure ) \
    || fail "SGEE standalone ctest -- this is the merged tree, so a failure here is an INTEGRATION failure even if every branch was green alone"
green "SGEE standalone ctest passed"

# ---------------------------------------------------------------------------
# Stage 3 -- SGEE as the backend actually builds it.
#
# backend/CMakeLists.txt FORCEs SGEE_NO_IMPORT_STD=ON. Until the import-std
# guards landed, NOT ONE SGEE test could compile in this configuration -- so
# every previous claim about SGEE's behaviour in this tree rested on Stage 2's
# configuration, which the backend does not use. That is exactly the gap this
# stage closes, and it is why it is separate from Stage 2 rather than folded in.
# ---------------------------------------------------------------------------
info "Stage 3: SGEE with SGEE_NO_IMPORT_STD=ON (the backend's real configuration)"
cmake -S "$INT_WT" -B "${BUILD}/sgee-noimportstd" -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_COMPILER="$CXX_REAL" -DCMAKE_C_COMPILER="$C_REAL" \
      -DSGEE_NO_IMPORT_STD=ON \
      -DSGEE_BUILD_EXAMPLES=OFF -DSGEE_BUILD_BENCHMARKS=OFF >/dev/null \
    || fail "SGEE no-import-std configure"
cmake --build "${BUILD}/sgee-noimportstd" -j"$(nproc)" >/dev/null \
    || fail "SGEE no-import-std build"
( cd "${BUILD}/sgee-noimportstd" && ctest --output-on-failure ) \
    || fail "SGEE ctest under SGEE_NO_IMPORT_STD=ON"
green "SGEE no-import-std ctest passed"

# ---------------------------------------------------------------------------
# Stage 4 -- backend, including the four services' own suites.
#
# Reuses already-populated dependency sources when present: a redundant gRPC
# clone is ~2.5 GB against a quota that has already been exhausted twice today.
# ---------------------------------------------------------------------------
info "Stage 4: backend + service suites"
GRPC_SRC="${REPO}/backend/build/_deps/grpc-src"
HTTPLIB_SRC="${REPO}/backend/build/_deps/httplib-src"
REUSE=()
[[ -d "$GRPC_SRC"    ]] && REUSE+=("-DFETCHCONTENT_SOURCE_DIR_GRPC=${GRPC_SRC}")
[[ -d "$HTTPLIB_SRC" ]] && REUSE+=("-DFETCHCONTENT_SOURCE_DIR_HTTPLIB=${HTTPLIB_SRC}")

cmake -S "${REPO}/backend" -B "${BUILD}/backend" -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_COMPILER="$CXX_REAL" -DCMAKE_C_COMPILER="$C_REAL" \
      -DENABLE_LLAMACPP_BACKEND=OFF \
      "${REUSE[@]}" >/dev/null \
    || fail "backend configure"
cmake --build "${BUILD}/backend" -j"$(nproc)" >/dev/null || fail "backend build"

# Named explicitly rather than globbed: a suite silently disappearing from the
# build is the failure this catches, and a glob would simply not notice.
for t in test_finance_service_validation test_option_pricing_service \
         test_calculator_service test_market_data_resilience \
         test_inference_admission test_api_key_entitlement; do
    bin="${BUILD}/backend/${t}"
    [[ -x "$bin" ]] || { red "  MISSING: $t"; fail "expected suite $t was not built"; }
    info "  $t"
    "$bin" || fail "$t"
done
green "backend suites passed"

# ---------------------------------------------------------------------------
# Stage 5 -- frontend.
# ---------------------------------------------------------------------------
info "Stage 5: frontend"
( cd "${REPO}/frontend" && npx vitest run ) || fail "frontend vitest"
green "frontend passed"

info "Cleanup"
if (( KEEP_BUILD )); then
    info "keeping ${BUILD} (--keep-build)"
else
    rm -rf "${BUILD}"
fi
git -C "$SGEE" worktree remove --force "$INT_WT" 2>/dev/null || true

green "INTEGRATION VALIDATION PASSED"
echo "Merged branches: ${MERGED[*]:-none}"
echo
echo "What this run does NOT prove, and must not be read as proving:"
echo "  - Nothing here exercises a real MULTI-NODE cluster over a network"
echo "    transport. The consensus tests drive the in-process transport, and the"
echo "    queue service's replicated test hand-drives a SINGLE-node cluster."
echo "  - No Railway topology is exercised. Raft still needs three separately"
echo "    addressable services with volumes; replicas of one service cannot"
echo "    provide that."
