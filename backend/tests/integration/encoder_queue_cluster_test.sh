#!/usr/bin/env bash
#
# Gate for the encoder assistants on the shared queue, against a REAL three-node SGEE cluster and
# REAL engines holding the REAL encoder weights.
#
# @author Olumuyiwa Oluwasanmi
#
# WHAT IT DECIDES, AND WHAT THE HERMETIC test_encoder_queue CANNOT:
#
#   1. The encoders go THROUGH the queue. Before this existed, ~120 production assistant calls
#      advanced the cluster's `last_applied` by 10 -- housekeeping -- because the encoder ran
#      in-process and `admission_` was consulted only for decoder prompts. Here every request is
#      accounted for: the engines' own "executed leased job" counts must sum to the requests sent.
#   2. Work is SHARED. A burst sent to engine A is executed partly by engine B (per-engine counts).
#   3. The SURFACE partition holds: a mortgage burst is executed by mortgage runners only, a
#      strategy burst by strategy runners only, and no request falls back to a local answer.
#   4. The served answers are BYTE-IDENTICAL to local mode over the repository's own corpora: the
#      272-row mortgage visitor regression and the 1500-row strategy holdout.
#   5. A cluster that has gone away costs nothing but the queue: the engine still answers, with the
#      same answer, and says why in its log -- and does NOT claim the queue carried it.
#   6. A worker only leases tasks it would answer identically. Across a deploy overlap two builds
#      share one queue; a replica whose build fingerprint differs executes none of the other's
#      tasks, in either direction, while same-build replicas still share.
#
# SKIPS (77) rather than passing when the weights or the python gRPC stack are absent: a gate that
# passes because its input was missing is worse than no gate.
#
# Usage: encoder_queue_cluster_test.sh <sgee_queue_node> <calculator_engine> <repo-root>
#
# Every process this script starts is attributed by PID or by the SGEE run token; nothing is ever
# signalled by name, because other runs share this machine.

set -uo pipefail

NODE_BIN="${1:?usage: $0 <sgee_queue_node> <calculator_engine> <repo-root>}"
ENGINE_BIN="${2:?usage: $0 <sgee_queue_node> <calculator_engine> <repo-root>}"
REPO="${3:?usage: $0 <sgee_queue_node> <calculator_engine> <repo-root>}"
EQC_BURST="${EQC_BURST:-96}"                      # requests per surface in the sharing run
OVERLOAD_CONC="${EQC_OVERLOAD_CONCURRENCY:-24}"   # callers in the overload run
STRATEGY_ROWS="${EQC_STRATEGY_ROWS:-300}"         # strategy holdout rows replayed (1500 = all; ~4 min)

# shellcheck source=lib/encoder_cluster.sh
. "$REPO/backend/tests/integration/lib/encoder_cluster.sh"
eqc_init || exit $?

eqc_start_cluster || exit 1
eqc_log "starting engine L (INFERENCE_QUEUE=local), A and B (INFERENCE_QUEUE=sgee)"
eqc_start_engine L "$PORT_L" local || exit 1
# A and B send every request to the queue first (ENCODER_LOCAL_MAX_IN_FLIGHT=0): see lib/encoder_cluster.sh.
eqc_start_engine A "$PORT_A" sgee ENCODER_LOCAL_MAX_IN_FLIGHT=0 || exit 1
eqc_start_engine B "$PORT_B" sgee ENCODER_LOCAL_MAX_IN_FLIGHT=0 || exit 1
sleep 1   # let both runners complete a first idle poll

# Both assistants of a queue-mode engine must say they joined the queue. Before the encoders were
# wired to it they did not: the encoder branch of each worker returned before the queue was
# configured, and an operator reading the boot log saw "INFERENCE_QUEUE=sgee" and believed it.
for e in A B; do
    check_eq "engine $e: both assistants announce INFERENCE_QUEUE=sgee" "$(grep -c 'INFERENCE_QUEUE=sgee' "$WORK/$e.log")" 2
done
check_eq "engine L: local mode announces no queue" "$(grep -c 'INFERENCE_QUEUE=sgee' "$WORK/L.log")" 0

