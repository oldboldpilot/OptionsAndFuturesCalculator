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
# An unknown model and an unknown route are asserted on the property that is
# unambiguous -- they must NOT be admitted -- because both currently answer with
# a status this suite would otherwise have to bake in as correct. See the two
# KNOWN DEFECTS below.
um=$(code -X POST "$H/v1/chat/completions" -H "Authorization: Bearer $GK" -H "$J" -d '{"model":"no_such_model","max_tokens":8,"messages":[{"role":"user","content":"x"}]}')
[ "$um" != "200" ] && chk "an unknown model is NOT admitted" y y || chk "an unknown model is NOT admitted" y n
chk "a malformed body is 400"               400 "$(code -X POST "$H/v1/chat/completions" -H "Authorization: Bearer $GK" -H "$J" -d 'not json')"
ur=$(code "$H/v1/nonsense" -H "Authorization: Bearer $GK")
[ "$ur" != "200" ] && chk "an unknown route is NOT admitted" y y || chk "an unknown route is NOT admitted" y n

echo "— KNOWN DEFECTS (reported, not failed; fix upstream) —"
# 1. `GateTable::decide` tests `known_models_` BEFORE placement and the enum
#    documents ModelUnknown for it, but candidate selection refuses first, so an
#    absent model is reported as a PLACEMENT problem. Fails safe (it discloses
#    less) and still sends an operator to the wrong place.
printf '  NOTE  %-52s %s (enum documents model_unknown)\n' "unknown model reports no_permitted_placement" "$um"
# 2. An unknown route answers 503 "no admission path is wired behind it yet",
#    which is false whenever the admit check above returned 200 -- and a monitor
#    reads 503 as the gateway being down rather than the path not existing.
printf '  NOTE  %-52s %s (should be 404)\n' "unknown route reports not_wired" "$ur"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
