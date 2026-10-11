#!/usr/bin/env bash
# Gate: `deploy/gateway/require_merged.sh` refuses to let a gateway deploy ship code that is on
# no master ref, admits code that is, and says which ref answered.
#
# @author Olumuyiwa Oluwasanmi
#
# WHY THIS EXISTS. `redeploy.sh` reads its source tree from `SRC`, whose default is a WORKTREE --
# ambient state that is whatever branch somebody last checked out there. A worktree left on a lane
# therefore makes a default `redeploy.sh` ship unreviewed code to production and PRINT ITS SHA,
# which reads exactly like a legitimate release. That is a hazard created by the convenience of a
# default, and the only thing that can close it is a preflight that asks where the code came from.
#
# WHAT IT ASSERTS, against the real script and on throwaway repositories rather than on the live
# worktree (which is the one tree a gate must not depend on the state of):
#   1. HEAD contained in a cached `refs/remotes/*/master` -> ADMITTED, and the ref is NAMED
#   2. HEAD on a lane that no master ref contains         -> REFUSED, rc 1
#   3. the same lane with ALLOW_UNMERGED=1                -> ADMITTED, and the output says it is
#      deliberate and that the code is unmerged
#   4. a path that is not a git work tree                 -> REFUSED rather than passed over: a
#      tree with no history cannot say the code was reviewed, and "no answer" is not "yes"
#   5. a NON-master remote-tracking ref containing HEAD   -> REFUSED. The glob is `*/master`, so a
#      lane pushed to a remote is still unmerged; being fetchable is not being merged.
#   6. no remote URL appears in any output on any arm. This repository's remote URLs embed a
#      credential, so a script that logs one leaks it; the ref NAME is the identifier here.
#
# THE REFUSAL MESSAGE IS ASSERTED TO NAME THE STALE-CACHE CASE, because a cached remote-tracking
# ref answers from local disk and only `git ls-remote` asks the server -- so the guard CAN refuse
# code that really is merged, and an operator who is not told to fetch will read that as the guard
# being broken and reach for ALLOW_UNMERGED.
set -uo pipefail

GUARD="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/deploy/gateway/require_merged.sh"
[ -x "$GUARD" ] || { echo "FAIL: no executable guard at $GUARD"; exit 1; }

PASS=0; FAIL=0
ok(){ PASS=$((PASS+1)); printf '  ok   %s\n' "$1"; }
no(){ FAIL=$((FAIL+1)); printf '  FAIL %s\n' "$1"; }
check(){ if [ "$2" = "$3" ]; then ok "$1"; else no "$1 (expected '$3', got '$2')"; fi; }
has(){ if printf '%s' "$2" | grep -qF -- "$3"; then ok "$1"; else no "$1 (output did not contain '$3')"; fi; }
hasnt(){ if printf '%s' "$2" | grep -qF -- "$3"; then no "$1 (output contained '$3')"; else ok "$1"; fi; }

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

# A throwaway repository with a real remote-tracking ref. `update-ref` writes one directly, which
# is what the guard reads -- no network, and no dependence on any remote existing.
REPO="$WORK/tree"
git init -q "$REPO"
git -C "$REPO" -c user.name=t -c user.email=t@t commit -q --allow-empty -m base
BASE="$(git -C "$REPO" rev-parse HEAD)"
git -C "$REPO" update-ref refs/remotes/origin/master "$BASE"
# A URL for the forbidden-output check: if the guard ever prints one, this is the string to catch.
git -C "$REPO" remote add origin "https://user:secrettoken@example.invalid/repo.git"

echo "1. HEAD is contained in a cached */master"
OUT="$("$GUARD" "$REPO" 2>&1)"; RC=$?
check "admitted (rc 0)" "$RC" "0"
has  "names the ref that answered" "$OUT" "refs/remotes/origin/master"
hasnt "prints no remote URL" "$OUT" "example.invalid"
hasnt "prints no credential" "$OUT" "secrettoken"