# ---- 1-3. requests through A: accounted for, shared, partitioned by surface -----------------------
eqc_burst mortgage "mortgage encoder" "strategy encoder"
eqc_burst strategy "strategy encoder" "mortgage encoder"

# ---- 3b. overload: more callers than the replica's bound ------------------------------------------
#
# The queue's ceiling (a replicated write per queue operation, three per request) is far below one
# replica's own, so past the bound a replica answers itself. What this proves is the absence of the
# collapse the bound exists to prevent: no request waits out the deadline, no orphan task is left
# for a worker to execute for nobody, and every caller is still answered.
overload() {   # <surface> <label>
    local surface="$1" label="$2" a0 b0 a1 b1 f0 f1 l0 l1 executed q0 q1 p0 p1
    a0="$(eqc_executed A "$label")"; b0="$(eqc_executed B "$label")"; f0="$(eqc_fallbacks A)"; l0="$(eqc_applied)"
    q0="$(eqc_queued A "$label")"; p0="$(eqc_inprocess A "$label")"
    python3 -P "$PROBE" latency --target "127.0.0.1:$PORT_A" --surface "$surface" -c "$OVERLOAD_CONC" \
        -n "$EQC_BURST" --warmup 0 > "$WORK/overload_$surface.json" 2>&1 \
        || { fail "$surface overload run reported errors"; cat "$WORK/overload_$surface.json"; }
    sleep 1
    a1="$(eqc_executed A "$label")"; b1="$(eqc_executed B "$label")"; f1="$(eqc_fallbacks A)"; l1="$(eqc_applied)"
    q1="$(eqc_queued A "$label")"; p1="$(eqc_inprocess A "$label")"
    executed=$(( (a1-a0) + (b1-b0) ))
    eqc_log "$surface overload ($OVERLOAD_CONC callers, $EQC_BURST requests): $executed went through the queue, $((EQC_BURST-executed)) answered by A itself; $(cut -c1-160 "$WORK/overload_$surface.json")"
    check_eq "$surface overload: every request is accounted for (queued + answered in-process)" "$(( (q1-q0) + (p1-p0) ))" "$EQC_BURST"
    check_ge "$surface overload: the queue still carried requests while it had room" "$executed" 1
    check_ge "$surface overload: the overflow was answered in-process" "$(( p1-p0 ))" 1
    check_eq "$surface overload: every queued request was executed by some replica" "$executed" "$(( q1-q0 ))"
    check_eq "$surface overload: no request waited out the deadline" "$(( f1-f0 ))" 0
    check_eq "$surface overload: no orphan -- the log moved by exactly three writes per executed request (+/- housekeeping)" \
        "$(( (l1-l0) >= 3*executed && (l1-l0) <= 3*executed + 12 ))" 1
}
overload mortgage "mortgage encoder"
overload strategy "strategy encoder"

