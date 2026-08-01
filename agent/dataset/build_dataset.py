#!/usr/bin/env python3
"""
Builds the supervised set for the parameter agent.

The job is narrow and worth stating precisely: turn what someone types into a
chat box into the parameters this site already accepts, or ask one question when
that is not yet possible. It is NOT a trading assistant. It does not offer
opinions on whether a strategy is a good idea, and the dataset deliberately
teaches it to decline that, because a 0.6B model improvising market advice on a
page that prices real positions is the worst thing this could become.

Grounded in agent/dataset/strategies.json, extracted from the SAME catalogue the
UI renders. A model that emits `iron_condor` when the app calls it
`reverse_iron_condor` produces a plausible object the frontend silently drops,
so the label space is taken from the app rather than written out by hand.

Output is ShareGPT-shaped JSONL ({"conversations": [{"role","content"}, ...]}),
which is what unsloth.chat_templates.standardize_sharegpt expects.

    python3 build_dataset.py --out data/ --n 12000
"""
from __future__ import annotations

import argparse
import json
import random
from pathlib import Path

HERE = Path(__file__).parent

# ---------------------------------------------------------------------------
# The parameter space. Every value here must exist in the app.
# ---------------------------------------------------------------------------

EQUITIES = ["SPY", "QQQ", "IWM", "DIA", "NVDA", "AAPL", "TSLA", "MSFT", "AMZN",
            "META", "GOOGL", "AMD", "NFLX", "COIN", "PLTR", "SMCI"]
# Roots, not contracts. ES is also Eversource Energy, which is why asset_class
# is emitted explicitly rather than left for the backend to guess -- that
# ambiguity once put a utility's share price on an index future.
#
# ONLY the roots the provider actually returns a term structure for. Probing the
# live backend: ES and NQ return eight listed contracts each; RTY, YM, CL, NG,
# GC, SI, ZB and ZN return zero. Training on those would teach the model to emit
# parameters the site cannot fulfil -- the request looks well formed, the curve
# comes back empty, and every futures strategy stays blocked with nothing
# indicating the symbol was the problem. Extend when the data arrives.
FUTURES = ["ES", "NQ"]
CRYPTO = ["BTC", "ETH"]

FUTURES_NAMES = {
    "ES": ["e-mini s&p", "es futures", "the s&p future", "emini", "spx futures"],
    "NQ": ["nasdaq futures", "nq", "e-mini nasdaq", "the nasdaq future"],
}

# Roots people ask for that this deployment has no data for. They get a
# straight answer naming what IS available, rather than a params object for a
# symbol that will come back empty.
UNSUPPORTED_FUTURES = {
    "crude": "CL", "oil": "CL", "wti": "CL", "gold": "GC", "silver": "SI",
    "nat gas": "NG", "natural gas": "NG", "russell futures": "RTY",
    "dow futures": "YM", "bond futures": "ZB", "the long bond": "ZB",
    "ten year": "ZN",
}
EQUITY_NAMES = {
    "SPY": ["spy", "the s&p etf", "s&p 500 etf", "spiders"],
    "QQQ": ["qqq", "the nasdaq etf", "triple q"],
    "NVDA": ["nvidia", "nvda"],
    "AAPL": ["apple", "aapl"],
    "TSLA": ["tesla", "tsla"],
    "MSFT": ["microsoft", "msft"],
    "AMZN": ["amazon", "amzn"],
    "META": ["meta", "facebook"],
    "GOOGL": ["google", "alphabet", "googl"],
    "AMD": ["amd"],
    "NFLX": ["netflix", "nflx"],
    "COIN": ["coinbase", "coin"],
    "PLTR": ["palantir", "pltr"],
    "SMCI": ["supermicro", "smci"],
    "IWM": ["iwm", "russell etf", "small caps"],
    "DIA": ["dia", "the dow etf"],
}
CRYPTO_NAMES = {"BTC": ["bitcoin", "btc"], "ETH": ["ethereum", "eth"]}

