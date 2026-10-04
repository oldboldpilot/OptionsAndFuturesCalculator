#!/usr/bin/env python3
"""Score ParseOperation's SERVED params numerically, over the whole holdout.

eval_grpc_mortgage.py compares with dict EQUALITY against the corpus's own text, which is
right for a decoder that echoes that text and wrong for a model that COMPUTES the value:
gold "740700.00" against a computed "740700" is the same number and a failed string compare.
docs/FINANCE_API.md tells callers to parse money as decimal strings of UNSPECIFIED length and
"do not pin the count" -- so this scores values, reusing the comparator that gated reconstruct.

TWO WIRE SHAPES, AND ONLY ONE OF THEM IS WHAT A CLIENT SENDS.
  --one-call (default) sends the trainer's RENDERED text -- `first [SEP] question [SEP] later`
      -- in the single `utterance` field. That is the TRAINER's input and it is the right
      control for the model: every turn is present, so nothing is missing and the score is
      about extraction alone. It is NOT the contract: no client renders a [SEP] join.
  --two-call sends the three fields the contract carries, on two calls, exactly as
      `eval_grpc_mortgage.py` and a real client do -- turn one with the utterance, turn two
      with `prior_question` echoed and `prior_clarification` carrying the reply.

THE GAP BETWEEN THEM IS THE MEASUREMENT. Under --one-call a clarification row has nothing
left to ask about, which is why this project's own `asked-when-ambiguous 0/86` was recorded
as meaningless; under --two-call the serving layer asks, the user answers, and the question
is whether the answer is USED. On 2026-10-04 it was not: the verifier was handed both turns
by `grounding_text()` and the model was handed one, so the second turn refused on the very
field the first turn had asked for.

THE SCHEMA IS READ OUT OF THE SERVED GGUF, not out of a /tmp fixture. A fixture written from
a training checkpoint is a second copy of the label space with nothing binding it to the
weights the engine loaded, which is the defect behind every four-tables scar in this tree.
`--schema` still overrides it, for scoring one model's answers against another's label space
deliberately.
"""
import glob, json, sys
sys.path.insert(0, '/home/muyiwa/Development/OptionsAndFuturesCalculator/scripts')
sys.path.insert(0, '/home/muyiwa/Development/OptionsAndFuturesCalculator/agent/train')
sys.path[:0] = glob.glob('/home/muyiwa/Development/OptionsAndFuturesCalculator/agent/**/', recursive=True)
import os
import re
import time
import grpc
import mortgage_assistant_pb2 as pb, mortgage_assistant_pb2_grpc as pbg
import importlib
cmp_mod = importlib.import_module('check_encoder_reconstruct_parity')
import encoder_corpus as ec
import eval_grpc_mortgage as egm
from decimal import Decimal, InvalidOperation
from pathlib import Path



def _load_inert_fields():
    """method -> {fields the service drops}, parsed from the verifier's own table.

    Refuses rather than returning an empty dict: an empty table would silently make every
    ComputeDepreciation row compare against fields the service correctly dropped, which is
    the failure this function exists to remove.
    """
    import re
    src = Path('backend/src/modules/mortgage_verification.cppm').read_text()
    out: dict[str, set] = {}
    for m, f in re.findall(
            r'\{"ComputeDepreciation",\s*"method",\s*"([A-Z_]+)",\s*"([a-z_]+)"\}', src):
        out.setdefault(m, set()).add(f)
    if not out:
        raise SystemExit(
            "REFUSED: parsed 0 entries from kVariantInertFields in "
            "mortgage_verification.cppm. The pattern or the table moved; comparing against "
            "an empty inert set would score the service's correct drops as model errors.")
    return out


def as_dec(x):
    try:
        return Decimal(str(x))
    except (InvalidOperation, ValueError):
        return None


def eq(gold, got):
    """Compare one field, at the LABEL's own precision and in the WIRE's types.

    Three bridges, each for a documented reason rather than to make a number larger:
      - the wire is map<string,string>, so a bool arrives as "true"/"false" and an array
        arrives flattened into a bracketed string;
      - `at_label_precision` is the trainer's own rule: the corpus rounds a per-period rate
        to SIX places, and the encoder computes fifteen, so an exact compare would report the
        more precise answer as the failure. The gap is a finding, not an error -- CLAUDE.md
        predicted it before this was built.
    """
    if isinstance(gold, bool):
        return str(gold).lower() == str(got).strip().lower()
    if isinstance(gold, list):
        body = str(got).strip()
        if body.startswith('['):
            body = body[1:-1] if body.endswith(']') else body[1:]
        parts = [p for p in body.split(',') if p.strip() != '']
        if len(parts) != len(gold):
            return False
        return all(eq(g, p) for g, p in zip(gold, parts))
    dg, dt = as_dec(gold), as_dec(got)
    if dg is not None and dt is not None:
        return ec.at_label_precision(dt, dg)
    return str(gold).strip() == str(got).strip()