# ---- 3c. version skew: another build on the same queue leases nothing of ours --------------------
#
# Railway stands the new containers up while the old ones still carry traffic, all against one queue.
# Engine S plays "the other build": its MORTGAGE encoder loads the strategy GGUF, so its mortgage
# fingerprint differs from A's and B's exactly as a retrained model or a changed binary would make it
# (the fingerprint is derived from the model file and the executable, never from a number someone
# bumps). Its answers are meaningless for mortgage and are never read; only who EXECUTES is counted.
#
# Two directions, because one alone proves nothing: "S executed none of A's tasks" is also true of a
# runner that is dead, so S must be shown to execute its OWN.
skew() {   # <surface> <label>
    local surface="$1" label="$2" s0 a0 b0 s1 a1 b1 s2 a2 b2 f0 f1 fs0 fs1
    s0="$(eqc_executed S "$label")"; a0="$(eqc_executed A "$label")"; b0="$(eqc_executed B "$label")"
    f0="$(eqc_fallbacks A)"; fs0="$(eqc_fallbacks S)"
    python3 -P "$PROBE" latency --target "127.0.0.1:$PORT_A" --surface "$surface" -c 1 \
        -n "$EQC_BURST" --warmup 0 > "$WORK/skew_a_$surface.json" 2>&1 \
        || { fail "$surface skew run against A reported errors"; cat "$WORK/skew_a_$surface.json"; }
    sleep 0.5
    s1="$(eqc_executed S "$label")"; a1="$(eqc_executed A "$label")"; b1="$(eqc_executed B "$label")"
    python3 -P "$PROBE" latency --target "127.0.0.1:$PORT_S" --surface "$surface" -c 1 \
        -n "$EQC_BURST" --warmup 0 > "$WORK/skew_s_$surface.json" 2>&1 \
        || { fail "$surface skew run against S reported errors"; cat "$WORK/skew_s_$surface.json"; }
    sleep 0.5
    s2="$(eqc_executed S "$label")"; a2="$(eqc_executed A "$label")"; b2="$(eqc_executed B "$label")"
    f1="$(eqc_fallbacks A)"; fs1="$(eqc_fallbacks S)"
    check_eq "$surface skew: the other build executed NONE of A's $EQC_BURST tasks" "$(( s1-s0 ))" 0
    check_eq "$surface skew: A and B (same build) executed all of them" "$(( (a1-a0) + (b1-b0) ))" "$EQC_BURST"
    check_eq "$surface skew: A and B executed NONE of the other build's $EQC_BURST tasks" "$(( (a2-a1) + (b2-b1) ))" 0
    check_eq "$surface skew: the other build executed all of its own (its runner is alive)" "$(( s2-s1 ))" "$EQC_BURST"
    check_eq "$surface skew: nothing fell back to a local answer on either side" "$(( (f1-f0) + (fs1-fs0) ))" 0
}
eqc_log "starting engine S: a mortgage encoder of ANOTHER build on the same queue"
eqc_start_engine S "$PORT_S" sgee ENCODER_LOCAL_MAX_IN_FLIGHT=0 MORTGAGE_ENCODER_PATH="$STRATEGY_GGUF" || exit 1
sleep 1
skew mortgage "mortgage encoder"
kill -TERM "${ENGINE_PIDS[-1]}" 2>/dev/null || true   # the pid this script captured; never by name

# ---- 3d. the two bounds are operator switches, and are read as ones ----------------------------
#
# The effective bounds are logged at boot (they used to be invisible: a misconfiguration could only
# be inferred from per-request lines under load). 0 is a VALUE -- local 0 means every request is busy,
# queue 0 means this assistant never submits -- and a typo is a REFUSAL to start, naming the variable
# and the value, rather than a quiet coercion to the default.
check_eq "engine A: each assistant logs its bounds" "$(grep -c 'only when 0 are already executing here -- at most 1 at a time' "$WORK/A.log")" 2
rc="$(eqc_engine_refuses Z "$PORT_S" sgee ENCODER_QUEUE_MAX_IN_FLIGHT=abc)"
check_eq "a non-numeric ENCODER_QUEUE_MAX_IN_FLIGHT stops the engine at boot (exit status)" "$rc" 1
check_eq "and it names the value it refused" "$(grep -c 'ENCODER_QUEUE_MAX_IN_FLIGHT="abc" is not a whole number' "$WORK/Z.log")" 1
rc="$(eqc_engine_refuses Z "$PORT_S" sgee ENCODER_LOCAL_MAX_IN_FLIGHT=-3)"
check_eq "a malformed ENCODER_LOCAL_MAX_IN_FLIGHT stops the engine too" "$rc" 1
check_eq "and names ITS variable and value" "$(grep -c 'ENCODER_LOCAL_MAX_IN_FLIGHT="-3" is not a whole number' "$WORK/Z.log")" 1
eqc_start_engine Z "$PORT_S" sgee ENCODER_LOCAL_MAX_IN_FLIGHT=0 ENCODER_QUEUE_MAX_IN_FLIGHT=0 || exit 1
check_eq "both bounds at 0 are accepted and logged" "$(grep -c 'only when 0 are already executing here -- at most 0 at a time' "$WORK/Z.log")" 2
z0="$(eqc_inprocess Z "mortgage encoder")"; zq0="$(eqc_queued Z "mortgage encoder")"
python3 -P "$PROBE" latency --target "127.0.0.1:$PORT_S" --surface mortgage -c 1 -n 24 --warmup 0 \
    > "$WORK/zero_bound.json" 2>&1 || { fail "engine Z (queue bound 0) reported errors"; cat "$WORK/zero_bound.json"; }
