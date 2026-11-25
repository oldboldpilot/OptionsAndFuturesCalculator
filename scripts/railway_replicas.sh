#!/usr/bin/env bash
# Print the replica count Railway will ACTUALLY run for a service, read from the
# service's own persisted configuration.
#
# @author Olumuyiwa Oluwasanmi
#
# This number used to be read out of `railway.json`. That file stopped being the
# answer: Railway deprecated Config as Code and stops reading it on 2026-12-01,
# and the settings now live on the service instance. Keeping the derivation
# pointed at a file would have left a number in the repository that an operator
# could edit, re-read and believe while the platform ignored it -- which is the
# exact class of failure this script exists to prevent, and which this project
# has already been bitten by on another service.
#
# Exits NON-ZERO WITHOUT PRINTING A NUMBER when the count cannot be determined.
# That is deliberate and is the whole design: a wrong expected count is worse
# than no expectation. This gate has twice told an operator to expect a count
# that was not the truth -- it said 3 for four days after the count dropped to
# 2 -- and both times an honest reading looked like a shortfall. A caller that
# wants to proceed without the number must say so itself.
set -uo pipefail

svc="${1:?usage: railway_replicas.sh <service-name>}"

env_id="$(timeout 60 railway status --json 2>/dev/null | python3 -c '
import json, sys
try:
    d = json.load(sys.stdin)
except Exception:
    raise SystemExit(0)
edges = (d.get("environments") or {}).get("edges", [])
nodes = [e.get("node", {}) for e in edges]
# Prefer the environment named production; fall back to the only one there is.
named = [n for n in nodes if (n.get("name") or "").lower() == "production"]
pick = (named or nodes or [{}])[0]
print(pick.get("id") or "")
' 2>/dev/null)"

[[ -n "$env_id" ]] || {
    echo "ERROR: could not resolve the linked Railway environment." >&2
    exit 1
}

read -r -d '' q <<'GRAPHQL' || true
query Replicas($eid: String!) {
  environment(id: $eid) {
    serviceInstances { edges { node { serviceName numReplicas } } }
  }
}
GRAPHQL

n="$(timeout 60 railway api "$q" --variables "{\"eid\":\"${env_id}\"}" 2>/dev/null | python3 -c '
import json, sys
svc = sys.argv[1]
try:
    d = json.load(sys.stdin)
except Exception:
    raise SystemExit(0)
try:
    edges = d["data"]["environment"]["serviceInstances"]["edges"]
except (KeyError, TypeError):
    raise SystemExit(0)
for e in edges:
    node = e.get("node") or {}
    if node.get("serviceName") == svc:
        v = node.get("numReplicas")
        # A null here means the service carries no override. That is NOT "1":
        # it is "this instrument cannot tell you", and the caller must hear so.
        if isinstance(v, int):
            print(v)
        raise SystemExit(0)
' "$svc" 2>/dev/null)"

[[ -n "$n" ]] || {
    echo "ERROR: could not read numReplicas for service '${svc}' from Railway." >&2
    echo "       Not guessing: an expected replica count that is wrong reads as a" >&2
    echo "       failed cutover when the fleet is healthy, and has done here before." >&2
    exit 1
}

echo "$n"