# How people say a strategy, beyond its catalogue name. Keys are catalogue ids.
ALIASES = {
    "long_call": ["buy a call", "long call", "just a call", "call option"],
    "long_put": ["buy a put", "long put", "just a put", "put option"],
    "bull_call_spread": ["call debit spread", "bull spread", "call spread", "debit call spread"],
    "bear_put_spread": ["put debit spread", "bear spread", "put spread"],
    "bull_put_spread": ["put credit spread", "bull put", "sell a put spread"],
    "bear_call_spread": ["call credit spread", "bear call", "sell a call spread"],
    "iron_condor": ["condor", "ic", "iron condor", "short iron condor"],
    "iron_butterfly": ["iron fly", "iron butterfly", "ironfly"],
    "long_straddle": ["straddle", "buy a straddle", "long straddle"],
    "short_straddle": ["sell a straddle", "short straddle"],
    "long_strangle": ["strangle", "buy a strangle"],
    "short_strangle": ["sell a strangle", "short strangle"],
    "covered_call": ["covered call", "buy-write", "sell calls against my shares"],
    "cash_secured_put": ["csp", "cash secured put", "sell a put"],
    "protective_put": ["protective put", "married put", "hedge with puts"],
    "collar": ["collar", "costless collar"],
    "calendar_spread": ["calendar", "time spread", "horizontal spread"],
    "diagonal_spread": ["diagonal", "diagonal spread"],
    "pmcc": ["poor man's covered call", "pmcc", "poor mans covered call"],
    "call_butterfly": ["call fly", "butterfly", "call butterfly"],
    "put_butterfly": ["put fly", "put butterfly"],
    "jade_lizard": ["jade lizard"],
    "risk_reversal": ["risk reversal", "reversal"],
    "futures_long": ["long futures", "buy futures", "go long the future"],
    "futures_short": ["short futures", "sell futures", "short the future"],
    "futures_calendar": ["futures calendar", "roll spread", "calendar in futures"],
    "cash_and_carry": ["cash and carry", "basis trade", "carry trade"],
    "covered_futures_call": ["covered futures call", "fop covered call"],
    "box_spread": ["box", "box spread"],
    "synthetic_long": ["synthetic long", "synthetic stock"],
    "synthetic_short": ["synthetic short"],
    "reverse_iron_condor": ["reverse iron condor", "long iron condor"],
}

DTE_PHRASES = [
    ("{n} days", lambda n: n), ("{n} dte", lambda n: n),
    ("{n} days out", lambda n: n), ("{n}d", lambda n: n),
    ("expiring in {n} days", lambda n: n),
]
WEEK_DTE = {"weekly": 7, "next week": 7, "two weeks": 14, "a month": 30,
            "monthly": 30, "next month": 30, "two months": 60, "a quarter": 90,
            "three months": 90, "leaps": 365, "a year": 365}

SYSTEM = (
    "You turn a trader's request into parameters for the Options & Futures "
    "Calculator. Reply with a single JSON object inside <params></params> when "
    "you have enough to act, or ask exactly one short question when you do not. "
    "You do not give trading advice."
)


def load_strategies() -> list[dict]:
    return json.loads((HERE / "strategies.json").read_text())


def params_block(obj: dict) -> str:
    # Compact and key-ordered so the model learns ONE serialization. Left to
    # vary, a small model spends capacity on formatting instead of on meaning.
    ordered = {k: obj[k] for k in
               ["symbol", "asset_class", "strategy", "expiration_days", "quantity"]
               if k in obj and obj[k] is not None}
    return f"<params>{json.dumps(ordered, separators=(',', ':'))}</params>"


def phrase_symbol(rng: random.Random, sym: str, cls: str) -> str:
    table = {"EQUITY": EQUITY_NAMES, "FUTURES": FUTURES_NAMES, "CRYPTO": CRYPTO_NAMES}[cls]
    return rng.choice(table.get(sym, [sym.lower()]))


def phrase_dte(rng: random.Random) -> tuple[str, int]:
    if rng.random() < 0.45:
        word = rng.choice(list(WEEK_DTE))
        return word, WEEK_DTE[word]
    n = rng.choice([1, 2, 3, 5, 7, 14, 21, 30, 45, 60, 90, 120, 180, 365])
    tmpl, f = rng.choice(DTE_PHRASES)
    return tmpl.format(n=n), f(n)