sleep 0.5
check_eq "with a queue bound of 0 even a request that is always busy is answered in-process" "$(( $(eqc_inprocess Z "mortgage encoder") - z0 ))" 24
check_eq "and none is reported as spilled to the queue" "$(( $(eqc_queued Z "mortgage encoder") - zq0 ))" 0
kill -TERM "${ENGINE_PIDS[-1]}" 2>/dev/null || true   # the pid this script captured; never by name

# ---- 3e. a listener that cannot bind exits cleanly even with queue runners already polling -------
#
# EngineBindFailureTest covers the engine with no queue. With one, the process holds lease-runner
# threads when it exits, and std::exit runs their destructors: this is the case where an exit could
# hang on a thread stuck in a lease call, or crash, instead of leaving.
python3 -P -c '
import socket, sys, time
s = socket.socket()
s.bind(("0.0.0.0", int(sys.argv[1])))
s.listen(1)
time.sleep(60)
' "$PORT_S" &
holder=$!
sleep 0.5
started=$SECONDS
rc="$(eqc_engine_refuses Y "$PORT_S" sgee)"
kill "$holder" 2>/dev/null || true   # the pid this script captured
check_eq "an engine whose port is taken exits 1, with its queue runners live" "$rc" 1
check_eq "and names the address" "$(grep -c "could not start the gRPC server on 0.0.0.0:$PORT_S" "$WORK/Y.log")" 1
if [ $(( SECONDS - started )) -le 15 ]; then pass "and leaves promptly ($(( SECONDS - started )) s)"; else fail "took $(( SECONDS - started )) s to exit"; fi

# ---- 4. byte-identical served answers, over the whole RPC ----------------------------------------
eqc_replay mortgage "mortgage encoder" 272
eqc_replay strategy "strategy encoder" "$STRATEGY_ROWS"
if [ -n "${EQC_KEEP_DUMPS:-}" ]; then cp "$WORK"/local_*.jsonl "$WORK"/raw_local_*.txt "$EQC_KEEP_DUMPS"/; fi

