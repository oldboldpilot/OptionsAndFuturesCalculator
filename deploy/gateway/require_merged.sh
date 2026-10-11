#!/usr/bin/env bash
# Refuse to deploy code that was never merged.
#
# `redeploy.sh` takes its source tree from `SRC`, which defaults to a WORKTREE --
# and a worktree is whatever branch somebody last checked out in it. That is
# ambient state with no relationship to what is being shipped, so a default
# deploy can take an unreviewed lane to production and PRINT ITS SHA, which
# reads exactly like a legitimate release. This script is the preflight that
# says no: HEAD must be reachable from a master ref, and the ref that answered
# is NAMED rather than implied.
#
# IT IS EVALUATED AGAINST A CACHED REMOTE-TRACKING REF, and that is stated
# rather than hidden. `git for-each-ref refs/remotes/...` answers from local
# disk; only `git ls-remote` asks the server. So a stale cache can refuse code
# that IS merged (fetch and retry -- the refusal says so) and can admit code
# pushed to a master somebody has since rewritten. This guards against the
# ambient worktree; it is not an attestation of review. A guard whose basis is
# misunderstood is worse than no guard.
#
# IT NEVER PRINTS A REMOTE URL. The ref NAME is what identifies the remote here;
# this repository's remote URLs carry a credential, so `git remote -v` and
# `%(upstream:remoteurl)` are forbidden in anything that logs.
#
# `ALLOW_UNMERGED=1` is the deliberate exception and says so in the output: a
# hotfix to a live outage is a real case. It is not the path for ordinary work.
#
# usage: require_merged.sh <src-tree>
#   rc 0  -> contained in a master ref, or ALLOW_UNMERGED=1
#   rc 1  -> refused (unmerged, unfetched, or not a git tree)
# @author Olumuyiwa Oluwasanmi
set -uo pipefail

SRC="${1:-}"
[ -n "$SRC" ] || { echo "require_merged: no source tree given" >&2; exit 1; }

SHA="$(git -C "$SRC" rev-parse --short=9 HEAD 2>/dev/null)" || {
    echo "require_merged: '$SRC' is not a git work tree, so there is nothing to check and
nothing that could say this code was reviewed." >&2
    exit 1
}

MERGED_FROM=""
for REF in $(git -C "$SRC" for-each-ref --format='%(refname)' 'refs/remotes/*/master' 2>/dev/null); do
    if git -C "$SRC" merge-base --is-ancestor HEAD "$REF" 2>/dev/null; then MERGED_FROM="$REF"; break; fi
done

if [ -n "$MERGED_FROM" ]; then
    echo "preflight: $SHA is contained in $MERGED_FROM (a cached remote-tracking ref)"
    exit 0
fi

if [ "${ALLOW_UNMERGED:-0}" = "1" ]; then
    echo "preflight: WARNING -- $SHA is on no cached */master ref and ALLOW_UNMERGED=1 was set."
    echo "preflight: deploying UNMERGED code deliberately."
    exit 0
fi

echo "require_merged: $SRC is at $SHA, which no cached */master ref contains -- so this is
unmerged or unfetched code, and a deploy would ship it to production with its SHA printed as
though it had been released. Merge it; or, if it IS merged and the local cache is stale,
'git -C $SRC fetch' and retry; or set ALLOW_UNMERGED=1 to deploy it deliberately." >&2
exit 1
