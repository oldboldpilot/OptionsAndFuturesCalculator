#!/usr/bin/env bash
#
# @author Olumuyiwa Oluwasanmi
#
# Shared bring-up for the encoder-on-the-queue gate and benchmark: a REAL three-node SGEE cluster,
# and REAL engines holding the REAL encoder weights, on ports from the one registry, every process
# attributed by PID or by the SGEE run token (never by name -- other runs share this machine).
#
# A caller sets NODE_BIN, ENGINE_BIN and REPO, then calls `eqc_init`, which returns 77 when the
# weights or the python gRPC stack are absent (a gate that passes because its input was missing is
# worse than no gate), and afterwards `eqc_start_cluster` and `eqc_start_engine`.

# shellcheck source=/dev/null
. "$REPO/backend/external/SGEE/tests/integration/lib/queue_node_attribution.sh"

declare -a ENGINE_PIDS=()
FAILURES=0

eqc_log()  { printf '[eqc] %s\n' "$*"; }
pass() { printf '  PASS: %s\n' "$*"; }
fail() { printf '  FAIL: %s\n' "$*"; FAILURES=$(( FAILURES + 1 )); }
check_eq() { if [ "$2" = "$3" ]; then pass "$1 ($2)"; else fail "$1: expected $3, got $2"; fi; }
check_ge() { if [ "$2" -ge "$3" ] 2>/dev/null; then pass "$1 ($2 >= $3)"; else fail "$1: $2 is not >= $3"; fi; }

eqc_find_model() {
    local name="$1" d common
    common="$(git -C "$REPO" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
    for d in "${ENCODER_MODEL_DIR:-}" "$REPO/backend/models" "${common:+$common/../backend/models}"; do
        [ -n "$d" ] && [ -r "$d/$name" ] && { printf '%s' "$d/$name"; return 0; }
    done
    return 1
}

eqc_init() {
    PROBE="$REPO/scripts/encoder_queue_probe.py"
    STRATEGY_GGUF="$(eqc_find_model strategy-encoder.gguf)" || { echo "SKIP: strategy-encoder.gguf not found (set ENCODER_MODEL_DIR)"; return 77; }
    python3 -P -c 'import grpc, google.protobuf' 2>/dev/null || { echo "SKIP: python3 grpc/protobuf not importable"; return 77; }
    { [ -x "$NODE_BIN" ] && [ -x "$ENGINE_BIN" ]; } || { echo "SKIP: node or engine binary missing"; return 77; }

    SGEE_NODE_BIN="$NODE_BIN"
    SGEE_RUN_TOKEN="$(qn_run_token)"
    qn_init
    WORK="$(mktemp -d)"
    trap eqc_cleanup EXIT

    local window base i
    window="$(qn_reserve_ports 16)" || { echo "FAIL: no free port window" >&2; return 1; }
    base="${window%% *}"
    declare -gA CPORT QPORT HPORT
    for i in 1 2 3; do
        CPORT[$i]=$(( base + (i - 1) * 3 )); QPORT[$i]=$(( base + (i - 1) * 3 + 1 )); HPORT[$i]=$(( base + (i - 1) * 3 + 2 ))
    done
    PORT_A=$(( base + 9 )); PORT_B=$(( base + 10 )); PORT_L=$(( base + 11 )); PORT_S=$(( base + 12 )); PORT_D=$(( base + 13 )); PORT_P=$(( base + 14 ))
    PEERS="1=[::1]:${CPORT[1]},2=[::1]:${CPORT[2]},3=[::1]:${CPORT[3]}"
    CLIENT_PEERS="1=[::1]:${QPORT[1]},2=[::1]:${QPORT[2]},3=[::1]:${QPORT[3]}"
}

eqc_cleanup() {
    local p
    for p in "${ENGINE_PIDS[@]}"; do kill -TERM "$p" 2>/dev/null || true; done
    sleep 0.5
    for p in "${ENGINE_PIDS[@]}"; do kill -KILL "$p" 2>/dev/null || true; done
    qn_kill_self || true
    if [ "${KEEP_WORK_DIR:-0}" = "1" ] || [ "$FAILURES" -ne 0 ]; then
        printf '[eqc] work dir kept: %s\n' "$WORK"
    else
        rm -rf "$WORK"
    fi
}