# ---- 4b. THE DEFAULT ROUTING: in-process first, spill to the queue only when busy -----------------
#
# A and B above send every request to the queue first (ENCODER_LOCAL_MAX_IN_FLIGHT=0) so that the
# queue path is exercised at a concurrency of one. A replica at the DEFAULTS does the opposite: it
# answers in-process, and only when its own chain already has `ENCODER_LOCAL_MAX_IN_FLIGHT`
# executions in flight does a request spill. Two claims, each needing its own engine:
#
#   D (defaults): at concurrency 1 a replica is never busy, so NOTHING reaches the queue -- no spill,
#      no leased job anywhere, and the cluster log does not move. The first routing made this the
#      ~600x slower path at the deployed heartbeat.
#   P (local bound 1, queue bound 8): under a burst above its local bound the overflow IS spilled,
#      and it is executed by the OTHER engines; the accounting closes (in-process + spilled ==
#      requests, executed == spilled), and the served answers stay byte-identical to local mode
#      although they arrived by two different routes.
#
# A write to the cluster log is housekeeping at the margin (a sweep, a snapshot), never a request: one
# queued request is THREE writes, so a delta under three cannot contain one.
route_idle() {   # <surface> <label>   (engine D, defaults)
    local surface="$1" label="$2" ip0 sp0 dg0 f0 x0 l0 ip1 sp1 dg1 f1 x1 l1
    ip0="$(eqc_inprocess D "$label")"; sp0="$(eqc_queued D "$label")"; dg0="$(eqc_degraded D "$label")"; f0="$(eqc_fallbacks D)"
    x0=$(( $(eqc_executed A "$label") + $(eqc_executed B "$label") + $(eqc_executed D "$label") )); l0="$(eqc_applied)"
    python3 -P "$PROBE" latency --target "127.0.0.1:$PORT_D" --surface "$surface" -c 1 \
        -n "$EQC_BURST" --warmup 0 > "$WORK/route_idle_$surface.json" 2>&1 \
        || { fail "$surface idle run reported errors"; cat "$WORK/route_idle_$surface.json"; }
    sleep 0.5
    ip1="$(eqc_inprocess D "$label")"; sp1="$(eqc_queued D "$label")"; dg1="$(eqc_degraded D "$label")"; f1="$(eqc_fallbacks D)"
    x1=$(( $(eqc_executed A "$label") + $(eqc_executed B "$label") + $(eqc_executed D "$label") )); l1="$(eqc_applied)"
    eqc_log "$surface idle: $EQC_BURST requests at c=1 on a default replica: in-process $((ip1-ip0)), spilled $((sp1-sp0)), cluster log +$((l1-l0)); $(cut -c1-150 "$WORK/route_idle_$surface.json")"
    check_eq "$surface idle: every request was answered in-process" "$(( ip1-ip0 ))" "$EQC_BURST"
    check_eq "$surface idle: none was spilled to the queue" "$(( sp1-sp0 ))" 0
    check_eq "$surface idle: none degraded, none fell back" "$(( (dg1-dg0) + (f1-f0) ))" 0
    check_eq "$surface idle: no replica executed a leased job" "$(( x1-x0 ))" 0
    check_eq "$surface idle: zero queue writes (the cluster log moved by less than one request's three)" "$(( (l1-l0) < 3 ))" 1
}
route_burst() {   # <surface> <label> <rows>   (engine P)
    local surface="$1" label="$2" rows="$3" ip0 sp0 dg0 f0 a0 b0 p0 ip1 sp1 dg1 f1 a1 b1 p1 s2 n="${EQC_ROUTE_BURST:-480}"
    ip0="$(eqc_inprocess P "$label")"; sp0="$(eqc_queued P "$label")"; dg0="$(eqc_degraded P "$label")"; f0="$(eqc_fallbacks P)"
    a0="$(eqc_executed A "$label")"; b0="$(eqc_executed B "$label")"; p0="$(eqc_executed P "$label")"; r0="$(eqc_raw_count P "$surface")"
    python3 -P "$PROBE" latency --target "127.0.0.1:$PORT_P" --surface "$surface" -c "$OVERLOAD_CONC" \
        -n "$n" --warmup 0 > "$WORK/route_burst_$surface.json" 2>&1 \
        || { fail "$surface burst reported errors"; cat "$WORK/route_burst_$surface.json"; }
    sleep 1
    ip1="$(eqc_inprocess P "$label")"; sp1="$(eqc_queued P "$label")"; dg1="$(eqc_degraded P "$label")"; f1="$(eqc_fallbacks P)"
    a1="$(eqc_executed A "$label")"; b1="$(eqc_executed B "$label")"; p1="$(eqc_executed P "$label")"; r1="$(eqc_raw_count P "$surface")"
    eqc_log "$surface burst ($OVERLOAD_CONC callers, $n requests, local bound 1): in-process $((ip1-ip0)), spilled $((sp1-sp0)), degraded $((dg1-dg0)); executed by A $((a1-a0)) B $((b1-b0)) P $((p1-p0))"
    check_ge "$surface burst: part of it was answered in-process" "$(( ip1-ip0 ))" 1
    check_ge "$surface burst: the overflow above the local bound was spilled to the queue" "$(( sp1-sp0 ))" 1
    # Bracketed, because no exact denominator exists for every surface: some corpus rows are answered
    # before the encoder is asked at all, and the service's raw-output line is written only for a
    # params block (not for <NONE> or a refusal). So every request that produced a params block must
    # have been counted (the raw-output delta is a floor), and no more can have been counted than were
    # sent. The strategy surface closes exactly; the mortgage one lands inside the bracket.
    acc=$(( (ip1-ip0) + (sp1-sp0) + (dg1-dg0) ))
    check_eq "$surface burst: every request that produced a params block was accounted for, and none more than were sent (in-process + spilled + degraded = $acc, within [$(( r1-r0 )), $n])" \
        "$(( acc >= r1-r0 && acc <= n ))" 1
    check_eq "$surface burst: every spilled request was executed by some replica" "$(( (a1-a0) + (b1-b0) + (p1-p0) ))" "$(( sp1-sp0 ))"
    check_ge "$surface burst: the OTHER engines executed some of the overflow" "$(( (a1-a0) + (b1-b0) ))" 1
    check_eq "$surface burst: nothing fell back or degraded" "$(( (f1-f0) + (dg1-dg0) ))" 0
    # The same rows as the local-mode dump, sent at concurrency 8 so BOTH routes carry some of them.
    s2="$(eqc_queued P "$label")"
    python3 -P "$PROBE" dump --target "127.0.0.1:$PORT_P" --surface "$surface" -c 8 --limit "$rows" --out "$WORK/mixed_$surface.jsonl" \
        > "$WORK/dump_mixed_$surface.out" 2>&1 || { fail "$surface mixed-route dump had transport errors"; cat "$WORK/dump_mixed_$surface.out"; }
    check_ge "$surface mixed routes: some of the dump's requests really did go through the queue" "$(( $(eqc_queued P "$label") - s2 ))" 1
    if cmp -s "$WORK/local_$surface.jsonl" "$WORK/mixed_$surface.jsonl"; then
        pass "$surface mixed routes: $(wc -l < "$WORK/mixed_$surface.jsonl") served answers are BYTE-IDENTICAL to local mode although they arrived by two routes"
    else
        fail "$surface mixed routes: served answers differ from local mode"
        diff <(head -c 4000 "$WORK/local_$surface.jsonl") <(head -c 4000 "$WORK/mixed_$surface.jsonl") | head -n 6
    fi
}
eqc_log "starting engine D (defaults) and, after it, engine P (local bound 1, queue bound 8)"
eqc_start_engine D "$PORT_D" sgee || exit 1
route_idle mortgage "mortgage encoder"
route_idle strategy "strategy encoder"
kill -TERM "${ENGINE_PIDS[-1]}" 2>/dev/null || true   # the pid this script captured; never by name
eqc_start_engine P "$PORT_P" sgee ENCODER_LOCAL_MAX_IN_FLIGHT=1 ENCODER_QUEUE_MAX_IN_FLIGHT=8 || exit 1
route_burst mortgage "mortgage encoder" 272
route_burst strategy "strategy encoder" "$STRATEGY_ROWS"
kill -TERM "${ENGINE_PIDS[-1]}" 2>/dev/null || true

