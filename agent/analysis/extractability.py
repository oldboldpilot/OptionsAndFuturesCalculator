#!/usr/bin/env python3
"""Measure whether an ENCODER-only model could serve these corpora.

An encoder cannot generate an unseen value. It can only point at a span of its
input and name a transformation. So the question that decides whether a small
BERT-family model can replace the Qwen3-0.6B decoders is:

    for every (row, field, gold value), does there exist a numeric literal L in
    the utterance and a map M from the verifier's OWN enumerated map set such
    that M(L) == gold?

Decimal throughout: comparing 7.2/100 against 0.072 in binary float is a coin
toss, and a measurement resting on float noise is not a measurement.

THE DENOMINATOR TRAP, paid for once already: money and rate values arrive as
JSON **strings** ('843200.00', '0.0758') because the wire format is BigDecimal
decimal strings -- `FinanceParams.params` is a `map<string,string>`. A first
version of this script tested `isinstance(v, float)` and so measured 3458 of
the ~12k values present, reporting a figure for a denominator it never named.
`as_decimal` is the fix: a value is numeric if it PARSES as a number, not if
JSON happened to type it as one.

Three outcome classes, and the distinction is the whole design input:
  EXTRACTED  -- recoverable from a literal under a map  -> pointer + map head
  CADENCE    -- a small-cardinality convention constant -> small classifier head
  UNEXPLAINED-- neither; needs arithmetic or is unreachable
"""
from __future__ import annotations

import argparse
import collections
import json
import re
import sys
from decimal import ROUND_HALF_UP, Decimal, InvalidOperation, getcontext

getcontext().prec = 60

# ---------------------------------------------------------------- lexer
# THE MAGNITUDE SUFFIX MUST BE WORD-BOUNDED, AND THIS PATTERN WAS NOT.
# It read `\s*(?P<suffix>[kKmM])?`, so the space-then-m of "$500 more a month"
# matched as a MEGA suffix and the literal lexed as 500,000,000. That is the
# whole of the residue this script reported as UNEXPLAINED for
# `monthly_overpayment` (1152/1408 explained) and `extra_monthly_payment`
# (201/388): the values WERE stated, and the lexer had eaten them. Word-bounding
# the suffix takes both to 1408/1408 and 388/388.
#
# Two changes: no `\s*` before the suffix (a space then a letter is a word, not
# a unit), and a negative lookahead so "500k" matches while "500 more",
# "500 months" and "500kg" do not.
_NUM = re.compile(
    r"""(?P<dollar>\$)?\s*
        (?P<num>\d{1,3}(?:,\d{3})+(?:\.\d+)?|\d+(?:\.\d+)?)
        (?P<suffix>[kKmM](?![A-Za-z]))?
        \s*(?P<pct>%)?
    """,
    re.VERBOSE,
)
_UNIT_AFTER = re.compile(
    r"^[\s-]*(years?|yrs?|months?|mos?|days?|weeks?|quarters?)\b", re.IGNORECASE
)

# Words that NAME a cadence without writing its number. "compounded quarterly"
# -> 4 is a lexical cue to a small class, not an extraction; an encoder
# classifier handles it natively where a decoder must generate the digit.
CADENCE_WORDS = {
    "annually": 1, "annual": 1, "a year": 1, "yearly": 1,
    "semiannually": 2, "semi-annually": 2, "semiannual": 2,
    "quarterly": 4, "quarter": 4,
    "monthly": 12, "a month": 12, "month": 12,
    "biweekly": 26, "bi-weekly": 26, "fortnightly": 26,
    "weekly": 52, "week": 52,
    "daily": 365,
}


class Lit:
    __slots__ = ("value", "tag", "start", "end", "text")

    def __init__(self, value, tag, start, end, text):
        self.value, self.tag, self.start, self.end, self.text = value, tag, start, end, text

    def __repr__(self):
        return f"{self.text!r}={self.value}:{self.tag}"


def lex(utterance: str) -> list[Lit]:
    out: list[Lit] = []
    for m in _NUM.finditer(utterance):
        raw = m.group("num").replace(",", "")
        try:
            v = Decimal(raw)
        except InvalidOperation:
            continue
        sfx = (m.group("suffix") or "").lower()
        if sfx == "k":
            v *= 1000
        elif sfx == "m":
            v *= 1000000
        unit = _UNIT_AFTER.match(utterance[m.end():])
        if m.group("pct"):
            tag = "percent"
        elif unit:
            u = unit.group(1).lower()
            tag = ("years" if u.startswith(("year", "yr")) else
                   "months" if u.startswith(("month", "mo")) else
                   "days" if u.startswith("day") else
                   "weeks" if u.startswith("week") else "quarters")
        elif m.group("dollar"):
            tag = "money"
        else:
            tag = "bare"
        out.append(Lit(v, tag, m.start("num"), m.end(), m.group(0).strip()))
    return out


# ---------------------------------------------------------------- maps
D100, D12 = Decimal(100), Decimal(12)
_ANY = lambda t: True