import argparse  # noqa: E402

_ap = argparse.ArgumentParser(add_help=True)
_ap.add_argument("--two-call", action="store_true",
                 help="send the three ParseRequest fields over two calls, as a client does")
_ap.add_argument("--model", default="backend/models/mortgage-encoder.gguf",
                 help="the GGUF whose schema_json is the label space (read, never guessed)")
_ap.add_argument("--schema", default=None,
                 help="override the schema with a JSON file instead of reading the GGUF")
_ap.add_argument("--val", default="agent/dataset/data_mortgage/val.jsonl")
_ap.add_argument("--n", type=int, default=None)
_args = _ap.parse_args()

if _args.schema:
    _schema_obj = json.load(open(_args.schema))
    _schema_src = _args.schema
else:
    sys.path.insert(0, f"{ROOT_DIR}/scripts" if (ROOT_DIR := os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))) else "scripts")
    from encoder_schema_from_gguf import schema_json as _schema_from_gguf
    _schema_obj = _schema_from_gguf(_args.model)
    _schema_src = f"{_args.model} (sensen-encoder.schema_json)"

sch = ec.Schema.from_json(_schema_obj)
_val = Path(_args.val)
# THE HOLDOUT'S sha256 IS PRINTED, not assumed. This file's own rule, paid for twice: a
# served/asked pair whose measurement environment was unrecorded had to be RETIRED as
# unquotable, and a `served` figure was invalidated by a harness change on a second occasion.
import hashlib  # noqa: E402
print(f"holdout : {_val.resolve()}")
print(f"          sha256 {hashlib.sha256(_val.read_bytes()).hexdigest()}")
print(f"schema  : {_schema_src}")
print(f"wire    : {'two-call (the contract)' if _args.two_call else 'one-call rendered join (the trainer control)'}")
ds = ec.load_dialogues(_val, limit=_args.n)
facts = [ec.make_facts(d, sch.op_key, sch.question_mode) for d in ds]
examples = ec.build_examples(facts, sch)
# Index-aligned by construction: build_examples emits one Example per Facts with no drops,
# so `ds[i]` is the dialogue behind `examples[i]` and its three turns are what --two-call
# sends. Asserted rather than trusted -- a silent misalignment would score every row against
# a neighbour's gold, which looks exactly like a model that lost half its capability.
assert len(ds) == len(examples) == len(facts), (
    f"REFUSED: {len(ds)} dialogues, {len(facts)} facts, {len(examples)} examples. "
    f"build_examples dropped or added rows, so the dialogue a row is scored against is "
    f"not the dialogue it was sent.")

# TARGET. Default localhost, because that is where a pre-deploy baseline is taken.
# ENCODER_RPC_TARGET points it at PRODUCTION instead -- the apex speaks gRPC-Web and
# the JSON transcoder but NOT native gRPC (Railway's edge strips the HTTP/2 trailer
# gRPC carries grpc-status in), so a native-gRPC target must be the TCP proxy, which
# is TLS with a self-signed certificate. ENCODER_RPC_KEY supplies x-api-key, which the
# Pro gate requires on this surface in production and does not require locally.
_target = os.environ.get("ENCODER_RPC_TARGET", "localhost:50051")
_key = os.environ.get("ENCODER_RPC_KEY", "")
if os.environ.get("ENCODER_RPC_TLS"):
    _chan = grpc.secure_channel(_target, grpc.ssl_channel_credentials(
        root_certificates=open(os.environ["ENCODER_RPC_TLS"], "rb").read()),
        options=[("grpc.ssl_target_name_override",
                  os.environ.get("ENCODER_RPC_AUTHORITY", "grpc-native.optionsandfuturescalculator.com"))])
else:
    _chan = grpc.insecure_channel(_target)
