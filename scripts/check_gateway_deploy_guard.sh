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

echo "7. the store-version predicate: the success line passes and every refusal fails"
# WHY THIS IS GATED AND THE REST OF SECTION 1b IS NOT. The preflight itself needs podman, an
# image and the live store, so it cannot run here -- but its VERDICT is a discriminator, and the
# first version of that verdict (an inline `case` with a looser pattern) matched one of the
# refusals it exists to catch. The predicate therefore lives in its own file and is fed the REAL
# strings: `admin show`'s success line, and three refusals taken verbatim from
# `GatewayStore::verifyLocked` and the image loader.
PRED="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/deploy/gateway/store_version_ok.sh"
if [ -r "$PRED" ]; then
    # shellcheck source=deploy/gateway/store_version_ok.sh
    . "$PRED"
    verdict() { if printf '%s' "$2" | store_version_ok; then echo ok; else echo refused; fi; }
    check "show's success line is a PASS" \
          "$(verdict a 'store transport: cleartext (sslmode=disable)
schema_version=7 orgs=1
installed_epoch=ge_10 digest=abc inputs=def')" "ok"
    # The BEHIND refusal, measured against the live store on 2026-10-11. It names neither
    # "schema_version" nor "schema version", so a rule written to match the refusals would
    # have missed exactly the state this preflight was written for.
    check "the BEHIND refusal is REFUSED" \
          "$(verdict b "sensen-gateway admin: migration 8 ('gateway_hold_principal') has not been applied to this store")" \
          "refused"
    # The AHEAD refusal, verbatim from verifyLocked. THIS is the one the first pattern passed:
    # it contains the words "schema version", so a store NEWER than the image read as a pass
    # and the deploy would have gone ahead and replaced a serving gateway.
    check "the AHEAD refusal is REFUSED (the one a looser pattern passed)" \
          "$(verdict c 'sensen-gateway admin: the store is at schema version 14 but this binary only knows 12')" \
          "refused"
    check "a loader failure is REFUSED" \
          "$(verdict d 'error while loading shared libraries: libc++.so.1: cannot open shared object file')" \
          "refused"
    check "empty output is REFUSED" "$(verdict e '')" "refused"
    # `admin show` prints -1 when it could NOT read the version (`v ? *v : -1`), so a surface
    # answering "I do not know" must not read as "yes".
    check "an unreadable version (-1) is REFUSED" \
          "$(verdict f 'store transport: cleartext (sslmode=disable)
schema_version=-1 orgs=-1')" "refused"
    # AND redeploy.sh MUST USE IT. A predicate nothing calls is not a gate, which is the lesson
    # this tree records against GroundingCorpusSweepTest.
    if grep -q 'store_version_ok' "$RD" 2>/dev/null; then
        ok "redeploy.sh uses the predicate"
    else
        no "redeploy.sh does not use the predicate, so gating it proves nothing"
    fi
    # AND ITS REMEDY MUST BE ONE THAT CAN SUCCEED. The refusal named `admin init
    # env:SGW_MAINT_DSN` until 2026-10-11, and that role cannot migrate: measured on the live
    # store, `gw_maint` had no CREATE on the database (so `migrate()`'s opening `create schema
    # if not exists` refuses even though the schema exists -- PostgreSQL checks the privilege
    # before the existence) and does not own the tables the pending migrations ALTER. An
    # operator following the message got `permission denied for database` and no way forward.
    if grep -q 'admin init env:SGW_MAINT_DSN' "$RD" 2>/dev/null; then
        no "the refusal tells the operator to run 'admin init env:SGW_MAINT_DSN', which the maint role cannot do"
    else
        ok "the refusal does not name the maint DSN for admin init"
    fi
    if grep -q 'SGW_OWNER_DSN' "$RD" 2>/dev/null; then
        ok "the refusal names an OWNER DSN, which is a role that can migrate"
    else
        no "the refusal names no owner DSN, so it does not say which credential migrates"
    fi
else
    no "deploy/gateway/store_version_ok.sh is not readable"
fi

# --- every submodule is at its recorded pin ---------------------------------
# A release must be the tree that was recorded. This replaced a check that named `external/tbqwf`
# ALONE, and the day it was written the deploy source had built with `external/
# fastestjsoninthewest` at `e8c970c` against a recorded `25ffd2e6` -- so the binary was not the
# recorded tree and nothing said so, because fastjson was not the submodule anybody had named.
SPO="$(dirname "$GUARD")/submodules_pinned_ok.sh"
if [ -r "$SPO" ]; then
    # shellcheck source=/dev/null
    . "$SPO"
    pinned(){ if printf '%s' "$1" | submodules_pinned_ok; then echo admitted; else echo refused; fi; }
    check "every submodule at its pin is ADMITTED" \
          "$(pinned ' 1111111111111111111111111111111111111111 external/a (heads/main)
 2222222222222222222222222222222222222222 external/b (v1.0)')" "admitted"
    # '+' is the case that actually happened, and it is refused in BOTH directions: a submodule
    # AHEAD of its pin is code that was never recorded, which is no safer than one behind.
    check "a submodule NOT at its pin (+) is REFUSED" \
          "$(pinned ' 1111111111111111111111111111111111111111 external/a (heads/main)