UNARY = [
    ("M1 identity", _ANY, lambda v: v),
    ("M2 percent/100", lambda t: t == "percent", lambda v: v / D100),
    ("M2' /100", _ANY, lambda v: v / D100),
    ("M3 annual%->monthly", lambda t: t == "percent", lambda v: v / D100 / D12),
    ("M3 annual%->semi", lambda t: t == "percent", lambda v: v / D100 / 2),
    ("M3 annual%->quarterly", lambda t: t == "percent", lambda v: v / D100 / 4),
    ("M3 annual%->weekly", lambda t: t == "percent", lambda v: v / D100 / 52),
    ("M3 annual%->biweekly", lambda t: t == "percent", lambda v: v / D100 / 26),
    ("M3 annual%->daily", lambda t: t == "percent", lambda v: v / D100 / 365),
    ("M3' /12", _ANY, lambda v: v / D12),
    ("M5 years->months", lambda t: t in ("years", "bare"), lambda v: v * D12),
    ("M5 years->quarters", lambda t: t == "years", lambda v: v * 4),
    ("M5 years->weeks", lambda t: t == "years", lambda v: v * 52),
    ("M5 years->biweekly", lambda t: t == "years", lambda v: v * 26),
    ("M6 months->years", lambda t: t in ("months", "bare"), lambda v: v / D12),
    ("M7 years->days", lambda t: t == "years", lambda v: v * 365),
    ("M8 negate", _ANY, lambda v: -v),
    ("M10 complement%", lambda t: t == "percent", lambda v: 1 - v / D100),
    ("M10' complement", _ANY, lambda v: 1 - v),
    ("x100", _ANY, lambda v: v * D100),
    ("x1000", _ANY, lambda v: v * 1000),
    ("/1000", _ANY, lambda v: v / 1000),
]


def at_label_precision(got, gold):
    """Compare at the LABEL's own stated precision.

    A corpus label carries finite precision: `rate` is written to 6 decimal
    places, so 7.21% -> 0.0721/12 = 0.00600833... is labelled `0.006008`. An
    exact comparison refuses that and reports an extraction failure, which is
    wrong in the direction that matters -- the value IS recoverable from the
    literal, the corpus just rounded it when writing it down.

    Quantizing to the gold's own exponent is the honest test. The GAP between
    the exact and at-precision counts is itself a finding: it measures how much
    precision the training label throws away relative to what the engine's
    256-bit BigDecimal could carry from the same literal.
    """
    exp = gold.as_tuple().exponent
    if not isinstance(exp, int):
        return False
    try:
        return got.quantize(Decimal(1).scaleb(exp), rounding=ROUND_HALF_UP) == gold
    except (InvalidOperation, ValueError, OverflowError):
        return False


def unary_hit(gold, lits, rounded_names=None):
    """Exact match first; only then a match at the label's own precision, so
    the two are counted separately and never conflated."""
    approx = None
    for lit in lits:
        for name, pred, fn in UNARY:
            if not pred(lit.tag):
                continue
            try:
                got = fn(lit.value)
            except (InvalidOperation, ZeroDivisionError, OverflowError):
                continue
            if got == gold:
                return name
            if approx is None and got != gold and at_label_precision(got, gold):
                approx = name
    if approx is not None:
        if rounded_names is not None:
            rounded_names[approx] += 1
        return f"{approx} @label-precision"
    return None


def binary_hit(gold, lits):
    """M9 and the term-arithmetic shapes: the only maps reading two literals."""
    for a in lits:
        for b in lits:
            if a is b:
                continue
            try:
                if a.value - b.value == gold:
                    return "M9 a-b"
                if a.value + b.value == gold:
                    return "M9' a+b"
                if b.tag == "percent" and a.value * (1 - b.value / D100) == gold:
                    return "M9 a*(1-p)"
                if b.tag == "percent" and a.value * (b.value / D100) == gold:
                    return "M9 a*p"
                if a.value * D12 - b.value * D12 == gold:
                    return "M11 (a-b)*12"
                if a.value * D12 - b.value == gold:
                    return "M11' a*12-b"
            except (InvalidOperation, ZeroDivisionError, OverflowError):
                continue
    return None


def cadence_hit(gold, utterance):
    """A cadence NAMED by a word rather than written as a number."""
    low = utterance.lower()
    for word, n in CADENCE_WORDS.items():
        if word in low and Decimal(n) == gold:
            return f"CADENCE-WORD({word}->{n})"
    return None


# Small-cardinality convention constants the corpus teaches directly
# (kConventionValues / kUngroundedFields in mortgage-nest-egg's mortgage_verification.cppm).
CONVENTION = {Decimal(0), Decimal(15), Decimal("0.1"), Decimal(1)}


# ---------------------------------------------------------------- corpus
_PARAMS = re.compile(r"<params>(.*?)</params>", re.DOTALL)


def as_decimal(v):
    """Numeric if it PARSES as a number -- not if JSON happened to type it so."""
    if isinstance(v, bool):
        return None
    if isinstance(v, (int, float, Decimal)):
        return Decimal(str(v))
    if isinstance(v, str):
        s = v.strip().replace(",", "").lstrip("$")
        if not s or s in ("-", "."):
            return None
        try:
            return Decimal(s)
        except InvalidOperation:
            return None
    return None


