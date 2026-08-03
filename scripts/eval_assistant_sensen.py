#!/usr/bin/env python3
"""Score a strategy-assistant model through the engine that actually serves it.

    eval_assistant_sensen.py <holdout.jsonl> <engine.log> [--layer raw|rpc] [target]

Production serves the assistant IN-PROCESS on sensen (`ASSISTANT_BACKEND` defaults
to `sensen`; the startup line reads `Strategy assistant ready: backend=sensen`).
Scoring a candidate with llama.cpp measures an engine that never handles a request
-- that mistake scored the deployed model 7/16 and triggered a retrain for a
regression that did not exist. Measured here the same model scores 13/16.

Start ONE engine against the candidate first:

    MODEL_PATH=/path/to/candidate-Q8_0.gguf ASSISTANT_BACKEND=sensen \
        ./backend/build/calculator_engine > engine.log 2>&1 &

Two layers, answering different questions:

  --layer raw  (default)  Scores `[assistant] raw model output`, which the engine
                          logs BEFORE verification. Use when live market data is
                          unavailable: symbol verification then refuses or
                          clarifies regardless of what the model produced, and
                          every model scores identically (observed: a good and a
                          bad model both scored 3/16 -- a measurement of the
                          verification layer, not the model).
  --layer rpc             Scores the ParseResponse. Requires Alpaca credentials.
                          Measures the model PLUS verification and aliasing, which
                          is what a user experiences.

Requires grpcio and stubs generated from backend/proto/assistant.proto:

    python -m grpc_tools.protoc -I backend/proto --python_out=. --grpc_python_out=. \
        backend/proto/assistant.proto

@author Olumuyiwa Oluwasanmi
"""
import argparse
import json
import re
import subprocess
import sys
import time

import grpc

import assistant_pb2
import assistant_pb2_grpc


def die(msg):
    print(f"eval_assistant_sensen: {msg}", file=sys.stderr)
    sys.exit(2)


def single_engine_or_die(target):
    """Refuse to measure while several engines are listening.

    Engines bind :50051 with SO_REUSEPORT, so N stale engines each holding a
    DIFFERENT model all listen at once and the kernel splits requests between
    them. That produced a SPY iron-condor prompt answered with bond-futures text
    -- indistinguishable from KV-cache bleed or a corrupt checkpoint. Five had
    accumulated before anyone noticed.

    Note `pkill -x calculator_engine` matches NOTHING: comm is truncated to 15
    characters. Use `pkill -x calculator_engi`. Never `pkill -f` the binary path,
    or the launching shell matches its own command line and kills itself.

    Only meaningful against a local engine -- skipped for a remote target, whose
    processes and listening sockets are not visible from here.
    """
    host, _, port = target.rpartition(":")
    if host not in ("localhost", "127.0.0.1", "::1", ""):
        print(f"  NOTE: {target} is remote; cannot verify a single engine is serving it.")
        return
    procs = subprocess.run(["pgrep", "-x", "calculator_engi"],
                           capture_output=True, text=True).stdout.split()
    listeners = subprocess.run(["ss", "-ltn"], capture_output=True, text=True).stdout
    n_listen = sum(1 for line in listeners.splitlines() if f":{port}" in line)
    if len(procs) != 1 or n_listen != 1:
        die(f"expected exactly 1 engine and 1 listener on :{port}, found "
            f"{len(procs)} engine(s) and {n_listen} listener(s). "
            f"Run `pkill -x calculator_engi` and start a single engine.")


def expected_params(text):
    m = re.search(r"<params>(.*?)</params>", text, re.S)
    return json.loads(m.group(1)) if m else None


def turns(conv):
    """(utterance, prior_clarification).

    prior_clarification carries the trader's ANSWER, not the question:
    build_prompt emits it as a user turn after a placeholder assistant question.
    The proto comment calling it "the question this service asked" is stale.
    """
    users = [m["content"] for m in conv if m["role"] == "user"]
    return users[0], (users[1] if len(users) > 1 else "")


def score(got, want):
    if want is None:
        return got is None, ("asked (no params)" if got is None else f"emitted {got}")
    if got is None:
        return False, "no <params> emitted"
    bad = [f"{k}: {got.get(k)!r}!={v!r}" for k, v in want.items() if got.get(k) != v]
    return (not bad), ("match" if not bad else "; ".join(bad))


def main():
    ap = argparse.ArgumentParser(
        description="Score a strategy-assistant model through the sensen serving path.")
    ap.add_argument("holdout", help="JSONL holdout, one conversation per line")
    ap.add_argument("engine_log", help="stdout/stderr log of the engine under test")
    ap.add_argument("target", nargs="?", default="localhost:50051")
    ap.add_argument("--layer", choices=("raw", "rpc"), default="raw",
                    help="raw: score pre-verification model output (default). "
                         "rpc: score ParseResponse (needs live market data).")
    a = ap.parse_args()
    hold, log, target, layer = a.holdout, a.engine_log, a.target, a.layer

    single_engine_or_die(target)
    rows = [json.loads(l) for l in open(hold)]
    # Byte offset, not character count: the log is read back with errors="replace",
    # so a multibyte sequence would make a character-length mark drift.
    with open(log, "rb") as fh:
        fh.seek(0, 2)
        mark = fh.tell()

    ch = grpc.insecure_channel(target)
    grpc.channel_ready_future(ch).result(timeout=60)
    stub = assistant_pb2_grpc.StrategyAssistantStub(ch)

    rpc_out = []
    for r in rows:
        utt, prior = turns(r["conversations"])
        try:
            rpc_out.append(stub.ParseStrategy(
                assistant_pb2.ParseRequest(utterance=utt, prior_clarification=prior),
                timeout=180))
        except grpc.RpcError as e:
            rpc_out.append(e)

    if layer == "raw":
        time.sleep(2)
        with open(log, "rb") as fh:
            fh.seek(mark)
            tail = fh.read().decode("utf-8", errors="replace")
        chunks = re.split(r"\[assistant\] raw model output \(\d+ bytes\):", tail)[1:]
        if len(chunks) != len(rows):
            print(f"  WARNING: {len(chunks)} raw outputs for {len(rows)} rows -- "
                  f"is this the log of the engine you queried? Unmatched rows are "
                  f"scored as failures, never skipped.")
        gots = []
        for chunk in chunks:
            m = re.search(r"<params>(.*?)</params>", chunk, re.S)
            try:
                gots.append(json.loads(m.group(1).strip()) if m else None)
            except Exception:
                gots.append(None)
    else:
        gots = []
        for resp in rpc_out:
            if isinstance(resp, grpc.RpcError) or resp.WhichOneof("outcome") != "params":
                gots.append(None)
            else:
                p = resp.params
                gots.append({"symbol": p.symbol, "asset_class": p.asset_class,
                             "strategy": p.strategy, "expiration_days": p.expiration_days,
                             "quantity": p.quantity})

    # A short `gots` must never silently shorten the run: zip() would stop early
    # and the missing rows would vanish from the score instead of failing.
    if len(gots) < len(rows):
        gots += [None] * (len(rows) - len(gots))

    ok = 0
    print(f"  layer={layer}  target={target}  rows={len(rows)}\n")
    for r, got in zip(rows, gots):
        utt, _ = turns(r["conversations"])
        good, detail = score(got, expected_params(r["conversations"][-1]["content"]))
        ok += good
        print(f"  {'PASS' if good else 'FAIL'}  {utt[:42]:44s} {detail[:60]}")
    print(f"\n  {ok}/{len(rows)} correct")
    return 0 if ok == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
