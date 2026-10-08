#!/usr/bin/env python3
"""Label builder for a tiny from-scratch ENCODER that would replace the two
fine-tuned Qwen3-0.6B decoders.

@author Olumuyiwa Oluwasanmi

THE IDEA, AND WHAT THIS FILE PROVES ABOUT IT. A decoder reproduces a number by
generating its digits one forward pass at a time. An encoder cannot generate
anything: it can only POINT at a numeric literal already in the utterance and
NAME a transformation of it (the "map"), or pick a small-cardinality
convention constant. So the model never emits a number; the serving layer
computes each value from (literal text, map) in BigDecimal. The question this
file answers is whether the gold labels admit that factorisation at all, and
`reconstruct()` is the answer in executable form: it takes ONLY the labels this
file produces (operation, per-literal (slot, map) pairs, convention classes)
and rebuilds the full params object. The fraction of rows it rebuilds exactly
is the ORACLE COVERAGE, which is a hard CEILING on any model's row accuracy,
and `--min-coverage` refuses to continue when it falls below a floor.

THE REFERENCE CODE (lexer, map table, `at_label_precision`, ...) IS COPIED
FROM `agent/analysis/extractability.py`, WHICH IS THE MEASUREMENT THAT
PROPOSED THIS DESIGN (the copy was taken from a file that is byte-identical to
the tracked one, sha256 7f264d71...). It is copied rather than imported so the
analysis script stays a frozen record of what was measured while this file is
free to change. It is copied VERBATIM EXCEPT FOR ONE
DEFECT, found by running it, and ONE ADDITION (last bullet below);
`--compare-reference` reproduces the reference's own accounting beside this one
so the effect of the defect is a measurement:

  * THE LEXER'S SUFFIX WAS NOT WORD-BOUNDED. `\\s*(?P<suffix>[kKmM])?` allows
    whitespace before the suffix and no boundary after it, so "$304,500
    mortgage" lexed as 304,500,000,000 and "327 months" lexed as 327,000,000
    with its `months` unit tag destroyed (the `m` was eaten). 3,427 of 140,986
    mortgage literals were wrong (2,274 "months", 595 "mortgage", 445 "more",
    84 "monthly", 29 "months."); the corpus contains ZERO genuine attached
    suffixes ("250k"), so nothing was lost by fixing it. This is why the
    reference reported ComputeRefinance, ComputeHeloc and ComputeMortgageRecast
    as 1-8% row-extractable: the loan balance and the remaining months were
    unreadable. Fixed lexer alone: rows fully covered 80.96% -> 88.77%.
  * THE ADDITION: the word "percent" / "per cent" lexes as "%". The corpora never
    write it (0 of 27,764 mortgage user segments), so no label moves and
    `--compare-reference` is unaffected; it exists because `train_encoder.py`'s
    format probe showed a model whose lexer calls "6.5 percent" a BARE number has
    never seen a rate spelled that way, and the label builder could not even label
    such a row: `M2 percent/100` is a candidate only for a percent-tagged literal, and
    the fallback `M2' /100` is a pair the vocabulary dropped as too rare.

FIVE FURTHER TRAPS, EACH MEASURED ON THESE CORPORA, EACH WHY THE ATTRIBUTION
BELOW IS NOT THE ONE-LINER IT LOOKS LIKE ("minimum absolute error", exact
before approximate, which is the rule it started from and is kept):

  1. MINIMUM ERROR DOES NOT FIX THE ZERO-GOLD PROBLEM. `at_label_precision`
     quantises to the label's own exponent, so a gold of 0.00 "matches" every
     literal-map result below 0.005, and the minimum-error one is always the
     smallest divisor, `M3 annual%->daily`. 29,715 approximate-only matches
     existed; 26,731 (90%) were on a ZERO gold and 26,729 of those were
     attributed to `daily`. A zero is the ABSENCE of a stated value, not an
     extraction, so RULE Z': a zero gold may be pointed at only by a literal
     that itself says zero ("salvage $0"). 3,717 further EXACT zero
     attributions, most of them `1 - 100%`, fall to the same rule.
  2. AN INTEGER GOLD HAS NO ROUNDING TO UNDO. Its exponent is 0, so the same
     quantising accepted every result in [0.5, 1.5): "compounded annually"
     (gold 1) was "explained" by `1 - 6.06/100 = 0.9394` in ~100% of its rows,
     when 126 of 132 have no literal at all and 6 an accidental one. Approximate
     matching is allowed only for a decimal with >= 4 significant digits.
  3. A DEFAULT CAN BE EXPLAINED BY AN UNRELATED LITERAL. Strategy `quantity=1`
     is the default for 25,101 rows and 974 of them are "explained" by a "1"
     that is an expiry (637 by "1 days", 337 by a bare "1 dte"). Value routing:
     a value that the corpus repeatedly states WITHOUT any literal (>=
     `min_class_count` unexplained rows, pooled by field name) is a CONVENTION
     of that field and is class-labelled. A literal may ALSO be pointed at for a
     convention value, but only through a (tag, map) the field is seen using for
     its non-convention values: `quantity` is only ever stated by a bare number,
     so "1 days" cannot be it, while `expiration_days` is stated by "45 days" so
     "30 days" can be. That second half is what lets "50 days" (never seen) work
     by pointing instead of silently snapping to the nearest class.
  4. ONE LITERAL CAN FILL SEVERAL SLOTS. A stated price is both `loan_amount`
     and `original_home_value` (and, for ComputeAmortizationBatch, element k of
     both arrays); depreciation's `life`/`recovery_period` and `period`/`year`
     are aliases. 21.9% of mortgage rows (4,715 of 21,520) carry such a literal
     and 0% of strategy rows, so the per-literal target is a SET of (slot, map)
     pairs, not a single class. Fields are matched to literals by a global
     greedy matching, highest mechanism-affinity first, unclaimed literals
     before shared ones: field-by-field matching labelled "3 days ... bump it to
     3 lots" with the expiry on the lots and left "3 days" unlabelled.
  5. "EARLIEST LITERAL, ANY MAP" IS WRONG FOR ARRAYS. It read "$500,000 ...
     $300/month extra ... $300,000" as `[500000, 300 x1000]`: the numbers came
     out right and the labels were wrong, found by this file's own self-test.
     Each array slot is held to the mechanisms it is seen using.

WHAT THE MAP TABLE TURNED OUT TO NEED. Of the reference's 22 unary and 6 binary
maps, the 109 mortgage (slot, map) pairs use exactly six unary (M1 identity,
M2 percent/100, M3 annual%->monthly, M5 years->months, M8 negate, M10
complement%) and two binary (M9 a-b, M9 a*(1-p)); the strategy corpus uses M1
alone. The other twenty (sixteen unary, four binary) fitted noise: with the traps above
fixed they never fire.

ARRAYS (10% of mortgage rows) need three mechanisms and each is a measured
shape of THIS corpus, not a generalisation: (a) the anchor array (first array
field) is matched left to right against the literals, one literal per element,
and a trailing run of equal elements with no literal left is a REPEAT of the
last literal (ComputePaybackPeriod serves `[-L1, L2 x20]` from two stated
dollar figures; the 20 is a convention of the operation, carried as a role
`#rep20`); (b) every other array field is aligned to the anchor's literals by
TEXT GROUP (ComputeAmortizationBatch's `extra_payments` is `[0, 200, 300]` from
two clauses, so which offer a clause belongs to is positional); (c) an element
with no literal is the fill, zero. ComputeXirr's `dates[0] = 0` is group 0's
fill: "invest $X today" states no day count.

INPUT RENDERING. The encoder sees `first_user [SEP] question [SEP] later_user`.
Only the two USER segments are lexed: the serving layer's grounding text is
the user's own words (`grounding_text` in mortgage-nest-egg's mortgage_assistant_service.cpp), and a
"30" inside the assistant's question ("30, 15, or something else?") is not a
value the user stated. The question text is passed through as context (it binds
an untyped reply such as "$700" to its slot) even though the decoder contract
sends a placeholder instead; `--question-mode placeholder` reproduces that.
A revision turn ("what if the rate is 7.1% instead?") has an EMPTY middle
segment, because the serving layer has no params block to replay either.

WHAT IS NOT MODELLED, STATED HERE SO IT IS NOT MISTAKEN FOR COVERAGE:
  * The decision to ASK a clarifying question (a request with a missing
    field). The corpus's first turn of a clarification dialogue is a question;
    this label set describes only the FINAL params turn, so asking would have
    to come from the serving layer's existing missing-field logic.
    `train_encoder.py`'s ask probe measures whether that is plausible.
  * Rows with no params at all (declines / out-of-scope) get op = <NONE>, so the
    mortgage operation head has 29 classes, not 28, and the strategy head 48.
  * A constant the corpus never varies cannot be learned to vary, however the
    utterance reads: `pmi_drop_off_ltv` is 0.80 in 985 of 985 ComputeRefinance
    rows including those that say "once I'm at 80% LTV", so a user who says 78%
    gets 0.80 from this scheme exactly as from the decoder trained on the same data.
  * Nothing here says anything about real users: every number this file or its
    siblings report is on a held-out split of the SAME synthetic generator.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import math
import re
import sys
import time
from dataclasses import dataclass, field
from decimal import ROUND_HALF_UP, Decimal, InvalidOperation, getcontext
from pathlib import Path
from typing import Callable, Iterable, Iterator, NamedTuple, Sequence

getcontext().prec = 60

# ===========================================================================
# REFERENCE CODE -- copied from agent/analysis/extractability.py (the
# measurement that proposed the encoder design). The ONLY behavioural change is
# the lexer's suffix; `lex(..., legacy=True)` restores the original so the
# difference can be measured, and `binary_hit` is now defined through the same
# table as `binary_matches` so the two cannot disagree.
# ===========================================================================
_NUM_LEGACY = re.compile(
    r"""(?P<dollar>\$)?\s*
        (?P<num>\d{1,3}(?:,\d{3})+(?:\.\d+)?|\d+(?:\.\d+)?)
        \s*(?P<suffix>[kKmM])?
        \s*(?P<pct>%)?
    """,
    re.VERBOSE,
)
# The fix: the suffix must be ATTACHED to the digits ("250k", "1.2M") and must
# not begin a word. The legacy form let "304,500 mortgage" / "327 months" /
# "$750 more" be read as thousands-of-millions.
# A SECOND, SMALLER CHANGE: the word "percent" / "per cent" counts as "%". Neither
# corpus contains the word (0 of 27,764 mortgage user segments), so no training label
# moves; it is here because the format probe in train_encoder.py found that a model
# whose lexer tags "6.5 percent" as a BARE number has never seen a rate spelled that
# way, and the lexer is the half of the pipeline that can be fixed without a retrain.
# A THIRD CHANGE, and it fixes a SILENT 10x-TO-100x RATE ERROR. Both alternatives above
# require a LEADING DIGIT, so a leading-dot decimal matched only the digits AFTER the dot:
# ".5%" lexed as 5 percent and ".75%" as 75 percent -- a real literal, a real field, every
# bound satisfied, and a rate an order of magnitude wrong. That is the documented dangerous
# failure class ("20% down priced as a 20% interest rate"), reached by typing a rate the way
# a spreadsheet does. Found on 2026-10-02 by the format probe: "'0.5%' -> '.5%' (leading
# dot)" is a TRAINED rewrite and scored 100.00% row error on all three seeds -- a trained
# case failing completely and deterministically is a lexer bug, not a model weakness, and
# it also meant that rewrite taught the model NOTHING while reading like coverage.
# The lookbehind keeps it from starting a new literal inside "3.5.2" or just after a digit.
_NUM = re.compile(
    r"""(?P<dollar>\$)?\s*
        (?P<num>\d{1,3}(?:,\d{3})+(?:\.\d+)?|\d+(?:\.\d+)?|(?<![\d.])\.\d+)
        (?P<suffix>[kKmM](?![A-Za-z]))?
        \s*(?P<pct>%|(?i:percent|per\s?cent|pct)\b)?
    """,
    re.VERBOSE,
)
_UNIT_AFTER = re.compile(
    r"^[\s-]*(years?|yrs?|months?|mos?|days?|weeks?|quarters?)\b", re.IGNORECASE
)
# A SPELLED scale -- "1.2 million", "350 thousand" -- which the DEPLOYED lexer
# (mortgage-nest-egg's `mortgage_verification.cppm`: word == "million" / "millions" / "mm" / "thousand") reads as a
# money literal times the scale, with the SPAN ending after the digits and the word outside it,
# exactly like "years" or "months". The trainer's lexer did not know the word, so "payment on 1.2
# million at 6.25%" lexed as the bare number 1.2 and a label of 1,200,000 had no literal that
# explained it; a visitor writes it that way (measured 2026-10-06, pay-04 in the visitor
# regression). It is the same lexer-side fix as "percent" below: the half of the pipeline that can
# move without a retrain.
_SCALE_AFTER = re.compile(r"^[\s-]*(millions?|mm|thousand)\b", re.IGNORECASE)

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
    """A numeric literal. `start`/`end` are character offsets into the string
    that was lexed; `end` EXCLUDES trailing whitespace (the reference's
    `m.end()` included it, which is harmless for the unit lookahead and wrong
    for mapping a span to tokens)."""

    __slots__ = ("value", "tag", "start", "end", "text")

    def __init__(self, value, tag, start, end, text):
        self.value, self.tag, self.start, self.end, self.text = value, tag, start, end, text

    def __repr__(self):
        return f"{self.text!r}={self.value}:{self.tag}"


def lex(utterance: str, legacy: bool = False) -> list[Lit]:
    out: list[Lit] = []
    pattern = _NUM_LEGACY if legacy else _NUM
    for m in pattern.finditer(utterance):
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
        scaled = False
        if not legacy and not sfx and not m.group("pct"):
            sc = _SCALE_AFTER.match(utterance[m.end():])
            if sc:
                v *= 1000 if sc.group(1).lower() == "thousand" else 1000000
                scaled = True
        unit = _UNIT_AFTER.match(utterance[m.end():])
        if m.group("pct"):
            tag = "percent"
        elif scaled:
            tag = "money"
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
        out.append(Lit(v, tag, m.start("num"), m.start() + len(m.group(0).rstrip()),
                       m.group(0).strip()))
    return out


# ---------------------------------------------------------------- maps
D100, D12 = Decimal(100), Decimal(12)
_ANY = lambda t: True  # noqa: E731

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
UNARY_FN = {name: fn for name, _, fn in UNARY}
UNARY_PRIO = {name: i for i, (name, _, _) in enumerate(UNARY)}


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

    TRAP, measured: this is only meaningful for a NONZERO gold. At gold 0.00 it
    accepts every result below 0.005, which is how 26,729 `annual%->daily`
    "extractions" of a zero default arose. Callers apply RULE Z first.
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


# The two-literal maps, in the reference's order. `a` and `b` are `Lit`s; `v`
# the gold. Roles: for every map `a` is operand A and `b` is operand B (the
# sum is symmetric, so for it A is simply the earlier literal).
BINARY = [
    ("M9 a-b", lambda a, b, v: a.value - b.value == v),
    ("M9' a+b", lambda a, b, v: a.value + b.value == v),
    ("M9 a*(1-p)", lambda a, b, v: b.tag == "percent" and a.value * (1 - b.value / D100) == v),
    ("M9 a*p", lambda a, b, v: b.tag == "percent" and a.value * (b.value / D100) == v),
    ("M11 (a-b)*12", lambda a, b, v: a.value * D12 - b.value * D12 == v),
    ("M11' a*12-b", lambda a, b, v: a.value * D12 - b.value == v),
]
BINARY_PRIO = {name: 100 + i for i, (name, _) in enumerate(BINARY)}


def binary_matches(gold, lits) -> Iterator[tuple[int, int, str]]:
    """M9 and the term-arithmetic shapes: the only maps reading two literals.
    Yields (index of A, index of B, map name) in the reference's loop order."""
    for ia, a in enumerate(lits):
        for ib, b in enumerate(lits):
            if ia == ib:
                continue
            for name, test in BINARY:
                try:
                    if test(a, b, gold):
                        yield ia, ib, name
                except (InvalidOperation, ZeroDivisionError, OverflowError):
                    continue


