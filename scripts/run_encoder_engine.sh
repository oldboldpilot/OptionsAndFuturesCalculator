#!/usr/bin/env bash
# Start ONE engine on its own port with the mortgage ENCODER backend, and REFUSE
# rather than report if the measurement could not be attributed to it.
#
# Every refusal here is a measurement this project has already paid for:
#
#   * Engines bind :50051 with SO_REUSEPORT, so a leaked engine makes the kernel
#     SPLIT requests between two binaries and both arms are partly served by the
#     wrong one. `scripts/measure_restricted_projection.py` turned 0.99x into
#     1.54x on identical code purely by refusing that state. So: zero engines and
#     a free port BEFORE the boot, and exactly one engine alive after it.
#   * `pgrep -x calculator_engine` matches NOTHING -- `comm` is truncated to 15
#     characters, so the name to match is `calculator_engi`.
#   * A long-lived engine keeps serving a binary that no longer exists;
#     /proc/<pid>/exe reads `... (deleted)` while `ps` looks perfectly healthy.
#     Checked, because smoke-testing that engine measures the OLD build.
#   * Sourcing config/.env in a shell DELETES the quotes from its bare-JSON
#     values, which silently disables the API-key and quota gates. The engine is
#     started through scripts/run_with_env.py, which execve()s instead.
#   * A readiness probe that asks "did someone answer this port?" cannot tell
#     this engine from any engine, so readiness is taken from THIS process's own
#     log: its parsed backend line and its `model is LOADED`.
#
# Usage: scripts/run_encoder_engine.sh <log-path> [port] [extra --set K=V ...]
set -u -o pipefail

ROOT="/home/muyiwa/Development/OptionsAndFuturesCalculator"
LOG="${1:?usage: run_encoder_engine.sh <log-path> [port] [--set K=V ...]}"
PORT="${2:-50061}"
shift 2 2>/dev/null || shift 1
ENGINE="${ENCODER_ENGINE_BIN:-$ROOT/backend/build-enc/calculator_engine}"
MODEL="${ENCODER_MODEL:-$ROOT/backend/models/mortgage-encoder.gguf}"

[ -x "$ENGINE" ] || { echo "FATAL: no engine at $ENGINE" >&2; exit 2; }
[ -r "$MODEL" ]  || { echo "FATAL: no encoder model at $MODEL" >&2; exit 2; }

# --- PRECONDITION: zero engines, free port ---------------------------------
alive=$(pgrep -x calculator_engi | wc -l)
if [ "$alive" -ne 0 ]; then
  echo "FATAL: $alive calculator_engine process(es) already alive." >&2
  pgrep -ax calculator_engi >&2
  echo "SO_REUSEPORT would split requests between them; refusing to measure." >&2
  exit 3
fi
if ss -ltn "sport = :$PORT" 2>/dev/null | grep -q ":$PORT"; then
  echo "FATAL: something is listening on :$PORT already; refusing." >&2
  exit 3
fi

# --- BOOT -------------------------------------------------------------------
: > "$LOG"
"$ROOT/scripts/run_with_env.py" \
  --set MORTGAGE_ASSISTANT_BACKEND=encoder \
  --set MORTGAGE_ENCODER_PATH="$MODEL" \
  --set ENGINE_GRPC_PORT="$PORT" \
  --set PRO_GATE_MODE=off \
  --set INFERENCE_QUEUE=local \
  --set DATABASE_URL= \
  --set QUOTA_POLICY= \
  "$@" \
  -- "$ENGINE" >>"$LOG" 2>&1 &
launcher=$!

# --- READINESS, FROM THIS ENGINE'S OWN LOG ---------------------------------
# `$!` is the LAUNCHER (run_with_env.py execve()s, so in fact it becomes the
# engine -- but a wrapper in the chain would make the pid a lie). The engine's
# real pid is recovered by name and asserted to be unique.
for _ in $(seq 1 120); do
  if grep -q 'Mortgage assistant model is LOADED' "$LOG" 2>/dev/null; then break; fi
  if ! kill -0 "$launcher" 2>/dev/null; then
    echo "FATAL: the engine exited during boot. Tail of $LOG:" >&2
    tail -25 "$LOG" >&2
    exit 4
  fi
  sleep 1
done

if ! grep -q 'Mortgage assistant model is LOADED' "$LOG"; then
  echo "FATAL: the engine never logged 'Mortgage assistant model is LOADED'." >&2
  tail -25 "$LOG" >&2
  exit 4
fi
# The PARSED backend, not the variable that asked for it -- the
# MORTGAGE_RESTRICTED_PROJECTION lesson: log what you parsed.
if ! grep -q 'MORTGAGE_ASSISTANT_BACKEND=encoder' "$LOG"; then
  echo "FATAL: this engine did not parse the ENCODER backend." >&2
  grep -i 'backend' "$LOG" >&2
  exit 4
fi

# --- POSTCONDITION: exactly one engine, and it is THIS binary --------------
alive=$(pgrep -x calculator_engi | wc -l)
if [ "$alive" -ne 1 ]; then
  echo "FATAL: $alive engines alive after boot; refusing to measure." >&2
  pgrep -ax calculator_engi >&2
  exit 3
fi
pid=$(pgrep -x calculator_engi)
exe=$(readlink "/proc/$pid/exe" 2>/dev/null || echo '?')
case "$exe" in
  *'(deleted)') echo "FATAL: /proc/$pid/exe is $exe -- this engine serves a binary that no longer exists." >&2; exit 5;;
esac
if [ "$exe" != "$ENGINE" ]; then
  echo "FATAL: the live engine is $exe, not $ENGINE." >&2
  exit 5
fi

echo "ENGINE_PID=$pid"
echo "ENGINE_EXE=$exe"
echo "ENGINE_PORT=$PORT"
grep -E 'ENCODER assistant ready|MORTGAGE_ASSISTANT_BACKEND|model is LOADED' "$LOG" | sed 's/^/  /'