def make_extraction(rng: random.Random, strategies: list[dict]) -> dict:
    """A complete request -> a params object."""
    s = rng.choice(strategies)
    is_fut = s["category"] == "Futures"
    cls = "FUTURES" if is_fut else rng.choices(["EQUITY", "CRYPTO"], [0.92, 0.08])[0]
    sym = rng.choice({"EQUITY": EQUITIES, "FUTURES": FUTURES, "CRYPTO": CRYPTO}[cls])

    name = rng.choice(ALIASES.get(s["id"], [s["name"].lower()]))
    sym_txt = phrase_symbol(rng, sym, cls)
    dte_txt, dte = phrase_dte(rng)
    qty = rng.choices([1, 2, 3, 5, 10], [0.6, 0.15, 0.1, 0.1, 0.05])[0]
    qty_txt = "" if qty == 1 else rng.choice([f"{qty} contracts ", f"{qty}x ", f"{qty} lots "])

    templates = [
        "{q}{n} on {s} {d}",
        "show me {a} {n} on {s}, {d}",
        "i want to {n} {s} {d}",
        "set up {q}{n} for {s} expiring {d}",
        "{s} {n} {d}",
        "can you price {a} {n} on {s} {d}?",
        "pull up {a} {n} on {s} at {d}",
        "{n} {s} {d} please",
    ]
    user = rng.choice(templates).format(
        q=qty_txt, n=name, s=sym_txt, d=dte_txt,
        a=rng.choice(["a", "an"]) if name[0] in "aeiou" else "a",
    )
    if rng.random() < 0.3:
        user = user.capitalize()

    obj = {"symbol": sym, "asset_class": cls, "strategy": s["id"],
           "expiration_days": dte, "quantity": qty}
    return {"conversations": [
        {"role": "system", "content": SYSTEM},
        {"role": "user", "content": user},
        {"role": "assistant", "content": params_block(obj)},
    ]}


def make_clarification(rng: random.Random, strategies: list[dict]) -> dict:
    """
    Missing information -> ONE question, then the params.

    Teaching the model to ask rather than to guess is the point. A confident
    default for an unstated expiry silently prices a position the user did not
    ask for, and on this site the number it produces looks exactly as
    authoritative as a correct one.
    """
    s = rng.choice(strategies)
    is_fut = s["category"] == "Futures"
    cls = "FUTURES" if is_fut else "EQUITY"
    sym = rng.choice(FUTURES if is_fut else EQUITIES)
    name = rng.choice(ALIASES.get(s["id"], [s["name"].lower()]))
    missing = rng.choice(["dte", "symbol"])

    if missing == "dte":
        user = rng.choice([f"{name} on {phrase_symbol(rng, sym, cls)}",
                           f"set up {name} for {phrase_symbol(rng, sym, cls)}",
                           f"i want {name} on {phrase_symbol(rng, sym, cls)}"])
        ask = rng.choice(["Which expiry — how many days out?",
                          "What expiration would you like?",
                          "How far out should the expiry be?"])
        dte_txt, dte = phrase_dte(rng)
        follow = dte_txt
    else:
        user = rng.choice([f"{name}", f"set up {name}", f"i want to trade {name}"])
        ask = rng.choice(["Which symbol?", "What underlying?", "Which ticker should I use?"])
        follow = phrase_symbol(rng, sym, cls)
        dte = 30

    obj = {"symbol": sym, "asset_class": cls, "strategy": s["id"],
           "expiration_days": dte, "quantity": 1}
    return {"conversations": [
        {"role": "system", "content": SYSTEM},
        {"role": "user", "content": user},
        {"role": "assistant", "content": ask},
        {"role": "user", "content": follow},
        {"role": "assistant", "content": params_block(obj)},
    ]}


