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
import hashlib
import json
import re
import sys
import time
from pathlib import Path

import grpc

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "dataset"))
try:
    from build_mortgage_dataset import OP_EXCLUDED_FIELDS
except Exception:                                   # dataset module unavailable
    OP_EXCLUDED_FIELDS = {}


def drop_excluded(obj: dict | None) -> dict | None:
    """Remove fields the chosen operation DISCARDS, as the service does.

    `mortgage_assistant_service.cpp` drops an excluded field BEFORE building
    `verifiable.fields`, so it never reaches grounding and never reaches the
    Finance RPC. Comparing it here measures something production cannot see.

    It is not hypothetical arithmetic. On 2026-09-14 the raw comparison scored
    ComputeRate 0/7, ComputeIrr 0/6 and ComputeXirr 0/6 -- every failure the
    `guess` seed alone, with every other field exact. All three are perfect as
    production serves them, and three of the four "unserved operations" a
    coverage sweep reported were this and nothing else. A harness that reports
    0/N for an operation that answers N/N is not conservative; it is wrong in
    the direction that triggers an unnecessary retrain, which this project has
    already paid for once.

    `guess` appears ZERO times in the training corpus and the system prompt is
    322 characters with no field list, so the model is not learning it here --
    it is a pretraining prior (Excel's `IRR(values, [guess])`). That is why
    this is a measurement fix and not a corpus one.
    """
    if not obj:
        return obj
    excluded = OP_EXCLUDED_FIELDS.get(obj.get("operation", ""), set())
    return {k: v for k, v in obj.items() if k not in excluded} if excluded else obj
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


def served_mismatch_shape(served: dict, want_served: dict) -> str:
    """How a SERVED object differs from gold, as a shape rather than a verdict.

    WHY THIS EXISTS, and why it does not simply mirror the service's rules.
    `served_exact` compares the service's answer to the gold label byte for
    byte, and the service deliberately TRANSFORMS its answer on the way out:
    it drops `kOperationExcludedFields` and `kVariantInertFields` before
    building the params map, and it flips the sign of `payment` on
    ComputeRate/ComputePeriods for the TVM convention the Finance RPC
    documents. All three are correct and intentional, and every one of them
    makes a perfect parse compare unequal.

    Measured on the v19 run, 2026-09-16: ComputeRate scored **13/13 raw and
    0/13 served**, which cannot be a property of a model. So did
    ComputePeriods (9/9 -> 0/9), ComputeAmortizationBatch (7/7 -> 0/7) and
    ComputeDepreciation (7/10 -> 0/10). `served_exact` was understating the
    user experience on every operation the service translates, which is the
    `drop_excluded` trap this file already documents -- applied to the raw
    comparison and forgotten on the served one.

    The fix is NOT to reimplement the three rules here. This repository's
    standing lesson is that a contract copied into a second place drifts and
    has no mechanism to keep it honest -- the five-tables rule, and the
    client's hand-written ALLOWED_OPERATIONS that refused thirteen live
    operations. So this classifies the DIFFERENCE instead, which is derived
    from the two objects in front of it and stays correct when a rule changes:

      exact           -- equal
      dropped-fields  -- served is a strict SUBSET of gold and agrees on every
                         shared key: the service removed fields, nothing
                         disagrees
      sign            -- identical except that one numeric field differs only
                         by a leading minus
      extra-fields    -- served carries a key gold does not
      value           -- a genuine disagreement on a shared key

    Only `value` and `extra-fields` are model errors. The others are read
    beside `served_exact`, never folded into it silently.
    """
    if served == want_served:
        return "exact"
    s_keys, w_keys = set(served), set(want_served)
    if s_keys - w_keys:
        return "extra-fields"
    shared_agree = all(served[k] == want_served[k] for k in s_keys & w_keys)
    if s_keys < w_keys and shared_agree:
        return "dropped-fields"
    differing = [k for k in s_keys & w_keys if served[k] != want_served[k]]
    if differing and all(
            str(served[k]).lstrip("-") == str(want_served[k]).lstrip("-")
            and str(served[k]) != str(want_served[k])
            for k in differing):
        return "sign" if s_keys == w_keys else "sign+dropped-fields"
    return "value"


def gold_as_served(gold: dict) -> dict:
    return {k: encode_like_service(v) for k, v in gold.items() if k != "operation"}


