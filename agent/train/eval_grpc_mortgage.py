#!/usr/bin/env python3
"""
Measures the mortgage fine-tune the way a homeowner's browser actually
experiences it: through mortgage.assistant.MortgageAssistant's real
ParseOperation RPC, against a running `calculator_engine`, on the Q8_0 GGUF --
rather than through transformers.generate() on the merged bf16 checkpoint.

@author Olumuyiwa Oluwasanmi

WHY THIS EXISTS ALONGSIDE evaluate.py, NOT INSTEAD OF IT

This is the exact sibling of eval_grpc.py, and its long docstring applies here
unchanged: evaluate.py measures the merged 16-bit weights straight out of
training, which is a fine sanity check on whether the LoRA adapter learned the
task but is NOT what ships. Production serves a Q8_0 GGUF through sensen's
in-process LLMPipeline with a q8 KV cache, behind mortgage_assistant_service.cpp's
own `build_prompt()` and `kSystemPrompt`. Weight quantization, KV quantization
and the real prompt-construction code all sit upstream of a ParseOperation call
and downstream of nothing this script controls -- which is the point. A gap
between evaluate.py's number and this one is itself a finding.

The project has already paid for measuring on the wrong engine once: a
`llama-cli` holdout scored a deployed strategy model 7/16 and triggered a
retrain to fix a regression that did not exist; through the real RPC the same
model scored 13/16. See docs/guides/ASSISTANT_EVALUATION.md.

WHAT MAKES THIS DIFFERENT FROM eval_grpc.py, BEYOND THE CONTRACT

The mortgage service runs a MANDATORY GP-ARA verification gate
(backend/src/modules/mortgage_verification.cppm) between the model's output and
the response: nothing reaches `ParseResponse.params` except a verdict of Proven.
So the model's accuracy and the user's experience are two DIFFERENT numbers and
this script reports them separately, never collapsed:

  1. RAW MODEL ACCURACY -- exact match of the model's own `<params>` JSON
     against the gold label, BEFORE any verification. Directly comparable to
     evaluate.py's number, because it is the same comparison (parsed dict
     equality) on the same rows. Sourced from the engine's own
     `[mortgage-assistant] raw model output (N bytes): ...` stderr line, which
     interpret_model_output() emits UNCONDITIONALLY and ahead of every check.

  2. SERVED OUTCOME DISTRIBUTION -- of N requests, how many came back as
     `params` / `clarification` / `refusal`, and for refusals which
     Refusal.Reason and which failure shape. This is what a user experiences.

The raw line is matched to a request POSITIONALLY, which is exact only because
this harness is strictly sequential: the engine flushes that line before it
writes the response, so every block that appears in the log between one RPC
returning and the next being sent belongs to the RPC just completed. Do not add
concurrency here without adding a correlation id to that log line first.

ROW SHAPES, from agent/dataset/build_mortgage_dataset.py:

  * 3-turn (system, user, assistant-with-params) -- a direct extraction. One
    ParseOperation call.
  * 3-turn (system, user, assistant-prose)       -- a refusal or a question is
    the correct answer. One call; correct iff the model emitted no params.
  * 5-turn -- TWO calls, mirroring how a real client uses the contract:
      1. ParseOperation(utterance, prior_clarification="")
      2. ParseOperation(utterance, prior_clarification=reply) -- scored.

    THE 5-TURN ROWS ARE TWO DIFFERENT SHAPES AND MUST NOT BE POOLED. Both
    `make_clarification` and `make_modification` in build_mortgage_dataset.py
    produce five turns, and the only thing that tells them apart is whether the
    FIRST assistant turn carries a `<params>` block. In val.jsonl: 81
    clarification rows (first assistant turn is a question) and 61 modification
    rows (first assistant turn is already an answer, and the user's reply is
    "redo it over 20-year").

    Pooling them makes the first-call metric meaningless, and worse, it reads as
    a model failure: scoring "did it ask?" against a modification row punishes
    the model for answering a question that was fully specified, which is the
    correct behaviour. Only the clarification rows are scored as
    `asked_when_expected`; the modification rows' first call is recorded
    separately as `answered_when_expected`.

    A caveat on modification rows that is a CONTRACT limit, not a model one:
    `build_prompt` always frames `prior_clarification` as the answer to a
    question, inserting a fixed "Could you clarify?" assistant placeholder ahead
    of it. On a modification row the real prior assistant turn was a params
    block, so the prompt this harness can construct is structurally unlike the
    row's training shape. ParseRequest carries no way to express "the previous
    turn was an answer", so this is a limit of the contract as it stands.

USAGE (the caller is responsible for having exactly ONE `calculator_engine`
running with MORTGAGE_MODEL_PATH pointed at the GGUF under test -- engines bind
:50051 with SO_REUSEPORT, so several stale engines each holding a different
model all listen at once and the kernel splits requests between them):

    pgrep -x calculator_engi | wc -l     # MUST be 1

    python3 eval_grpc_mortgage.py \\
        --val ../dataset/data_mortgage/val.jsonl --n 150 \\
        --engine-log /path/to/engine.log --addr localhost:50051 \\
        --label v2 --json-out v2.json

Generate the stubs the same way eval_grpc.py documents for its own:

    python3 -m grpc_tools.protoc -I<proto_dir> --python_out=. \\
        --grpc_python_out=. <proto_dir>/mortgage_assistant.proto
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
    import mortgage_assistant_pb2
    import mortgage_assistant_pb2_grpc
except ImportError as e:  # pragma: no cover
    raise SystemExit(
        "mortgage_assistant_pb2*.py not found next to this script. Generate them "
        "from backend/proto/mortgage_assistant.proto with:\n"
        "  python3 -m grpc_tools.protoc -I<proto_dir> --python_out=. "
        "--grpc_python_out=. <proto_dir>/mortgage_assistant.proto"
    ) from e

RAW_MARKER = b"[mortgage-assistant] raw model output ("

REASON_NAME = {
    v.number: v.name
    for v in mortgage_assistant_pb2.Refusal.Reason.DESCRIPTOR.values
}


# ---------------------------------------------------------------------------
# The engine's own stderr, read as a data source
# ---------------------------------------------------------------------------
class RawOutputTail:
    """Follows the engine log and yields each raw model output block.

    The block is delimited by a BYTE COUNT the engine itself prints, not by a
    newline: the model's output routinely contains newlines (`<think>` blocks
    are two of them), so a line-oriented reader would split one answer into
    several and silently score a fragment. Reading exactly N bytes after the
    marker is exact for any output the model can produce.
    """

    def __init__(self, path: Path) -> None:
        self._fh = path.open("rb")
        self._fh.seek(0, 2)  # only what happens from here on is ours
        self._buf = b""

    def drain(self, settle: float = 0.05, tries: int = 20) -> list[str]:
        """Returns every COMPLETE raw block written since the last drain.

        `settle` exists only for the tail of a block that was mid-write when the
        response was flushed; the engine writes the line before the response, so
        in practice the first read already has it.
        """
        out: list[str] = []
        for attempt in range(tries):
            chunk = self._fh.read()
            if chunk:
                self._buf += chunk
            out.extend(self._extract())
            if out or not self._pending():
                break
            time.sleep(settle)
        return out

    def _pending(self) -> bool:
        return RAW_MARKER in self._buf

    def _extract(self) -> list[str]:
        out: list[str] = []
        while True:
            start = self._buf.find(RAW_MARKER)
            if start < 0:
                # Nothing to keep but a possible partial marker at the tail.
                if len(self._buf) > len(RAW_MARKER):
                    self._buf = self._buf[-len(RAW_MARKER):]
                return out
            head = start + len(RAW_MARKER)
            close = self._buf.find(b" bytes): ", head)
            if close < 0:
                self._buf = self._buf[start:]
                return out
            try:
                nbytes = int(self._buf[head:close])
            except ValueError:
                # Not actually our marker; step past it rather than stalling.
                self._buf = self._buf[head:]
                continue
            body = close + len(b" bytes): ")
            if len(self._buf) < body + nbytes:
                self._buf = self._buf[start:]
                return out
            out.append(self._buf[body:body + nbytes].decode("utf-8", "replace"))
            self._buf = self._buf[body + nbytes:]


# ---------------------------------------------------------------------------
# Label handling
# ---------------------------------------------------------------------------
def parse_params_text(text: str) -> dict | None:
    m = re.search(r"<params>(.*?)</params>", text, re.S)
    if not m:
        return None
    try:
        obj = json.loads(m.group(1))
    except Exception:
        return None
    return obj if isinstance(obj, dict) else None


def has_params_block(text: str) -> bool:
    """Whether the model TRIED to answer, as the service decides it.

    Deliberately textual and NOT `parse_params_text(...) is not None`. The
    service's `extract_params_block` also matches on the tag alone, so an
    output carrying a malformed block ("timing"]=, an unquoted key, a fullwidth
    zero inside a number) is an ATTEMPTED ANSWER that fails validation -- not a
    clarifying question. Scoring it as a question inverts the pass condition on
    the clarification rows and inflates "asked when ambiguous" by exactly the
    rows where the model's JSON fell apart, which was measured as 8/41 before
    this distinction was drawn and is 0/41 after.
    """
    return "<params>" in strip_think(text)


def strip_think(text: str) -> str:
    """Mirrors the service's own `strip_think_block` closely enough to find the
    params block: Qwen3 emits `<think>` on EVERY response including correct
    ones, and the engine tolerates an unclosed tag (a real defect it was bitten
    by once -- see docs/guides/ASSISTANT_EVALUATION.md)."""
    if "</think>" in text:
        return text.split("</think>", 1)[1]
    return text.replace("<think>", "", 1) if text.lstrip().startswith("<think>") else text


def encode_like_service(value) -> str:
    """Renders a gold JSON value the way mortgage_assistant_service.cpp encodes
    the model's, so a SERVED map<string,string> can be compared with a gold
    object at all. Used only for the secondary served-params number; the
    headline raw number compares parsed JSON to parsed JSON and needs none of
    this."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(value)
    if isinstance(value, list):
        return "[" + ",".join(encode_like_service(v) for v in value) + "]"
    return str(value)