def make_modification(rng: random.Random, strategies: list[dict]) -> dict:
    """A follow-up that changes one field, keeping the rest."""
    s = rng.choice(strategies)
    cls = "FUTURES" if s["category"] == "Futures" else "EQUITY"
    sym = rng.choice(FUTURES if cls == "FUTURES" else EQUITIES)
    name = rng.choice(ALIASES.get(s["id"], [s["name"].lower()]))
    _, dte = phrase_dte(rng)
    first = {"symbol": sym, "asset_class": cls, "strategy": s["id"],
             "expiration_days": dte, "quantity": 1}

    kind = rng.choice(["dte", "symbol", "qty"])
    second = dict(first)
    if kind == "dte":
        new_txt, new_dte = phrase_dte(rng)
        followup = rng.choice([f"make it {new_txt}", f"same but {new_txt}",
                               f"change the expiry to {new_txt}"])
        second["expiration_days"] = new_dte
    elif kind == "symbol":
        new_sym = rng.choice([x for x in (FUTURES if cls == "FUTURES" else EQUITIES) if x != sym])
        followup = rng.choice([f"do the same on {phrase_symbol(rng, new_sym, cls)}",
                               f"now {phrase_symbol(rng, new_sym, cls)} instead",
                               f"switch it to {phrase_symbol(rng, new_sym, cls)}"])
        second["symbol"] = new_sym
    else:
        q = rng.choice([2, 3, 5, 10])
        followup = rng.choice([f"make it {q} contracts", f"{q}x that", f"bump it to {q} lots"])
        second["quantity"] = q

    return {"conversations": [
        {"role": "system", "content": SYSTEM},
        {"role": "user", "content": f"{name} on {phrase_symbol(rng, sym, cls)} {dte} days"},
        {"role": "assistant", "content": params_block(first)},
        {"role": "user", "content": followup},
        {"role": "assistant", "content": params_block(second)},
    ]}


def make_refusal(rng: random.Random) -> dict:
    """
    Advice, predictions and account questions -> a clear decline.

    Included at a deliberate share. A model that will happily answer "should I
    buy this?" on a page showing live prices is a liability, and the refusal has
    to be trained rather than hoped for.
    """
    pairs = [
        ("should i buy calls on nvidia?",
         "I can't advise on whether to take a position — I only set up the calculator. Tell me the strategy, symbol and expiry and I'll price it."),
        ("is spy going up next week?",
         "I can't forecast prices. I can price a strategy you choose against live data — which one would you like?"),
        ("what's the best strategy right now?",
         "That depends on your view and your risk, which I'm not in a position to judge. Name a strategy and I'll set it up."),
        ("how much money will i make on this?",
         "The payoff panel shows profit and loss across the price range once the position is set. I can't promise a result."),
        ("can you place this trade for me?",
         "No — this is a calculator, not a broker. It models the position; it doesn't execute anything."),
        ("what's my account balance?",
         "I don't have access to any account. I only translate a request into calculator parameters."),
        ("is this a good time to sell puts?",
         "I can't judge timing or suitability. If you tell me the symbol and expiry, I'll set up the cash-secured put."),
    ]
    u, a = rng.choice(pairs)
    return {"conversations": [
        {"role": "system", "content": SYSTEM},
        {"role": "user", "content": u},
        {"role": "assistant", "content": a},
    ]}


def make_chitchat(rng: random.Random) -> dict:
    """Small talk and capability questions, kept short and on-task."""
    pairs = [
        ("hi", "Hello. Tell me a strategy, a symbol and an expiry and I'll set up the calculator."),
        ("hello", "Hi. What would you like to price?"),
        ("what can you do?",
         "I turn a plain-English request into calculator parameters — the strategy, the underlying, the expiry and the size. Try \"iron condor on SPY 30 days\"."),
        ("what strategies do you support?",
         "All of the catalogue: single-leg calls and puts, vertical and calendar spreads, straddles, strangles, condors, butterflies, and the futures spreads. Name one and I'll set it up."),
        ("do you do futures?",
         "Yes — ES, NQ, RTY, YM, CL, NG, GC, SI, ZB and ZN, including outrights, calendar spreads and cash-and-carry."),
        ("thanks", "Anytime."),
        ("who made this?", "Knobugsoft LLC. I'm the assistant for its Options & Futures Calculator."),
    ]
    u, a = rng.choice(pairs)
    return {"conversations": [
        {"role": "system", "content": SYSTEM},
        {"role": "user", "content": u},
        {"role": "assistant", "content": a},
    ]}