# ---------------------------------------------------------------------------
# The RPC
# ---------------------------------------------------------------------------
def call(stub, utterance: str, prior: str, timeout: float,
         prior_question: str = "") -> tuple[str, dict | None, str, int]:
    """One ParseOperation. Returns (which, served_flat_or_None, text, reason).

    `prior_question` is the question THIS SERVICE asked on the previous turn,
    echoed back. A real client already holds it -- it is the
    `clarification.question` the first call returned -- and the harness must
    send it for the same reason: without it the service substitutes a neutral
    placeholder that appears ZERO times in the training corpus, and for an
    operation whose reply is untyped the question text is the only thing
    binding that reply to a slot. Measured: six models across three ranks and
    three corpora failed the identical 17 ComputeRentalRoi rows -- every one a
    clarification row -- while passing all 16 single-turn rows.
    """
    req = mortgage_assistant_pb2.ParseRequest(
        utterance=utterance, prior_clarification=prior, prior_question=prior_question)
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
             verbose: bool, strict: bool = False) -> dict:
    raw_exact = raw_total = 0            # gold-has-params rows, raw model output
    raw_emitted = 0                      # of those, how many emitted any params
    raw_nonparam_ok = raw_nonparam_total = 0
    served_exact = 0
    outcomes: dict[str, int] = {}
    refusal_shapes: dict[str, int] = {}
    outcomes_nonparam: dict[str, int] = {}
    asked_ok = asked_total = 0        # clarification rows: first call should ASK
    answered_ok = answered_total = 0  # modification rows: first call should ANSWER
    row_verdicts: list[dict] = []     # per-row, for pairing two models
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
            question1 = ""   # bound on every path: the single-turn branch never asks
            if len(convo) == 2:
                utterance, reply = convo[0]["content"], ""
            elif len(convo) == 4:
                utterance, reply = convo[0]["content"], convo[2]["content"]
                # Which of the two 5-turn shapes this is -- see the module
                # docstring. The gold first assistant turn is the only thing
                # that says, and scoring the wrong one inverts the pass
                # condition.
                is_modification = parse_params_text(convo[1]["content"]) is not None
                which1, _, question1, _ = call(stub, utterance, "", timeout)
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
            # Echo the service's OWN question back on the second call, exactly
            # as a real client does -- `call` returns it as `text` on a
            # clarification outcome. Sending "" makes the service substitute a
            # placeholder that is out-of-distribution for every model trained on
            # this corpus; see call()'s docstring.
            # `question1` IS the echo this comment describes, and it was computed
            # on every path and then not passed -- so `prior_question` defaulted
            # to "" on every scored call and the service saw a placeholder that
            # appears zero times in the training corpus. It is also the only
            # thing that tells the service's derivation layer a REVISION from an
            # ANSWER: empty means the previous turn was an answer. Without it the
            # HELOC reply "75%" reads as a restatement of the interest rate.
            which, served, text, reason = call(stub, utterance, reply, timeout, question1)
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
        if strict:
            comparable = got
        else:
            comparable = drop_excluded(got)
        # PER-ROW VERDICTS, so two models can be PAIRED.
        #
        # Without these a comparison has only totals, and a difference of
        # totals cannot distinguish "the new model fixed 40 rows and broke 28"
        # from "it changed nothing twice". McNemar needs the pairing, this
        # file's own instructions demand McNemar, and on 2026-09-15 the v16/v15
        # comparison could not run one because only aggregates were written.
        #
        # Keyed by the row's INDEX IN THE HOLDOUT, which is stable only within
        # one holdout file -- `holdout_provenance` already refuses to compare
        # two runs whose sha256 differs, which is what makes the index safe to
        # pair on.
        row_verdicts.append({
            "row": idx,
            "operation": (want or {}).get("operation", ""),
            "raw_exact": comparable == want,
            "served_exact": served is not None
                            and served == {"operation": (want or {}).get("operation", ""),
                                           **gold_as_served(want)},
            "served_shape": served_mismatch_shape(
                served, {"operation": (want or {}).get("operation", ""),
                         **gold_as_served(want)}) if served is not None else "",
            "outcome": which,
        })
        if comparable == want:
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
        "row_verdicts": row_verdicts,
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
    shapes: dict[str, int] = {}
    for rv in res.get("row_verdicts", []):
        if rv.get("served_shape"):
            shapes[rv["served_shape"]] = shapes.get(rv["served_shape"], 0) + 1
    if shapes:
        # `dropped-fields` and `sign` are the service's OWN documented
        # translations, not model errors -- see served_mismatch_shape. They are
        # reported beside the exact count and never folded into it, because a
        # metric that forgives silently is how the excluded-field trap got
        # here in the first place.
        print("  served vs gold, by difference SHAPE "
              "(dropped-fields/sign are service translations, not model errors):")
        for k in ("exact", "dropped-fields", "sign", "sign+dropped-fields",
                  "extra-fields", "value"):
            if k in shapes:
                print(f"    {shapes[k]:4}  {k}")
        agree = sum(shapes.get(k, 0) for k in
                    ("exact", "dropped-fields", "sign", "sign+dropped-fields"))
        print(f"  served agreeing with gold up to service translation: "
              f"{agree}/{res['raw_total']}")
    if res["outcomes_nonparam"]:
        print(f"  (of the {res['raw_nonparam_total']} rows whose gold is prose: "
              f"{dict(res['outcomes_nonparam'])})")
    if res["errors"]:
        print(f"RPC errors: {res['errors']}")

    print("\n-- 3. PER-OPERATION, raw AND served")
    per: dict[str, list[int]] = {}
    for rv in res.get("row_verdicts", []):
        slot = per.setdefault(rv["operation"], [0, 0, 0])
        slot[0] += 1
        slot[1] += bool(rv["raw_exact"])
        slot[2] += bool(rv["served_exact"])
    # Sorted by the RAW shortfall, so the operation most in need of attention
    # leads. Served is printed beside it because the two answer different
    # questions and a gap between them is itself the finding: a large raw
    # shortfall with a small served one means the serving layer is carrying the
    # model, and the reverse means serving is destroying correct parses.
    for op, (n, r, s) in sorted(per.items(), key=lambda kv: kv[1][1] - kv[1][0]):
        flag = ""
        if n and r and not s:
            flag = "   <- serving loses EVERY correct parse"
        elif n and s > r:
            flag = f"   <- serving recovers {s - r}"
        print(f"  {op:32s} raw {r:3d}/{n:<3d}  served {s:3d}/{n:<3d}{flag}")

    # OPERATION-NAME CONFUSION.
    #
    # WHY THIS EXISTS. A per-operation line reading "ComputeFutureValueDetailed
    # 0/44" is a number, not a diagnosis, and the two readings it admits call for
    # opposite fixes: the model may be extracting the wrong VALUES, or it may be
    # naming the wrong OPERATION with the values essentially right. Measured on
    # 2026-09-16, that 0/44 was 44 of 44 naming the plain sibling
    # `ComputeFutureValue` -- an operation-choice failure, which
    # mortgage_verification.cppm states outright that it cannot decide ("G1
    # proves the operation EXISTS; nothing here proves it is the one the user
    # meant"). Nothing in this report said so, so the zero was read as lost
    # extraction capability and sent a diagnosis in the wrong direction for a
    # day. The confusion is one dict comprehension away from the failure list
    # that was already being collected.
    confusion: dict[tuple[str, str], int] = {}
    for f in res["failures"]:
        want = (f.get("want") or {}).get("operation")
        got = (f.get("got") or {}).get("operation")
        if want and got and want != got:
            confusion[(want, got)] = confusion.get((want, got), 0) + 1
    if confusion:
        print("\n-- 4. OPERATION-NAME CONFUSION (the model named a DIFFERENT operation)")
        for (want, got), n in sorted(confusion.items(), key=lambda kv: -kv[1]):
            # A sibling pair is one name containing the other, which is the
            # shape this family fails in: *Detailed* dropped, or a dated
            # operation named as its undated twin.
            sibling = " SIBLING" if (want in got or got in want) else ""
            print(f"  {n:4d}  {want} -> {got}{sibling}")
        named = sum(confusion.values())
        wrong = res["raw_total"] - res["raw_exact"]
        print(f"  {named} of {wrong} raw failures are the wrong OPERATION, "
              f"not wrong values.")

    if res["failures"]:
        print(f"\n-- raw failures (first {n_failures})")
        for f in res["failures"][:n_failures]:
            print(f"\n  user : {f['utterance'][:110]}")
            if f["reply"]:
                print(f"  reply: {f['reply'][:80]}")
            print(f"  gold : {json.dumps(f['want'])}")
            print(f"  raw  : {f['raw'][:400]!r}")
            print(f"  served: {f['served']} {f['served_detail'][:120]}")


