#!/usr/bin/env bash
# sensen-gateway regression suite.
#
# @author Olumuyiwa Oluwasanmi
#
# EVERY CHECK ASSERTS BOTH DIRECTIONS WHERE ONE EXISTS. A gateway that refuses
# everything passes a refuse-only suite, which is why the admit arms are here
# and why the first check is a 200: if that stops working, nothing below it
# means anything.
#
# Usage: regression.sh <base-url> <bearer> [--cacert <pem>]
set -uo pipefail
H="${1:?base url}"; GK="${2:?bearer}"; shift 2
CA=(); [ "${1:-}" = "--cacert" ] && CA=(--cacert "$2")
pass=0; fail=0
chk() { # name expected_code actual_code [extra]
  if [ "$2" = "$3" ]; then printf '  PASS  %-52s %s\n' "$1" "$3"; pass=$((pass+1));
  else printf '  FAIL  %-52s got %s want %s %s\n' "$1" "$3" "$2" "${4:-}"; fail=$((fail+1)); fi
}
body() { curl -s "${CA[@]}" -k -m 30 "$@"; }
code() { curl -s "${CA[@]}" -k -m 30 -o /dev/null -w '%{http_code}' "$@"; }
J='content-type: application/json'
OK='{"model":"qwen3_mortgage","max_tokens":32,"messages":[{"role":"user","content":"hello"}]}'

echo "— liveness and readiness —"
chk "/healthz is 200"                      200 "$(code "$H/healthz")"
chk "/readyz is 200"                       200 "$(code "$H/readyz")"
r=$(body "$H/readyz"); echo "$r" | grep -q '"ready":true'      && chk "/readyz says ready"        y y || chk "/readyz says ready" y n
echo "$r" | grep -q '"store_reachable":true' && chk "/readyz store reachable"  y y || chk "/readyz store reachable" y n
echo "$r" | grep -q '"epoch_current":true'   && chk "/readyz epoch current"    y y || chk "/readyz epoch current" y n

echo "— authentication, BOTH directions —"
chk "/v1/models with a valid key"           200 "$(code "$H/v1/models" -H "Authorization: Bearer $GK")"
chk "/v1/models with NO key"                401 "$(code "$H/v1/models")"
chk "/v1/models with a bogus key"           401 "$(code "$H/v1/models" -H "Authorization: Bearer gk_nope_nope")"
chk "/v1/models with a malformed header"    401 "$(code "$H/v1/models" -H "Authorization: $GK")"
chk "completion with NO key"                401 "$(code -X POST "$H/v1/chat/completions" -H "$J" -d "$OK")"
chk "completion with a bogus key"           401 "$(code -X POST "$H/v1/chat/completions" -H "Authorization: Bearer gk_nope_nope" -H "$J" -d "$OK")"

echo "— the ADMIT path (if this fails, nothing else matters) —"
chk "a well-formed completion is 200"       200 "$(code -X POST "$H/v1/chat/completions" -H "Authorization: Bearer $GK" -H "$J" -d "$OK")"
body -X POST "$H/v1/chat/completions" -H "Authorization: Bearer $GK" -H "$J" -d "$OK" | grep -q '"choices"' \
  && chk "the 200 carries the leaf's choices" y y || chk "the 200 carries the leaf's choices" y n

echo "— request validation —"
chk "no max_tokens is 400"                  400 "$(code -X POST "$H/v1/chat/completions" -H "Authorization: Bearer $GK" -H "$J" -d '{"model":"qwen3_mortgage","messages":[{"role":"user","content":"x"}]}')"
# THESE TWO WERE `NOTE`s AND ARE NOW ASSERTIONS. They were reported rather than
# failed because the gateway answered a status this suite would have had to bake
# in as correct; both are fixed upstream, so the honest thing is to pin the right
# answer and let a regression fail here.
#
#   unknown model: candidate selection discarded each candidate's reason and
#     concluded "nothing was permitted", so an absent model came back
#     `no_permitted_placement` (403) where kHttpStatusTable says `model_unknown`
#     (404). GateTable::reach now answers the machine-invariant half first.
#   unknown route: gateway_router answered EVERY /v1/<any> with 503 `not_wired`,
#     which is right for a listener-only gateway and false for this one -- it had
#     just served a completion. ReadyState::admission_wired tells them apart.
#
# The STATUS and the REASON STRING are both asserted. A status alone would pass
# against any 404, and the reason is what sends an operator to the right place.
um=$(code -X POST "$H/v1/chat/completions" -H "Authorization: Bearer $GK" -H "$J" -d '{"model":"no_such_model","max_tokens":8,"messages":[{"role":"user","content":"x"}]}')
chk "an unknown model is 404"               404 "$um"
body -X POST "$H/v1/chat/completions" -H "Authorization: Bearer $GK" -H "$J" -d '{"model":"no_such_model","max_tokens":8,"messages":[{"role":"user","content":"x"}]}' \
  | grep -q 'model_unknown' \
  && chk "and names model_unknown, not no_permitted_placement" y y \
  || chk "and names model_unknown, not no_permitted_placement" y n
chk "a malformed body is 400"               400 "$(code -X POST "$H/v1/chat/completions" -H "Authorization: Bearer $GK" -H "$J" -d 'not json')"
ur=$(code "$H/v1/nonsense" -H "Authorization: Bearer $GK")
chk "an unknown /v1 route is 404"           404 "$ur"
urb=$(body "$H/v1/nonsense" -H "Authorization: Bearer $GK")
printf '%s' "$urb" | grep -q 'not_found' \
  && chk "and says not_found" y y || chk "and says not_found" y n
# The DIRECTION matters as much as the code: `not_wired` carries a Retry-After,
# so a client told that polls for a route that is never going to appear. Asserted
# as the absence of the old answer, which is what actually regressed.
printf '%s' "$urb" | grep -q 'not_wired' \
  && chk "and does NOT claim the gateway is unwired" y n \
  || chk "and does NOT claim the gateway is unwired" y y

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
