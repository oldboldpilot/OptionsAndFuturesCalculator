#!/usr/bin/env python3
"""Does the local engine lose determinism when OTHER traffic shares the batch?

@author Olumuyiwa Oluwasanmi

Local is bit-repeatable (11/16, identical pattern, three runs) and production is
not. The single biggest difference is that production's replicas carry live user
traffic from two public sites, so the iteration-level scheduler fuses the
measured request with strangers, while local fuses it with nothing.

This adds that one variable and nothing else: the SAME 16 rows go through the
SAME local engine sequentially, while background threads keep the batch
occupied with DIFFERENT utterances. If the pattern stops repeating, production's
instability is reproduced locally -- and every remaining experiment can be run
here instead of on a service two public sites depend on.

The background utterances are deliberately different from the measured ones, so
a shared-state effect cannot be mistaken for the measured request simply being
answered twice.
"""
import os, sys, json, threading, time, importlib
SP = "/tmp/claude-1000/-home-muyiwa-Development-OptionsAndFuturesCalculator/8dccb92c-7fce-41f1-a147-a0ff289eef49/scratchpad"
sys.path.insert(0, SP)
os.environ.setdefault("ROWS", "16")
m = importlib.import_module("serving_2x2")

BG = [json.loads(l) for l in open(f"{SP}/val2_legacy.jsonl")][:40]
BG = [[t["content"] for t in r["conversations"] if t["role"] == "user"][0] for r in BG]

import grpc, mortgage_assistant_pb2 as mpb, mortgage_assistant_pb2_grpc as mrpc

stop = threading.Event()
sent = [0]


def noise(seed):
    ch = grpc.insecure_channel("localhost:50051")
    st = mrpc.MortgageAssistantStub(ch)
    i = seed
    while not stop.is_set():
        try:
            st.ParseOperation(mpb.ParseRequest(utterance=BG[i % len(BG)]), timeout=120)
            sent[0] += 1
        except grpc.RpcError:
            pass                      # RESOURCE_EXHAUSTED is the queue working
        i += 7
    ch.close()


THREADS = int(os.environ.get("BG", "3"))
if __name__ == "__main__":
    workers = [threading.Thread(target=noise, args=(k,), daemon=True) for k in range(THREADS)]
    for w in workers:
        w.start()
    time.sleep(3)                     # let the batch fill before measuring
    try:
        for k in range(int(os.environ.get("RUNS", "3"))):
            out, dt = m.run(m.local_once, "sequential")
            print(f"local seq under bg={THREADS}, run {k+1}: {out.count('params')}/16 {dt:.0f}s "
                  + "".join("P" if o == "params" else "." if o == "refusal" else "E" for o in out),
                  flush=True)
    finally:
        stop.set()
        print(f"background requests completed: {sent[0]}")
