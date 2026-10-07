#!/usr/bin/env python3
"""
VISITOR-STYLE phrasing, with STATED-ONLY labels.

WHY THIS FILE EXISTS (2026-10-06). The site's AI deal assistant was refusing or
mis-answering ordinary requests, and the cause was measured on the production
model rather than guessed: every extraction generator in
`build_mortgage_dataset.py` writes the utterance FROM a complete scenario, so
every training row states every figure the operation could use, in one of two
or three templated shapes. A visitor does not talk like that. They say
"closing costs on 450k", "refinance 320000 at 7% to 6%", "rent vs buy: 600k home,
20% down, 6.5% for 30 years, rent 3000 a month" -- a price, a rate, a rent, and
nothing else -- and a model that has only ever seen the fully specified shape
points the figures it was given at the wrong slots or declines to answer.

Three properties this generator holds, and each is a measured defect, not a
taste:

  1. SPARSE. Most requests state only the essential inputs. The optional ones are
     sampled independently, so the model reads each slot on its own evidence
     rather than learning that slots arrive together.

  2. STATED-ONLY LABELS. A field the visitor did not say is absent from the
     label, or carries the field's CONVENTION value when the schema already treats
     that value as a class (`future_value 0`, `payments_per_year 12`). It is never
     filled by pointing a literal that belongs to another field at it -- the old
     corpus labelled `original_home_value` with the LOAN literal whenever no home
     value was stated, which taught the model to point one number at two slots.
     The serving layer then returns only what was stated (see
     `shape_stated_params` in mortgage_verification.cppm), and this corpus is the
     half of that contract the model learns.

  3. MISSING ESSENTIALS ARE NORMAL. A request that omits an essential input has a
     label that omits it too, and a two-turn variant in which the assistant asks
     the question the SERVICE asks (parsed from `kQuestions` at import time, so the
     wording the model reads back as context is the wording the visitor was shown)
     and the visitor answers. Advice wording ("should I...", "is it worth...")
     wraps a calculable request without changing what is computed.

Nothing here invents a figure the utterance does not carry: every label value is
either a literal of the utterance under a map `encoder_corpus.py` already
enumerates (identity, /100, annual->monthly, years->months, complement, a-b,
a*(1-p), a*p), or a convention constant. `test_corpus_invariants.py` and
`encoder_corpus.py --min-coverage` verify that rather than trusting it.
"""
from __future__ import annotations

import json
import random
import re
from decimal import ROUND_HALF_UP, Decimal
from pathlib import Path
from typing import Callable

HERE = Path(__file__).parent
REPO = HERE.parent.parent

# ---------------------------------------------------------------------------
# The clarifying questions are DERIVED from the serving layer's own table.
# ---------------------------------------------------------------------------
_QROW = re.compile(r'\{"(?P<op>[A-Za-z]*)",\s*"(?P<field>[a-z_]+)",\s*"(?P<q>(?:[^"\\]|\\.)*)"\}')


def load_questions() -> dict[tuple[str, str], str]:
    """(operation or "", field) -> question, read from `kQuestions`."""
    src = (REPO / "backend" / "src" / "modules" / "mortgage_verification.cppm").read_text()
    start = src.index("inline constexpr auto kQuestions")
    end = src.index("});", start)
    out: dict[tuple[str, str], str] = {}
    for m in _QROW.finditer(src[start:end]):
        out[(m.group("op"), m.group("field"))] = m.group("q").replace('\\"', '"')
    if len(out) < 40:
        raise RuntimeError(f"parsed only {len(out)} questions from kQuestions; the table moved")
    return out


QUESTIONS = load_questions()


def question_for(op: str, field: str) -> str:
    return QUESTIONS.get((op, field)) or QUESTIONS[("", field)]


# ---------------------------------------------------------------------------
# Number formatting. Utterance text is derived from the SAME value the label is,
# so the two cannot disagree (the `phrase_money` lesson).
# ---------------------------------------------------------------------------
D = Decimal


def dtxt(d: Decimal) -> str:
    s = format(d.normalize(), "f")
    return s


def lbl_money(v: float) -> str:
    return f"{v:.2f}"