_md = [("x-api-key", _key)] if _key else []
st = pbg.MortgageAssistantStub(_chan)
agree = differ = none_rows = refused = 0
shapes = {}
msgs = []
# --two-call bookkeeping. `asked_ok` here is NOT eval_grpc_mortgage's: that one scores the
# FIRST call, and a layer that asks correctly and then refuses forever scores 86/86 on it.
# This counts the EXCHANGE -- asked on turn one AND completed on turn two -- which is the
# only number a user's experience is a function of.
exchange_asked = exchange_completed = exchange_total = 0


def _send(dlg, ex):
    """One row, in whichever wire shape was asked for. Returns the FINAL response."""
    if not _args.two_call:
        return _rpc(utterance=ex.text)
    if dlg.later is None:
        return _rpc(utterance=dlg.first)
    # Turn one. Its outcome is the thing --one-call structurally cannot observe.
    first = _rpc(utterance=dlg.first)
    q = first.clarification.question if first.WhichOneof("outcome") == "clarification" else ""
    global exchange_asked, exchange_completed, exchange_total
    if dlg.kind == "clarify":
        exchange_total += 1
        if q:
            exchange_asked += 1
    # Turn two, as a real client sends it: THIS SERVICE'S OWN question echoed back. An empty
    # echo makes the service substitute a placeholder that appears zero times in the training
    # corpus, and it is also the only thing that tells a REVISION from an ANSWER.
    second = _rpc(utterance=dlg.first, prior_clarification=dlg.later, prior_question=q)
    if dlg.kind == "clarify" and q and second.WhichOneof("outcome") == "params":
        exchange_completed += 1
    return second


def _rpc(**kw):
    """One ParseOperation, waiting out a quota refusal rather than scoring it."""
    for _attempt in range(6):
        try:
            return st.ParseOperation(pb.ParseRequest(**kw), timeout=60, metadata=_md)
        except grpc.RpcError as _e:
            if _e.code() is not grpc.StatusCode.RESOURCE_EXHAUSTED:
                raise
            _m = re.search(r"retry in (\d+)s", _e.details() or "")
            _wait = min(90, int(_m.group(1)) + 2 if _m else 15 * (_attempt + 1))
            print(f"  [quota] {_e.details()} -- waiting {_wait}s", flush=True)
            time.sleep(_wait)
    raise SystemExit("quota refused six consecutive attempts; the budget is exhausted, "
                     "not the model -- re-run later rather than reading a partial score")


