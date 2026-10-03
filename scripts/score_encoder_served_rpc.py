#!/usr/bin/env python3
"""Score ParseOperation's SERVED params numerically, over the whole holdout.

eval_grpc_mortgage.py compares with dict EQUALITY against the corpus's own text, which is
right for a decoder that echoes that text and wrong for a model that COMPUTES the value:
gold "740700.00" against a computed "740700" is the same number and a failed string compare.
docs/FINANCE_API.md tells callers to parse money as decimal strings of UNSPECIFIED length and
"do not pin the count" -- so this scores values, reusing the comparator that gated reconstruct.
"""
import glob, json, sys
sys.path.insert(0, '/home/muyiwa/Development/OptionsAndFuturesCalculator/scripts')
sys.path.insert(0, '/home/muyiwa/Development/OptionsAndFuturesCalculator/agent/train')
sys.path[:0] = glob.glob('/home/muyiwa/Development/OptionsAndFuturesCalculator/agent/**/', recursive=True)
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

sch = ec.Schema.from_json(json.load(open('/tmp/parity_regen/schema.json')))
ds = ec.load_dialogues(Path('agent/dataset/data_mortgage/val.jsonl'))
facts = [ec.make_facts(d, sch.op_key, sch.question_mode) for d in ds]
examples = ec.build_examples(facts, sch)

st = pbg.MortgageAssistantStub(grpc.insecure_channel('localhost:50051'))
agree = differ = none_rows = refused = 0
shapes = {}
msgs = []
for i, ex in enumerate(examples):
    r = st.ParseOperation(pb.ParseRequest(utterance=ex.text), timeout=30)
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
print(f"\nSERVED-THROUGH-gRPC params exact-match: {agree}/{total} = {100.0*agree/total:.2f}%")
print("(gold adjusted for the TVM sign flip and the per-method inert-field drops -- both\n documented service behaviour, verified against tvm_payment_needs_sign_flip and\n kVariantInertFields, not excused on the served side)")
print("\ndisagreements by (operation, shape):")
for (op, sh), n in sorted(shapes.items(), key=lambda x: -x[1]):
    print(f"  {n:4d}  {op:32s} {sh}")
if msgs:
    print("\nthe 'other' disagreements:")
    for m in msgs:
        print("  " + m)