def lbl_frac(pct: Decimal, places: int = 4) -> str:
    """A percent as the fraction a label carries: 6.875 -> '0.06875', 6.5 -> '0.0650'."""
    frac = (pct / D(100)).normalize()
    s = format(frac, "f")
    decimals = len(s.split(".")[1]) if "." in s else 0
    if decimals < places:
        s = format(frac.quantize(D(1).scaleb(-places)), "f")
    return s


def lbl_per(pct: Decimal, n: int) -> str:
    """The per-period rate, to six places, exactly as the TVM generators write it.

    ROUND_HALF_UP in DECIMAL, because that is the oracle's own rule (`at_label_precision`).
    A float f-string rounds the binary neighbour of the value instead: 5.625% / 12 is exactly
    0.0046875, which the float formatter writes as 0.004687 and the oracle reads as 0.004688,
    so every eighth-percent rate that lands on a half was an unexplained value -- and enough of
    them (>= 20) promoted `rate` to a CLASS head with a pile of spurious values."""
    q = (pct / D(100) / D(n)).quantize(D("0.000001"), rounding=ROUND_HALF_UP)
    return format(q, "f")


class Item:
    """One fragment of an utterance together with the label it supports.

    `text`  how it reads inside a longer sentence ("at 6.5%").
    `terse` how it reads in a bare list ("6.5%").
    `put`   the label fields it states (name -> JSON value).
    `reply` what a visitor would answer if asked for it.
    `field` the essential field this item answers (for the missing-field rows)."""

    __slots__ = ("text", "terse", "put", "reply", "field", "lead", "last", "subj_text")

    def __init__(self, text: str, put: dict, *, terse: str | None = None,
                 reply: str | None = None, field: str | None = None,
                 lead: bool = False, last: bool = False, subj_text: str | None = None):
        # `subj_text`: the SAME figure phrased as a noun phrase, for when this item opens a
        # verb-led sentence ("What's the payment on ..."). Absent, `text` is used.
        self.subj_text = subj_text
        self.text, self.put = text, put
        self.terse = terse if terse is not None else text
        self.reply = reply if reply is not None else self.terse
        self.field = field
        self.lead, self.last = lead, last


ADVICE_PREFIX = [
    "Should I go 15 or 30 years? ", "Should I worry about this? ", "Is it smart to do this? ",
    "Is it going to be too much each month? ", "Am I paying too much? ", "Does this seem right? ", "Is that a lot? ",
    "Should I do this? ", "Is it worth it? ", "Is this a good idea? ",
    "Trying to decide something. ", "Thinking about it. ", "Quick question. ",
    "Is this a mistake? ", "Would this make sense? ", "Help me decide. ",
    "Is it a good deal? ", "Worth it? ", "Not sure if I should. ",
]
ADVICE_SUFFIX = [
    " Is that too much?", " Does that seem right?", " Am I overpaying?",
    " Is that going to be too much each month?",
    " Is that worth it?", " Should I go for it?", " Does that make sense?",
    " Is that a good deal?", " Worth it?", " Is it a mistake?",
    " Is that smart?", " Should I?", " Good idea?",
]