def gold_has_params(row: dict) -> bool:
    """The SAME predicate `raw_total` counts with -- the gold (last) turn parsing
    to a params block. Written once and used by both, because a provenance field
    that answers a slightly different question than the metric it describes is
    worse than no field: it looks like corroboration."""
    convo = [t for t in row["conversations"] if t["role"] != "system"]
    return parse_params_text(convo[-1]["content"]) is not None


def holdout_provenance(path: Path, scored: list[dict]) -> dict:
    """Identify the DATASET a score was measured on.

    WHY THIS EXISTS. On 2026-09-15 a 40-row "regression" was chased through a
    submodule bisect, a full CCACHE_DISABLE rebuild and a per-row failure diff
    before the cause turned out to be that two runs read two different files --
    /tmp/.../val-v15.jsonl (563 gold-params rows) and
    agent/dataset/data_mortgage/val.jsonl (558). `--label` recorded what the run
    was CALLED; nothing recorded what it was MEASURED ON, so two incomparable
    numbers sat side by side looking comparable.

    `gold_has_params` is the tell and it is a property of the INPUT FILE: a model
    change cannot move it. Recording it next to the sha256 means the next reader
    sees the denominator move before they start theorising about weights.
    """
    return {
        "path": str(path.resolve()),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "bytes": path.stat().st_size,
        "rows_scored": len(scored),
        "gold_has_params": sum(1 for r in scored if gold_has_params(r)),
    }


