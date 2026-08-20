#!/usr/bin/env python3
"""Is production's instability PER-REPLICA, or per-request?

@author Olumuyiwa Oluwasanmi

Three replicas serve this endpoint and nothing makes routing sticky. If each
replica is internally deterministic but they disagree -- different prefix-cache
snapshot, different kernel dispatch -- then a single row asked N times in a row
produces a PERIODIC outcome string, period 3, not a random one. If instead the
cause is per-request numeric noise (batch shape flipping a near-tied argmax),
the string is aperiodic and the hit rate is unstructured.

Costs nothing and changes nothing: it is the same read-only RPC the harness
already sends. Run it before any experiment that requires a config change.
"""
import os, sys, json
from collections import Counter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault("ROWS", "16")
sys.path.insert(0, "/tmp/claude-1000/-home-muyiwa-Development-OptionsAndFuturesCalculator/8dccb92c-7fce-41f1-a147-a0ff289eef49/scratchpad")
import importlib
m = importlib.import_module("serving_2x2")

N = int(os.environ.get("REPS", "30"))
for idx in (int(x) for x in os.environ.get("IDX", "7,1").split(",")):
    s = "".join("P" if m.prod_once(idx) == "params" else "." for _ in range(N))
    print(f"row {idx:2d}: {s}  ({s.count('P')}/{N})", flush=True)
    for lag in (1, 2, 3, 4, 5):
        agree = sum(1 for i in range(N - lag) if s[i] == s[i + lag])
        print(f"    lag {lag}: {agree}/{N-lag} agree = {agree/(N-lag):.2f}")
