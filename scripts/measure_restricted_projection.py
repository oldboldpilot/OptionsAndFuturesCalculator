"""Restricted lm_head projection: paired A/B under MIXED concurrent load.

@author Olumuyiwa Oluwasanmi

Run as:  python3 scripts/measure_restricted_projection.py

WHY THIS EXISTS RATHER THAN A ONE-OFF. The 1.58x first recorded for this
optimisation was measured with eight copies of ONE utterance, which synchronises
every sequence into the same grammar state on the same step -- so every projection
in the batch is restricted at once, which real ingress never does. A figure whose
harness is unrecorded cannot be compared with one whose harness is; this file IS
the harness for the mixed figure.

WHAT MAKES IT TRUSTWORTHY, and every clause was paid for:

  * It REFUSES rather than reports. A leaked engine still bound to this port is
    served ALONGSIDE the new one, because engines bind SO_REUSEPORT and the kernel
    splits requests between them -- so both arms get partly served by the same
    binary and the result is ~1.00x whatever the change does. That is not
    hypothetical: the first run of this script measured 0.99x with a stale engine
    holding the port at 5m48s of CPU in 131s elapsed. Hardening it turned 0.99x
    into 1.54x on identical code.
  * It waits for THIS engine to log its own parsed arm AND `model is LOADED`,
    never for "somebody answered the port" -- a readiness probe cannot tell your
    engine from any engine, and the stale one answers instantly, so timing starts
    before yours has loaded.
  * Teardown waits for the process to be GONE, not a fixed sleep.
  * Arms are INTERLEAVED and repeated, so drift hits both, and the spread is
    reported beside the median -- a throughput claim gets a correctness claim's bar.
  * Two of the eight sequences are deliberately under-specified, so they get a
    clarifying question, never enter <params>, and never arm a grammar. At 25% that
    is heavier than production's ~7.6%, which measures the arm where it has LEAST
    to gain.

Identity is checked in the same run: the canonical form of all eight responses must
hash identically across arms. Comparing raw bytes would be invalid -- FinanceParams
is a protobuf map, whose iteration order differs between processes.
"""
import sys, json, time, hashlib, subprocess, os, signal, re
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, str(Path("agent/train").resolve()))
import grpc, mortgage_assistant_pb2 as pb, mortgage_assistant_pb2_grpc as pbg

PORT = "50079"
# Two populations, deliberately: params rows ARM a grammar, clarification rows do NOT.
#
# A clarification row is FIVE-TURN in this holdout, so filtering for single-turn rows
# whose answer contains '?' finds zero of them -- the first attempt did exactly that and
# its own assert caught the shortfall. The right source is the FIRST USER TURN of a
# multi-turn row whose first assistant reply is a question: under-specified by
# construction, because turn two is what supplies the missing figure. Such a sequence
# never enters <params>, so it never arms a grammar and its projection stays full-width.
params_utts, clar_utts, seen = [], [], set()
for line in open('agent/dataset/data_mortgage/val.jsonl'):
    turns = json.loads(line)['conversations']
    users = [t for t in turns if t['role'] == 'user']
    asst  = [t for t in turns if t['role'] == 'assistant']
    if not users or not asst:
        continue
    first = asst[0]['content']
    if len(users) == 1 and '<params>' in first:
        op = re.search(r'"operation":"(\w+)"', first)
        if op and op.group(1) not in seen:
            seen.add(op.group(1)); params_utts.append(users[0]['content'])
    elif len(users) > 1 and '<params>' not in first and '?' in first and len(clar_utts) < 6:
        clar_utts.append(users[0]['content'])

# ~75/25 params-to-clarification at N=8: heavier on clarification than production's
# ~7.6%, deliberately, so the arm is measured where it has LEAST to gain. Two of the
# eight sequences in every batch never arm a grammar, so every step of the run mixes a
# restricted projection with a full-width one -- which is the whole point: the identical
# utterance synchronised all eight into the same grammar state on the same token.
batch = params_utts[:6] + clar_utts[:2]
assert len(batch) == 8, f"population shortfall: {len(params_utts)} params, {len(clar_utts)} clar"
print(f"mixed batch: {len(params_utts[:6])} grammar-arming + {len(clar_utts[:2])} clarification "
      f"= {len(batch)} in flight")

def engines_running():
    """PIDs of running engines. `comm` truncates to 15 chars, so the name is
    `calculator_engi` -- `pgrep -x calculator_engine` matches NOTHING."""
    out = subprocess.run(["pgrep","-x","calculator_engi"], capture_output=True, text=True)
    return [int(x) for x in out.stdout.split()]

def port_busy():
    out = subprocess.run(["ss","-ltn"], capture_output=True, text=True)
    return any(f":{PORT}" in ln for ln in out.stdout.splitlines())

def assert_clean():
    """REFUSE, do not report. A leaked engine still bound to this port is served
    ALONGSIDE the new one -- engines bind with SO_REUSEPORT, so the kernel splits
    requests between them and both arms get partly served by the same binary.
    That measures ~1.00x no matter what the change does, which is exactly what the
    first run of this script produced: a stale engine held :50079 with 5m48s of CPU
    in 131s elapsed, the readiness probe answered from IT instantly, and timing
    began before any fresh engine had finished loading."""
    stale = engines_running()
    if stale:
        raise SystemExit(f"REFUSING: {len(stale)} engine(s) already running: {stale}")
    if port_busy():
        raise SystemExit(f"REFUSING: something is already listening on :{PORT}")

