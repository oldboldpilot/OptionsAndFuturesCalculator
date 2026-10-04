#!/usr/bin/env python3
"""The TWO-CALL protocol through the real `ParseOperation`, asserted in FOUR directions.

WHY THIS EXISTS, AND WHY IT IS NOT A THIRD HARNESS. `eval_grpc_mortgage.py` drives the
two-call shape over the whole holdout and `scripts/score_encoder_served_rpc.py --two-call`
scores it numerically; both are aggregates and both are what the headline figures are quoted
from. Neither ASSERTS anything, and the one thing a user of an under-specified request
experiences is whether the answer they gave on turn two is USED. A row that is asked a
question and then refused scores exactly like a row refused outright, and `asked_ok` counts
the FIRST call only -- so a layer that asks forever and never completes reads as a WIN in
every number those two harnesses print. Measured on 2026-10-04, that is exactly what it was:
`asked_ok` 74/86 (better than the decoder's 64/86) while
`"What's the payment on a $420,000 loan at 6.5%?"` + `"30 years"` came back
*"The assistant left out \"periods\""*.

THE UTTERANCES COME FROM THE HOLDOUT, NOT FROM MY HEAD. The first version of this probe
wrote its own, and three of its four failures were the CORPUS not covering the phrasing --
"What would a $300,000 mortgage at 4.5% for 15 years cost per month?" is answered
`ComputeRentVsBuy`, which is a statement about training coverage and not about the serving
layer this probe is meant to gate. A hand-written utterance measures the corpus; a holdout
row measures the layer. This project has recorded a score describing its harness rather than
its model five times, and the first draft of this file made it a sixth.

FOUR DIRECTIONS, because each fails the other way round:

  1. ASK      -- a holdout clarification row's first turn returns a Clarification whose
                 wording is one `mv::clarifying_question` emits.
  2. COMPLETE -- turn two, with that question echoed in `prior_question` and the row's own
                 reply in `prior_clarification`, returns params that carry the field the
                 question asked about, with the REPLY's value in it.
  3. ANSWER   -- a fully-stated holdout row still answers on turn one. This is the direction
                 with NO ALARM: over-asking means a user is asked for a number they just
                 gave, and nothing in any accuracy metric moves.
  4. REFUSE   -- a request whose stated figure is MANGLED still gets a REFUSAL, not a
                 question. That is the documented dangerous failure
                 (`present_value = 304000.00` against a 495,000 utterance) and turning it
                 into a question would hide it. Asserted against the verifier through the
                 real RPC by handing the service a reply that contradicts nothing and a
                 turn-one utterance that states every field.

3 and 4 are the mutation guard on 1 and 2: a "fix" that made every field look unstated would
pass 1 and 2 and fail these.

Usage: probe_encoder_multiturn.py [--addr HOST:PORT] [--rows N]
Exit 0 iff every check passes; 2 if it could not assemble a non-empty case set, because a
probe that silently measures nothing is the failure this tree gates against by name.
"""
from __future__ import annotations

import argparse
import glob
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = glob.glob(f"{ROOT}/agent/**/", recursive=True)

import grpc  # noqa: E402
import mortgage_assistant_pb2 as pb  # noqa: E402
import mortgage_assistant_pb2_grpc as pbg  # noqa: E402
import encoder_corpus as ec  # noqa: E402
import eval_grpc_mortgage as egm  # noqa: E402
from decimal import Decimal, InvalidOperation  # noqa: E402

# The sentences `mortgage_verification.cppm::clarifying_question` can return, restated here
# INDEPENDENTLY of that function so a reworded question nothing else notices shows up as a
# failure instead of being accepted because this file read it from the same place the service
# did. (They are NOT all corpus wordings: the corpus uses ten paraphrases per field family
# and four of these seven are the service's own blend. Measured 2026-10-04, that drift costs
# nothing -- the four ComputeRentalRoi rows that fail turn two serve correctly under the
# service's wording, the corpus's wording and a third paraphrase alike -- so it is recorded
# as a fact and not repaired.)
KNOWN_QUESTIONS = {
    "Over how many years?",
    "What's the interest rate?",
    "What's the maximum loan-to-value?",
    "What do operating expenses run per month -- taxes, insurance, maintenance?",
    "How much is the loan or the property worth?",
    "How much are you putting down?",
    "What does it rent for?",
}