class Voice:
    """Per-row sampling and rendering. One instance per generated row."""

    def __init__(self, rng: random.Random):
        self.r = rng

    # -- primitives
    def chance(self, p: float) -> bool:
        return self.r.random() < p

    def pick(self, seq, weights=None):
        if weights is None:
            return self.r.choice(list(seq))
        return self.r.choices(list(seq), weights)[0]

    # -- sampling ----------------------------------------------------------
    def amt(self, lo: float, hi: float, mode: float, grain: str = "auto") -> float:
        v = self.r.triangular(lo, hi, mode)
        if grain == "auto":
            grain = self.pick(["k", "h", "f"], [0.6, 0.28, 0.12])
        step = {"k": 1000, "h": 100, "f": 500, "t": 10, "o": 1}[grain]
        out = float(max(step, round(v / step) * step))
        if out >= 900_000 and grain in ("k", "f") and self.chance(0.55):
            # A seven-figure amount a visitor types is "1.2 million" or "1.5m": land on a multiple of
            # 50,000 so those spellings exist at all (a uniform 1,000 grain made them 1 in 10).
            out = float(round(out / 50_000) * 50_000)
        return out

    def cents(self, lo: float, hi: float, mode: float) -> float:
        """A computed money figure: a payment with real cents, 30% of the time."""
        v = self.r.triangular(lo, hi, mode)
        if self.chance(0.30):
            return round(v, 2)
        return float(round(v / 50) * 50)

    def pct(self, lo: float, hi: float) -> Decimal:
        v = self.r.uniform(lo, hi)
        if self.chance(0.68):
            return D(f"{round(v * 8) / 8:.3f}").normalize()
        return D(f"{v:.2f}").normalize()

    def years(self, choices=(30, 15, 20, 10, 25, 40), weights=(0.55, 0.2, 0.1, 0.06, 0.06, 0.03)) -> int:
        return int(self.pick(choices, weights))

    # -- money rendering ---------------------------------------------------
    def m(self, v: float, *, dollar: float = 0.5, shorthand: float = 0.4) -> str:
        """A money literal the lexer reads back as `v` (checked by the oracle)."""
        whole = abs(v - round(v)) < 1e-9
        if not whole:
            return f"${v:,.2f}" if self.chance(0.8) else f"{v:,.2f}"
        n = int(round(v))
        forms: list[tuple[str, float]] = [("full", 46.0), ("plain", 8.0), ("raw", 12.0)]
        if n >= 1000 and n % 100 == 0 and n < 1_000_000 and self.chance(shorthand * 1.4):
            forms.append(("k", 30.0))
        if n >= 1_000_000 and n % 10_000 == 0 and self.chance(shorthand * 2.0):
            forms.append(("m", 40.0))
        if n >= 100_000 and n % 1000 == 0 and n < 1_000_000 and self.chance(shorthand):
            forms.append(("k", 24.0))
        form = self.pick([f for f, _ in forms], [w for _, w in forms])
        if form == "full":
            return f"${n:,}"
        if form == "plain":
            return f"{n:,}"
        if form == "raw":
            return f"${n}" if self.chance(0.25) else f"{n}"
        if form == "k":
            body = dtxt(D(n) / D(1000))
            k = self.pick(["k", "K"], [0.85, 0.15])
            return (f"${body}{k}" if self.chance(0.35) else f"{body}{k}")
        body = dtxt(D(n) / D(1_000_000))
        return self.pick([f"{body} million", f"${body} million", f"{body}M", f"${body}M", f"{body}m"],
                         [0.35, 0.2, 0.2, 0.15, 0.10])

    def p(self, pct: Decimal) -> str:
        s = dtxt(pct)
        return self.pick([f"{s}%", f"{s} percent", f"{s}%"], [0.78, 0.14, 0.08])

    def p_bare(self, pct: Decimal) -> str:
        return dtxt(pct)

    def yrs_noun(self, n: int) -> str:
        return self.pick([f"{n} years", f"{n} yrs", f"{n} year", f"{n}yr"], [0.74, 0.14, 0.08, 0.04])

    def yrs_adj(self, n: int) -> str:
        return self.pick([f"{n}-year", f"{n} year", f"{n}-yr"], [0.7, 0.24, 0.06])

    def mo_noun(self, n: int) -> str:
        return self.pick([f"{n} months", f"{n} mo", f"{n} months"], [0.8, 0.05, 0.15])

    # -- composition -------------------------------------------------------
    def order(self, items: list[Item], subject_first: bool) -> list[Item]:
        """`items[0]` is the SUBJECT of the sentence (the loan, the house, the balance). A layout
        that opens with a verb phrase needs it first; a bare list can start anywhere."""
        if len(items) <= 1:
            return list(items)
        first, rest = items[0], list(items[1:])
        if self.chance(0.85):
            self.r.shuffle(rest)
        if subject_first or self.chance(0.7):
            return [first] + rest
        out = [first] + rest
        self.r.shuffle(out)
        return out

    def join(self, texts: list[str]) -> str:
        if len(texts) <= 1:
            return "".join(texts)
        sep = self.pick([" ", ", ", " and ", "; "], [0.46, 0.34, 0.14, 0.06])
        if sep == " and " and len(texts) > 2:
            return ", ".join(texts[:-1]) + " and " + texts[-1]
        return sep.join(texts)

    def sentence(self, items: list[Item], *, open: list[str], ask: list[str],
                 fp: list[str], kw: list[str]) -> str:
        layout = self.pick(["open", "ask_last", "fp", "terse"], [0.34, 0.27, 0.17, 0.22])
        items = self.order(items, layout in ("open", "fp"))
        texts = [i.text for i in items]
        if layout in ("open", "fp") and items and items[0].subj_text:
            texts[0] = items[0].subj_text
        body = self.join(texts)
        if layout == "open":
            tail = self.pick(["?", "?", "", "."])
            s = f"{self.pick(open)} {body}{tail}"
        elif layout == "ask_last":
            sep = self.pick([". ", "? ", ", ", ": ", " -- "], [0.3, 0.1, 0.2, 0.2, 0.2])
            q = self.pick(ask)
            if sep in (", ", ": ", " -- "):
                q = q[:1].lower() + q[1:]
            s = f"{body}{sep}{q}"
        elif layout == "fp":
            s = f"{self.pick(fp)} {body}. {self.pick(ask)}"
        else:
            terse = self.join([i.terse for i in items])
            s = f"{self.pick(kw)} {terse}" if self.chance(0.8) else terse
            if self.chance(0.2):
                s += self.pick(["?", ""])
        return s

    def wrap(self, s: str, p_advice: float = 0.22, p_lower: float = 0.1, p_typo: float = 0.08) -> str:
        if self.chance(p_advice):
            if self.chance(0.5):
                s = self.pick(ADVICE_PREFIX) + s
            else:
                s = s.rstrip() + self.pick(ADVICE_SUFFIX)
        if self.chance(0.04):
            s = self.pick(["Denver", "Ohio", "Atlanta", "California", "Phoenix", "Seattle", "Florida", "Boston",
                           "Nashville", "Chicago"]) + self.pick([": ", " - ", ", "]) + s[:1].lower() + s[1:]
        if self.chance(p_lower):
            s = s.lower()
        if self.chance(p_typo):
            s = self.typo(s)
        return re.sub(r"\s+", " ", s).strip()

    def typo(self, s: str) -> str:
        # Never the words the LEXER reads a figure's unit from: a typo there changes what a literal
        # IS (a percent becomes a bare number), which is a different test from a misspelled noun.
        protect = {"percent", "million", "millions", "thousand"}
        words = [(m.start(), m.group()) for m in re.finditer(r"[A-Za-z]{5,}", s)
                 if m.group().lower() not in protect]
        if not words:
            return s
        at, w = self.pick(words)
        mode = self.pick(["swap", "drop", "dup"])
        i = self.r.randrange(1, len(w) - 1)
        if mode == "swap":
            w2 = w[:i] + w[i + 1] + w[i] + w[i + 2:]
        elif mode == "drop":
            w2 = w[:i] + w[i + 1:]
        else:
            w2 = w[:i] + w[i] + w[i:]
        return s[:at] + w2 + s[at + len(w):]