eqc_start_node() {
    local i="$1"
    mkdir -p "$WORK/node$i"
    (
        export SGEE_NODE_ID="$i" SGEE_PEERS="$PEERS" SGEE_CONSENSUS_PORT="${CPORT[$i]}"
        export SGEE_QUEUE_PORT="${QPORT[$i]}" SGEE_DATA_DIR="$WORK/node$i" PORT="${HPORT[$i]}" SGEE_SNAPSHOT_RETENTION_MS=21600000
        export SGEE_RUN_TOKEN="$SGEE_RUN_TOKEN"
        exec env ${EQC_NODE_ENV:-} "$NODE_BIN"
    ) >> "$WORK/node$i.log" 2>&1 &
}

eqc_leader() {
    local i
    for i in 1 2 3; do
        if curl -sf -m 2 "http://[::1]:${HPORT[$i]}/statusz" 2>/dev/null | grep -q '"is_leader": true'; then
            printf '%s' "$i"; return 0
        fi
    done
    return 1
}

eqc_start_cluster() {
    local i deadline leader=""
    for i in 1 2 3; do eqc_start_node "$i"; done
    deadline=$(( SECONDS + 40 ))
    while [ "$SECONDS" -lt "$deadline" ]; do leader="$(eqc_leader)" && break; sleep 0.25; done
    [ -n "$leader" ] || { echo "FAIL: no leader elected"; return 1; }
    eqc_log "SGEE cluster up, leader is node $leader"
}

# The highest last_applied any node reports: how much the cluster's log has moved.
eqc_applied() {
    local i best=0 v
    for i in 1 2 3; do
        v="$(curl -sf -m 2 "http://[::1]:${HPORT[$i]}/statusz" 2>/dev/null | grep -o '"last_applied": *[0-9]*' | grep -o '[0-9]*$' || true)"
        [ -n "$v" ] && [ "$v" -gt "$best" ] && best="$v"
    done
    printf '%s' "$best"
}

# The environment an engine under test is started with, as the array EQC_ENGINE_ENV: nothing is
# inherited (a stray variable in the caller's shell must not decide what is being measured), and
# later K=V arguments override earlier ones.
eqc_engine_env() {   # <port> <queue-mode> [K=V ...]
    local port="$1" mode="$2"; shift 2
    EQC_ENGINE_ENV=(env -i PATH="$PATH" HOME="${HOME:-/tmp}" ${LD_LIBRARY_PATH:+LD_LIBRARY_PATH="$LD_LIBRARY_PATH"}
        ENGINE_GRPC_PORT="$port" INFERENCE_QUEUE="$mode" SGEE_PEERS="$CLIENT_PEERS"
        ASSISTANT_MODEL=encoder STRATEGY_ENCODER_PATH="$STRATEGY_GGUF"
        PRO_GATE_MODE=off QUOTA_POLICY= DATABASE_URL= "$@")
}

# eqc_engine_refuses <name> <port> <queue-mode> [K=V ...]: an engine that is expected to REFUSE to
# boot. Prints its exit status; 124 means it was still running after 30 s (and was stopped, by
# `timeout`, which signals only the child it started).
eqc_engine_refuses() {
    local name="$1" port="$2" mode="$3" rc; shift 3
    : > "$WORK/$name.log"
    eqc_engine_env "$port" "$mode" "$@"
    "${EQC_ENGINE_ENV[@]}" timeout 30 stdbuf -oL -eL "$ENGINE_BIN" >> "$WORK/$name.log" 2>&1
    rc=$?
    printf '%s' "$rc"
}

