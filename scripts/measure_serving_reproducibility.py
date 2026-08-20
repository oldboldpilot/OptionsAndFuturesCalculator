#!/usr/bin/env python3
"""{local, production} x {sequential, concurrent}, on ONE fixed set of rows.

@author Olumuyiwa Oluwasanmi

This replaces a claim that was measured wrong. "12/12 locally, 6/12 in
production" compared a SEQUENTIAL local run against a CONCURRENT production run
on ONE cherry-picked utterance, then attributed the whole gap to "the serving
path". Two variables moved at once -- host AND concurrency -- so the comparison
could not attribute anything to either, and a single utterance cannot separate
"this row is hard" from "this host is worse".

Three things are fixed here:

  * BOTH variables are crossed, so each is readable with the other held fixed.
  * The SAME rows go through all four arms, so an arm cannot win by drawing an
    easier question.
  * Scoring is on the response STRUCTURE -- which arm of the `outcome` oneof is
    set -- never on message text, which has been reworded twice.

The transports differ by necessity: production is reached through Envoy's
grpc_json_transcoder and there is no local Envoy. So the decisive comparisons
are WITHIN a host, where transport is fixed. Across hosts, transport and queue
(postgres vs sgee) and replica count all move together and cannot be separated
by this harness -- which is exactly the mistake being corrected, so the summary
below refuses to make that attribution.
"""
import json, os, re, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

SP = os.environ.get("HOLDOUT_DIR", os.path.dirname(os.path.abspath(__file__)))
ROOT = "/home/muyiwa/Development/OptionsAndFuturesCalculator"
ROWS = int(os.environ.get("ROWS", "16"))

utts = []
with open(f"{SP}/val2_closingcosts.jsonl") as fh:
    for line in fh:
        r = json.loads(line)
        utts.append([t["content"] for t in r["conversations"] if t["role"] == "user"][0])
        if len(utts) == ROWS:
            break

# The key is origin-locked, so the Origin header is part of the credential, not
# decoration -- without it every arm scores zero for a reason unrelated to the model.
_kf = open(f"{ROOT}/config/keys/mortgagefv-key-2026-08-10.txt").read()
KEY = re.search(r"^\s*key\s+(\S+)", _kf, re.M).group(1)
ORIGIN = re.search(r"^\s*origin\s+(\S+)", _kf, re.M).group(1)
URL = "https://api.optionsandfuturescalculator.com/mortgage.assistant.MortgageAssistant/ParseOperation"


def prod_once(i):
    p = subprocess.run(
        ["curl", "-sS", "--max-time", "180", "-X", "POST", URL,
         "-H", "Content-Type: application/json",
         "-H", f"x-api-key: {KEY}", "-H", f"Origin: {ORIGIN}",
         "-d", json.dumps({"utterance": utts[i]})],
        capture_output=True, text=True)
    try:
        d = json.loads(p.stdout)
    except Exception:
        return "error"
    if isinstance(d.get("params"), dict) and d["params"]:
        return "params"
    if "refusal" in d or "clarification" in d:
        return "refusal"
    return "error"


import grpc                                  # noqa: E402
sys.path.insert(0, SP)
import mortgage_assistant_pb2 as mpb         # noqa: E402
import mortgage_assistant_pb2_grpc as mrpc   # noqa: E402


def local_once(i):
    ch = grpc.insecure_channel("localhost:50051")
    try:
        r = mrpc.MortgageAssistantStub(ch).ParseOperation(
            mpb.ParseRequest(utterance=utts[i]), timeout=300)
    except grpc.RpcError:
        return "error"
    finally:
        ch.close()
    return "params" if r.WhichOneof("outcome") == "params" else "refusal"


def run(fn, mode):
    t0 = time.time()
    if mode == "sequential":
        out = [fn(i) for i in range(ROWS)]
    else:
        with ThreadPoolExecutor(max_workers=ROWS) as ex:
            out = list(ex.map(fn, range(ROWS)))
    return out, time.time() - t0


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    arms = [(h, f) for h, f in (("local", local_once), ("prod", prod_once))
            if which in ("all", h)]
    print(f"rows={ROWS}\n{'arm':26} {'params':>7} {'refusal':>8} {'error':>6} {'secs':>7}  detail")
    res = {}
    for host, fn in arms:
        for mode in ("sequential", "concurrent"):
            out, dt = run(fn, mode)
            res[(host, mode)] = out
            print(f"{host+' '+mode:26} {out.count('params'):>7} "
                  f"{out.count('refusal'):>8} {out.count('error'):>6} {dt:>7.1f}  "
                  + "".join("P" if o == "params" else "." if o == "refusal" else "E" for o in out))
    for host, _ in arms:
        s, c = res.get((host, "sequential")), res.get((host, "concurrent"))
        if s and c:
            flips = sum(1 for a, b in zip(s, c) if a != b)
            print(f"{host}: rows answered differently sequential vs concurrent = {flips}/{ROWS}")
    json.dump({f"{h}_{m}": v for (h, m), v in res.items()},
              open(f"{SP}/serving_2x2.json", "w"), indent=1)
