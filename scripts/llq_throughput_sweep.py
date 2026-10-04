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


def no_competing_engine():
    """Refuse to measure while an engine is running.

    THIS CHECK USED TO REQUIRE EXACTLY ONE ENGINE ON :50051, AND THAT WAS THE
    WRONG GUARD FOR THIS HARNESS -- wrong in the direction that corrupts the
    measurement it was protecting.

    The port guard belongs to the harnesses that speak to an engine over gRPC,
    where SO_REUSEPORT silently splits requests across stale engines each
    holding a different model. `llq_throughput_probe` is not one of them: it
    constructs an LLMPipeline IN-PROCESS and binds no port at all, so no engine
    can answer for it and the identity of a listening engine is irrelevant to
    the number.

    What a running engine CAN do is compete for the cores this is timing. So
    requiring one to exist forced a competitor onto the box as a precondition
    for measuring, which is the opposite of what the guard was for. The right
    precondition for an in-process probe is that nothing else is decoding.
    """
    out = subprocess.run(["pgrep", "-x", "calculator_engi"], capture_output=True, text=True)
    pids = out.stdout.split()
    if pids:
        sys.exit(f"FATAL: {len(pids)} calculator_engine process(es) running (pids {','.join(pids)}); "
                 "this probe decodes in-process, so a live engine is a COMPETITOR for the cores "
                 "being timed, not a prerequisite. Stop it and re-run.")
    return "none"


def run(probe, model, arm, threads, extra):
    env = dict(os.environ)
    store = arm
    if arm == "dense-nofusion":
        store = "dense"
        env["SENSEN_QKV_FUSION"] = "0"
    elif arm == "dense":
        env.pop("SENSEN_QKV_FUSION", None)
    cmd = [probe, model, "--store", store, "--threads", str(threads), "--label", arm] + extra
    load = os.getloadavg()[0]
    p = subprocess.run(cmd, capture_output=True, text=True, env=env)
    for line in p.stdout.splitlines():
        if line.startswith("RESULT "):
            r = json.loads(line[len("RESULT "):])
            r["loadavg_at_start"] = round(load, 2)
            r["arm"] = arm
            # ASSERT the fusion state from the probe's OWN report, not from the
            # env we just set. Setting a variable and believing it is how the
            # first 16-bit run came out invalid: the dense arm ran with fusion
            # ON and the LLQ arm with it OFF, so the two columns were not the
            # same experiment, and the tell was a 16.9% gap in kernel-call count
            # beside byte-identical tokens.
            if arm != "dense" and r.get("qkv_fusion") is not False:
                sys.exit(f"FATAL: {arm}@{threads} reports qkv_fusion={r.get('qkv_fusion')}; "
                         "every arm comparable to an LLQ arm must have it OFF")
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
    ap.add_argument("--probe", default=os.path.join(REPO, "backend", "build",
                                                    "llq_throughput_probe"),
                    help="the probe binary; point it at a dedicated build dir so a parallel "
                         "session's rebuild cannot swap the binary mid-sweep")
    ap.add_argument("--timed", default="4")
    ap.add_argument("--tokens", default="96")
    ap.add_argument("--reps", default="3")
    a = ap.parse_args()

    if not os.path.exists(a.probe):
        sys.exit(f"FATAL: no probe at {a.probe}")
    no_competing_engine()
    # The binary's mtime, because a stale binary is this project's recurring
    # measurement defect and `ninja` saying "no work to do" is not evidence.
    print(f"no competing engine; probe {a.probe} "
          f"(mtime {__import__('datetime').datetime.fromtimestamp(os.path.getmtime(a.probe))})",
          file=sys.stderr)
    extra = ["--prompts", a.prompts, "--timed", a.timed, "--tokens", a.tokens,
             "--reps", a.reps, "--count-coverage"]
    rows = {}
    sink = open(a.out, "w") if a.out else None
    for rnd in range(a.rounds):
        for th in [int(x) for x in a.threads.split(",")]:
            for arm in a.arms.split(","):
                r = run(a.probe, a.model, arm, th, extra)
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

    # ── the SLICE-COUNT check, which is what catches a contaminated arm ──────
    #
    # Two arms that generate identical tokens with a DIFFERENT number of kernel
    # calls are not two weight stores -- they are two amounts of work. That is
    # exactly how the QKV-fusion contamination was caught: byte-identical
    # tokens beside a 16.9% gap in the front-door call count. A rate comparison
    # alone would have shown the LLQ arm "1.8x slower" and been believed,
    # because a slower LLQ arm is what everyone already expected.
    #
    # Reported rather than fatal, because a legitimate reason for a gap exists
    # (the Q5_0 fused gate+up kernel declines when an image is registered, so
    # an LLQ arm makes two front-door calls where dense-with-fusion makes one)
    # -- but it must be SEEN, and a silent difference must not be.
    print()
    for th in [int(x) for x in a.threads.split(",")]:
        counts = {}
        for arm in a.arms.split(","):
            rs = rows.get((th, arm))
            if not rs:
                continue
            tot = {x["coverage_source_calls"] + x["coverage_dense_calls"] for x in rs}
            counts[arm] = tot
        if not counts:
            continue
        flat = {next(iter(v)) for v in counts.values() if len(v) == 1}
        verdict = "EQUAL across arms" if len(flat) == 1 and all(
            len(v) == 1 for v in counts.values()) else "*** DIFFER -- arms are not the same work"
        print(f"threads {th:>2} slice calls: " +
              "  ".join(f"{k}={sorted(v)}" for k, v in counts.items()) + f"   {verdict}")


if __name__ == "__main__":
    main()
