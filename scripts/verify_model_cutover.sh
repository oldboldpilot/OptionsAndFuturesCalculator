#!/usr/bin/env bash
# Prove a model swap is SERVING, not merely deployed.
#
# A green healthcheck is not evidence: it passes before a throw, and this
# repository has a recorded case of every deployment reporting SUCCESS while all
# three containers crash-looped. Nor is `railway logs --service`, which REPLAYS
# the last dead session's scrollback when nothing is running -- so a grep against
# it passes against the container being replaced.
#
# Usage: verify_model_cutover.sh <deployment-id> <expected-sha256> [replicas]
set -uo pipefail
dep=$1 want=$2 replicas=${3:-}
svc=options-calculator-backend
fail=0
note() { printf '%-46s %s\n' "$1" "$2"; }
bad()  { note "$1" "FAIL -- $2"; fail=1; }

# Replica count DERIVED, not restated: this check once hardcoded 3 for four days
# after numReplicas dropped to 2, so an honest 4 lines read as a shortfall.
if [ -z "$replicas" ]; then
  replicas=$(python3 -c "import json;print(json.load(open('railway.json'))['deploy']['numReplicas'])" 2>/dev/null || echo 2)
fi
want_loaded=$((replicas * 2))   # two assistants per replica

# 1. Railway's own answer about THIS rollout.
st=$(timeout 90 railway deployment list --service "$svc" --json 2>/dev/null \
     | python3 -c "import json,sys;r=json.load(sys.stdin);r=r if isinstance(r,list) else r.get('deployments',[]);print(r[0].get('status','?'))" 2>/dev/null)
[ "$st" = "SUCCESS" ] && note "deployment status" "SUCCESS" || bad "deployment status" "$st"

# 2. THE CHECKSUM INSIDE THE CONTAINER. Not a variable, not a local pin -- this
#    repository's model-of-record line has been stale three times over, and only
#    this reading has ever caught it.
got=$(timeout 180 railway ssh --service "$svc" -- sha256sum /app/model/mortgage-assistant.gguf 2>/dev/null | awk '{print $1}' | tail -1)
[ "$got" = "$want" ] && note "container sha256" "matches ($got)" || bad "container sha256" "got '$got' want '$want'"

logs=$(mktemp); timeout 180 railway logs --deployment "$dep" >"$logs" 2>/dev/null

# 3. One load line per replica per assistant, from a FRESH boot.
n=$(grep -c 'model is LOADED' "$logs")
[ "$n" -ge "$want_loaded" ] && note "model is LOADED lines" "$n (want >= $want_loaded)" \
                            || bad "model is LOADED lines" "$n, want $want_loaded"

# 4. Zero warnings and errors -- on the PADDED pattern, with a positive control.
#    `grep -c '\[WARN\]'` returns 0 because the logger pads the level to
#    `[WARN ]`, and that zero was once quoted as health.
ctl=$(grep -cE '\[(INFO|WARN|ERROR) *\]' "$logs")
[ "$ctl" -gt 0 ] && note "log level positive control" "$ctl level-tagged lines" \
                 || bad "log level positive control" "pattern matches NOTHING -- a zero below would be meaningless"
for lvl in WARN ERROR; do
  c=$(grep -cE "\[$lvl *\]" "$logs")
  [ "$c" -eq 0 ] && note "[$lvl] count" "0" || bad "[$lvl] count" "$c (see $logs)"
done

# 5. SIMD tier, one per replica: proves each container booted this binary.
s=$(grep -c 'SIMD: runtime tier' "$logs")
[ "$s" -ge "$replicas" ] && note "SIMD tier lines" "$s (want >= $replicas)" || bad "SIMD tier lines" "$s"

echo
[ "$fail" -eq 0 ] && echo "CUTOVER VERIFIED" || echo "CUTOVER NOT VERIFIED -- logs at $logs"
exit "$fail"
