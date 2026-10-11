#!/usr/bin/env bash
# Gate: the two container entry scripts set their memory knobs instead of inheriting defaults that
# were tuned for a different machine: backend/start.sh (the engine + Envoy) and
# deploy/gateway/entrypoint.sh (sensen-gateway).
#
# @author Olumuyiwa Oluwasanmi
#
# WHY THIS EXISTS. Envoy defaults `--concurrency` to the number of hardware threads it can see,
# and Railway shows this container 48 of them. Envoy then starts 48 worker threads, each with its
# own listener sockets, connection pools, stats scope and per-thread allocator cache, to serve an
# edge that its own local rate limit caps at ten requests per second per replica. The workers
# cost resident memory on every replica for capacity nothing can use, and Railway bills RAM by
# the MB-minute.
#
# THE SECOND KNOB IS THE ENGINE'S ALLOCATOR. The engine links jemalloc, whose default keeps freed
# pages dirty until some LATER allocation happens to purge them, and a quiet service allocates
# nothing for long stretches -- so after a burst the resident set stays at its peak indefinitely
# (measured 83-92 MB against a 57 MB idle floor, still 83-92 MB thirty seconds later). A purge on
# a background thread releases it without adding any work to the request path.
#
# WHAT IT ASSERTS, against the real start.sh and not a copy of its logic:
#   1. unset            -> Envoy is launched with `--concurrency 2`
#   2. a valid value    -> Envoy is launched with exactly that value
#   3. an invalid value -> start.sh REFUSES to start (exit 1, names the variable) and never
#                          launches Envoy. A typo must not silently serve the default the
#                          operator meant to override, which is the MORTGAGE_WEIGHT_STORE rule.
#   4. MALLOC_CONF unset -> the ENGINE starts with the project's allocator settings
#   5. MALLOC_CONF set   -> the engine gets the operator's value verbatim, not ours
#   6. Envoy never sees MALLOC_CONF: it links tcmalloc, and a jemalloc option string means
#      nothing to it. The setting is scoped to the one process it was measured on.
#   7. deploy/gateway/entrypoint.sh: MALLOC_CONF unset -> sensen-gateway starts with the gateway's
#      allocator settings; set by the operator -> passed through verbatim. The gateway holds the
#      memory of a 200-connection TLS burst at 52 MB until something purges it (measured); the
#      same burst settles at 24 MB with a background purge.
#
# HOW. The script is run inside a throwaway container (podman, `--network none`) whose only
# contents are the real start.sh and two stand-ins: an `envoy` that records its argv and an
# engine that sleeps. start.sh addresses /etc/envoy, /app and /usr/local/bin by absolute path,
# so a container is the one place its real code path can run unmodified without root.
#
# POSITIVE CONTROL. Arm 1 reads the argv the stand-in recorded; if podman ran but recorded
# nothing, that is a FAILURE of the harness and is reported as one, never as a pass.
#
# Exit 0 = all arms passed, 1 = a check failed, 77 = podman is unavailable (a named skip).
set -uo pipefail
cd "$(dirname "$0")/.." || exit 9

START=backend/start.sh
ENTRYPOINT=deploy/gateway/entrypoint.sh
[ -f "$START" ] || { echo "FAIL: $START does not exist"; exit 1; }
[ -f "$ENTRYPOINT" ] || { echo "FAIL: $ENTRYPOINT does not exist"; exit 1; }

command -v podman >/dev/null 2>&1 || { echo "SKIP: podman is not installed"; exit 77; }

IMAGE=${START_TEST_IMAGE:-docker.io/library/ubuntu:24.04}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cat >"$WORK/fake_envoy" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >/out/envoy.argv
printf '%s\n' "${MALLOC_CONF-<unset>}" >/out/envoy.malloc_conf
exec sleep 1
EOF
cat >"$WORK/fake_engine" <<'EOF'
#!/bin/bash
printf '%s\n' "${MALLOC_CONF-<unset>}" >/out/engine.malloc_conf
exec sleep 30
EOF
cat >"$WORK/run_start.sh" <<'EOF'
#!/bin/bash
# Runs inside the container: lay down the stand-ins, run the REAL start.sh, report what happened.
mkdir -p /app /etc/envoy
cp /in/fake_engine /app/calculator_engine
cp /in/fake_envoy /usr/local/bin/envoy
chmod +x /app/calculator_engine /usr/local/bin/envoy
: >/etc/envoy/envoy.yaml
rm -f /out/envoy.argv /out/start.out /out/start.rc /out/engine.malloc_conf /out/envoy.malloc_conf
bash /in/start.sh >/out/start.out 2>&1
echo $? >/out/start.rc
EOF
cat >"$WORK/fake_gateway" <<'EOF'
#!/bin/bash
printf '%s\n' "${MALLOC_CONF-<unset>}" >/out/gateway.malloc_conf
exit 0
EOF
cat >"$WORK/run_entrypoint.sh" <<'EOF'
#!/bin/bash
# Runs inside the container: stand-in sensen-gateway, REAL entrypoint.sh.
cp /in/fake_gateway /usr/local/bin/sensen-gateway
chmod +x /usr/local/bin/sensen-gateway
rm -f /out/gateway.malloc_conf /out/start.rc
SENSEN_GATEWAY_CERT_PEM=x SENSEN_GATEWAY_KEY_PEM=x SENSEN_GATEWAY_TICKET_KEY_HEX=x SENSEN_GATEWAY_STORE=/x \
  bash /in/entrypoint.sh >/out/start.out 2>&1