def binary_hit(gold, lits):
    for _, _, name in binary_matches(gold, lits):
        # The reference reported one name per PAIR and stopped at the first
        # pair that matched; the generator's loop order is the same.
        return name
    return None


def cadence_hit(gold, utterance):
    """A cadence NAMED by a word rather than written as a number."""
    low = utterance.lower()
    for word, n in CADENCE_WORDS.items():
        if word in low and Decimal(n) == gold:
            return f"CADENCE-WORD({word}->{n})"
    return None


# Small-cardinality convention constants the reference hard-coded
# (kConventionValues / kUngroundedFields in mortgage-nest-egg's mortgage_verification.cppm). This
# file DERIVES the conventions from the corpus instead; the set is kept only so
# `--compare-reference` can reproduce the reference's own partition.
CONVENTION = {Decimal(0), Decimal(15), Decimal("0.1"), Decimal(1)}

_PARAMS = re.compile(r"<params>(.*?)</params>", re.DOTALL)


def as_decimal(v):
    """Numeric if it PARSES as a number -- not if JSON happened to type it so.

    THE DENOMINATOR TRAP, paid for once already: money and rate values arrive as
    JSON **strings** ('843200.00', '0.0758') because the wire format is
    BigDecimal decimal strings. Testing `isinstance(v, float)` measured 3458 of
    the ~12k values present."""
    if isinstance(v, bool):
        return None
    if isinstance(v, (int, float, Decimal)):
        return Decimal(str(v))
    if isinstance(v, str):
        s = v.strip().replace(",", "").lstrip("$")
        if not s or s in ("-", "."):
            return None
        try:
            d = Decimal(s)
        except InvalidOperation:
            return None
        return d if d.is_finite() else None
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


# ===========================================================================
# DIALOGUES AND RENDERING
# ===========================================================================
NONE_OP = "<NONE>"
ARRAY_SUFFIX = "[]"
SEP = " [SEP] "
PLACEHOLDER_QUESTION = "Could you clarify?"   # what the decoder contract sends today
TAGS = ["percent", "years", "months", "days", "weeks", "quarters", "money", "bare"]
N_MAG = 16
IGNORE = -100
COMPAT_MIN_SHARE = 0.02   # a (tag, map) must explain >= 2% of a field's stated rows


@dataclass(frozen=True)
class Dialogue:
    """One corpus row reduced to what the SERVING contract carries.

    kind   single  one user turn, one assistant turn
           clarify user -> question -> reply -> params
           revise  user -> params   -> revision -> params
    The target is always the FINAL assistant turn; `gold is None` means that
    turn carried no `<params>` (a decline / out-of-scope answer)."""
    kind: str
    first: str
    question: str | None
    later: str | None
    gold: dict | None


def parse_params(text: str) -> dict | None:
    m = _PARAMS.search(text or "")
    if not m:
        return None
    try:
        obj = json.loads(m.group(1))
    except json.JSONDecodeError:
        return None
    return obj if isinstance(obj, dict) else None


def to_dialogue(row: dict) -> Dialogue:
    turns = [c for c in (row.get("conversations") or row.get("messages") or [])
             if c.get("role") in ("user", "assistant")]
    users = [c["content"] for c in turns if c["role"] == "user"]
    assistants = [c["content"] for c in turns if c["role"] == "assistant"]
    if not users or len(users) != len(assistants) or len(users) > 2:
        raise ValueError(f"unsupported turn structure: {[c['role'] for c in turns]}")
    gold = parse_params(assistants[-1])
    if len(users) == 1:
        return Dialogue("single", users[0], None, None, gold)
    if parse_params(assistants[0]) is not None:
        return Dialogue("revise", users[0], None, users[1], gold)
    return Dialogue("clarify", users[0], assistants[0], users[1], gold)


def load_dialogues(path: Path, limit: int | None = None) -> list[Dialogue]:
    out: list[Dialogue] = []
    for _, row in iter_rows(path):
        out.append(to_dialogue(row))
        if limit is not None and len(out) >= limit:
            break
    return out