# ---------------------------------------------------------------------------
# Row assembly
# ---------------------------------------------------------------------------
# The CONVENTION values the schema already treats as a class or a constant: the value a field
# takes when the visitor said nothing. This is NOT a list of what the serving layer returns --
# `shape_stated_params` drops every one of these unless the utterance states it -- it is what the
# label carries so that the encoder's decoded value for an unstated field equals the gold's, which
# `params_match` requires. Copied from `const_default` / `op_default` of the schema the corpus
# trains, and verified by `encoder_corpus.py --min-coverage` rather than trusted.
CONST_DEFAULT = {
    "annual_appreciation": "0.0000", "annual_cost_growth": "0.0000", "annual_expense_increase": "0.0000",
    "annual_inflation_rate": "0.0000", "annual_insurance": "0.00", "annual_other_expenses": "0.00",
    "annual_rent_increase": "0.0000", "annual_repairs": "0.00", "appraisal_fee": "0.00",
    "cash_out_amount": "0.00", "closing_costs_buy": "0.00", "current_pmi_monthly": "0.00",
    "discount_points_percent": "0.0000", "down_payment": "0.00", "factor": 2.0, "future_value": "0.00",
    "heloc_annual_rate": "0.0000", "heloc_drawn_amount": "0.00", "heloc_term_years": 0,
    "inspection_fee": "0.00", "loan_amount": "0.00", "loan_annual_rate": "0.0000", "loan_term_years": 0,
    "management_fee_rate": "0.0000", "monthly_hoa": "0.00", "monthly_overpayment": "0.00",
    "monthly_piti_and_maintenance": "0.00", "monthly_taxes_ins_maintenance": "0.00",
    "new_pmi_monthly": "0.00", "origination_fee_percent": "0.0000", "other_lender_fees": "0.00",
    "payments_per_year": 12, "periods_per_year": 12, "pmi_annual_rate": "0.0000",
    "pmi_drop_off_ltv": "0.80", "rate": "0.000000", "recording_fees": "0.00",
    "seller_lender_credits": "0.00", "selling_cost_percent": "0.0000", "timing": "END_OF_PERIOD",
    "transfer_tax_percent": "0.0000",
}
CLASS_DEFAULT = {
    ("ComputeCumulative", "component"): "PRINCIPAL",
    ("ComputeDepreciation", "method"): "STRAIGHT_LINE",
    ("ComputeFutureValueDetailed", "compound_frequency"): 12,
    ("ComputeRefinance", "closing_cost_type"): "PAID_IN_CASH",
    ("ComputePaybackPeriod", "discounted"): False,
}