echo $? >/out/start.rc
EOF
cp "$START" "$WORK/start.sh"
cp "$ENTRYPOINT" "$WORK/entrypoint.sh"
chmod +x "$WORK/fake_envoy" "$WORK/fake_engine" "$WORK/fake_gateway" "$WORK/run_start.sh" "$WORK/run_entrypoint.sh"

# run_case <name> [ENV=VAL ...]  ->  leaves $WORK/out/<name>/{envoy.argv,start.out,start.rc}
# INNER selects which entry script is exercised (default: backend/start.sh).
INNER=run_start.sh
run_case() {
  local name=$1; shift
  local out="$WORK/out/$name"; mkdir -p "$out"
  local envargs=()
  local e; for e in "$@"; do envargs+=(-e "$e"); done
  podman run --rm --network none --security-opt label=disable \
    -v "$WORK:/in:ro" -v "$out:/out" "${envargs[@]}" "$IMAGE" bash "/in/$INNER" >"$out/podman.out" 2>&1
  return $?
}

fail=0
ok()  { printf '  PASS  %s\n' "$1"; }
bad() { printf '  FAIL  %s\n' "$1"; fail=1; }

# Does the harness work at all? Arm 1 doubles as the positive control.
run_case unset
if [ $? -ne 0 ] && [ ! -f "$WORK/out/unset/start.rc" ]; then
  echo "SKIP: podman could not run $IMAGE ($(head -c 200 "$WORK/out/unset/podman.out" | tr '\n' ' '))"
  exit 77
fi

argv() { cat "$WORK/out/$1/envoy.argv" 2>/dev/null; }

if [ ! -s "$WORK/out/unset/envoy.argv" ]; then
  bad "harness: the stand-in envoy recorded no argv (start.sh never launched it, or the stand-in did not run)"
else
  a=$(argv unset)
  case " $a " in
    *" --concurrency 2 "*) ok "unset -> --concurrency 2" ;;
    *) bad "unset -> expected '--concurrency 2', Envoy was launched with: $a" ;;
  esac
fi

run_case valid ENVOY_CONCURRENCY=7
a=$(argv valid)
case " $a " in
  *" --concurrency 7 "*) ok "ENVOY_CONCURRENCY=7 -> --concurrency 7" ;;
  *) bad "ENVOY_CONCURRENCY=7 -> expected '--concurrency 7', Envoy was launched with: ${a:-<never launched>}" ;;
esac

for v in abc 0 -1 2.5 ' 3' '4 '; do
  run_case "invalid" "ENVOY_CONCURRENCY=$v"
  rc=$(cat "$WORK/out/invalid/start.rc" 2>/dev/null || echo missing)
  if [ "$rc" = "1" ] && [ ! -e "$WORK/out/invalid/envoy.argv" ] \
     && grep -q 'ENVOY_CONCURRENCY' "$WORK/out/invalid/start.out"; then
    ok "ENVOY_CONCURRENCY='$v' is refused (exit 1, Envoy never launched)"
  else
    bad "ENVOY_CONCURRENCY='$v' should be refused: rc=$rc, envoy launched=$([ -e "$WORK/out/invalid/envoy.argv" ] && echo yes || echo no)"
  fi
done

# --- allocator arms -------------------------------------------------------------------------
EXPECTED_CONF='narenas:2,background_thread:true,dirty_decay_ms:3000'
conf_of() { cat "$WORK/out/$1/$2.malloc_conf" 2>/dev/null || echo "<never ran>"; }

# The default case was already run above as "unset".
c=$(conf_of unset engine)
if [ "$c" = "$EXPECTED_CONF" ]; then ok "MALLOC_CONF unset -> engine runs with $EXPECTED_CONF"
else bad "MALLOC_CONF unset -> expected the engine to get '$EXPECTED_CONF', it got: $c"; fi

run_case operator MALLOC_CONF=dirty_decay_ms:0
c=$(conf_of operator engine)
if [ "$c" = "dirty_decay_ms:0" ]; then ok "MALLOC_CONF set by the operator reaches the engine verbatim"
else bad "an operator's MALLOC_CONF must not be overridden: the engine got: $c"; fi

c=$(conf_of unset envoy)
if [ "$c" = "<unset>" ]; then ok "Envoy does not inherit MALLOC_CONF (it links tcmalloc)"
else bad "Envoy must not receive MALLOC_CONF, it got: $c"; fi

# --- the gateway entrypoint -----------------------------------------------------------------
GW_EXPECTED_CONF='narenas:4,background_thread:true,dirty_decay_ms:3000'
INNER=run_entrypoint.sh
run_case gw_unset
c=$(conf_of gw_unset gateway)
if [ "$c" = "$GW_EXPECTED_CONF" ]; then ok "gateway: MALLOC_CONF unset -> sensen-gateway runs with $GW_EXPECTED_CONF"
else bad "gateway: MALLOC_CONF unset -> expected '$GW_EXPECTED_CONF', sensen-gateway got: $c"; fi

run_case gw_operator MALLOC_CONF=dirty_decay_ms:0
c=$(conf_of gw_operator gateway)
if [ "$c" = "dirty_decay_ms:0" ]; then ok "gateway: an operator's MALLOC_CONF reaches sensen-gateway verbatim"
else bad "gateway: an operator's MALLOC_CONF must not be overridden: it got: $c"; fi

if [ "$fail" -ne 0 ]; then echo "check_start_memory_knobs: FAILED"; exit 1; fi
echo "check_start_memory_knobs: all arms passed"