@dataclass
class Rendered:
    text: str
    q_start: int          # first character of the question segment
    u2_start: int         # first character of the later-user segment
    lits: list[Lit]       # literals, offsets into `text`
    lit_seg: list[int]    # 0 = first user turn, 2 = later user turn


def render(d: Dialogue, question_mode: str = "real") -> Rendered:
    """`first [SEP] question [SEP] later`; only user text is lexed."""
    lits = lex(d.first)
    seg = [0] * len(lits)
    text, q_start, u2_start = d.first, len(d.first), len(d.first)
    if d.later is not None:
        q = d.question if (d.kind == "clarify") else ""
        if d.kind == "clarify" and question_mode == "placeholder":
            q = PLACEHOLDER_QUESTION
        text += SEP
        q_start = len(text)
        text += (q or "") + SEP
        u2_start = len(text)
        text += d.later
        for lit in lex(d.later):
            lit.start += u2_start
            lit.end += u2_start
            lits.append(lit)
            seg.append(2)
    return Rendered(text, q_start, u2_start, lits, seg)


# ===========================================================================
# CANONICAL VALUES
# ===========================================================================
def canon_num(d: Decimal) -> str:
    """0.80 and 0.8 are one value; 360 stays 360 (not 3.6E+2)."""
    return format(d.normalize(), "f")


def canon_cat(v) -> str:
    return json.dumps(v)


def field_kind(v) -> str:
    if isinstance(v, list):
        return "arr"
    if as_decimal(v) is not None:
        return "num"
    return "cat"


def parse_role(role: str) -> tuple[str, str, int]:
    """'M1 identity' -> (name, 'unary', 1); 'M1 identity#rep20' -> (.., 'rep', 20);
    'M9 a-b#A' -> ('M9 a-b', 'A', 1)."""
    if "#" not in role:
        return role, "unary", 1
    name, tail = role.split("#", 1)
    if tail in ("A", "B"):
        return name, tail, 1
    if tail.startswith("rep"):
        return name, "rep", int(tail[3:])
    raise ValueError(role)


def mag_bucket(v: Decimal) -> int:
    """log10 magnitude as a small integer: lets the literal head see "this is a
    six-figure amount" without parsing digits out of WordPiece fragments."""
    if v == 0:
        return 0
    try:
        e = int(math.floor(math.log10(abs(float(v)))))
    except (ValueError, OverflowError):
        return N_MAG - 1
    return max(1, min(N_MAG - 1, e + 5))


# ===========================================================================
# CANDIDATES
# ===========================================================================
class Cand(NamedTuple):
    lits: tuple[int, ...]      # literal indices (1 unary, 2 binary)
    roles: tuple[str, ...]     # role string per literal
    exact: bool
    err: Decimal
    prio: int


def sig_digits(v: Decimal) -> int:
    return len(v.normalize().as_tuple().digits)


def approx_eligible(v: Decimal) -> bool:
    """Can a gold value have been ROUNDED from a longer exact one?

    Only a decimal with enough significant digits for the rounding window to be
    narrow. TRAP, measured: an integer gold has exponent 0, so `at_label_precision`
    accepted every result in [0.5, 1.5) -- "compounded annually" (gold 1) was
    "explained" by 1 - 6.06/100 = 0.9394 -- and `compound_frequency=1` looked
    extractable in 100% of its rows when 126 of 132 have no literal at all."""
    return v.as_tuple().exponent < 0 and sig_digits(v) >= 4


def unary_cands(v: Decimal, lits: Sequence[Lit]) -> tuple[list[Cand], list[Cand]]:
    """(exact, approx) literal+map candidates for gold `v`; every map that fits
    is returned, so a caller can prefer the field's own mechanism.

    RULE Z': a zero gold is attributed ONLY to a literal that itself says zero
    ("salvage $0", "0% down"). Zero is otherwise the absence of a stated value:
    `at_label_precision` made 26,729 spurious `annual%->daily` matches of a 0.00
    default, and `1 - 100/100` made "100%" an exact explanation of every zero
    field of ComputeRentalCashFlow."""
    exact: list[Cand] = []
    approx: list[Cand] = []
    can_round = v != 0 and approx_eligible(v)
    for i, lit in enumerate(lits):
        if v == 0 and lit.value != 0:
            continue
        for p, (name, pred, fn) in enumerate(UNARY):
            if not pred(lit.tag):
                continue
            try:
                got = fn(lit.value)
            except (InvalidOperation, ZeroDivisionError, OverflowError):
                continue
            if got == v:
                exact.append(Cand((i,), (name,), True, Decimal(0), p))
            elif can_round and at_label_precision(got, v):
                approx.append(Cand((i,), (name,), False, abs(got - v), p))
    return exact, approx


def binary_cands(v: Decimal, lits: Sequence[Lit]) -> list[Cand]:
    if v == 0:
        return []
    return [Cand((a, b), (f"{n}#A", f"{n}#B"), True, Decimal(0), BINARY_PRIO[n])
            for a, b, n in binary_matches(v, lits)]


# ===========================================================================
# PER-ROW FACTS (the expensive part, computed once and reused by every pass)
# ===========================================================================
class FieldFact(NamedTuple):
    name: str
    kind: str                         # num | cat | arr
    raw: object
    value: object                     # Decimal | tuple[Decimal, ...] | None (cat)
    exact: list[Cand]
    approx: list[Cand]


@dataclass
class Facts:
    dialogue: Dialogue
    rendered: Rendered
    op: str | None
    fields: list[FieldFact]


def make_facts(d: Dialogue, op_key: str, question_mode: str) -> Facts:
    r = render(d, question_mode)
    fields: list[FieldFact] = []
    op = None
    if d.gold is not None:
        op = str(d.gold.get(op_key))
        for k, v in d.gold.items():
            if k == op_key:
                continue
            kind = field_kind(v)
            if kind == "num":
                dv = as_decimal(v)
                ex, ap = unary_cands(dv, r.lits)
                fields.append(FieldFact(k, kind, v, dv, ex, ap))
            elif kind == "arr":
                els = tuple(as_decimal(e) for e in v)
                if any(e is None for e in els):
                    raise ValueError(f"non-numeric array element in {k}: {v}")
                # The SAME name is a scalar in one operation and an array in another
                # (`term_months` is 360 in ComputeAmortization and [180, 360, 360] in
                # ComputeAmortizationBatch), so an array's schema key carries "[]".
                fields.append(FieldFact(k + ARRAY_SUFFIX, kind, v, els, [], []))
            else:
                fields.append(FieldFact(k, kind, v, None, [], []))
    return Facts(d, r, op, fields)


# ===========================================================================
# SCHEMA
# ===========================================================================
@dataclass
class Schema:
    """Everything the model and the serving layer must share. JSON-serialisable
    and stored INSIDE the checkpoint: a label space that travels separately from
    its weights is the four-tables drift this repository already documents."""
    op_key: str
    question_mode: str
    ops: list[str]                                  # index 0 is NONE_OP
    op_fields: dict[str, list[str]]                 # ordered, op_key excluded
    field_kind: dict[str, str]
    pairs: list[tuple[str, str]]                    # (slot, role)
    op_pairs: dict[str, list[int]]                  # op -> pair ids seen with it
    conv_fields: list[str]                          # fields with a class head
    conv_vocab: dict[str, list[str]]                # field -> class texts
    conv_op_mask: dict[str, dict[str, list[int]]]   # field -> op -> allowed classes
    const_default: dict[str, str]                   # field -> its only value
    op_default: dict[str, dict[str, str]]           # op -> field -> modal class text
    compat: dict[str, dict[str, float]]             # field -> {"tag|map": share of its stated rows}
    anchor: dict[str, str]                          # op -> anchor array field
    params: dict[str, float] = field(default_factory=dict)

    # ---- (de)serialisation
    def to_json(self) -> dict:
        return {
            "op_key": self.op_key, "question_mode": self.question_mode, "ops": self.ops,
            "op_fields": self.op_fields, "field_kind": self.field_kind,
            "pairs": [list(p) for p in self.pairs], "op_pairs": self.op_pairs,
            "conv_fields": self.conv_fields, "conv_vocab": self.conv_vocab,
            "conv_op_mask": self.conv_op_mask, "const_default": self.const_default,
            "op_default": self.op_default,
            "compat": self.compat, "anchor": self.anchor, "params": self.params,
        }

    @classmethod
    def from_json(cls, d: dict) -> "Schema":
        d = dict(d)
        d["pairs"] = [tuple(p) for p in d["pairs"]]
        sch = cls(**d)
        sch.reindex()
        return sch

    def reindex(self) -> None:
        """Derived lookups. Call after mutating `pairs`, `ops` or the class
        vocabularies; they are plain attributes, deliberately NOT serialised."""
        self._pid = {p: i for i, p in enumerate(self.pairs)}
        self._oidx = {o: i for i, o in enumerate(self.ops)}
        self._cset = {n: frozenset(v) for n, v in self.conv_vocab.items()}
        for n, v in self.const_default.items():
            self._cset.setdefault(n, frozenset([v]))

    def pair_id(self) -> dict[tuple[str, str], int]:
        return self._pid

    def op_index(self) -> dict[str, int]:
        return self._oidx

    def conv_values(self, name: str) -> frozenset:
        """Numeric convention values of a field (class texts)."""
        return self._cset.get(name, frozenset())

    @property
    def n_ops(self) -> int:
        return len(self.ops)

    @property
    def n_pairs(self) -> int:
        return len(self.pairs)

    @property
    def n_conv(self) -> int:
        return len(self.conv_fields)


@dataclass
class Example:
    """One labelled row, in CHARACTER coordinates (tokenisation is a separate
    step so the labels never depend on a tokenizer)."""
    text: str
    q_start: int
    u2_start: int
    kind: str
    lits: list[dict]                   # start, end, text, tag, value, mag, seg
    op: int
    pairs: list[list[int]]             # per literal: pair ids
    conv: dict[str, int]               # conv field -> class idx (absent == IGNORE)
    gold: dict | None