def engine(flag):
    assert_clean()
    log = f"/home/muyiwa/.claude/jobs/606f4f9d/tmp/mixed-{flag}.log"
    p = subprocess.Popen(["python3","scripts/run_with_env.py",
        "--set",f"ENGINE_GRPC_PORT={PORT}","--set","PRO_GATE_MODE=off",
        "--set","INFERENCE_QUEUE=local","--set","DATABASE_URL=","--set","QUOTA_POLICY=",
        "--set","MORTGAGE_ASSISTANT_MAX_CONCURRENT=8",
        "--set",f"MORTGAGE_RESTRICTED_PROJECTION={flag}",
        "--set","MORTGAGE_MODEL_PATH=backend/models/mortgagefv-assistant-v20-q8_0.gguf",
        "--","./backend/build/calculator_engine"],
        stdout=open(log,"w"), stderr=subprocess.STDOUT, preexec_fn=os.setsid)

    # Wait for THIS engine to say it loaded, not for "somebody answered the port".
    want = f"MORTGAGE_RESTRICTED_PROJECTION={flag} -- restricted lm_head projection "
    deadline = time.time() + 300
    ready = False
    while time.time() < deadline:
        if p.poll() is not None:
            raise SystemExit(f"engine for flag={flag} exited early, rc={p.returncode}")
        try:
            body = open(log, encoding="utf-8", errors="replace").read()
        except OSError:
            body = ""
        if want in body and "Mortgage assistant model is LOADED" in body:
            ready = True
            break
        time.sleep(1)
    if not ready:
        raise SystemExit(f"engine for flag={flag} never logged its parsed switch + LOADED")

    # And assert OUR process is the only engine, so nothing else can serve a request.
    live = engines_running()
    if len(live) != 1:
        raise SystemExit(f"REFUSING: {len(live)} engines running after boot: {live}")

    stub = pbg.MortgageAssistantStub(grpc.insecure_channel(f"localhost:{PORT}"))
    for _ in range(300):
        try:
            stub.ParseOperation(pb.ParseRequest(utterance=batch[0]), timeout=300); break
        except grpc.RpcError:
            time.sleep(1)
    arm = "ON" if flag else "OFF"
    assert f"projection {arm}" in open(log, encoding="utf-8", errors="replace").read(), \
        f"engine did not report the arm as {arm}"
    return p, stub

def canon(r):
    w = r.WhichOneof("outcome")
    if w == "params":
        return json.dumps({"op": r.params.operation,
                           "p": dict(sorted(r.params.params.items()))}, sort_keys=True)
    if w == "clarification": return "CLAR:" + r.clarification.question
    return f"REF:{r.refusal.reason}:{r.refusal.message}"

def concurrent_round(stub):
    def one(u):
        t = time.perf_counter()
        r = stub.ParseOperation(pb.ParseRequest(utterance=u), timeout=300)
        return canon(r), time.perf_counter() - t
    t0 = time.perf_counter()
    with ThreadPoolExecutor(max_workers=len(batch)) as ex:
        res = list(ex.map(one, batch))
    return [r[0] for r in res], [r[1] for r in res], time.perf_counter() - t0

ROUNDS = 3
results = {0: [], 1: []}
outs = {}
for rnd in range(ROUNDS):
    for flag in (0, 1):            # interleaved, so drift hits both arms
        p, stub = engine(flag)
        o, lat, wall = concurrent_round(stub)
        results[flag].append((wall, max(lat), sum(lat)/len(lat)))
        outs.setdefault(flag, o)
        os.killpg(os.getpgid(p.pid), signal.SIGKILL); p.wait()
        # Wait for the process to be GONE, not a fixed sleep: a leaked engine is
        # what invalidated the first run, and `time.sleep(2)` is a guess about how
        # long teardown takes rather than a check that it happened.
        gone = time.time() + 60
        while engines_running() and time.time() < gone:
            time.sleep(1)
        if engines_running():
            raise SystemExit(f"engine survived SIGKILL: {engines_running()}")
        print(f"  round {rnd+1} flag={flag}: wall {wall:.2f}s  slowest {max(lat):.2f}s  mean {sum(lat)/len(lat):.2f}s")

sha = lambda L: hashlib.sha256("\n".join(L).encode()).hexdigest()[:16]
diff = [i for i,(a,b) in enumerate(zip(outs[0], outs[1])) if a != b]
print(f"\n=== MIXED CONCURRENT N={len(batch)} ===")
for flag, name in ((0,"restricted OFF"),(1,"restricted ON ")):
    walls = sorted(w for w,_,_ in results[flag])
    print(f"{name}: wall median {walls[len(walls)//2]:.2f}s  range {walls[0]:.2f}-{walls[-1]:.2f}s")
mo = sorted(w for w,_,_ in results[0])[ROUNDS//2]
mn = sorted(w for w,_,_ in results[1])[ROUNDS//2]
print(f"\nspeedup (median wall, mixed N=8): {mo/mn:.2f}x")
print(f"identity: sha OFF {sha(outs[0])}  ON {sha(outs[1])}  rows differing {len(diff)} of {len(batch)}")
for i in diff[:3]:
    print(f"  [{i}] OFF {outs[0][i][:110]}")
    print(f"      ON  {outs[1][i][:110]}")