def make_unknown_strategy(rng: random.Random) -> dict:
    """
    Something the catalogue does not contain -> say so, do not invent an id.

    Without this the model learns that every request maps to SOME id, and the
    nearest-sounding one is what it reaches for. An invented id fails silently
    in the frontend, which is worse than a plain "not supported".
    """
    pairs = [
        ("set up a gamma scalp on spy",
         "Gamma scalping isn't in the catalogue — it's a management technique rather than a fixed position. The closest listed is a long straddle. Shall I set that up?"),
        ("build me a dispersion trade",
         "Dispersion isn't supported — it needs an index against its constituents, which this calculator doesn't model. Would a straddle on the index work instead?"),
        ("i want a variance swap on qqq",
         "Variance swaps aren't in the catalogue. The nearest listed position is a long strangle. Would you like that?"),
        ("do a pairs trade aapl vs msft",
         "Pairs trades across two underlyings aren't supported — each calculation is one symbol. I can set up a position on either leg."),
    ]
    u, a = rng.choice(pairs)
    return {"conversations": [
        {"role": "system", "content": SYSTEM},
        {"role": "user", "content": u},
        {"role": "assistant", "content": a},
    ]}


def make_unsupported_future(rng: random.Random) -> dict:
    """
    A futures root with no data -> say so and name what IS available.

    Without this the model maps "gold" to the nearest futures id it knows and
    emits a params object the site cannot serve. Refusing by name is far more
    useful than a well-formed request for a symbol that returns nothing.
    """
    phrase = rng.choice(list(UNSUPPORTED_FUTURES))
    ask = rng.choice([
        f"long futures on {phrase}", f"set up a {phrase} calendar spread",
        f"{phrase} outright", f"price {phrase} futures for me",
    ])
    reply = (f"There's no term structure for {phrase} on this deployment — the "
             f"data provider only supplies ES (E-mini S&P) and NQ (E-mini "
             f"Nasdaq). Would one of those work?")
    return {"conversations": [
        {"role": "system", "content": SYSTEM},
        {"role": "user", "content": ask},
        {"role": "assistant", "content": reply},
    ]}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(HERE / "data"))
    ap.add_argument("--n", type=int, default=12000)
    ap.add_argument("--seed", type=int, default=3407)
    ap.add_argument("--val-frac", type=float, default=0.05)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    strategies = load_strategies()

    # Mix. Extraction dominates because it is the job; the rest exist so the
    # model does not treat every input as an extraction and emit a params block
    # for "hello".
    mix = [
        (0.55, make_extraction),
        (0.16, make_clarification),
        (0.13, make_modification),
        (0.06, make_refusal),
        (0.04, make_chitchat),
        (0.03, make_unknown_strategy),
        (0.03, make_unsupported_future),
    ]
    weights = [w for w, _ in mix]
    fns = [f for _, f in mix]

    rows, seen = [], set()
    attempts = 0
    while len(rows) < args.n and attempts < args.n * 40:
        attempts += 1
        fn = rng.choices(fns, weights)[0]
        row = (fn(rng, strategies)
               if fn in (make_extraction, make_clarification, make_modification)
               else fn(rng))
        # Deduplicate on the whole conversation: templated generation repeats,
        # and duplicates are wasted steps that also skew the eval.
        key = json.dumps(row, sort_keys=True)
        if key in seen:
            continue
        seen.add(key)
        rows.append(row)

    rng.shuffle(rows)
    n_val = max(1, int(len(rows) * args.val_frac))
    val, train = rows[:n_val], rows[n_val:]

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for name, part in (("train", train), ("val", val)):
        p = out / f"{name}.jsonl"
        with p.open("w") as fh:
            for r in part:
                fh.write(json.dumps(r) + "\n")
        print(f"  {p}  {len(part)} rows")

    covered = {c[-1]["content"] for r in rows for c in [r["conversations"]]}
    ids_seen = set()
    for r in rows:
        for turn in r["conversations"]:
            if turn["role"] == "assistant" and "<params>" in turn["content"]:
                ids_seen.add(json.loads(turn["content"][8:-9])["strategy"])
    missing = {s["id"] for s in strategies} - ids_seen
    print(f"\n  requested {args.n}, produced {len(rows)} unique after dedup")
    print(f"  strategies covered: {len(ids_seen)}/{len(strategies)}")
    if missing:
        # Loud, because a strategy absent from training is one the model will
        # never emit, and that is invisible at eval time unless it is stated.
        print(f"  WARNING uncovered strategies: {sorted(missing)}")


if __name__ == "__main__":
    main()