# ---------------------------------------------------------------- statistics
@dataclass
class BuildStats:
    n_rows: int = 0
    n_params_rows: int = 0
    route: collections.Counter = field(default_factory=collections.Counter)
    unexplained: collections.Counter = field(default_factory=collections.Counter)
    multi_slot_rows: int = 0
    dropped_rows: int = 0
    tie_rows: int = 0
    dropped_pairs: collections.Counter = field(default_factory=collections.Counter)
    keyset_variants: dict = field(default_factory=dict)


def _gather_value_counts(all_facts: Sequence[Facts]):
    """Pass 1 of the schema: for every numeric field NAME (pooled over
    operations), how many rows state each value with no literal explanation."""
    n_rows = collections.defaultdict(collections.Counter)
    n_unexpl = collections.defaultdict(collections.Counter)
    cat_vals = collections.defaultdict(collections.Counter)
    for f in all_facts:
        for fd in f.fields:
            if fd.kind == "num":
                key = canon_num(fd.value)
                n_rows[fd.name][key] += 1
                if not fd.exact and not fd.approx:
                    n_unexpl[fd.name][key] += 1
            elif fd.kind == "cat":
                cat_vals[fd.name][canon_cat(fd.raw)] += 1
    return n_rows, n_unexpl, cat_vals


def build_schema(all_facts: Sequence[Facts], op_key: str, question_mode: str,
                 min_class_count: int, min_pair_count: int,
                 stats: BuildStats | None = None) -> Schema:
    """Two data passes; nothing is hard-coded about either corpus."""
    stats = stats or BuildStats()
    op_rows = collections.Counter(f.op for f in all_facts if f.op is not None)
    ops = [NONE_OP] + sorted(op_rows)

    # ---- per-op field lists, in the generator's own key order
    op_fields: dict[str, list[str]] = {}
    keysets: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    kind: dict[str, str] = {}
    for f in all_facts:
        if f.op is None:
            continue
        names = tuple(fd.name for fd in f.fields)
        keysets[f.op][names] += 1
        op_fields.setdefault(f.op, list(names))
        for fd in f.fields:
            if kind.setdefault(fd.name, fd.kind) != fd.kind:
                raise ValueError(f"field {fd.name!r} is both {kind[fd.name]} and {fd.kind}")
    for op, c in keysets.items():
        if len(c) > 1:
            # THE UNION, most common key set first. It used to be the most common key set alone,
            # which was right while every gold carried every field and wrong the moment labels
            # became stated-only: a SPARSE key set can be the commonest, and a field that is not in
            # `op_fields` cannot be emitted at all -- not even when the visitor states it. Order is
            # the commonest set's own, so an array's anchor (the first array field) is unchanged.
            ordered = list(c.most_common(1)[0][0])
            for names in sorted(c, key=lambda ns: -c[ns]):
                for n in names:
                    if n not in ordered:
                        ordered.append(n)
            op_fields[op] = ordered
        stats.keyset_variants[op] = len(c)

    # ---- value routing: which values of each field are CONVENTIONS
    n_rows, n_unexpl, cat_vals = _gather_value_counts(all_facts)
    conv_vals: dict[str, list[str]] = {}
    for name, cnt in n_unexpl.items():
        keep = sorted((v for v, c in cnt.items() if c >= min_class_count), key=Decimal)
        if keep:
            conv_vals[name] = keep
    for name, cnt in cat_vals.items():
        conv_vals[name] = [v for v, _ in sorted(cnt.items(), key=lambda kv: (-kv[1], kv[0]))]

    const_default = {n: v[0] for n, v in conv_vals.items() if len(v) == 1}
    conv_fields = sorted(n for n, v in conv_vals.items() if len(v) >= 2)
    conv_vocab = {n: conv_vals[n] for n in conv_fields}

    # ---- compatibility: which (tag, map) a field uses for its NON-convention values.
    # Per literal only the simplest map is counted: "M2 percent/100" and "M2' /100"
    # are the same number on a percent literal, and counting both made every
    # percent field look ambiguous and therefore learned nothing.
    compat_cnt: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)

    def _count(name: str, pool: list[Cand], lits: Sequence[Lit]) -> None:
        best: dict[int, Cand] = {}
        for c in pool:
            if c.lits[0] not in best or c.prio < best[c.lits[0]].prio:
                best[c.lits[0]] = c
        mechs = {(lits[i].tag, c.roles[0]) for i, c in best.items()}
        if len(mechs) == 1:
            compat_cnt[name][next(iter(mechs))] += 1

    for f in all_facts:
        lits = f.rendered.lits
        for fd in f.fields:
            if fd.kind == "arr":
                # An array element is held to the same standard: "$300" x1000 is
                # not an explanation of a 300,000 loan when "$300,000" is stated.
                for e in fd.value:
                    if e != 0:
                        _count(fd.name, unary_cands(e, lits)[0], lits)
            elif fd.kind == "num" and canon_num(fd.value) not in conv_vals.get(fd.name, ()):
                _count(fd.name, fd.exact or fd.approx, lits)
    compat: dict[str, dict[str, float]] = {}
    for name, cnt in compat_cnt.items():
        total = sum(cnt.values())
        compat[name] = {f"{tag}|{role}": c / total for (tag, role), c in sorted(cnt.items())
                        if c >= 5 and c / total >= COMPAT_MIN_SHARE}

    sch = Schema(op_key=op_key, question_mode=question_mode, ops=ops, op_fields=op_fields,
                 field_kind=kind, pairs=[], op_pairs={}, conv_fields=conv_fields,
                 conv_vocab=conv_vocab, conv_op_mask={}, const_default=const_default,
                 op_default={}, compat=compat, anchor={},
                 params={"min_class_count": min_class_count, "min_pair_count": min_pair_count})
    for op, names in op_fields.items():
        arrs = [n for n in names if kind[n] == "arr"]
        if arrs:
            sch.anchor[op] = arrs[0]
    sch.reindex()

    # ---- pass 2: label everything with provisional (name-level) pairs to learn the
    # pair vocabulary, the per-op masks and each op's modal class
    pair_count: collections.Counter = collections.Counter()
    op_pair_seen: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    conv_seen: dict[str, dict[str, collections.Counter]] = collections.defaultdict(
        lambda: collections.defaultdict(collections.Counter))
    for f in all_facts:
        lab = label_facts(f, sch)
        if f.op is None:
            continue
        for pr in lab.pair_names:
            for p in pr:
                pair_count[p] += 1
                op_pair_seen[f.op][p] += 1
        for name, cls in lab.conv_text.items():
            conv_seen[name][f.op][cls] += 1
    kept = sorted(p for p, c in pair_count.items() if c >= min_pair_count)
    for p, c in pair_count.items():
        if c < min_pair_count:
            stats.dropped_pairs[p] = c
    sch.pairs = kept
    sch.reindex()
    pid = sch.pair_id()
    sch.op_pairs = {op: sorted(pid[p] for p in cnt if p in pid) for op, cnt in op_pair_seen.items()}
    for name in conv_fields:
        idx = {v: i for i, v in enumerate(conv_vocab[name])}
        sch.conv_op_mask[name] = {op: sorted(idx[c] for c in cnt)
                                  for op, cnt in conv_seen[name].items()}
    for name, per_op in conv_seen.items():
        for op, cnt in per_op.items():
            sch.op_default.setdefault(op, {})[name] = cnt.most_common(1)[0][0]
    sch.reindex()
    return sch


# ===========================================================================
# LABELLING
# ===========================================================================
@dataclass
class RawLabels:
    pair_names: list[set]                  # per literal: {(slot, role)}
    conv_text: dict[str, str]              # field -> class text
    route: dict[str, str]                  # field -> how it was explained
    tied: bool = False


def _alloc(n: int) -> list[set]:
    return [set() for _ in range(n)]


def _share(sch: Schema, name: str, lit: Lit, role: str) -> float:
    return sch.compat.get(name, {}).get(f"{lit.tag}|{role}", 0.0)


def _affinity(sch: Schema, name: str, lits: Sequence[Lit], c: Cand) -> float:
    """How much of the field's own stated rows use this literal's (tag, map)."""
    return min(_share(sch, name, lits[i], r) for i, r in zip(c.lits, c.roles))


def _tier(fd: FieldFact, lits: Sequence[Lit]) -> list[Cand]:
    """The candidates worth choosing among: exact unary, else exact binary, else
    the minimum-error approximate ones (the reference's rule, and the user's)."""
    if fd.exact:
        return fd.exact
    b = binary_cands(fd.value, lits)
    if b:
        return b
    if fd.approx:
        m = min(c.err for c in fd.approx)
        return [c for c in fd.approx if c.err == m]
    return []