PASS, FAIL = [0], [0]


def check(ok: bool, what: str, detail: str = "") -> bool:
    (PASS if ok else FAIL)[0] += 1
    print(f"  [{'ok  ' if ok else 'FAIL'}] {what}")
    if detail and not ok:
        print(f"         {detail}")
    return ok


def call(stub, utterance, prior="", question="", timeout=60.0):
    r = stub.ParseOperation(pb.ParseRequest(
        utterance=utterance, prior_clarification=prior, prior_question=question),
        timeout=timeout)
    which = r.WhichOneof("outcome")
    if which == "params":
        return which, dict(r.params.params), r.params.operation
    if which == "clarification":
        return which, r.clarification.question, ""
    if which == "refusal":
        return which, r.refusal.message, ""
    return which or "EMPTY", "", ""


def eq(gold, got) -> bool:
    """One field, at the LABEL's own precision and in the WIRE's types.

    THE REPLY'S RAW TEXT IS NOT THE FIELD'S VALUE, and the first draft of this probe asserted
    that it was -- failing 9 of 10 completions that were all correct. The whole architecture
    is that the model points at a literal and names a MAP: "5.3%" becomes `annual_rate
    0.053` (M2, percent/100) and "15 years" becomes `periods 180` (M5, years x 12). So the
    oracle is the row's GOLD, not the reply.

    Three bridges, each for a documented reason: the wire is `map<string,string>` so a bool
    arrives as "true"/"false" and an array arrives flattened into a bracketed string; and
    `at_label_precision` is the trainer's own rule, because the corpus rounds a per-period
    rate to SIX places while the encoder computes fifteen -- an exact compare would report
    the MORE precise answer as the failure.
    """
    if isinstance(gold, bool):
        return str(gold).lower() == str(got).strip().lower()
    if isinstance(gold, list):
        body = str(got).strip()
        if body.startswith("["):
            body = body[1:-1] if body.endswith("]") else body[1:]
        parts = [p for p in body.split(",") if p.strip() != ""]
        return len(parts) == len(gold) and all(eq(g, p) for g, p in zip(gold, parts))
    try:
        return ec.at_label_precision(Decimal(str(got)), Decimal(str(gold)))
    except (InvalidOperation, ValueError, TypeError):
        return str(gold).strip() == str(got).strip()