# ---- 5. the cluster goes away: the engine still answers, identically ------------------------------
eqc_log "stopping the whole cluster"
qn_kill_self || true
sleep 1
f0="$(eqc_fallbacks A)"; q0="$(eqc_queued A "mortgage encoder")"; d0="$(eqc_degraded A "mortgage encoder")"
started=$SECONDS
python3 -P "$PROBE" dump --target "127.0.0.1:$PORT_A" --surface mortgage -c 1 --limit 6 --out "$WORK/down_mortgage.jsonl" \
    > "$WORK/dump_down.out" 2>&1 || { fail "engine A returned transport errors with the cluster down"; cat "$WORK/dump_down.out"; }
elapsed=$(( SECONDS - started ))
f1="$(eqc_fallbacks A)"; q1="$(eqc_queued A "mortgage encoder")"; d1="$(eqc_degraded A "mortgage encoder")"
check_ge "cluster down: the engine says in its log that it answered locally" "$(( f1 - f0 ))" 1
check_eq "cluster down: NO request is reported as answered through the shared queue" "$(( q1 - q0 ))" 0
check_eq "cluster down: every fallback is accounted for as a degraded answer" "$(( d1 - d0 ))" "$(( f1 - f0 ))"
head -n 6 "$WORK/local_mortgage.jsonl" > "$WORK/local_mortgage_head.jsonl"
if cmp -s "$WORK/local_mortgage_head.jsonl" "$WORK/down_mortgage.jsonl"; then
    pass "cluster down: answers are identical to local mode"
else
    fail "cluster down: answers differ from local mode"
fi
if [ "$elapsed" -le 60 ]; then pass "cluster down: 6 rows answered in ${elapsed}s (each bounded, none hung)"; else fail "cluster down: took ${elapsed}s"; fi

echo
if [ "$FAILURES" -eq 0 ]; then echo "encoder_queue_cluster_test: all checks passed"; exit 0; fi
echo "encoder_queue_cluster_test: $FAILURES check(s) FAILED"
for n in A B; do echo "--- engine $n (last warnings) ---"; grep -E 'WARN|ERROR' "$WORK/$n.log" | tail -n 5 | cut -c1-240; done
exit 1