echo "2. HEAD is on a lane no */master contains"
git -C "$REPO" -c user.name=t -c user.email=t@t commit -q --allow-empty -m lane
OUT="$("$GUARD" "$REPO" 2>&1)"; RC=$?
check "refused (rc 1)" "$RC" "1"
has  "names the tree" "$OUT" "$REPO"
has  "tells the operator to fetch when the cache is stale" "$OUT" "fetch"
has  "names the deliberate override" "$OUT" "ALLOW_UNMERGED"
hasnt "prints no remote URL" "$OUT" "example.invalid"

echo "3. the same lane with the deliberate override"
OUT="$(ALLOW_UNMERGED=1 "$GUARD" "$REPO" 2>&1)"; RC=$?
check "admitted (rc 0)" "$RC" "0"
has  "says the code is unmerged" "$OUT" "UNMERGED"
has  "says it is deliberate" "$OUT" "deliberately"

echo "4. a path that is not a git work tree"
mkdir -p "$WORK/plain"
OUT="$("$GUARD" "$WORK/plain" 2>&1)"; RC=$?
check "refused (rc 1)" "$RC" "1"
has  "says there is no history to check" "$OUT" "not a git work tree"
OUT="$("$GUARD" 2>&1)"; RC=$?
check "no argument is refused (rc 1)" "$RC" "1"

echo "5. a non-master remote-tracking ref containing HEAD is NOT enough"
# Being FETCHABLE is not being MERGED: pushing a lane to a remote must not satisfy the guard.
# The discriminating case is a ONE-COMPONENT non-master branch, `origin/develop`. A multi-component
# `origin/lane/some-work` is refused by ANY two-component glob whatever its last component is, so
# it cannot tell `*/master` from `*/*` -- it was the first thing written here and it measured
# nothing. Both are asserted; only the first is the discriminator.
git -C "$REPO" update-ref refs/remotes/origin/lane/some-work "$(git -C "$REPO" rev-parse HEAD)"
OUT="$("$GUARD" "$REPO" 2>&1)"; RC=$?
check "a pushed lane is still refused (rc 1)" "$RC" "1"
git -C "$REPO" update-ref refs/remotes/origin/develop "$(git -C "$REPO" rev-parse HEAD)"
OUT="$("$GUARD" "$REPO" 2>&1)"; RC=$?
check "another branch on the remote is still refused (rc 1)" "$RC" "1"
git -C "$REPO" update-ref -d refs/remotes/origin/develop
# And a SECOND remote's master does admit it, so the glob is a glob and not a hardcoded origin.
git -C "$REPO" update-ref refs/remotes/gitea/master "$(git -C "$REPO" rev-parse HEAD)"
OUT="$("$GUARD" "$REPO" 2>&1)"; RC=$?
check "a second remote's master admits (rc 0)" "$RC" "0"
has  "and names that remote" "$OUT" "refs/remotes/gitea/master"

echo "6. redeploy.sh actually calls it, before it does anything"
RD="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/deploy/gateway/redeploy.sh"
if [ -r "$RD" ]; then
    CALL="$(grep -n 'require_merged.sh' "$RD" | head -1 | cut -d: -f1)"
    # The first podman COMMAND, not the first mention of one: this file explains in its header
    # why secrets travel as podman secrets, and a bare grep matches that comment on line 33 and
    # reports the guard as too late. Same stripping the CSPRNG gate does for the same reason.
    PODMAN="$(grep -n '^[[:space:]]*[^#[:space:]].*podman' "$RD" | head -1 | cut -d: -f1)"
    if [ -n "$CALL" ]; then ok "redeploy.sh calls the guard"; else no "redeploy.sh does not call the guard"; fi
    if [ -n "$CALL" ] && [ -n "$PODMAN" ] && [ "$CALL" -lt "$PODMAN" ]; then
        ok "it is called before the first podman command"
    else
        no "the guard is not called before the first podman command (guard line $CALL, podman line $PODMAN)"
    fi
else
    no "redeploy.sh is not readable"
fi

printf '\n%s: %d passed, %d failed\n' "gateway deploy guard" "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