def refuse_on_holdout_mismatch(here: dict, priors: list[str], allow: bool) -> None:
    """Compare THIS run's holdout against each prior result's, and refuse.

    Checked BEFORE the first RPC, deliberately: a mismatch found after a
    twelve-minute decode is one the reader is invested in explaining away.
    """
    for ref in priors:
        prior = json.loads(Path(ref).read_text())
        theirs = prior.get("holdout")
        label = prior.get("label", "?")
        if theirs is None:
            msg = (f"{ref} (label {label!r}) predates holdout provenance, so the "
                   f"dataset it was scored on cannot be established. It is not "
                   f"comparable to this run by inspection.")
        elif theirs.get("sha256") == here["sha256"] and \
                theirs.get("rows_scored") != here["rows_scored"]:
            # Same FILE, different slice. `--n 100` against a 600-row prior shares
            # a sha and still divides by a different denominator, which is the
            # same error this whole mechanism exists to stop -- just one level in.
            msg = (f"ROW-COUNT MISMATCH against {ref} (label {label!r}): this run "
                   f"scored {here['rows_scored']} rows of that file, the prior "
                   f"scored {theirs.get('rows_scored')}. Same dataset, different "
                   f"denominator -- drop --n, or compare like for like.")
        elif theirs.get("sha256") != here["sha256"]:
            msg = (f"HOLDOUT MISMATCH against {ref} (label {label!r}).\n"
                   f"  this run : {here['path']}\n"
                   f"             sha256 {here['sha256'][:16]}... "
                   f"{here['gold_has_params']} gold-params of {here['rows_scored']}\n"
                   f"  {ref:<9}: {theirs.get('path')}\n"
                   f"             sha256 {str(theirs.get('sha256'))[:16]}... "
                   f"{theirs.get('gold_has_params')} gold-params of "
                   f"{theirs.get('rows_scored')}\n"
                   f"  These are different datasets. Their scores are not "
                   f"comparable, and the denominator is the proof.")
        else:
            continue
        if not allow:
            raise SystemExit("refusing to compare: " + msg)
        print("WARNING (--allow-holdout-mismatch): " + msg)