for i, ex in enumerate(examples):
    # A QUOTA REFUSAL IS NOT A MEASUREMENT, so it is waited out rather than scored.
    # Against production the partner tier has a compute-unit budget per hour, and a
    # 560-row sweep crosses it: the engine answers RESOURCE_EXHAUSTED with its own
    # "retry in Ns" hint, which is the right behaviour and would otherwise be counted
    # as a row the model got wrong. The hint is PARSED rather than guessed, and the
    # wait is capped so a permanently exhausted budget fails loudly instead of hanging.
    r = _send(ds[i], ex)
    which = r.WhichOneof('outcome')
    if ex.gold is None:
        # The row's gold is prose: a clarification or refusal is the right answer here.
        none_rows += 1
        continue
    if which != 'params':
        refused += 1
        if len(msgs) < 8:
            msgs.append(f"row {i}: {which}: {str(r).replace(chr(10), ' ')[:150]}")
        continue
    got = dict(r.params.params)
    # gold_as_served models the service's OWN documented transformations -- the TVM sign
    # flip on an outgoing payment, the per-method inert-field drops, the array flattening.
    # Comparing against raw gold would score those as model errors; they are the service
    # doing what this repository says it must.
    want = egm.gold_as_served(ex.gold)

    # TWO TRANSFORMATIONS THE SERVICE APPLIES DELIBERATELY, which gold_as_served does not
    # model. Applying them to GOLD rather than excusing them on the served side is the whole
    # point: it keeps every other field strictly compared, and it stops a correct service from
    # being scored as a wrong model.
    #
    # Leaving them unmodelled is how this script came to report 93.57% for a chain that is
    # actually right on every row -- the harness-not-the-model mistake this project has
    # recorded four times, made a fifth time here, in my own scoring script.
    #
    # 1. THE TVM SIGN FLIP. ComputeRate and ComputePeriods solve
    #    PV*(1+r)^n + PMT*annuity + FV = 0, which with FV = 0 has a root only when PV and PMT
    #    OPPOSE. The corpus says "$1,011,000 loan, $7,899.07/month" because that is how a
    #    person says it, and mortgage_verification::tvm_payment_needs_sign_flip signs the
    #    OUTGOING payment so the Finance RPC it names will accept it. The engine's own refusal
    #    is unchanged and deliberate, so the flip is what makes the call answerable.
    if ex.gold['operation'] in ('ComputeRate', 'ComputePeriods'):
        fv = as_dec(ex.gold.get('future_value', 0))
        pay = as_dec(ex.gold.get('payment'))
        pv = as_dec(ex.gold.get('present_value'))
        if (fv is not None and fv == 0 and pay is not None and pv is not None
                and pay != 0 and ((pay > 0) == (pv > 0))):
            want['payment'] = str(-pay)

    # 2. THE PER-METHOD INERT FIELDS. DepreciationRequest is ONE message serving four
    #    methods, and finance.proto restricts six of its eight fields in comments no consumer
    #    can see. The service drops a field the chosen METHOD never reads -- forwarding it is
    #    what made a straight-line request refuse on "factor" = 3 in production. Verified
    #    against kVariantInertFields: for STRAIGHT_LINE the dropped set is exactly
    #    {period, factor, recovery_period, year}.
    # DERIVED from mortgage_verification.cppm's own kVariantInertFields, not hand-copied.
    # The hand-copied version had MACRS as {salvage} when the table says
    # {salvage, life, period, factor} -- so it under-reported by two rows and I went looking
    # for a service defect that did not exist. A table maintained by hand in a second place
    # has no mechanism that could keep it honest; a derived one cannot drift. Same reasoning
    # as check_vendored_protos.sh asserting byte-identity against the source of truth rather
    # than recording a checksum, and as pending_enqueues_in_log() replacing a cached counter.
    _INERT = _load_inert_fields()
    if ex.gold['operation'] == 'ComputeDepreciation':
        for f in _INERT.get(str(ex.gold.get('method', '')), set()):
            want.pop(f, None)
    bad = []
    if r.params.operation != ex.gold['operation']:
        bad.append(f"operation: gold {ex.gold['operation']} served {r.params.operation}")
    for k in set(want) | set(got):
        if k not in got:
            bad.append(f"{k}: missing")
        elif k not in want:
            bad.append(f"{k}: invented")
        elif not eq(want[k], got[k]):
            bad.append(f"{k}: gold {want[k]!r} served {got[k]!r}")
    if bad:
        differ += 1
        shape = ('sign-flip' if all('payment: gold' in b and b.endswith("'-" + b.split("gold '")[1].split("'")[0] + "'") for b in bad)
                 else 'inert-fields-dropped' if all(b.endswith(': missing') for b in bad)
                 else 'other')
        shapes[(ex.gold['operation'], shape)] = shapes.get((ex.gold['operation'], shape), 0) + 1
        if shape == 'other' and len(msgs) < 10:
            msgs.append(f"row {i} ({ex.gold['operation']}): " + "; ".join(bad[:3]))
    else:
        agree += 1

total = agree + differ + refused
print(f"rows with gold params      : {total}")
print(f"  served and NUMERICALLY equal to gold : {agree}")
print(f"  served and differing                 : {differ}")
print(f"  refused / clarified instead          : {refused}")
print(f"rows whose gold is prose (skipped)     : {none_rows}")
if _args.two_call and exchange_total:
    # THE EXCHANGE, not the first call. A clarification row is only served if turn one asks
    # AND turn two completes; counting the ask alone is what let a dead second turn read as
    # a 74/86 win.
    print(f"\nclarification rows, as an EXCHANGE     : {exchange_total}")
    print(f"  turn 1 asked                         : {exchange_asked}")
    print(f"  turn 2 then returned params          : {exchange_completed}")
print(f"\nSERVED-THROUGH-gRPC params exact-match: {agree}/{total} = {100.0*agree/total:.2f}%")
print("(gold adjusted for the TVM sign flip and the per-method inert-field drops -- both\n documented service behaviour, verified against tvm_payment_needs_sign_flip and\n kVariantInertFields, not excused on the served side)")
print("\ndisagreements by (operation, shape):")
for (op, sh), n in sorted(shapes.items(), key=lambda x: -x[1]):
    print(f"  {n:4d}  {op:32s} {sh}")
if msgs:
    print("\nthe 'other' disagreements:")
    for m in msgs:
        print("  " + m)