def gold_as_served(gold: dict) -> dict:
    return {k: encode_like_service(v) for k, v in gold.items() if k != "operation"}


# ---------------------------------------------------------------------------
# The RPC
# ---------------------------------------------------------------------------
def call(stub, utterance: str, prior: str, timeout: float) -> tuple[str, dict | None, str, int]:
    """One ParseOperation. Returns (which, served_flat_or_None, text, reason)."""
    req = mortgage_assistant_pb2.ParseRequest(utterance=utterance, prior_clarification=prior)
    resp = stub.ParseOperation(req, timeout=timeout)
    which = resp.WhichOneof("outcome")
    if which == "params":
        served = {"operation": resp.params.operation}
        served.update(dict(resp.params.params))
        return "params", served, "", 0
    if which == "clarification":
        return "clarification", None, resp.clarification.question, 0
    if which == "refusal":
        return "refusal", None, resp.refusal.message, resp.refusal.reason
    return which or "EMPTY", None, "", 0


def classify_refusal(reason: int, message: str) -> str:
    """A coarse failure SHAPE on top of the Refusal.Reason enum.

    The enum collapses ten mv::ReasonCode values onto INVALID_PARAMETERS (see
    `map_verification_reason`), and the interesting distinction -- a structurally
    fine object carrying a number the user never said, versus an object missing a
    field -- lives only in the message. The GP-ARA reason codes are logged at
    DEBUG, which the default INFO level suppresses, so the message is what there
    is.
    """
    name = REASON_NAME.get(reason, str(reason))
    m = message.lower()
    if "is not one of the finance operations" in m:
        shape = "unknown-operation"
    elif "did not name a calculation" in m:
        shape = "no-operation-key"
    elif "left out" in m:
        shape = "missing-field"
    elif "is not a parameter of" in m or "is not a field of" in m:
        shape = "unknown-field"
    elif "grounded" in m or "does not appear" in m or "not derivable" in m:
        shape = "ungrounded-value"
    elif "could not be parsed as a json object" in m:
        shape = "unparseable-json"
    elif "could not produce structured parameters" in m:
        shape = "no-params-no-question"
    elif "not a constant of" in m or "not a boolean" in m:
        shape = "bad-enum-or-bool"
    elif "could not be verified against your request" in m:
        shape = "verification-indeterminate"
    else:
        shape = "other"
    return f"{name}/{shape}"