def label_facts(f: Facts, sch: Schema) -> RawLabels:
    """Name-level labels for one row.

    Scalars are assigned by a GLOBAL greedy matching, highest mechanism-affinity
    first, rather than field by field. Field by field gave the wrong answer on
    "3 days ... bump it to 3 lots": `expiration_days` came first, saw two
    equally exact literals, took the later one, and left "3 days" unlabelled
    while `quantity` then shared "3 lots". Ordered by affinity, `quantity`
    (only ever a bare number) claims "3 lots" and `expiration_days` is left
    with "3 days".

    Pass 1 hands each field the best literal NOBODY has claimed; pass 2 lets
    the fields that found none SHARE (a stated price is both `loan_amount` and
    `original_home_value`). A convention-valued field points only at a literal
    that is still free after both: its class already answers it, and pointing
    at a literal another field needs is the noise this ordering removes."""
    lits = f.rendered.lits
    segs = f.rendered.lit_seg
    pair_names = _alloc(len(lits))
    conv_text: dict[str, str] = {}
    route: dict[str, str] = {}
    if f.op is None:
        return RawLabels(pair_names, conv_text, route)
    claimed: set[int] = set()
    tied = False
    arr_facts = [fd for fd in f.fields if fd.kind == "arr"]
    if arr_facts:
        _label_arrays(f, arr_facts, sch, pair_names, route)
        claimed.update(i for i, pn in enumerate(pair_names) if pn)

    plain: list[tuple[int, FieldFact, list[Cand]]] = []
    convs: list[tuple[int, FieldFact, list[Cand]]] = []
    for order, fd in enumerate(f.fields):
        if fd.kind == "cat":
            conv_text[fd.name] = canon_cat(fd.raw)
            route[fd.name] = "class(cat)"
            continue
        if fd.kind != "num":
            continue
        key = canon_num(fd.value)
        if len({c.lits for c in fd.exact}) > 1:
            tied = True
        if key in sch.conv_values(fd.name):
            conv_text[fd.name] = key
            pool = [c for c in fd.exact + fd.approx
                    if _share(sch, fd.name, lits[c.lits[0]], c.roles[0]) >= COMPAT_MIN_SHARE]
            convs.append((order, fd, [c for c in pool if c.exact] or pool))
        else:
            cands = _tier(fd, lits)
            if cands:
                plain.append((order, fd, cands))
            else:
                route[fd.name] = "UNEXPLAINED"

    def key_of(order: int, fd: FieldFact, c: Cand):
        return (-_affinity(sch, fd.name, lits, c), c.prio, -max(segs[i] for i in c.lits),
                order, min(c.lits))

    entries = sorted(((key_of(o, fd, c), fd, c) for o, fd, cands in plain for c in cands),
                     key=lambda e: e[0])
    chosen: dict[str, Cand] = {}
    for _, fd, c in entries:                       # pass 1: unclaimed literals only
        if fd.name not in chosen and not any(i in claimed for i in c.lits):
            chosen[fd.name] = c
            claimed.update(c.lits)
    for _, fd, c in entries:                       # pass 2: the rest may share
        if fd.name not in chosen:
            chosen[fd.name] = c
            claimed.update(c.lits)
    for o, fd, cands in plain:
        c = chosen[fd.name]
        for li, role in zip(c.lits, c.roles):
            pair_names[li].add((fd.name, role))
        route[fd.name] = "point:" + ("exact" if c.exact else "approx") + \
            ("-binary" if len(c.lits) > 1 else "")
    conv_entries = sorted(((key_of(o, fd, c), fd, c) for o, fd, cands in convs for c in cands),
                          key=lambda e: e[0])
    done: set[str] = set()
    for _, fd, c in conv_entries:
        if fd.name in done or any(i in claimed for i in c.lits):
            continue
        done.add(fd.name)
        claimed.update(c.lits)
        for li, role in zip(c.lits, c.roles):
            pair_names[li].add((fd.name, role))
    for o, fd, cands in convs:
        route[fd.name] = "class+point" if fd.name in done else "class"
    return RawLabels(pair_names, conv_text, route, tied)


def _find(e: Decimal, lits: Sequence[Lit], lo: int, hi: int, slot: str, sch: Schema
          ) -> tuple[int, str] | None:
    """Earliest literal in [lo, hi) that explains `e` through a mechanism this
    array slot is known to use; failing that, the earliest through any map.

    TRAP, caught by the self-test: "earliest literal, any map" read
    "$500,000 ...; $300,000" as `[500000, 300 x1000]` because "$300" (a monthly
    extra payment, three literals earlier) times 1000 is 300,000. The numbers
    came out right and the labels were wrong, which is the worse way to be wrong."""
    fallback = None
    for j in range(lo, hi):
        ex, _ = unary_cands(e, [lits[j]])
        if not ex:
            continue
        known = [c for c in ex if _share(sch, slot, lits[j], c.roles[0]) >= COMPAT_MIN_SHARE]
        if known:
            return j, min(known, key=lambda c: c.prio).roles[0]
        if fallback is None:
            fallback = (j, min(ex, key=lambda c: c.prio).roles[0])
    return fallback


def _label_arrays(f: Facts, arr_facts: list[FieldFact], sch: Schema,
                  pair_names: list[set], route: dict[str, str]) -> None:
    lits = f.rendered.lits
    anchor, rest = arr_facts[0], arr_facts[1:]
    ptr, runs = 0, []                       # runs: [lit index, role, repeat]
    for i, e in enumerate(anchor.value):
        hit = _find(e, lits, ptr, len(lits), anchor.name, sch) if e != 0 else None
        if hit:
            runs.append([hit[0], hit[1], 1])
            ptr = hit[0] + 1
        elif i > 0 and runs and e == anchor.value[i - 1]:
            runs[-1][2] += 1
        else:
            route[anchor.name] = "UNEXPLAINED"
            return
    for j, role, rep in runs:
        pair_names[j].add((anchor.name, role if rep == 1 else f"{role}#rep{rep}"))
    route[anchor.name] = "array:anchor"
    bounds = [r[0] for r in runs] + [len(lits)]
    reps = [r[2] for r in runs]
    total = sum(reps)
    for fd in rest:
        if len(fd.value) == len(runs):
            # one element per anchor literal: the original shape
            slots = [(k, bounds[k]) for k in range(len(runs))]
        elif len(fd.value) == total:
            # ONE ANCHOR LITERAL, SEVERAL ELEMENTS: "$350,000 at 6.75% for 30 years against 6% for
            # 15 years" states the loan once and compares two offers, so the anchor is one literal
            # repeated (`#rep2`) and the SIBLING arrays carry one literal per copy. Each group owns
            # `reps[k]` consecutive elements, found left to right inside the group's own window.
            #
            # When the loan is the ONLY anchor literal the offers may stand on either side of it
            # ("compare 6.25% for 30 years with 5.5% for 15 years on a $500,000 loan"), so its window
            # is the whole utterance; with several anchor literals each window starts at its own.
            single = len(runs) == 1
            slots = [(k, 0 if single else bounds[k]) for k in range(len(runs)) for _ in range(reps[k])]
        else:
            route[fd.name] = "UNEXPLAINED"
            continue
        found, lo, last_k = [], 0, -1
        for (k, start), e in zip(slots, fd.value):
            if k != last_k:
                lo, last_k = start, k
            if e == 0:
                continue                    # the fill: no literal states it
            hit = _find(e, lits, lo, bounds[k + 1], fd.name, sch)
            if hit is None:
                found = None
                break
            found.append(hit)
            lo = hit[0] + 1
        if found is None:
            route[fd.name] = "UNEXPLAINED"
            continue
        for j, role in found:
            pair_names[j].add((fd.name, role))
        route[fd.name] = "array:group"


def finish(f: Facts, lab: RawLabels, sch: Schema) -> Example:
    """Name-level labels -> an Example with ids. A pair the vocabulary dropped
    (too rare to learn) is removed here, and the oracle then reports the row as
    uncovered: the loss is counted, never hidden."""
    pid = sch.pair_id()
    cidx = sch.op_index()
    lits = f.rendered.lits
    ids: list[list[int]] = []
    for names in lab.pair_names:
        row = []
        for p in sorted(names):
            if p in pid:
                row.append(pid[p])
        ids.append(row)
    conv: dict[str, int] = {}
    for name, text in lab.conv_text.items():
        voc = sch.conv_vocab.get(name)
        if voc is not None and text in voc:
            conv[name] = voc.index(text)
    litinfo = [dict(start=l.start, end=l.end, text=l.text, tag=l.tag, value=str(l.value),
                    mag=mag_bucket(l.value), seg=s)
               for l, s in zip(lits, f.rendered.lit_seg)]
    return Example(f.rendered.text, f.rendered.q_start, f.rendered.u2_start, f.dialogue.kind,
                   litinfo, cidx[f.op] if f.op is not None else 0, ids, conv, f.dialogue.gold)


def build_examples(facts: Sequence[Facts], sch: Schema, stats: BuildStats | None = None
                   ) -> list[Example]:
    out = []
    pid = sch.pair_id()
    for f in facts:
        lab = label_facts(f, sch)
        if stats is not None:
            stats.n_rows += 1
            if f.op is not None:
                stats.n_params_rows += 1
                for name, how in lab.route.items():
                    stats.route[how] += 1
                    if how == "UNEXPLAINED":
                        stats.unexplained[(f.op, name)] += 1
                if lab.tied:
                    stats.tie_rows += 1
                if any(len(p) > 1 for p in lab.pair_names):
                    stats.multi_slot_rows += 1
                if any(p not in pid for names in lab.pair_names for p in names):
                    stats.dropped_rows += 1
        out.append(finish(f, lab, sch))
    return out


# ===========================================================================
# RECONSTRUCTION -- the inverse of labelling, and what serving would do
# ===========================================================================
class Missing:
    """A field no route could produce."""
    def __repr__(self):
        return "MISSING"


MISSING = Missing()


def _unary_value(role: str, lit: dict) -> Decimal | None:
    name, kind, rep = parse_role(role)
    if kind not in ("unary", "rep"):
        return None
    try:
        return UNARY_FN[name](Decimal(lit["value"]))
    except (InvalidOperation, ZeroDivisionError, OverflowError, KeyError):
        return None


def _binary_value(name: str, a: Decimal, b: Decimal) -> Decimal | None:
    try:
        if name == "M9 a-b":
            return a - b
        if name == "M9' a+b":
            return a + b
        if name == "M9 a*(1-p)":
            return a * (1 - b / D100)
        if name == "M9 a*p":
            return a * (b / D100)
        if name == "M11 (a-b)*12":
            return a * D12 - b * D12
        if name == "M11' a*12-b":
            return a * D12 - b
    except (InvalidOperation, ZeroDivisionError, OverflowError):
        return None
    return None


def reconstruct(sch: Schema, op: int, lits: list[dict], lit_pairs: list[list[int]],
                conv: dict[str, int], lit_scores: list[dict[int, float]] | None = None
                ) -> dict | None:
    """Rebuild the params object from (operation, per-literal pairs, class picks).

    This is the SERVING-SIDE half of the design: it is the only place a number
    is computed, in Decimal, from a literal's text and a map -- never emitted by
    the model. `lit_scores` (probabilities per literal and pair) arbitrate a
    scalar claimed by two literals; absent, the more recent literal wins."""
    if op == 0:
        return None
    op_name = sch.ops[op]
    out: dict = {sch.op_key: op_name}
    by_slot: dict[str, list[tuple[int, str]]] = collections.defaultdict(list)
    for li, plist in enumerate(lit_pairs):
        for p in plist:
            slot, role = sch.pairs[p]
            by_slot[slot].append((li, role))

    def score(li: int, slot: str, role: str) -> float:
        if lit_scores is None:
            return float(li)
        pid = sch.pair_id().get((slot, role))
        return lit_scores[li].get(pid, 0.0) if pid is not None else 0.0

    anchor = sch.anchor.get(op_name)
    groups: list[int] = []
    reps: list[int] = []
    if anchor is not None:
        groups = sorted({li for li, _ in by_slot.get(anchor, [])})
        rep_at = {li: parse_role(role)[2] for li, role in by_slot.get(anchor, [])}
        reps = [rep_at[g] for g in groups]
    for name in sch.op_fields[op_name]:
        kind = sch.field_kind[name]
        if kind == "arr":
            out[name[:-len(ARRAY_SUFFIX)]] = _build_array(sch, name, anchor, by_slot, groups, lits, reps)
            continue
        val = _scalar_from_pairs(name, by_slot.get(name, []), lits, score)
        if val is None:
            val = _class_value(sch, op_name, name, conv)
        out[name] = val
    return out


