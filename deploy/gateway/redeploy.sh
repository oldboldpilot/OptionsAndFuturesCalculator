#!/usr/bin/env bash
#
# Re-image the PostgreSQL gateway tier and prove the new binary serves it.
#
# @author Olumuyiwa Oluwasanmi
#
# THIS IS NOT A CUTOVER, and the difference is the reason it exists. The
# gw_cutover*.sh scripts move the rig from the SQLite container to the
# PostgreSQL pair ONCE, and their failure path starts the SQLite container back
# up. That path is spent: the pg tier is already serving and the SQLite
# container is a cold rollback copy. What every later sensen gateway fix needs
# is this instead -- rebuild the image, replace the two containers, prove it.
# Re-running a one-way cutover to redeploy would stop a container that is
# already exited and roll back to a store that is no longer the system of
# record.
#
# THE GATE IS A RESTART, NOT A COLD /readyz, AND A COLD CHECK WOULD HAVE PASSED
# THE DEFECT THIS WAS WRITTEN FOR. sensen 64c3e1f5f fixed the gateway
# restart-looping twenty times on "port in use" while nothing listened: TBQWF
# passed SO_REUSEADDR | SO_REUSEPORT as ONE option NAME (2|15 == 15), and the
# bind probe passed no options at all, on the false premise that a plain bind
# tolerates TIME_WAIT. A container that has served NO traffic holds no socket
# in TIME_WAIT, so it answers /readyz on the broken image exactly as it does on
# the fixed one. `ss -ltn` being clean is not "port bindable" either. The
# sequence is therefore: deploy -> regression -> restart IMMEDIATELY -> require
# /readyz back inside RESTART_BUDGET -> regression again.
#
# IT DOES NOT ROLL BACK, deliberately. The PostgreSQL store is the system of
# record; starting the cold SQLite container on a failure here would serve
# older data and call it success. A failure leaves the containers exactly as
# they are and says so, because the next step is diagnosis and not traffic.
#
# Secrets travel as podman secrets and are never printed. Do not add a step
# that dumps container environment: those values include multi-line PEM, which
# line-based redaction leaks.
set -uo pipefail

SRC="${SRC:-/home/muyiwa/.cache/sensen-gwpg}"
OFC="${OFC:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
CFG="${CFG:-$HOME/.config/sensen-gateway}"
IMG="${IMG:-localhost/sensen-gateway:pg-tier}"
PORT="${PORT:-9443}"
BASE="https://127.0.0.1:${PORT}"
BIN="$SRC/build-gateway/bin/sensen-gateway"
# The stall it exists to catch ran up to 60 s. 20 s is comfortably above a
# healthy restart and comfortably below the defect, so it discriminates.
RESTART_BUDGET="${RESTART_BUDGET:-20}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

die(){ echo "FAIL: $*" >&2; exit 1; }
say(){ printf '%s\n' "$*"; }
ready(){ curl -sk -m3 "$BASE/readyz" 2>/dev/null | grep -q '"ready":true'; }
wait_ready(){ for _ in $(seq 1 "$1"); do ready && return 0; sleep 1; done; return 1; }

# --- 0. preflight: the binary is the committed code, and it is not stale ------
[ -x "$BIN" ] || die "no binary at $BIN -- build it first (ninja -C $SRC/build-gateway sensen-gateway)"
git -C "$SRC" diff --quiet HEAD -- agent/ || die "$SRC agent/ has uncommitted changes"
# General staleness, not one representative file: ANY agent/ source newer than
# the binary means the binary does not contain it.
STALE="$(find "$SRC/agent" -type f -newer "$BIN" -print -quit 2>/dev/null)"
[ -z "$STALE" ] || die "binary is older than $STALE -- rebuild"
SC="$(git -C "$SRC" rev-parse --short=9 HEAD)"
TC="$(git -C "$SRC/external/tbqwf" rev-parse --short=9 HEAD)"
TP="$(git -C "$SRC" ls-tree HEAD external/tbqwf | awk '{print substr($3,1,9)}')"
[ "$TC" = "$TP" ] || die "tbqwf checkout $TC is not the recorded pin $TP"
say "preflight ok: sensen=$SC tbqwf=$TC"

# --- 1. image, and prove the loader runs as the unprivileged uid --------------
CTX="$WORK/ctx"; mkdir -p "$CTX/runtime"
cp "$OFC/deploy/gateway/Dockerfile" "$OFC/deploy/gateway/entrypoint.sh" "$BIN" "$CTX/"
cp -L /usr/local/lib/x86_64-unknown-linux-gnu/{libc++.so.1,libc++abi.so.1,libunwind.so.1} "$CTX/runtime/"
SHA="$(sha256sum "$BIN" | cut -d' ' -f1)"
podman build -q --build-arg GATEWAY_SHA256="$SHA" --build-arg SENSEN_COMMIT="$SC" \
  --build-arg TBQWF_COMMIT="$TC" -t "$IMG" "$CTX" > "$WORK/img.log" 2>&1 \
  || die "image build failed: $(tail -3 "$WORK/img.log")"