def iter_rows(path):
    with open(path) as fh:
        for ln, line in enumerate(fh, 1):
            line = line.strip()
            if line:
                yield ln, json.loads(line)


def row_parts(row):
    convs = row.get("conversations") or row.get("messages") or []
    users = [c["content"] for c in convs if c.get("role") == "user"]
    gold = None
    for c in convs:
        if c.get("role") == "assistant":
            m = _PARAMS.search(c.get("content") or "")
            if m:
                try:
                    gold = json.loads(m.group(1))
                except json.JSONDecodeError:
                    gold = None
    return "\n".join(users), gold


def flatten(gold):
    """Yield (field, Decimal). Arrays expand element-wise: a parallel-array
    field is a SEQUENCE of independent extractions, which is what makes it easy
    for a token-labeller and hard for a digit-by-digit decoder."""
    for k, v in gold.items():
        if isinstance(v, list):
            for el in v:
                d = as_decimal(el)
                if d is not None:
                    yield k, d
        else:
            d = as_decimal(v)
            if d is not None:
                yield k, d


def categorical(gold):
    for k, v in gold.items():
        if isinstance(v, bool):
            yield k, str(v)
        elif isinstance(v, str) and as_decimal(v) is None:
            yield k, v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("corpus")
    ap.add_argument("--op-key", default="operation")
    ap.add_argument("--show-residue", type=int, default=20)
    args = ap.parse_args()

    n_rows = n_gold = rows_full = 0
    f_tot, f_ext, f_cad = collections.Counter(), collections.Counter(), collections.Counter()
    maps = collections.Counter()
    op_rows, op_full = collections.Counter(), collections.Counter()
    cat_vals = collections.defaultdict(set)
    resid_field = collections.Counter()
    resid_card = collections.defaultdict(set)
    residue = []

    for _, row in iter_rows(args.corpus):
        n_rows += 1
        utt, gold = row_parts(row)
        if not gold:
            continue
        n_gold += 1
        lits = lex(utt)
        op = str(gold.get(args.op_key, "?"))
        op_rows[op] += 1
        ok = True
        for field, val in flatten(gold):
            f_tot[field] += 1
            hit = unary_hit(val, lits) or binary_hit(val, lits)
            if hit:
                f_ext[field] += 1
                maps[hit] += 1
                continue
            hit = cadence_hit(val, utt)
            if hit is None and val in CONVENTION:
                hit = "CONVENTION-CONST"
            if hit:
                f_cad[field] += 1
                maps[hit] += 1
                continue
            ok = False
            resid_field[field] += 1
            resid_card[field].add(str(val))
            if len(residue) < 5000:
                residue.append((op, field, str(val), utt[:140]))
        for k, v in categorical(gold):
            cat_vals[k].add(v)
        if ok:
            rows_full += 1
            op_full[op] += 1

    tot, ext, cad = sum(f_tot.values()), sum(f_ext.values()), sum(f_cad.values())
    une = tot - ext - cad
    print(f"corpus                : {args.corpus}")
    print(f"rows                  : {n_rows}   with gold params: {n_gold}")
    print(f"distinct operations   : {len(op_rows)}")
    print()
    print(f"NUMERIC (row,field) values     : {tot}")
    print(f"  EXTRACTED  literal+map       : {ext:>6}  ({100*ext/max(tot,1):6.2f}%)  -> pointer+map head")
    print(f"  CADENCE    small-card const  : {cad:>6}  ({100*cad/max(tot,1):6.2f}%)  -> small classifier head")
    print(f"  UNEXPLAINED                  : {une:>6}  ({100*une/max(tot,1):6.2f}%)")
    print()
    print(f"rows where EVERY numeric value is covered: {rows_full}/{n_gold} "
          f"({100*rows_full/max(n_gold,1):.2f}%)")
    print()
    print("--- how each value was recovered ---")
    for k, c in maps.most_common(30):
        print(f"  {c:>7}  {k}")
    print()
    print("--- categorical gold fields (classifier targets) + cardinality ---")
    for k, vs in sorted(cat_vals.items(), key=lambda kv: -len(kv[1]))[:12]:
        shown = sorted(vs)[:4]
        print(f"  card={len(vs):<5} {k:<26} e.g. {shown}")
    print()
    print("--- UNEXPLAINED fields: count, distinct values seen, the values ---")
    for f, c in resid_field.most_common(20):
        vals = sorted(resid_card[f], key=lambda s: (len(s), s))[:10]
        print(f"  {c:>6}/{f_tot[f]:<6} card={len(resid_card[f]):<5} {f:<26} {vals}")
    print()
    print("--- per-operation full-row coverage ---")
    for op, c in sorted(op_rows.items(), key=lambda kv: -kv[1]):
        print(f"  {op_full[op]:>5}/{c:<5} {100*op_full[op]/c:6.2f}%  {op}")
    if residue and args.show_residue:
        print()
        print(f"--- residue sample ({min(args.show_residue,len(residue))} of {len(residue)}) ---")
        for op, field, val, utt in residue[: args.show_residue]:
            print(f"  [{op}] {field} = {val}\n      {utt}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
