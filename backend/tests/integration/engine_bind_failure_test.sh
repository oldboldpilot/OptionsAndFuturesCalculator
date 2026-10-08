#!/usr/bin/env bash
#
# An engine whose listener cannot bind must say so and exit, not crash.
#
# @author Olumuyiwa Oluwasanmi
#
# grpc::ServerBuilder::BuildAndStart() returns null when the port is held by a process that did not
# set SO_REUSEPORT, and RunServer() dereferenced the result: a SEGV on the main thread, which reads
# as an engine bug and names no port. Observed 2026-10-07 when a benchmark engine lost its port to
# another process between reserving it and binding it; the bring-up carried on with a dead engine
# and reported 120 transport errors.
#
# The port is held by a python process for the whole run, so the engine cannot bind it whichever
# order they start in.
#
# Usage: engine_bind_failure_test.sh <calculator_engine>
set -uo pipefail
ENGINE_BIN="${1:?usage: $0 <calculator_engine>}"
[ -x "$ENGINE_BIN" ] || { echo "SKIP: no engine binary"; exit 77; }
WORK="$(mktemp -d)"
holder=""
cleanup() { [ -n "$holder" ] && kill "$holder" 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT

python3 -P -c '
import socket, sys, time
s = socket.socket()
s.bind(("0.0.0.0", 0))
s.listen(1)
print(s.getsockname()[1], flush=True)
time.sleep(120)
' > "$WORK/port" &
holder=$!
for _ in $(seq 1 50); do [ -s "$WORK/port" ] && break; sleep 0.1; done
PORT="$(cat "$WORK/port")"
[ -n "$PORT" ] || { echo "FAIL: could not hold a port"; exit 1; }

env -i PATH="$PATH" HOME="${HOME:-/tmp}" ${LD_LIBRARY_PATH:+LD_LIBRARY_PATH="$LD_LIBRARY_PATH"} \
    ENGINE_GRPC_PORT="$PORT" QUOTA_POLICY= DATABASE_URL= \
    timeout 60 "$ENGINE_BIN" > "$WORK/engine.log" 2>&1
rc=$?

fail=0
if [ "$rc" -eq 1 ]; then echo "  PASS: the engine exited with status 1 (got $rc)"; else echo "  FAIL: expected exit status 1, got $rc (139 is the SEGV this guards against, 124 a hang)"; fail=1; fi
if grep -q "could not start the gRPC server on 0.0.0.0:$PORT" "$WORK/engine.log"; then echo "  PASS: and it named the address it could not bind"; else echo "  FAIL: no message naming the address"; fail=1; fi
[ "$fail" -eq 0 ] && echo "engine_bind_failure_test: all checks passed" || { echo "engine_bind_failure_test: FAILED"; tail -n 5 "$WORK/engine.log" | cut -c1-200; }
exit "$fail"