# ---------------------------------------------------------------------------
# The measurement
# ---------------------------------------------------------------------------
def evaluate(rows: list[dict], stub, tail: RawOutputTail, timeout: float,
             verbose: bool) -> dict:
    raw_exact = raw_total = 0            # gold-has-params rows, raw model output
    raw_emitted = 0                      # of those, how many emitted any params
    raw_nonparam_ok = raw_nonparam_total = 0
    served_exact = 0
    outcomes: dict[str, int] = {}
    refusal_shapes: dict[str, int] = {}
    outcomes_nonparam: dict[str, int] = {}
    asked_ok = asked_total = 0        # clarification rows: first call should ASK
    answered_ok = answered_total = 0  # modification rows: first call should ANSWER
    asked_raw_ok = [0]                # the same two, measured on the raw output
    raw_block_invalid = [0]           # <params> emitted but not parseable as JSON
    answered_raw_ok = [0]
    errors = 0
    failures: list[dict] = []

    for idx, r in enumerate(rows):
        convo = [t for t in r["conversations"] if t["role"] != "system"]
        target = convo[-1]["content"]
        want = parse_params_text(target)

        try:
            if len(convo) == 2:
                utterance, reply = convo[0]["content"], ""
            elif len(convo) == 4:
                utterance, reply = convo[0]["content"], convo[2]["content"]
                # Which of the two 5-turn shapes this is -- see the module
                # docstring. The gold first assistant turn is the only thing
                # that says, and scoring the wrong one inverts the pass
                # condition.
                is_modification = parse_params_text(convo[1]["content"]) is not None
                which1, _, _, _ = call(stub, utterance, "", timeout)
                raws1 = tail.drain()
                # The RAW view of the same question, for the same reason the
                # headline accuracy is measured raw: `which1` is the model plus
                # the GP-ARA gate, and a model that asked correctly can still be
                # served as something else.
                asked_raw = bool(raws1) and not has_params_block(raws1[-1])
                if is_modification:
                    answered_total += 1
                    if which1 == "params":
                        answered_ok += 1
                    if not asked_raw:
                        answered_raw_ok[0] += 1
                else:
                    asked_total += 1
                    if which1 == "clarification":
                        asked_ok += 1
                    elif verbose:
                        print(f"  [should-have-asked] {utterance[:70]!r} -> {which1}")
                    if asked_raw:
                        asked_raw_ok[0] += 1
            else:
                continue
            which, served, text, reason = call(stub, utterance, reply, timeout)
            raws = tail.drain()
        except grpc.RpcError as e:
            errors += 1
            if verbose:
                print(f"  [rpc error] {e.code()}: {e.details()}")
            continue

        raw_text = raws[-1] if raws else ""
        got = parse_params_text(strip_think(raw_text)) if raw_text else None

        outcomes[which] = outcomes.get(which, 0) + 1
        if which == "refusal":
            shape = classify_refusal(reason, text)
            refusal_shapes[shape] = refusal_shapes.get(shape, 0) + 1

        if want is None:
            # A question or a refusal is the correct answer here.
            raw_nonparam_total += 1
            outcomes_nonparam[which] = outcomes_nonparam.get(which, 0) + 1
            if got is None:
                raw_nonparam_ok += 1
            elif verbose:
                print(f"  [should-refuse/ask] {utterance[:70]!r} -> params instead")
            continue

        raw_total += 1
        if got is not None:
            raw_emitted += 1
        elif raw_text and has_params_block(raw_text):
            # Tried to answer and produced a <params> block that will not parse.
            # Counted apart from "emitted nothing" because the two are different
            # defects: one is a model that stayed silent, the other is a decode
            # that fell apart mid-object.
            raw_block_invalid[0] += 1
        if got == want:
            raw_exact += 1
        else:
            failures.append({
                "row": idx,
                "utterance": utterance,
                "reply": reply,
                "want": want,
                "raw": raw_text,
                "got": got,
                "served": which,
                "served_detail": text if which != "params" else "",
            })
        if served is not None:
            want_served = {"operation": want.get("operation", ""), **gold_as_served(want)}
            if served == want_served:
                served_exact += 1

    return {
        "raw_exact": raw_exact, "raw_total": raw_total, "raw_emitted": raw_emitted,
        "raw_block_invalid": raw_block_invalid[0],
        "raw_nonparam_ok": raw_nonparam_ok, "raw_nonparam_total": raw_nonparam_total,
        "served_exact": served_exact,
        "outcomes": outcomes, "outcomes_nonparam": outcomes_nonparam,
        "refusal_shapes": refusal_shapes,
        "asked_ok": asked_ok, "asked_total": asked_total,
        "asked_raw_ok": asked_raw_ok[0],
        "answered_ok": answered_ok, "answered_total": answered_total,
        "answered_raw_ok": answered_raw_ok[0],
        "errors": errors, "failures": failures,
    }