def _scalar_from_pairs(name, entries, lits, score):
    unary = [(li, r) for li, r in entries if parse_role(r)[1] == "unary"]
    if unary:
        li, r = max(unary, key=lambda e: (score(e[0], name, e[1]), e[0]))
        v = _unary_value(r, lits[li])
        if v is not None:
            return v
    by_map: dict[str, dict[str, list[int]]] = collections.defaultdict(lambda: {"A": [], "B": []})
    for li, r in entries:
        mname, k, _ = parse_role(r)
        if k in ("A", "B"):
            by_map[mname][k].append(li)
    for mname, ab in by_map.items():
        if ab["A"] and ab["B"]:
            la = max(ab["A"], key=lambda i: (score(i, name, f"{mname}#A"), i))
            lb = max(ab["B"], key=lambda i: (score(i, name, f"{mname}#B"), i))
            v = _binary_value(mname, Decimal(lits[la]["value"]), Decimal(lits[lb]["value"]))
            if v is not None:
                return v
    return None


def _class_value(sch: Schema, op_name: str, name: str, conv: dict[str, int]):
    kind = sch.field_kind[name]
    text = None
    if name in conv and name in sch.conv_vocab:
        text = sch.conv_vocab[name][conv[name]]
    elif name in sch.const_default:
        text = sch.const_default[name]
    elif name in sch.op_default.get(op_name, {}):
        text = sch.op_default[op_name][name]
    if text is None:
        return MISSING
    return json.loads(text) if kind == "cat" else Decimal(text)


def _build_array(sch, name, anchor, by_slot, groups, lits, reps=None):
    entries = sorted(by_slot.get(name, []))
    if name == anchor:
        out: list = []
        for li, role in entries:
            _, kind, rep = parse_role(role)
            v = _unary_value(role, lits[li])
            if v is None:
                return MISSING
            out.extend([v] * rep)
        return out
    out = []
    bounds = groups + [10 ** 9]
    reps = reps or [1] * len(groups)
    for k in range(len(groups)):
        lo = -1 if (len(groups) == 1 and reps[0] > 1) else bounds[k]   # see `_label_arrays`
        hit = [(li, r) for li, r in entries if lo <= li < bounds[k + 1]]
        # A group whose anchor literal is REPEATED owns that many elements (see `_label_arrays`);
        # a plain group owns one. A repeated group with fewer literals than copies is filled with
        # zero, exactly as an empty plain group is.
        for c in range(reps[k]):
            v = _unary_value(hit[c][1], lits[hit[c][0]]) if c < len(hit) else Decimal(0)
            out.append(v if v is not None else MISSING)
    return out


def params_match(pred: dict | None, gold: dict | None) -> tuple[bool, list[str]]:
    """Every gold field reproduced, at the gold's OWN precision for numbers."""
    if gold is None or pred is None:
        return (gold is None and pred is None), (["<op>"] if (gold is None) != (pred is None) else [])
    bad = []
    for k in set(gold) | set(pred):
        g, p = gold.get(k, MISSING), pred.get(k, MISSING)
        if isinstance(g, Missing) and isinstance(p, Missing):
            # STATED-ONLY LABELS: a field the visitor did not state is absent from the gold, and
            # the decoder answers MISSING for a field with no pair, no class and no convention.
            # Neither side makes a claim, so there is nothing to disagree about. Before this
            # every gold carried every field, so this arm was unreachable.
            continue
        if isinstance(g, Missing) or isinstance(p, Missing):
            bad.append(k)
        elif isinstance(g, list):
            if (not isinstance(p, list) or len(p) != len(g)
                    or any(isinstance(x, Missing) for x in p)
                    or not all(_num_eq(x, y) for x, y in zip(p, g))):
                bad.append(k)
        elif isinstance(g, bool) or as_decimal(g) is None:
            if p != g:
                bad.append(k)
        elif isinstance(p, (Decimal, int, float)) and not isinstance(p, bool):
            if not _num_eq(p, g):
                bad.append(k)
        else:
            bad.append(k)
    return not bad, sorted(bad)


def _num_eq(got, gold) -> bool:
    g = as_decimal(gold)
    p = got if isinstance(got, Decimal) else as_decimal(got)
    return g is not None and p is not None and (p == g or at_label_precision(p, g))


# ===========================================================================
# ORACLE COVERAGE
# ===========================================================================
@dataclass
class Coverage:
    n: int = 0
    n_params: int = 0
    rows_ok: int = 0
    fields_total: int = 0
    fields_ok: int = 0
    by_op: collections.Counter = field(default_factory=collections.Counter)
    by_op_n: collections.Counter = field(default_factory=collections.Counter)
    bad_field: collections.Counter = field(default_factory=collections.Counter)

    @property
    def row_cov(self) -> float:
        return self.rows_ok / max(1, self.n_params)

    @property
    def field_cov(self) -> float:
        return self.fields_ok / max(1, self.fields_total)


def oracle_coverage(examples: Sequence[Example], sch: Schema) -> Coverage:
    cov = Coverage()
    for ex in examples:
        cov.n += 1
        if ex.gold is None:
            continue
        cov.n_params += 1
        op = sch.ops[ex.op]
        cov.by_op_n[op] += 1
        pred = reconstruct(sch, ex.op, ex.lits, ex.pairs, ex.conv)
        ok, bad = params_match(pred, ex.gold)
        cov.fields_total += len(ex.gold)
        cov.fields_ok += len(ex.gold) - len(bad)
        if ok:
            cov.rows_ok += 1
            cov.by_op[op] += 1
        for b in bad:
            cov.bad_field[(op, b)] += 1
    return cov


# ===========================================================================
# REFERENCE ACCOUNTING (for --compare-reference)
# ===========================================================================
def reference_partition(dialogues: Sequence[Dialogue], op_key: str, legacy_lexer: bool) -> dict:
    """The reference's own EXTRACTED / CADENCE / UNEXPLAINED partition, with its
    own loops, optionally on its own (buggy) lexer. It counts a zero gold
    "extracted" via `at_label_precision`; that is the accounting this file
    replaces, kept so the gap is a number."""
    tot = ext = cad = rows_full = n_gold = 0
    for d in dialogues:
        if d.gold is None:
            continue
        n_gold += 1
        utt = "\n".join(x for x in (d.first, d.later) if x)
        lits = lex(utt, legacy=legacy_lexer)
        ok = True
        for _, val in flatten(d.gold):
            tot += 1
            if unary_hit(val, lits) or binary_hit(val, lits):
                ext += 1
                continue
            if cadence_hit(val, utt) or val in CONVENTION:
                cad += 1
                continue
            ok = False
        rows_full += ok
    return dict(values=tot, extracted=ext, cadence=cad, unexplained=tot - ext - cad,
                rows=n_gold, rows_full=rows_full)


# ===========================================================================
# TENSORS
# ===========================================================================
def tensorize(examples: Sequence[Example], tok, sch: Schema, max_len: int):
    """Examples -> padded tensors. Literal char spans become TOKEN spans through
    the tokenizer's offset mapping; that mapping is why the tokenizer must
    return offsets. Refuses (raises) rather than truncating: a literal that
    falls beyond `max_len` would silently vanish from the labels."""
    import numpy as np
    import torch

    n = len(examples)
    max_lits = max((len(e.lits) for e in examples), default=1) or 1
    ids = np.zeros((n, max_len), dtype=np.int32)
    seg = np.zeros((n, max_len), dtype=np.int8)
    length = np.zeros(n, dtype=np.int32)
    lit_s = np.zeros((n, max_lits), dtype=np.int16)
    lit_e = np.zeros((n, max_lits), dtype=np.int16)
    lit_tag = np.zeros((n, max_lits), dtype=np.int8)
    lit_mag = np.zeros((n, max_lits), dtype=np.int8)
    lit_ok = np.zeros((n, max_lits), dtype=bool)
    op = np.zeros(n, dtype=np.int64)
    conv = np.full((n, max(1, sch.n_conv)), IGNORE, dtype=np.int64)
    ptr, plit, pid = [0], [], []
    too_long, no_tokens = [], 0
    for r, ex in enumerate(examples):
        enc = tok.encode(ex.text)
        t = len(enc.ids)
        if t > max_len:
            too_long.append((r, t))
            continue
        ids[r, :t] = enc.ids
        length[r] = t
        for i, (s, e) in enumerate(enc.offsets):
            seg[r, i] = 0 if s < ex.q_start else (1 if s < ex.u2_start else 2)
            if i == t - 1:
                seg[r, i] = seg[r, i - 1]
        for li, lit in enumerate(ex.lits):
            span = tok.span_to_tokens(enc.offsets, lit["start"], lit["end"])
            if span is None:
                no_tokens += 1
                continue
            lit_s[r, li], lit_e[r, li] = span
            lit_tag[r, li] = TAGS.index(lit["tag"])
            lit_mag[r, li] = lit["mag"]
            lit_ok[r, li] = True
            for p in ex.pairs[li]:
                plit.append(li)
                pid.append(p)
        ptr.append(len(plit))
        op[r] = ex.op
        for name, cls in ex.conv.items():
            conv[r, sch.conv_fields.index(name)] = cls
    if too_long:
        raise ValueError(f"{len(too_long)} rows exceed max_len={max_len} tokens "
                         f"(longest {max(t for _, t in too_long)}); raise --max-len, truncation "
                         f"would silently delete literals")
    if no_tokens:
        raise ValueError(f"{no_tokens} literals have no token overlap")
    return dict(ids=torch.from_numpy(ids), seg=torch.from_numpy(seg),
                length=torch.from_numpy(length), lit_s=torch.from_numpy(lit_s),
                lit_e=torch.from_numpy(lit_e), lit_tag=torch.from_numpy(lit_tag),
                lit_mag=torch.from_numpy(lit_mag), lit_ok=torch.from_numpy(lit_ok),
                op=torch.from_numpy(op), conv=torch.from_numpy(conv),
                pair_ptr=torch.tensor(ptr, dtype=torch.long),
                pair_lit=torch.tensor(plit, dtype=torch.long),
                pair_id=torch.tensor(pid, dtype=torch.long))