def paired_mcnemar(now: dict, prior: dict, label: str, prior_path: str) -> None:
    """Compare two models ROW BY ROW, not total against total.

    A difference of totals cannot tell "fixed 40, broke 28" from "changed
    nothing twice", and the two call for opposite decisions. This is the test
    the project's own retrain instructions have demanded since the v6 decision
    turned on 40 lost / 28 gained being p = 0.182 -- a net -12 that is not a
    regression however it reads.

    Exact binomial on the discordant pairs, not the chi-square approximation:
    b + c here is routinely under 25, where chi-square is not trustworthy, and
    an exact test needs no special-casing to stay honest at small counts.
    """
    mine = {r["row"]: r for r in now.get("row_verdicts", [])}
    theirs = {r["row"]: r for r in prior.get("row_verdicts", [])}
    shared = sorted(set(mine) & set(theirs))

    print(f"\n=== paired: {label} vs {prior.get('label', prior_path)} ===")
    if not shared:
        # Says WHY rather than printing a silent zero. An older result file
        # written before per-row verdicts existed has none, and "0 shared rows"
        # otherwise reads as two disjoint holdouts, which is a different and
        # much more alarming problem.
        print("  no per-row verdicts in common -- was the prior run scored by a")
        print("  build that predates `row_verdicts`? Re-score it to pair.")
        return

    for field in ("raw_exact", "served_exact"):
        gained = [i for i in shared if mine[i][field] and not theirs[i][field]]
        lost = [i for i in shared if theirs[i][field] and not mine[i][field]]
        b, c = len(gained), len(lost)

        # Two-sided exact binomial on b of (b + c) at p = 0.5.
        n = b + c
        if n == 0:
            p_value = 1.0
        else:
            from math import comb
            tail = sum(comb(n, k) for k in range(0, min(b, c) + 1))
            p_value = min(1.0, 2.0 * tail / (2 ** n))

        verdict = "no significant change" if p_value > 0.05 else (
            "IMPROVED" if b > c else "REGRESSED")
        print(f"  {field:12s} gained {b:4d}   lost {c:4d}   net {b - c:+5d}   "
              f"p = {p_value:.4f}   {verdict}")

        # Which OPERATIONS moved. A net of zero can hide one operation gaining
        # everything another lost, which is the trade the r16-vs-r64 work
        # turned on.
        if gained or lost:
            by_op: dict[str, list[int]] = {}
            for i in gained:
                by_op.setdefault(mine[i]["operation"], [0, 0])[0] += 1
            for i in lost:
                by_op.setdefault(mine[i]["operation"], [0, 0])[1] += 1
            moved = sorted(by_op.items(), key=lambda kv: kv[1][1] - kv[1][0], reverse=True)
            for op, (g, l) in moved[:8]:
                if g or l:
                    print(f"      {op or '(none)':32s} +{g:<4d} -{l}")


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
    ap.add_argument("--raw-strict", action="store_true",
                    help="compare the model's params VERBATIM, including fields the "
                         "chosen operation discards. The default drops them, as "
                         "mortgage_assistant_service.cpp does before grounding -- "
                         "otherwise an operation that answers N/N in production is "
                         "reported 0/N. Use this only to reproduce a pre-2026-09-14 "
                         "number.")
    ap.add_argument("--assert-disjoint-from", action="append", default=[],
                    metavar="TRAIN.JSONL",
                    help="a train.jsonl the holdout MUST NOT overlap. Repeatable -- "
                         "pass one per model being compared, not one per run.")
    ap.add_argument("--allow-contamination", action="store_true",
                    help="report the overlap and score anyway (default: refuse)")
    ap.add_argument("--compare-to", action="append", default=[], metavar="PRIOR.JSON",
                    help="a prior --json-out this run is meant to be compared "
                         "against. REFUSES before the first RPC if that result was "
                         "measured on a different holdout file. Repeatable.")
    ap.add_argument("--allow-holdout-mismatch", action="store_true",
                    help="warn instead of refusing. Only legitimate when you intend "
                         "to compare two DATASETS rather than two models.")
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

    # ---- WHICH DATASET IS THIS? ----
    provenance = holdout_provenance(path, rows)
    print(f"holdout   : {provenance['path']}")
    print(f"            sha256 {provenance['sha256'][:16]}...  "
          f"{provenance['rows_scored']} rows, "
          f"{provenance['gold_has_params']} with gold <params>")
    refuse_on_holdout_mismatch(provenance, args.compare_to,
                               args.allow_holdout_mismatch)

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
    res = evaluate(rows, stub, tail, args.timeout, args.verbose, args.raw_strict)
    dt = time.time() - t0
    print(f"[{len(rows)} rows, {dt:.1f}s, {dt / max(len(rows), 1) * 1000:.0f} ms/row]")
    report(res, args.label, args.show_failures)

    for prior_path in args.compare_to:
        paired_mcnemar(res, json.loads(Path(prior_path).read_text()), args.label, prior_path)

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(
            {"label": args.label, "holdout": provenance, **res}, indent=2))


if __name__ == "__main__":
    main()