# eqc_start_engine <name> <port> <queue-mode> [K=V ...]   (extra variables for the engine)
eqc_start_engine() {
    local name="$1" port="$2" mode="$3"; shift 3
    : > "$WORK/$name.log"
    eqc_engine_env "$port" "$mode" "$@"
    "${EQC_ENGINE_ENV[@]}" setsid stdbuf -oL -eL "$ENGINE_BIN" >> "$WORK/$name.log" 2>&1 &
    ENGINE_PIDS+=("$!")
    local deadline=$(( SECONDS + 60 )) pid="${ENGINE_PIDS[-1]}"
    while [ "$SECONDS" -lt "$deadline" ]; do
        # Ready means the weights loaded AND this process is accepting connections on its port: the
        # boot banner is printed before the listener binds, and an engine that loses its port dies
        # after it, so the log alone has certified a dead engine.
        if [ "$(grep -c 'ENCODER assistant ready' "$WORK/$name.log")" -ge 1 ] \
           && (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
            return 0
        fi
        kill -0 "$pid" 2>/dev/null || { echo "engine $name died:"; tail -n 8 "$WORK/$name.log"; return 1; }
        sleep 0.25
    done
    echo "engine $name did not become ready:"; tail -n 8 "$WORK/$name.log"; return 1
}

# Counts read from an engine's own log: the per-replica half of the work-sharing proof.
eqc_executed()  { grep -c "$2 executed leased job" "$WORK/$1.log" || true; }   # <engine> <label>
eqc_fallbacks() { grep -cE 'falling back to the local backend|answering locally instead' "$WORK/$1.log" || true; }

# The raw encoder answer the SERVICE logged for each request, before any verification -- the line
# every request writes. This is the only place the comparison between local and queue modes is
# meaningful over the whole holdout: without live market data the RPC answers most rows
# DATA_UNAVAILABLE whatever the model said, so comparing responses alone would compare refusals to
# refusals.
EQC_RAW_TAG='\[assistant\] raw model output'
eqc_raw_count() { grep -c "$EQC_RAW_TAG" "$WORK/$1.log" || true; }                          # <engine>
eqc_raw_since() { grep "$EQC_RAW_TAG" "$WORK/$1.log" | tail -n "+$(( $2 + 1 ))" | sort; }    # <engine> <count0>

# How the SERVICE says it answered each request (EncoderService::answer logs one line per request): the
# submit-side half of the accounting, independent of the worker-side "executed" count.
eqc_queued()    { grep -c "$2 spilled to the queue" "$WORK/$1.log" || true; }                   # <engine> <label>
eqc_inprocess() { grep -c "$2 answered in-process" "$WORK/$1.log" || true; }                    # <engine> <label>
# A request the admission layer answered ITSELF after the queue failed it. The queue's outcome and a
# local fallback's are the same bytes (ok, with an answer), so only the admission can say which this
# was; before it did, every fallback was logged as if the queue had answered and
# `queued == executed` was false by exactly the number of fallbacks.
#
# Per request the service writes ONE of three lines: "answered in-process", "spilled to the queue and
# was answered by it", "answered locally after the shared queue degraded". A request that was busy
# and found no queue slot is "answered in-process" with a suffix, so the first pattern counts it too.
eqc_degraded()  { grep -c "$2 answered locally after the shared queue degraded" "$WORK/$1.log" || true; }   # <engine> <label>

# --- the shared accounting checks, used by the SGEE gate and by the Postgres check ----------------
#
# THE ENGINES THESE RUN AGAINST (A and B) ARE STARTED WITH ENCODER_LOCAL_MAX_IN_FLIGHT=0, which makes
# every request "busy" and so sends it to the queue first: the routing the service had before
# 2026-10-07, kept as the lever that exercises the QUEUE PATH (sharing, partitioning, byte identity,
# degrade) at a concurrency of one. The default routing -- in-process first, spill when busy -- has
# its own sections in the cluster test, because at the defaults nothing here would reach the queue.
#
# SEQUENTIAL ON PURPOSE. An assistant keeps at most ENCODER_QUEUE_MAX_IN_FLIGHT (default 1) requests on
# the queue and answers the overflow itself, so concurrent callers would be answered partly in-process
# and "executed + fallen back == sent" would stop being checkable from the log. One caller at a time
# keeps the bound out of play, which is what makes the accounting exact. The overload run below is
# the opposite on purpose.
eqc_burst() {   # <surface> <label>
    local surface="$1" label="$2"
    local a0 b0 a1 b1 f0 f1 l0 l1 q0 q1
    a0="$(eqc_executed A "$label")"; b0="$(eqc_executed B "$label")"
    f0="$(eqc_fallbacks A)"; l0="$(eqc_applied)"; q0="$(eqc_queued A "$label")"
    python3 -P "$PROBE" latency --target "127.0.0.1:$PORT_A" -c 1 \
        -n "$EQC_BURST" --warmup 0 > "$WORK/burst_$surface.json" 2>&1 \
        || { fail "$surface requests reported errors"; cat "$WORK/burst_$surface.json"; }
    sleep 0.5
    a1="$(eqc_executed A "$label")"; b1="$(eqc_executed B "$label")"
    f1="$(eqc_fallbacks A)"; l1="$(eqc_applied)"; q1="$(eqc_queued A "$label")"
    eqc_log "$surface: $EQC_BURST requests sent to A: A executed $((a1-a0)), B executed $((b1-b0)), cluster log +$((l1-l0))"
    check_eq "$surface: A reports every request spilled to the queue and answered by it" "$(( q1-q0 ))" "$EQC_BURST"
    check_eq "$surface: replicas A + B executed exactly the requests sent" "$(( (a1-a0) + (b1-b0) ))" "$EQC_BURST"
    check_ge "$surface: replica B executed work submitted to A" "$(( b1-b0 ))" 1
    check_ge "$surface: replica A executed some of its own requests too" "$(( a1-a0 ))" 1
    check_eq "$surface: no request fell back to a local answer" "$(( f1-f0 ))" 0
    if [ "${EQC_QUEUE:-sgee}" = sgee ]; then
        check_ge "$surface: the cluster log moved by at least enqueue+lease+complete per request" "$(( l1-l0 ))" "$(( 3*EQC_BURST ))"
    fi
}

# eqc_replay <surface> <label> <rows>: the corpus replayed in local mode (engine L, eight callers) and
# through the queue (engine A, ONE caller so the bound is never met). Asserts every request that
# reached the encoder was carried by the queue and executed by some replica, and that the raw encoder
# answers and the served answers are byte-identical between the two modes.
eqc_replay() {   # <surface> <label> <rows>
    local surface="$1" label="$2" rows="$3" a0 b0 a1 b1 f0 f1 calls
    local rl0 rq0 q0 q1 p0 p1
    rl0="$(eqc_raw_count L)"
    python3 -P "$PROBE" dump --target "127.0.0.1:$PORT_L" -c 8 --limit "$rows" --out "$WORK/local_$surface.jsonl" \
        > "$WORK/dump_local_$surface.out" 2>&1 || { fail "$surface local dump had transport errors"; cat "$WORK/dump_local_$surface.out"; }
    a0="$(eqc_executed A "$label")"; b0="$(eqc_executed B "$label")"; f0="$(eqc_fallbacks A)"; rq0="$(eqc_raw_count A)"
    q0="$(eqc_queued A "$label")"; p0="$(eqc_inprocess A "$label")"
    python3 -P "$PROBE" dump --target "127.0.0.1:$PORT_A" -c 1 --limit "$rows" --out "$WORK/queue_$surface.jsonl" \
        > "$WORK/dump_queue_$surface.out" 2>&1 || { fail "$surface queue dump had transport errors"; cat "$WORK/dump_queue_$surface.out"; }
    sleep 0.5
    a1="$(eqc_executed A "$label")"; b1="$(eqc_executed B "$label")"; f1="$(eqc_fallbacks A)"
    q1="$(eqc_queued A "$label")"; p1="$(eqc_inprocess A "$label")"
    calls="$(python3 -P -c 'import json,sys; print(sum(len(json.loads(l)["calls"]) for l in open(sys.argv[1])))' "$WORK/queue_$surface.jsonl")"
    check_ge "$surface replay: the queue carried the requests that reached the encoder" "$(( q1-q0 ))" 1
    check_eq "$surface replay: every one of them was executed by a replica" "$(( (a1-a0) + (b1-b0) ))" "$(( q1-q0 ))"
    check_eq "$surface replay: none was answered in-process (one caller at a time never meets the bound)" "$(( p1-p0 ))" 0
    check_eq "$surface replay: no call fell back to a local answer" "$(( f1-f0 ))" 0
    eqc_raw_since L "$rl0" > "$WORK/raw_local_$surface.txt"
    eqc_raw_since A "$rq0" > "$WORK/raw_queue_$surface.txt"
    if [ -s "$WORK/raw_local_$surface.txt" ] && cmp -s "$WORK/raw_local_$surface.txt" "$WORK/raw_queue_$surface.txt"; then
        pass "$surface replay: the $(wc -l < "$WORK/raw_queue_$surface.txt") raw encoder answers the service logged are identical between local and queue modes"
    else
        fail "$surface replay: raw encoder answers differ between local and queue modes ($(wc -l < "$WORK/raw_local_$surface.txt") vs $(wc -l < "$WORK/raw_queue_$surface.txt") lines)"
    fi
    if cmp -s "$WORK/local_$surface.jsonl" "$WORK/queue_$surface.jsonl"; then
        pass "$surface replay: $calls served answers are BYTE-IDENTICAL between local and queue modes ($(wc -l < "$WORK/queue_$surface.jsonl") rows)"
    else
        fail "$surface replay: served answers differ between local and queue modes"
        diff <(head -c 4000 "$WORK/local_$surface.jsonl") <(head -c 4000 "$WORK/queue_$surface.jsonl") | head -n 6
    fi
}