# ===========================================================================
# DRIVER
# ===========================================================================
def sha256_file(path: Path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def prepare(train: Path, val: Path | None, op_key: str, question_mode: str = "real",
            min_class_count: int = 20, min_pair_count: int = 8, limit: int | None = None,
            verbose: bool = True):
    """Dialogues -> facts -> schema (TRAIN only) -> labelled examples."""
    t0 = time.time()
    dtr = load_dialogues(train, limit)
    dva = load_dialogues(val, None if limit is None else max(1, limit // 19)) if val else []
    ftr = [make_facts(d, op_key, question_mode) for d in dtr]
    fva = [make_facts(d, op_key, question_mode) for d in dva]
    if verbose:
        print(f"[corpus] facts for {len(ftr)} train + {len(fva)} val rows in {time.time() - t0:.1f}s",
              flush=True)
    bst = BuildStats()
    sch = build_schema(ftr, op_key, question_mode, min_class_count, min_pair_count, bst)
    tr_stats, va_stats = BuildStats(), BuildStats()
    xtr = build_examples(ftr, sch, tr_stats)
    xva = build_examples(fva, sch, va_stats)
    tr_stats.dropped_pairs = bst.dropped_pairs
    tr_stats.keyset_variants = bst.keyset_variants
    return dtr, dva, sch, xtr, xva, tr_stats, va_stats


def report(sch: Schema, xtr, xva, st: BuildStats, vst: BuildStats, dtr, dva, op_key: str,
           compare_reference: bool) -> tuple[Coverage, Coverage]:
    ctr, cva = oracle_coverage(xtr, sch), oracle_coverage(xva, sch)
    nl = [len(e.lits) for e in xtr]
    print(f"\nop key               : {op_key}")
    print(f"operations           : {sch.n_ops - 1} (+ {NONE_OP} for rows with no params)")
    print(f"rows                 : train {len(xtr)}  val {len(xva)}   "
          f"no-params rows: train {sum(e.gold is None for e in xtr)}  "
          f"val {sum(e.gold is None for e in xva)}")
    kinds = collections.Counter(e.kind for e in xtr)
    print(f"dialogue kinds       : {dict(kinds)}")
    print(f"literals per row    : mean {sum(nl) / max(1, len(nl)):.1f}  max {max(nl or [0])}")
    print(f"(slot, map) pairs    : {sch.n_pairs} kept, {len(st.dropped_pairs)} dropped as rarer than "
          f"{sch.params['min_pair_count']:.0f} rows")
    print(f"class heads          : {sch.n_conv}  "
          + "  ".join(f"{n}[{len(sch.conv_vocab[n])}]" for n in sch.conv_fields))
    print(f"constant fields      : {len(sch.const_default)} (a single value, so no head)")
    print(f"rows w/ multi-slot literal : train {st.multi_slot_rows}/{st.n_params_rows} "
          f"({100 * st.multi_slot_rows / max(1, st.n_params_rows):.1f}%)")
    print(f"rows w/ tied candidates    : train {st.tie_rows}/{st.n_params_rows} "
          f"({100 * st.tie_rows / max(1, st.n_params_rows):.1f}%)")
    print("\nroute of every (row, field) value, train:")
    tot = sum(st.route.values())
    for k, c in st.route.most_common():
        print(f"  {c:>8} {100 * c / tot:6.2f}%  {k}")
    for name, cov in (("train", ctr), ("val", cva)):
        print(f"\nORACLE COVERAGE ({name}): rows {cov.rows_ok}/{cov.n_params} = "
              f"{100 * cov.row_cov:.2f}%   fields {cov.fields_ok}/{cov.fields_total} = "
              f"{100 * cov.field_cov:.2f}%")
    worst = [(op, cov_n - ctr.by_op[op], cov_n) for op, cov_n in ctr.by_op_n.items()
             if ctr.by_op[op] < cov_n]
    if worst:
        print("\noperations NOT fully covered (train): uncovered / rows")
        for op, bad, n in sorted(worst, key=lambda x: -x[1])[:15]:
            print(f"  {bad:>6}/{n:<6} {op}")
        print("fields that fail (train): (op, field) count")
        for (op, fld), c in ctr.bad_field.most_common(15):
            print(f"  {c:>6}  {op}.{fld}")
    if compare_reference:
        print("\nREFERENCE ACCOUNTING (agent/analysis/extractability.py) on the train split:")
        for legacy in (True, False):
            p = reference_partition(dtr, op_key, legacy)
            lab = "legacy lexer (as shipped)" if legacy else "fixed lexer"
            print(f"  {lab:27s} values {p['values']}  extracted {p['extracted']} "
                  f"({100 * p['extracted'] / p['values']:.2f}%)  cadence/const {p['cadence']} "
                  f"({100 * p['cadence'] / p['values']:.2f}%)  unexplained {p['unexplained']} "
                  f"({100 * p['unexplained'] / p['values']:.2f}%)  rows fully covered "
                  f"{p['rows_full']}/{p['rows']} ({100 * p['rows_full'] / p['rows']:.2f}%)")
    return ctr, cva


# ===========================================================================
# SELF-TEST -- each check is a trap this file records, kept executable
# ===========================================================================
def selftest() -> int:
    """`python encoder_corpus.py --selftest`. No corpus needed; runs in well under a
    second. A check that cannot fail is not a check, so every block asserts the
    BAD behaviour too (the legacy lexer, the unguarded rounding) to prove the
    guard it is testing is the thing that changed the outcome."""
    import random
    import tempfile

    state = {"n": 0, "bad": 0}

    def check(cond: bool, what: str) -> None:
        state["n"] += 1
        if not cond:
            state["bad"] += 1
        print(f"  {'PASS' if cond else 'FAIL'}: {what}")

    # ---- lexer: the suffix bug
    text = "I owe $304,500 mortgage balance, 327 months left, $750 more a month, $250k or 1.2M"
    got = [(l.value, l.tag) for l in lex(text)]
    check(got == [(Decimal(304500), "money"), (Decimal(327), "months"), (Decimal(750), "money"),
                  (Decimal(250000), "money"), (Decimal(1200000), "bare")], f"lexer reads {got}")
    legacy = lex(text, legacy=True)
    check(legacy[0].value == Decimal(304500000000) and legacy[1].tag == "bare",
          "the LEGACY lexer reads '$304,500 mortgage' as 3.045e11 and loses '327 months' unit "
          "(the defect this file fixed)")
    lit = lex("at 6.5% for a while")[0]
    check("at 6.5% for a while"[lit.start:lit.end] == "6.5%", "a literal's span excludes trailing whitespace")
    pw = lex("a 6.5 percent rate, 5 percentage points, 30 percentile")
    check([(str(l.value), l.tag) for l in pw] == [("6.5", "percent"), ("5", "bare"), ("30", "bare")],
          "the WORD percent is a percent sign, but 'percentage' and 'percentile' are not")

    # ---- zero is the absence of a statement (Rule Z')
    ex, ap = unary_cands(Decimal("0.00"), lex("a 100% financed purchase, rate 7.2%"))
    check(not ex and not ap, "gold 0.00 is not explained by '100%' (1 - 1) nor by 7.2%/365 rounding to 0.00")
    ex, _ = unary_cands(Decimal("0"), lex("salvage value $0 over 5 years"))
    check(len(ex) > 0 and all(c.lits == (0,) for c in ex), "gold 0 IS explained by a literal that says $0")
    check(at_label_precision(Decimal("0.00197"), Decimal("0.00")),
          "at_label_precision alone accepts 7.2%/365 as '0.00' (why the rule is needed)")

    # ---- rounding may only be assumed where a longer exact value existed
    check(at_label_precision(Decimal("0.9394"), Decimal(1)),
          "at_label_precision accepts 1 - 6.06/100 as the integer 1")
    ex, ap = unary_cands(Decimal(1), lex("compounded annually at 6.06%"))
    check(not ex and not ap, "...but an integer gold takes no approximate explanation")
    ex, ap = unary_cands(Decimal("0.006008"), lex("a 7.21% mortgage"))
    check(not ex and [c.roles[0] for c in ap] == ["M3 annual%->monthly"] or
          "M3 annual%->monthly" in [c.roles[0] for c in ap], "0.006008 is 7.21%/12 at label precision")

    # ---- two-literal maps keep their operand roles
    bc = binary_cands(Decimal(400000), lex("a $500,000 home with $100,000 down"))
    check(any(c.roles == ("M9 a-b#A", "M9 a-b#B") and c.lits == (0, 1) for c in bc),
          "price minus down payment is a binary candidate with A = price, B = down")
    check(parse_role("M9 a-b#B") == ("M9 a-b", "B", 1) and parse_role("M1 identity#rep20") == (
        "M1 identity", "rep", 20) and parse_role("x100") == ("x100", "unary", 1), "role strings round-trip")

    # ---- a synthetic corpus exercising every mechanism
    rng = random.Random(0)

    def row(users: list[str], golds: list) -> dict:
        turns = [{"role": "system", "content": "s"}]
        for u, g in zip(users, golds):
            turns.append({"role": "user", "content": u})
            turns.append({"role": "assistant", "content":
                          g if isinstance(g, str) else "<params>" + json.dumps(g) + "</params>"})
        return {"conversations": turns}

    rows = []
    for i in range(60):
        amt, yrs, r100 = rng.randrange(100, 900) * 1000, rng.choice([10, 15, 20, 30]), rng.choice(
            [3.5, 4.25, 5.0, 6.0, 7.5])
        rows.append(row([f"Payment on ${amt:,} at {r100}% over {yrs}-year?"], [dict(
            operation="Pay", rate=str((Decimal(str(r100)) / 1200).quantize(Decimal("0.000001"))),
            periods=yrs * 12, present_value=f"{amt}.00", future_value="0.00", timing="END_OF_PERIOD")]))
        rows.append(row([f"Amortize ${amt:,} at {r100}% over {yrs}-year."], [dict(
            operation="Amort", loan_amount=f"{amt}.00", annual_rate=str(Decimal(str(r100)) / 100),
            original_home_value=f"{amt}.00", term_months=yrs * 12)]))
        a, b = rng.randrange(1, 9) * 1000, rng.randrange(1, 9) * 100
        rows.append(row([f"I put ${a:,} in and it returns ${b:,} a year. When does it pay back?"], [dict(
            operation="Payback", values=[-a] + [b] * 20, discounted=False)]))
        n = rng.choice([2, 3])
        offers = [(rng.randrange(1, 9) * 100000, rng.choice([3.0, 4.5, 6.0]), rng.choice([15, 30]),
                   rng.choice([0, 200, 300])) for _ in range(n)]
        txt = "; ".join(f"${x:,} at {y}% over {z}-year" + (f" with ${e}/month extra" if e else "")
                        for x, y, z, e in offers)
        rows.append(row([f"Compare: {txt}."], [dict(
            operation="Batch", loan_amounts=[o[0] for o in offers],
            annual_rates=[o[1] / 100 for o in offers], term_months=[o[2] * 12 for o in offers],
            extra_payments=[o[3] for o in offers])]))
        rows.append(row([f"Payment on ${amt:,} at {r100}% over {yrs}-year?", "what if 8% instead?"], [dict(
            operation="Pay", rate="0.006667", periods=yrs * 12, present_value=f"{amt}.00",
            future_value="0.00", timing="END_OF_PERIOD"), dict(
            operation="Pay", rate="0.006667", periods=yrs * 12, present_value=f"{amt}.00",
            future_value="0.00", timing="END_OF_PERIOD")]))
    rows.append(row(["How much is the meaning of life?"], ["I can only help with loans."]))
    facts = [make_facts(to_dialogue(r), "operation", "real") for r in rows]
    sch = build_schema(facts, "operation", "real", min_class_count=3, min_pair_count=2)
    exs = build_examples(facts, sch)
    cov = oracle_coverage(exs, sch)
    check(cov.rows_ok == cov.n_params and cov.n_params == len(rows) - 1,
          f"oracle reconstructs every synthetic row ({cov.rows_ok}/{cov.n_params})")
    if cov.rows_ok != cov.n_params:
        for e in exs:
            if e.gold is not None:
                pr = reconstruct(sch, e.op, e.lits, e.pairs, e.conv)
                ok, bad = params_match(pr, e.gold)
                if not ok:
                    print(f"    uncovered: {e.text!r}\n    gold {e.gold}\n    pred {pr}\n    bad {bad}")
                    print("    pairs", [[sch.pairs[p] for p in ps] for ps in e.pairs])
                    break
    check(sch.const_default.get("future_value") == "0" and "future_value" not in sch.conv_fields,
          "future_value is a constant of the schema, not a head")
    check(all(sch.pairs[p][0] != "future_value" for e in exs for ps in e.pairs for p in ps),
          "...and no literal is ever pointed at for it (Rule Z')")
    amort = [e for e in exs if e.gold and e.gold.get("operation") == "Amort"][0]
    check(any(len(ps) == 2 for ps in amort.pairs) and
          {sch.pairs[p][0] for ps in amort.pairs for p in ps} >= {"loan_amount", "original_home_value"},
          "one stated price fills BOTH loan_amount and original_home_value")
    pb = [e for e in exs if e.gold and e.gold.get("operation") == "Payback"][0]
    check([sch.pairs[p][1] for ps in pb.pairs for p in ps] == ["M8 negate", "M1 identity#rep20"],
          "payback's trailing run of 20 equal flows is a #rep20 role on the one stated figure")
    bt = [e for e in exs if e.gold and e.gold.get("operation") == "Batch"
          and 0 in e.gold["extra_payments"]][0]
    rec = reconstruct(sch, bt.op, bt.lits, bt.pairs, bt.conv)
    check([str(x) for x in rec["extra_payments"]] == [str(Decimal(x)) for x in bt.gold["extra_payments"]]
          and 0 in bt.gold["extra_payments"], "an offer with no extra payment gets the fill (0) by text group")
    rv = [e for e in exs if e.kind == "revise"][0]
    sup = [i for i, ps in enumerate(rv.pairs) if not ps and rv.lits[i]["seg"] == 0]
    check(len(sup) >= 1, "a superseded literal from the first turn is labelled NONE on a revision row")
    check(exs[-1].gold is None and exs[-1].op == 0, "a row with no params is class <NONE>")

    # ---- the schema travels as JSON
    sch2 = Schema.from_json(json.loads(json.dumps(sch.to_json())))
    same = all(C_eq(reconstruct(sch, e.op, e.lits, e.pairs, e.conv),
                    reconstruct(sch2, e.op, e.lits, e.pairs, e.conv)) for e in exs[:50])
    check(same, "schema -> JSON -> schema reconstructs identically")

    # ---- comparison is at the GOLD's precision, and wrong values are caught
    g = {"operation": "Pay", "rate": "0.006008"}
    check(params_match({"operation": "Pay", "rate": Decimal("0.0060083333333")}, g)[0],
          "0.00600833.. matches the label 0.006008")
    check(not params_match({"operation": "Pay", "rate": Decimal("0.006009")}, g)[0], "0.006009 does not")
    check(not params_match({"operation": "Pay"}, g)[0], "a missing field is a mismatch")

    # ---- the coverage gate refuses
    with tempfile.TemporaryDirectory() as d:
        f = Path(d) / "c.jsonl"
        f.write_text("\n".join(json.dumps(r) for r in rows))
        import contextlib
        import io as _io
        with contextlib.redirect_stdout(_io.StringIO()), contextlib.redirect_stderr(_io.StringIO()):
            hi = main(["--train", str(f), "--val", str(f), "--min-class-count", "3",
                       "--min-pair-count", "2", "--min-coverage", "1.01"])
            lo = main(["--train", str(f), "--val", str(f), "--min-class-count", "3",
                       "--min-pair-count", "2", "--min-coverage", "0.5"])
    check(hi == 1 and lo == 0, "--min-coverage above the achievable coverage exits 1, below exits 0")
    print(f"\n{state['n'] - state['bad']}/{state['n']} checks passed")
    return 1 if state["bad"] else 0


def C_eq(a, b) -> bool:
    return json.dumps(a, default=str, sort_keys=True) == json.dumps(b, default=str, sort_keys=True)


def main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--train", type=Path)
    ap.add_argument("--val", type=Path)
    ap.add_argument("--selftest", action="store_true", help="run the built-in checks and exit")
    ap.add_argument("--op-key", default="operation",
                    help="the params field naming the class of record: 'operation' "
                         "(mortgage) or 'strategy' (options)")
    ap.add_argument("--min-coverage", type=float, default=None,
                    help="REFUSE (exit 1) if the oracle row coverage on train OR val falls "
                         "below this fraction. Unset prints a warning, it does not gate.")
    ap.add_argument("--question-mode", choices=["real", "placeholder"], default="real")
    ap.add_argument("--min-class-count", type=int, default=20,
                    help="a value stated >= this many times with NO literal is a convention")
    ap.add_argument("--min-pair-count", type=int, default=8)
    ap.add_argument("--limit", type=int, default=None, help="first N train rows (smoke runs)")
    ap.add_argument("--compare-reference", action="store_true")
    ap.add_argument("--out", type=Path, default=None, help="write schema.json here")
    ap.add_argument("--tensors-out", type=Path, default=None,
                    help="directory to write train.pt / val.pt (padded tensors) and tokenizer.json; "
                         "written only AFTER the coverage gate passes")
    ap.add_argument("--tokenizer", type=Path, default=None,
                    help="an existing tokenizer.json to tensorize with (default: train one on the "
                         "train split)")
    ap.add_argument("--vocab-size", type=int, default=4096)
    ap.add_argument("--max-len", type=int, default=160)
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not (a.train and a.val):
        ap.error("--train and --val are required unless --selftest")

    dtr, dva, sch, xtr, xva, st, vst = prepare(
        a.train, a.val, a.op_key, a.question_mode, a.min_class_count, a.min_pair_count, a.limit)
    ctr, cva = report(sch, xtr, xva, st, vst, dtr, dva, a.op_key, a.compare_reference)
    if a.out:
        a.out.parent.mkdir(parents=True, exist_ok=True)
        a.out.write_text(json.dumps(sch.to_json(), indent=1))
        print(f"\nschema written to {a.out}")
    floor = a.min_coverage
    worst = min(ctr.row_cov, cva.row_cov)
    if floor is None:
        print("\nWARNING: no --min-coverage floor set; coverage is reported, NOT gated.")
    elif worst < floor:
        print(f"\nREFUSING: oracle row coverage {100 * worst:.2f}% is below the floor "
              f"{100 * floor:.2f}%. The labels cannot reproduce the gold, so no model trained "
              f"on them can reach it.", file=sys.stderr)
        return 1
    else:
        print(f"\ncoverage gate passed: {100 * worst:.2f}% >= {100 * floor:.2f}%")
    if a.tensors_out:
        import torch

        from encoder_tokenizer import EncoderTokenizer
        a.tensors_out.mkdir(parents=True, exist_ok=True)
        tok = (EncoderTokenizer.load(a.tokenizer) if a.tokenizer
               else EncoderTokenizer.train([e.text for e in xtr], a.vocab_size))
        tok.save(a.tensors_out / "tokenizer.json")
        for name, xs in (("train", xtr), ("val", xva)):
            t = tensorize(xs, tok, sch, a.max_len)
            torch.save(t, a.tensors_out / f"{name}.pt")
            print(f"wrote {a.tensors_out / (name + '.pt')}: " + ", ".join(
                f"{k}{list(v.shape)}" for k, v in t.items() if k in ("ids", "lit_s", "conv", "op")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