LOAD="$(podman run --rm --entrypoint /usr/local/bin/sensen-gateway "$IMG" admin 2>&1 | head -1)"
case "$LOAD" in *"shared object"*) die "loader failed in image: $LOAD";; esac
say "image ok sha=${SHA:0:16} uid=$(podman run --rm --entrypoint id "$IMG" -u)"

# --- 2. replace the two containers -------------------------------------------
GK="$(cat "$CFG/regress.key" 2>/dev/null)" || true
[ -n "${GK:-}" ] || die "no regression key at $CFG/regress.key"
podman rm -f sensen-gw-pg sensen-gw-reaper-pg >/dev/null 2>&1
podman run -d --name sensen-gw-pg --network host --restart unless-stopped \
  -e SENSEN_GATEWAY_PORT="$PORT" -e SENSEN_GATEWAY_ORG=sensen \
  --secret sgw_tls_cert,type=env,target=SENSEN_GATEWAY_CERT_PEM \
  --secret sgw_tls_key,type=env,target=SENSEN_GATEWAY_KEY_PEM \
  --secret sgw_ticket_key,type=env,target=SENSEN_GATEWAY_TICKET_KEY_HEX \
  --secret sgw_store_dsn,type=env,target=SENSEN_GATEWAY_STORE \
  "$IMG" >/dev/null || die "gateway container did not start"
podman run -d --name sensen-gw-reaper-pg --network host --restart unless-stopped \
  -e OPENSSL_CONF=/dev/null \
  --secret sgw_maint_dsn,type=env,target=SGW_MAINT_DSN \
  --entrypoint /usr/local/bin/sensen-gateway \
  "$IMG" admin reap-loop env:SGW_MAINT_DSN >/dev/null || die "reaper container did not start"
wait_ready 60 || die "not ready in 60s: $(podman logs sensen-gw-pg 2>&1 | grep -v 'postgresql://' | tail -2 | tr '\n' ' ')"
say "readyz: $(curl -sk -m3 "$BASE/readyz" | grep -oE '"(ready|store_reachable|epoch_id|epoch_current|admission_wired)":[^,]*' | tr '\n' ' ')"

# --- 3. regression, which is also what puts a socket into TIME_WAIT ----------
bash "$OFC/deploy/gateway/regression.sh" "$BASE" "$GK" > "$WORK/regress.txt" 2>&1; rc=$?
say "regression rc=$rc $(grep -oE '[0-9]+ passed, [0-9]+ failed' "$WORK/regress.txt" | tail -1)"
[ $rc = 0 ] || { grep -E 'FAIL' "$WORK/regress.txt" | grep -v "$GK" | head -5; die "regression failed"; }

sleep 3
RL="$(podman logs sensen-gw-reaper-pg 2>&1 | grep -v 'postgresql://')"
printf '%s\n' "$RL" | grep -qE 'reaper: tick 1 outcome=(not_due|reaped) .*orgs_without_retention=0' \
  || die "reaper did not tick healthily: $(printf '%s' "$RL" | tail -2 | tr '\n' ' ')"
say "reaper ok: $(printf '%s' "$RL" | tail -1 | cut -c1-110)"

# --- 4. THE GATE: restart with sockets in TIME_WAIT -------------------------
say "restart test: budget ${RESTART_BUDGET}s"
T0=$SECONDS
podman restart sensen-gw-pg >/dev/null || die "restart command failed"
wait_ready "$RESTART_BUDGET" || die "NOT ready ${RESTART_BUDGET}s after a restart that followed live traffic -- the TIME_WAIT bind stall is present: $(podman logs --tail 5 sensen-gw-pg 2>&1 | grep -v 'postgresql://' | tr '\n' ' ')"
say "restart ok: ready in $((SECONDS-T0))s"
RESTARTS="$(podman inspect sensen-gw-pg --format '{{.RestartCount}}')"
say "restart count: $RESTARTS"

bash "$OFC/deploy/gateway/regression.sh" "$BASE" "$GK" > "$WORK/regress2.txt" 2>&1; rc=$?
say "regression after restart rc=$rc $(grep -oE '[0-9]+ passed, [0-9]+ failed' "$WORK/regress2.txt" | tail -1)"
[ $rc = 0 ] || die "regression failed after restart"

say "containers: $(podman ps -a --format '{{.Names}}:{{.Status}}' | grep sensen-gw | tr '\n' ' ')"
say "REDEPLOY COMPLETE sensen=$SC tbqwf=$TC"