+e8c970c0dc76fd662316d1e8a09cb7e52c9cb48c external/fastestjsoninthewest (heads/main-13-ge8c970c)')" "refused"
    check "an UNINITIALISED submodule (-) is REFUSED" \
          "$(pinned '-1111111111111111111111111111111111111111 external/fastestjsoninthewest')" "refused"
    check "a CONFLICTED submodule (U) is REFUSED" \
          "$(pinned 'U1111111111111111111111111111111111111111 external/a')" "refused"
    # The deploy source has five submodules, so "nothing to check" means the check did not run.
    check "empty submodule status is REFUSED" "$(pinned '')" "refused"
    if grep -q 'submodules_pinned_ok' "$RD" 2>/dev/null; then
        ok "redeploy.sh uses the submodule predicate"
    else
        no "redeploy.sh does not use the submodule predicate, so gating it proves nothing"
    fi
    if grep -q 'is not the recorded pin' "$RD" 2>/dev/null; then
        no "redeploy.sh still checks only the single named submodule"
    else
        ok "redeploy.sh no longer checks one submodule by name"
    fi
else
    no "deploy/gateway/submodules_pinned_ok.sh is not readable"
fi

# --- migration 13's artefacts are in force ----------------------------------
# Section 1b proves the store's VERSION (the binary refuses a store behind OR ahead of it), and
# that is all it proves: `schema_migrations` keeps reading 13 after an object is dropped. The
# artefacts matter because losing one is SILENT -- `model_fair_tenants` is SECURITY DEFINER and
# fair rationing divides a model's capacity across the rows it returns, so a missing grant or
# policy hands every surviving tenant too LARGE a share with no error anywhere.
FPO="$(dirname "$GUARD")/fair_posture_ok.sh"
if [ -r "$FPO" ]; then
    # shellcheck source=/dev/null
    . "$FPO"
    posture(){ if printf '%s' "$1" | fair_posture_ok; then echo admitted; else echo refused; fi; }
    ALL3='capacity_can_select_orgs_id=true
orgs_capacity_policy=true
fair_tenants_includes_planless=true'
    check "all three artefacts present is ADMITTED" "$(posture "$ALL3")" "admitted"
    # One arm per artefact, because a predicate that only reads the first key would pass two of
    # these and still be wrong in the direction that over-admits.
    check "a missing GRANT is REFUSED" \
          "$(posture "$(printf '%s' "$ALL3" | sed 's/capacity_can_select_orgs_id=true/capacity_can_select_orgs_id=false/')")" "refused"
    check "a missing POLICY is REFUSED" \
          "$(posture "$(printf '%s' "$ALL3" | sed 's/orgs_capacity_policy=true/orgs_capacity_policy=false/')")" "refused"
    check "a stale model_fair_tenants is REFUSED" \
          "$(posture "$(printf '%s' "$ALL3" | sed 's/fair_tenants_includes_planless=true/fair_tenants_includes_planless=false/')")" "refused"
    # MEASURED on the live store at schema 12 on 2026-10-11, before migration 13 was applied:
    # all three answered false. So this is the real pre-migration text, not an invented one.
    check "the real schema-12 probe is REFUSED" \
          "$(posture 'capacity_can_select_orgs_id=false
orgs_capacity_policy=false
fair_tenants_includes_planless=false')" "refused"
    # A probe that errored before its third statement matches two keys. Requiring the KEY as
    # well as the value is what makes that a refusal instead of a pass.
    check "a TRUNCATED probe (third key absent) is REFUSED" \
          "$(posture 'capacity_can_select_orgs_id=true
orgs_capacity_policy=true')" "refused"
    check "a psql error instead of a probe is REFUSED" \
          "$(posture 'psql: error: connection to server failed')" "refused"
    check "empty probe output is REFUSED" "$(posture '')" "refused"
    if grep -q 'fair_posture_ok' "$RD" 2>/dev/null; then
        ok "redeploy.sh uses the posture predicate"
    else
        no "redeploy.sh does not use the posture predicate, so gating it proves nothing"
    fi
    # The probe must not need a credential: it runs inside the database container, so no DSN or
    # password can be leaked by a refusal that prints what it saw.
    if grep -qE 'fair posture|POSTURE=' "$RD" 2>/dev/null && ! grep -qE 'POSTURE=.*(SGW_|DSN|PGPASSWORD)' "$RD" 2>/dev/null; then
        ok "the posture probe carries no DSN or password"
    else
        no "the posture probe appears to take a credential"
    fi
else
    no "deploy/gateway/fair_posture_ok.sh is not readable"
fi

printf '\n%s: %d passed, %d failed\n' "gateway deploy guard" "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
