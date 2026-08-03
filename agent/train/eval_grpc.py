#!/usr/bin/env python3
"""
Measures the fine-tune the way a trader's browser actually experiences it:
through calculator.assistant.StrategyAssistant's real ParseStrategy RPC,
against a running backend process, rather than through transformers.generate()
on the merged bf16 checkpoint.

WHY THIS EXISTS ALONGSIDE evaluate.py, NOT INSTEAD OF IT:

evaluate.py measures the merged 16-bit weights straight out of training --
useful as a fast sanity check on whether the LoRA adapter itself learned the
task, but NOT what ships. Production serves a Q8_0 GGUF through sensen's
in-process LLMPipeline with a q8 KV cache (SENSEN_KV_DTYPE, default "q8" as
of this session -- see backend/sensen/src/kv_half.cppm's parseKvDtypeEnv()).
Weight quantization, KV-cache quantization and the ACTUAL prompt-construction
code in assistant_service.cpp (build_prompt(), the four-turn clarification
shape, kSystemPrompt) are all upstream of a real ParseStrategy call and
downstream of nothing this script controls -- which is the point. A number
from evaluate.py and a number from this script are both worth having; a gap
between them is itself a finding (see docs/STRATEGY_ASSISTANT_PIPELINE.md
section 5, point 6).

WHAT "the real backend" MEANS HERE: `calculator_engine`, the actual compiled
backend binary (built from this repo's backend/ tree, ASSISTANT_BACKEND=
sensen, the production default), pointed at a GGUF via MODEL_PATH and left to
pick every other default the way a Railway deployment would -- in particular,
SENSEN_KV_DTYPE is left UNSET so it defaults to q8, matching production
rather than pinning a value that happens to match today's default and would
go stale silently if that default ever moves again.

USAGE (the caller is responsible for having a `calculator_engine` already
running -- see docs/STRATEGY_ASSISTANT_PIPELINE.md or the bundle's own
run_engine.sh for how one was built and launched during this session):

    python3 eval_grpc.py --val ../dataset/data/val.jsonl --n 200 \\
        --addr localhost:50051

    python3 eval_grpc.py --file /path/to/defect_cases.jsonl --addr localhost:50051

Row shape is the same ShareGPT conversations list build_dataset.py produces:
a 3-turn row (system, user, assistant) is a direct extraction or a
refusal/question depending on whether the assistant turn contains a
<params> block; a 5-turn row (system, user, assistant-question, user-reply,
assistant-params) is make_clarification()'s shape and is evaluated as TWO
RPC calls, mirroring exactly how a real client uses this contract:

    1. ParseStrategy(utterance=user_turn, prior_clarification="")
       -- expected to come back as `clarification` (a question), not
       `params`. Recorded as `asked_when_expected`.
    2. ParseStrategy(utterance=user_turn, prior_clarification=reply_turn)
       -- expected to come back as `params` matching the row's final
       object. Scored into the same exact-match / per-field numbers as a
       direct extraction.

@author Olumuyiwa Oluwasanmi
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path

import grpc

sys.path.insert(0, str(Path(__file__).parent))
try:
    import assistant_pb2
    import assistant_pb2_grpc
except ImportError as e:  # pragma: no cover
    raise SystemExit(
        "assistant_pb2*.py not found next to this script. Generate them from "
        "backend/proto/assistant.proto with:\n"
        "  python3 -m grpc_tools.protoc -I<proto_dir> --python_out=. "
        "--grpc_python_out=. <proto_dir>/assistant.proto"
    ) from e

FIELDS = ["symbol", "asset_class", "strategy", "expiration_days", "quantity"]


def parse_params_text(text: str) -> dict | None:
    m = re.search(r"<params>(.*?)</params>", text, re.S)
    if not m:
        return None
    try:
        return json.loads(m.group(1))
    except Exception:
        return None


def call(stub: "assistant_pb2_grpc.StrategyAssistantStub", utterance: str, prior: str,
         timeout: float) -> tuple[dict | None, str, str | None]:
    """One ParseStrategy RPC. Returns (params_dict_or_None, oneof_name, text)."""
    req = assistant_pb2.ParseRequest(utterance=utterance, prior_clarification=prior)
    resp = stub.ParseStrategy(req, timeout=timeout)
    which = resp.WhichOneof("outcome")
    if which == "params":
        p = resp.params
        return ({"symbol": p.symbol, "asset_class": p.asset_class, "strategy": p.strategy,
                  "expiration_days": p.expiration_days, "quantity": p.quantity},
                which, None)
    if which == "clarification":
        return None, which, resp.clarification.question
    if which == "refusal":
        return None, which, resp.refusal.message
    return None, which or "EMPTY", None


def load_rows(path: Path, n: int | None) -> list[dict]:
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    return rows[:n] if n else rows


def evaluate(rows: list[dict], stub, timeout: float, verbose: bool) -> dict:
    exact = total = 0
    fields = {k: 0 for k in FIELDS}
    non_param_ok = non_param_total = 0
    asked_ok = asked_total = 0
    bad: list[tuple[str, dict, dict | None]] = []
    errors = 0

    for r in rows:
        convo = [t for t in r["conversations"] if t["role"] != "system"]
        target = convo[-1]["content"]
        want = parse_params_text(target)

        try:
            if len(convo) == 2:
                user_turn = convo[0]["content"]
                got, which, _ = call(stub, user_turn, "", timeout)
            elif len(convo) == 4:
                user_turn = convo[0]["content"]
                reply_turn = convo[2]["content"]
                # First call: the model should ASK, not answer -- this is
                # the defect-1/defect-5 disambiguation behaviour itself.
                got1, which1, _ = call(stub, user_turn, "", timeout)
                asked_total += 1
                if which1 != "params":
                    asked_ok += 1
                elif verbose:
                    print(f"  [should-have-asked] {user_turn!r} -> params instead of a question")
                got, which, _ = call(stub, user_turn, reply_turn, timeout)
            else:
                continue
        except grpc.RpcError as e:
            errors += 1
            if verbose:
                print(f"  [rpc error] {e.code()}: {e.details()}")
            continue

        if want is None:
            non_param_total += 1
            if which != "params":
                non_param_ok += 1
            elif verbose:
                print(f"  [should-refuse/ask] {convo[0]['content']!r} -> params instead")
            continue

        total += 1
        if got == want:
            exact += 1
        else:
            bad.append((convo[0]["content"], want, got))
        for k in FIELDS:
            if got and got.get(k) == want.get(k):
                fields[k] += 1

    return {
        "exact": exact, "total": total, "fields": fields,
        "non_param_ok": non_param_ok, "non_param_total": non_param_total,
        "asked_ok": asked_ok, "asked_total": asked_total,
        "errors": errors, "bad": bad,
    }


def report(res: dict, label: str) -> None:
    total = max(res["total"], 1)
    print(f"\n=== {label} ===")
    print(f"params exact-match : {res['exact']}/{res['total']} = {res['exact']/total:.1%}")
    for k, v in res["fields"].items():
        print(f"  {k:16} {v}/{res['total']} = {v/total:.1%}")
    npt = max(res["non_param_total"], 1)
    print(f"non-params correct : {res['non_param_ok']}/{res['non_param_total']} "
          f"= {res['non_param_ok']/npt:.1%}")
    at = max(res["asked_total"], 1)
    print(f"asked-when-ambiguous: {res['asked_ok']}/{res['asked_total']} = {res['asked_ok']/at:.1%}")
    if res["errors"]:
        print(f"RPC errors: {res['errors']}")
    if res["bad"]:
        print("\nfirst mismatches:")
        for u, w, g in res["bad"][:5]:
            print(f"  user: {u[:70]}")
            print(f"    want {w}")
            print(f"    got  {g}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--addr", default="localhost:50051")
    ap.add_argument("--val", help="held-out val.jsonl (evaluate.py-equivalent set)")
    ap.add_argument("--file", help="an arbitrary conversations-shaped JSONL "
                                   "(e.g. a hand-written defect regression set)")
    ap.add_argument("--n", type=int, default=None)
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--label", default="eval")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if not args.val and not args.file:
        raise SystemExit("pass --val or --file")

    channel = grpc.insecure_channel(args.addr)
    stub = assistant_pb2_grpc.StrategyAssistantStub(channel)

    path = Path(args.val or args.file)
    rows = load_rows(path, args.n)
    t0 = time.time()
    res = evaluate(rows, stub, args.timeout, args.verbose)
    dt = time.time() - t0
    print(f"[{len(rows)} rows, {dt:.1f}s, {dt/max(len(rows),1)*1000:.0f} ms/row]")
    report(res, args.label)


if __name__ == "__main__":
    main()
