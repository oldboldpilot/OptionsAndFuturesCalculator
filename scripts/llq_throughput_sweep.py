#!/usr/bin/env python3
"""Interleaved throughput sweep over weight stores and thread counts.

    python3 scripts/llq_throughput_sweep.py MODEL.gguf [--threads 4,8,16] [--rounds 3]
        [--arms dense,dense-nofusion,llq,llq-fused] [--out results.jsonl]

Runs `backend/build/llq_throughput_probe` once per (round, threads, arm) and
INTERLEAVES the arms inside each round, so slow drift on a shared machine lands
on every arm alike instead of on whichever ran last. Every run is a fresh
process, so no arm inherits another's page cache warm-up of the LLQ image or a
JIT'd kernel.

It refuses to start unless exactly ONE calculator_engine holds :50051 -- the
engines bind with SO_REUSEPORT and several stale ones each holding a different
model all listen at once -- and records the load average at the start of every
run, because a figure quoted without the load it was taken under is not one.

`dense` is the production default (QKV fusion ON). `dense-nofusion` is the
dense arm with SENSEN_QKV_FUSION=0, which is the ONLY dense arm comparable to the
LLQ arms (an LLQ store requires fusion off). Both are reported.

Prints one row per (threads, arm) with the median and range of decode tok/s
across rounds, the prefill rate, the aggregate token hash, and whether that hash
equals the dense-nofusion arm's at the same thread count.
"""
import argparse
import json
import os
import statistics
import subprocess
import sys

REPO = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True,
                      text=True, check=True).stdout.strip()
PROBE = os.path.join(REPO, "backend", "build", "llq_throughput_probe")


def one_engine_on_50051():
    out = subprocess.run(["pgrep", "-x", "calculator_engi"], capture_output=True, text=True)
    n = len(out.stdout.split())
    if n != 1:
        sys.exit(f"FATAL: {n} calculator_engine processes; need exactly 1 holding :50051 "
                 "(SO_REUSEPORT splits requests across stale engines)")
    exe = os.readlink(f"/proc/{out.stdout.split()[0]}/exe")
    return exe


def run(model, arm, threads, extra):
    env = dict(os.environ)
    store = arm
    if arm == "dense-nofusion":
        store = "dense"
        env["SENSEN_QKV_FUSION"] = "0"
    elif arm == "dense":
        env.pop("SENSEN_QKV_FUSION", None)
    cmd = [PROBE, model, "--store", store, "--threads", str(threads), "--label", arm] + extra
    load = os.getloadavg()[0]
    p = subprocess.run(cmd, capture_output=True, text=True, env=env)
    for line in p.stdout.splitlines():
        if line.startswith("RESULT "):
            r = json.loads(line[len("RESULT "):])
            r["loadavg_at_start"] = round(load, 2)
            r["arm"] = arm
            return r
    sys.exit(f"FATAL: {arm}@{threads} produced no RESULT\n{p.stderr[-800:]}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("--threads", default="4,8,16")
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--arms", default="dense,dense-nofusion,llq,llq-fused")
    ap.add_argument("--prompts", default="12")
    ap.add_argument("--out", default="")
    a = ap.parse_args()

    engine_exe = one_engine_on_50051()
    print(f"one engine on :50051 ({engine_exe}); probe {PROBE}", file=sys.stderr)
    extra = ["--prompts", a.prompts, "--timed", "4", "--tokens", "96", "--reps", "3"]
    rows = {}
    sink = open(a.out, "w") if a.out else None
    for rnd in range(a.rounds):
        for th in [int(x) for x in a.threads.split(",")]:
            for arm in a.arms.split(","):
                r = run(a.model, arm, th, extra)
                r["round"] = rnd
                rows.setdefault((th, arm), []).append(r)
                if sink:
                    sink.write(json.dumps(r) + "\n")
                    sink.flush()
                print(f"round {rnd} threads {th:>2} {arm:<15} decode {r['decode_tok_s']:>7.2f} "
                      f"prefill {r['prefill_tok_s']:>7.1f} load {r['loadavg_at_start']:>5}",
                      file=sys.stderr)

    print(f"\nmodel {os.path.basename(a.model)}  sha256 {next(iter(rows.values()))[0]['model_sha256']}")
    print(f"quantisation {next(iter(rows.values()))[0]['quantisation']} "
          f"({next(iter(rows.values()))[0]['bits_per_weight']} bits/weight)   rounds {a.rounds}\n")
    hdr = f"{'threads':>7} {'arm':<15} {'LLQ':<4} {'decode tok/s (median)':>22} {'range':>15} " \
          f"{'prefill tok/s':>13} {'tokens == dense-nofusion':>25}"
    print(hdr)
    print("-" * len(hdr))
    for th in [int(x) for x in a.threads.split(",")]:
        ref = None
        if (th, "dense-nofusion") in rows:
            ref = {x["token_sha"] for x in rows[(th, "dense-nofusion")]}
        for arm in a.arms.split(","):
            rs = rows.get((th, arm))
            if not rs:
                continue
            dec = [x["decode_tok_s"] for x in rs]
            pre = [x["prefill_tok_s"] for x in rs]
            shas = {x["token_sha"] for x in rs}
            same = "n/a" if ref is None else ("IDENTICAL" if shas == ref and len(shas) == 1 else
                                              "differs")
            print(f"{th:>7} {arm:<15} {'yes' if rs[0]['llq_used'] else 'no':<4} "
                  f"{statistics.median(dec):>22.2f} {min(dec):>7.2f}-{max(dec):<7.2f} "
                  f"{statistics.median(pre):>13.1f} {same:>25}")


if __name__ == "__main__":
    main()