def report(res: dict, label: str, n_failures: int) -> None:
    rt = max(res["raw_total"], 1)
    print(f"\n=== {label} ===")
    print("-- 1. RAW MODEL ACCURACY (pre-verification, comparable to evaluate.py)")
    print(f"params exact-match   : {res['raw_exact']}/{res['raw_total']} "
          f"= {res['raw_exact'] / rt:.1%}")
    print(f"emitted valid <params>: {res['raw_emitted']}/{res['raw_total']} "
          f"= {res['raw_emitted'] / rt:.1%}")
    print(f"<params> but bad JSON: {res['raw_block_invalid']}/{res['raw_total']} "
          f"= {res['raw_block_invalid'] / rt:.1%}")
    npt = max(res["raw_nonparam_total"], 1)
    print(f"non-params correct   : {res['raw_nonparam_ok']}/{res['raw_nonparam_total']} "
          f"= {res['raw_nonparam_ok'] / npt:.1%}")
    at = max(res["asked_total"], 1)
    print(f"asked-when-ambiguous : {res['asked_raw_ok']}/{res['asked_total']} raw, "
          f"{res['asked_ok']}/{res['asked_total']} served "
          f"= {res['asked_raw_ok'] / at:.1%} raw   (clarification rows)")
    ant = max(res["answered_total"], 1)
    print(f"answered-when-stated : {res['answered_raw_ok']}/{res['answered_total']} raw, "
          f"{res['answered_ok']}/{res['answered_total']} served "
          f"= {res['answered_raw_ok'] / ant:.1%} raw   (modification rows, first turn)")

    print("\n-- 2. SERVED OUTCOME (what a user gets, post-GP-ARA)")
    total_served = sum(res["outcomes"].values())
    for k in ("params", "clarification", "refusal"):
        v = res["outcomes"].get(k, 0)
        print(f"  {k:14} {v}/{total_served} = {v / max(total_served, 1):.1%}")
    for k, v in res["outcomes"].items():
        if k not in ("params", "clarification", "refusal"):
            print(f"  {k:14} {v}/{total_served}")
    if res["refusal_shapes"]:
        print("  refusal reasons:")
        for k, v in sorted(res["refusal_shapes"].items(), key=lambda kv: -kv[1]):
            print(f"    {v:4}  {k}")
    print(f"  served params exactly matching gold: {res['served_exact']}/{res['raw_total']}")
    if res["outcomes_nonparam"]:
        print(f"  (of the {res['raw_nonparam_total']} rows whose gold is prose: "
              f"{dict(res['outcomes_nonparam'])})")
    if res["errors"]:
        print(f"RPC errors: {res['errors']}")

    if res["failures"]:
        print(f"\n-- raw failures (first {n_failures})")
        for f in res["failures"][:n_failures]:
            print(f"\n  user : {f['utterance'][:110]}")
            if f["reply"]:
                print(f"  reply: {f['reply'][:80]}")
            print(f"  gold : {json.dumps(f['want'])}")
            print(f"  raw  : {f['raw'][:400]!r}")
            print(f"  served: {f['served']} {f['served_detail'][:120]}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--addr", default="localhost:50051")
    ap.add_argument("--val", help="held-out val.jsonl (evaluate.py-equivalent set)")
    ap.add_argument("--file", help="an arbitrary conversations-shaped JSONL")
    ap.add_argument("--engine-log", required=True,
                    help="the file the engine's stderr is redirected to; the raw "
                         "model output is read from it")
    ap.add_argument("--n", type=int, default=None)
    ap.add_argument("--timeout", type=float, default=180.0)
    ap.add_argument("--label", default="eval")
    ap.add_argument("--json-out", help="write the full result, failures included, here")
    ap.add_argument("--show-failures", type=int, default=8)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--assert-disjoint-from", action="append", default=[],
                    metavar="TRAIN.JSONL",
                    help="a train.jsonl the holdout MUST NOT overlap. Repeatable -- "
                         "pass one per model being compared, not one per run.")
    ap.add_argument("--allow-contamination", action="store_true",
                    help="report the overlap and score anyway (default: refuse)")
    args = ap.parse_args()

    if not args.val and not args.file:
        raise SystemExit("pass --val or --file")

    tail = RawOutputTail(Path(args.engine_log))
    channel = grpc.insecure_channel(args.addr)
    stub = mortgage_assistant_pb2_grpc.MortgageAssistantStub(channel)

    path = Path(args.val or args.file)
    rows = [json.loads(l) for l in path.read_text().splitlines() if l.strip()]
    if args.n:
        rows = rows[:args.n]

    # ---- the holdout must be held out FOR EVERY MODEL BEING COMPARED ----
    #
    # Each corpus revision shuffles and splits independently, so a row that is
    # val in corpus B can be TRAIN in corpus A. Comparing a model trained on A
    # against one trained on B, using B's val set, then scores one of them on
    # rows it memorised.
    #
    # This is not hypothetical and it inverted a real conclusion. 304 of 600
    # rows in the corpus-B holdout were byte-identical members of corpus A's
    # train split. On the full set the newer model looked like a wash (+16 net,
    # McNemar p = 0.17); on the 277 rows neither model had seen it was ahead by
    # 45 gained against 21 lost, p = 0.0043. The contamination was subsidising
    # the OLDER model and hiding a real improvement.
    #
    # Refuses by default rather than warning: a warning printed above a score
    # table is read as a caveat, and the number is quoted anyway.
    if args.assert_disjoint_from:
        def _key(row: dict) -> str:
            return json.dumps(row.get("conversations", row), sort_keys=True,
                              separators=(",", ":"))

        holdout = {_key(r) for r in rows}
        for train_path in args.assert_disjoint_from:
            tp = Path(train_path)
            train = {_key(json.loads(l))
                     for l in tp.read_text().splitlines() if l.strip()}
            overlap = holdout & train
            if overlap:
                msg = (f"CONTAMINATION: {len(overlap)}/{len(rows)} holdout rows "
                       f"({100 * len(overlap) / max(len(rows), 1):.1f}%) are also in "
                       f"{tp}. A model trained on that corpus is being scored on rows "
                       f"it memorised, which subsidises it against any model that was "
                       f"not.")
                if not args.allow_contamination:
                    raise SystemExit(msg + "\n  Re-run with a disjoint holdout, or pass "
                                           "--allow-contamination to score anyway.")
                print(f"[WARNING] {msg}")
            else:
                print(f"[ok] holdout is disjoint from {tp} ({len(train)} train rows)")

    t0 = time.time()
    res = evaluate(rows, stub, tail, args.timeout, args.verbose)
    dt = time.time() - t0
    print(f"[{len(rows)} rows, {dt:.1f}s, {dt / max(len(rows), 1) * 1000:.0f} ms/row]")
    report(res, args.label, args.show_failures)

    if args.json_out:
        Path(args.json_out).write_text(json.dumps({"label": args.label, **res}, indent=2))


if __name__ == "__main__":
    main()