class Builder:
    """Turns items into a conversation. `G` is build_mortgage_dataset."""

    def __init__(self, G):
        self.G = G

    def defaults(self, op: str) -> dict:
        known = self.G.op_field_names(op)
        out = {k: v for k, v in CONST_DEFAULT.items() if k in known}
        out.update({f: v for (o, f), v in CLASS_DEFAULT.items() if o == op and f in known})
        return out

    def params(self, op: str, obj: dict) -> str:
        known = self.G.op_field_names(op)
        extra = set(obj) - known
        if extra:
            raise AssertionError(f"{op}: {sorted(extra)} are not fields of the operation")
        ordered = {"operation": op}
        for f in self.G.OPERATIONS[op]["fields"]:
            if f["name"] in obj:
                ordered[f["name"]] = obj[f["name"]]
        return f"<params>{json.dumps(ordered, separators=(',', ':'))}</params>"

    def gold(self, op: str, items: list[Item], defaults: dict, extra: dict | None = None) -> dict:
        obj: dict = {}
        for it in items:
            obj.update(it.put)
        for k, v in defaults.items():
            obj.setdefault(k, v)
        if extra:
            obj.update(extra)
        return obj

    def row(self, v: Voice, op: str, items: list[Item], essentials: list[str], defaults: dict | None = None,
            *, open, ask, fp, kw, extra: dict | None = None,
            p_missing: float = 0.10, p_clarify: float = 0.10, protect: tuple = ()) -> dict:
        """One conversation: a complete request, a request missing one essential
        (label omits it), or the two-turn exchange that completes it."""
        G = self.G
        defaults = self.defaults(op) if defaults is None else defaults
        by_field = {it.field: it for it in items if it.field}
        droppable = [f for f in essentials if f in by_field and f not in protect]
        roll = v.r.random()
        if droppable and roll < p_missing + p_clarify:
            miss = v.pick(droppable)
            gone = by_field[miss]
            kept = [it for it in items if it is not gone]
            if len(kept) >= 1:
                first = v.wrap(v.sentence(kept, open=open, ask=ask, fp=fp, kw=kw))
                if roll < p_missing:
                    return G.convo(("system", G.SYSTEM), ("user", first),
                                   ("assistant", self.params(op, self.gold(op, kept, defaults, extra))))
                q = question_for(op, miss)
                reply = gone.reply
                return G.convo(("system", G.SYSTEM), ("user", first), ("assistant", q),
                               ("user", reply),
                               ("assistant", self.params(op, self.gold(op, items, defaults, extra))))
        user = v.wrap(v.sentence(items, open=open, ask=ask, fp=fp, kw=kw))
        return G.convo(("system", G.SYSTEM), ("user", user),
                       ("assistant", self.params(op, self.gold(op, items, defaults, extra))))