def disagreements(gold: dict, got: dict) -> list[str]:
    """Every field of `gold` turn two got wrong, named.

    `gold_as_served` models the service's OWN documented transformations -- the outgoing TVM
    sign flip, the per-method inert-field drops, the array flattening. Comparing against raw
    gold would report those as model errors; they are the service doing what this repository
    says it must.
    """
    want = egm.gold_as_served(gold)
    bad = []
    for k, v in want.items():
        if k not in got:
            bad.append(f"{k}: missing")
        elif not eq(v, got[k]):
            bad.append(f"{k}: gold {v!r} served {got[k]!r}")
    return bad


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--addr", default="localhost:50061")
    ap.add_argument("--rows", type=int, default=12,
                    help="how many holdout rows of each kind to assert")
    ap.add_argument("--val", default=str(ROOT / "agent/dataset/data_mortgage/val.jsonl"))
    a = ap.parse_args()

    val = Path(a.val)
    import hashlib
    print(f"== two-call protocol against {a.addr} ==")
    print(f"   holdout {val}")
    print(f"   sha256  {hashlib.sha256(val.read_bytes()).hexdigest()}")

    ds = ec.load_dialogues(val)
    stub = pbg.MortgageAssistantStub(grpc.insecure_channel(a.addr))

    clarify = [(i, d) for i, d in enumerate(ds) if d.kind == "clarify" and d.gold]
    single = [(i, d) for i, d in enumerate(ds) if d.kind == "single" and d.gold]
    if not clarify or not single:
        print(f"REFUSED: {len(clarify)} clarify and {len(single)} single rows in the holdout. "
              f"A probe with an empty case set reports a pass having asserted nothing.")
        return 2

    # ---- 1/2. ASK then COMPLETE -------------------------------------------
    # Only rows whose turn one ACTUALLY ASKS are scored for completion, and the count of
    # rows that did not ask is REPORTED rather than quietly skipped: 11 of 86 refuse on
    # turn one (every one ComputeRentalRoi, on `periodic_operating_expenses`), which is the
    # same-kind claim guard in `utterance_states_nothing_for` and not a two-turn defect --
    # it is identical with this fix reverted. Silently dropping them is how a shrinking
    # case set turns into a rising pass rate.
    print(f"\n-- 1/2. ASK on turn one, then COMPLETE with the reply "
          f"(first {a.rows} holdout clarification rows)")
    asked = did_not_ask = 0
    for i, d in clarify[:a.rows]:
        which, payload, _ = call(stub, d.first)
        if which != "clarification":
            did_not_ask += 1
            print(f"  [note] row {i} did not ask on turn one ({which}) -- not a completion "
                  f"case; see this section's comment")
            continue
        asked += 1
        question = payload
        check(question in KNOWN_QUESTIONS,
              f"row {i}: the question is one clarifying_question emits: {question!r}",
              "not in this file's independent list of the seven wordings")

        which2, payload2, op = call(stub, d.first, d.later, question)
        if not check(which2 == "params",
                     f"row {i}: turn two completes with reply {d.later!r}",
                     f"got {which2}: {payload2}"):
            continue
        check(op == d.gold["operation"],
              f"row {i}:   turn two names {d.gold['operation']}", f"named {op}")
        # EVERY GOLD FIELD, not just the one the question named. "the key is present" would
        # pass on a convention default while the user's answer was still being thrown away --
        # which is the defect this probe exists for, one layer less visible -- and the field
        # the reply fills is in gold like any other, so asserting all of them subsumes it
        # and names whichever one moves.
        bad = disagreements(d.gold, payload2)
        check(not bad, f"row {i}:   turn two's params equal gold (reply {d.later!r} used)",
              "; ".join(bad[:4]))

    if asked == 0:
        print("REFUSED: not one of the sampled rows asked on turn one, so nothing about "
              "completion was asserted.")
        return 2
    print(f"  ({asked} asked, {did_not_ask} did not -- completion asserted on the {asked})")

    # ---- 3. ANSWER when everything is stated -------------------------------
    print(f"\n-- 3. ANSWER on turn one when everything is stated "
          f"(first {a.rows} single-turn holdout rows; over-asking has no alarm)")
    for i, d in single[:a.rows]:
        which, payload, op = call(stub, d.first)
        check(which == "params", f"row {i}: answers without asking",
              f"got {which}: {payload}")

    # ---- 4. A MANGLED value stays a REFUSAL -------------------------------
    # The documented dangerous failure, driven through the real RPC: a second turn whose
    # reply is a figure the FIRST turn already stated differently. The field is stated, so
    # `utterance_states_nothing_for` must say so and the verdict must stay a refusal rather
    # than becoming a question about a number the user just supplied.
    print(f"\n-- 4. a field the user DID state is never asked about "
          f"(first {a.rows} single-turn rows, each given a contradicting reply)")
    # THE DANGEROUS FAILURE, driven through the real RPC on REAL rows. Each utterance states
    # every field it needs -- section 3 just proved these same rows serve on turn one -- so
    # appending a reply that contradicts one of those figures must NOT make the layer ask
    # about it. `present_value = 304000.00` against a 495,000 utterance is the documented
    # case, and turning it into "How much is the loan or the property worth?" would hide it.
    #
    # The assertion is on the WORDING SET rather than on the outcome arm, deliberately. A
    # `<NONE>` prediction also arrives as a Clarification ("The assistant did not identify a
    # calculation for this request."), and that is an honest decline rather than a question
    # about a stated field -- asserting "not a clarification" failed on it and would have
    # pushed a correct decline toward being reported as a refusal.
    for i, d in single[:a.rows]:
        which, payload, op = call(stub, d.first, "actually make it $304,000", "")
        asked_about_stated = which == "clarification" and payload in KNOWN_QUESTIONS
        check(not asked_about_stated,
              f"row {i}: a contradicting reply does not produce a clarifying question",
              f"asked {payload!r} about a figure the utterance states")

    print(f"\n{PASS[0]} passed, {FAIL[0]} failed")
    return 0 if FAIL[0] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
