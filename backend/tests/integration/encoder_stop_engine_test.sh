#!/usr/bin/env bash
#
# Hermetic check of eqc_stop_last_engine (lib/encoder_cluster.sh): it must not return while the
# engine it signalled is still alive, because its port is only free once the process has gone.
# Stand-in "engines" are plain shell children; no weights, cluster or network are needed.
#
# @author Olumuyiwa Oluwasanmi
#
# Usage: encoder_stop_engine_test.sh <repo-root>

set -uo pipefail

REPO="${1:?usage: $0 <repo-root>}"
# shellcheck source=lib/encoder_cluster.sh
. "$REPO/backend/tests/integration/lib/encoder_cluster.sh"

now_ms() { echo $(( $(date +%s%N) / 1000000 )); }

# A stand-in that ignores SIGTERM and exits by itself 1 s after it is ready: a helper that only signals
# returns at once, while one that waits cannot return before the stand-in has exited. The helper must
# not signal before the stand-in has installed SIG_IGN -- signalled during interpreter start-up it dies
# at once (measured: 104 ms) and the check would measure the race, not the helper.
ready="$(mktemp)"; rm -f "$ready"
python3 -I -c 'import signal, sys, time; signal.signal(signal.SIGTERM, signal.SIG_IGN); open(sys.argv[1], "w").close(); time.sleep(1)' "$ready" &
ENGINE_PIDS+=("$!")
pid="${ENGINE_PIDS[-1]}"
for (( tick = 0; tick < 100; tick++ )); do [ -e "$ready" ] && break; sleep 0.05; done
[ -e "$ready" ] || fail "the stand-in never became ready"
rm -f "$ready"
t0="$(now_ms)"
eqc_stop_last_engine; rc=$?
elapsed=$(( $(now_ms) - t0 ))
check_eq "the helper reports success once the engine has exited" "$rc" 0
check_ge "and it waited for the engine instead of returning after the signal (ms)" "$elapsed" 900
if kill -0 "$pid" 2>/dev/null; then fail "the engine is still alive when the helper returns"; else pass "the engine has exited when the helper returns"; fi

# A stand-in that dies on SIGTERM is stopped promptly, not after the full bound.
sleep 30 &
ENGINE_PIDS+=("$!")
t0="$(now_ms)"
eqc_stop_last_engine; rc=$?
elapsed=$(( $(now_ms) - t0 ))
check_eq "an engine that obeys SIGTERM is stopped" "$rc" 0
if [ "$elapsed" -lt 5000 ]; then pass "and quickly ($elapsed ms)"; else fail "and quickly: took $elapsed ms"; fi

if [ "$FAILURES" -eq 0 ]; then echo "encoder_stop_engine_test: all checks passed"; exit 0; fi
echo "encoder_stop_engine_test: $FAILURES check(s) FAILED"
exit 1
