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
# Roots, not contracts. ES is also Eversource Energy, CL is also
# Colgate-Palmolive -- which is why asset_class is emitted explicitly rather
# than left for the backend to guess, and why those two roots get their own
# clarification treatment below (see make_clarification's "asset_class"
# branch) instead of a guessed resolution.
#
# Broadened from {ES, NQ} to add CL, GC and ZB: the live data provider now
# returns a term structure for these too (see backend/src/modules/
# market_data.cppm's live Alpaca wiring), and the strategy catalogue prices
# them the same generic way it prices ES/NQ -- a FUTURE leg is a FUTURE leg
# regardless of root. Deliberately NOT extended to RTY, YM, NG, SI or ZN:
# those still have no data behind them (see UNSUPPORTED_FUTURES below), and
# NOT extended to crack_321 (a 3-2-1 crack spread on CL) -- that id does not
# exist in strategies.json / strategy_catalogue.cppm and is gated out of the
# UI for exactly that reason; broadening the ROOT must not be read as
# teaching the STRATEGY the product refuses to price.
FUTURES = ["ES", "NQ", "CL", "GC", "ZB"]
CRYPTO = ["BTC", "ETH"]

# Roots whose ticker also names a live, ordinarily-traded equity. A bare
# mention of one of these with no decisive wording is genuinely ambiguous --
# "Long ES, 30 days, 1 contract" could mean the E-mini future or a directional
# bet on Eversource stock -- and asking is the correct behaviour, not a
# guess in either direction. NQ, GC and ZB have no such collision in this
# dataset's symbol universe and are treated as unambiguous futures roots.
AMBIGUOUS_ROOTS = ["ES", "CL"]

# How an ambiguous root's EQUITY side is named, for the clarifying question
# and for the resolved equity-side answer. Deliberately separate from
# EQUITY_NAMES (whose keys are drawn from EQUITIES, not FUTURES) so the base
# extraction/modification/clarification generators -- which pick a symbol
# from EQUITIES or FUTURES, never both -- cannot accidentally produce an
# EQUITY-class row for "ES" or "CL" outside the dedicated ambiguous-root path.
AMBIGUOUS_EQUITY_NAMES = {
    "ES": ["eversource", "eversource energy", "es stock"],
    "CL": ["colgate", "colgate-palmolive", "cl stock"],
}

FUTURES_NAMES = {
    "ES": ["e-mini s&p", "es futures", "the s&p future", "emini", "spx futures",
           # Contract codes are decisive: naming a specific expiry cycle is
           # never something a trader does for a stock, so these need no
           # asset-class clarification even though the root is ambiguous.
           "esu26", "esz26", "esh27"],
    "NQ": ["nasdaq futures", "nq", "e-mini nasdaq", "the nasdaq future"],
    "CL": ["crude oil futures", "wti futures", "cl futures", "crude oil",
           "wti crude", "clz26", "clf27"],
    "GC": ["gold futures", "gc futures", "gold"],
    "ZB": ["30-year bond futures", "zb futures", "the long bond futures",
           "treasury bond futures"],
}

# Roots people ask for that this deployment still has no data for. They get a
# straight answer naming what IS available, rather than a params object for a
# symbol that will come back empty.
UNSUPPORTED_FUTURES = {
    "silver": "SI", "nat gas": "NG", "natural gas": "NG",
    "russell futures": "RTY", "dow futures": "YM", "ten year": "ZN",
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
    "covered_futures_call": ["covered futures call", "fop covered call",
                             "options on the futures", "futures option", "fop"],
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
    template = rng.choice(templates)

    # The template is chosen BEFORE the quantity, and a quantity other than 1 is
    # only drawn when the template can actually say it.
    #
    # Five of the eight templates have no {q}. Sampling qty first and labelling
    # it regardless -- which is what this did -- put a number in the answer that
    # appears nowhere in the question, so 5/8 * 0.4 = 25% of rows asked the model
    # to recover a digit that was never shown to it. Not a hard example: an
    # impossible one. It taught the model to guess on quantity, and the guessing
    # leaked into the other fields, since one wrong field fails the whole object.
    #
    # The measured cost was exactly the predicted 25%: quantity accuracy came in
    # at 74.7% on a run where the model had correctly learned to answer 1.
    if "{q}" in template:
        qty = rng.choices([1, 2, 3, 5, 10], [0.6, 0.15, 0.1, 0.1, 0.05])[0]
    else:
        qty = 1
    qty_txt = "" if qty == 1 else rng.choice([f"{qty} contracts ", f"{qty}x ", f"{qty} lots "])

    user = template.format(
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


def make_bare_futures_direction(rng: random.Random) -> dict:
    """
    Bare "long"/"short" + an UNAMBIGUOUS futures root -> the outright, direct.

    Production defect: "Long ES, 30 days, 1 contract." came back as
    strategy=long_put. Nothing in that sentence says "put" -- the model
    appears to treat the bare word "long" as evidence for long_call/long_put,
    presumably because those ids are the ones spelled "long_*". They are not
    the only strategies that start with a direction word: "long" and "short"
    on a futures root mean the outright futures position, and this dataset
    had no example teaching that reading at all -- every existing
    futures_long/futures_short alias said "futures" or "the future" outright
    (see ALIASES), never bare "Long {root}".

    Restricted to NQ, GC and ZB -- the futures roots with no equity-ticker
    collision -- deliberately. For ES and CL, "Long {root}" is genuinely
    ambiguous (see AMBIGUOUS_ROOTS) and the correct behaviour is to ask, which
    is make_clarification's job, not this one's: teaching this function to
    also answer confidently for ES/CL would just move the guessing problem
    rather than remove it.
    """
    root = rng.choice([r for r in FUTURES if r not in AMBIGUOUS_ROOTS])
    direction = rng.choice(["long", "short"])
    strat_id = "futures_long" if direction == "long" else "futures_short"
    sym_txt = phrase_symbol(rng, root, "FUTURES")
    dte_txt, dte = phrase_dte(rng)

    # Every template below states a quantity explicitly (unlike the bare
    # "go long {s} {d}" phrasing this generator used to also offer, which had
    # no {q} slot at all): sampling a quantity regardless of whether the
    # template can say it is the exact bug make_extraction's own comment
    # documents -- a label the utterance never stated, taught as if it were
    # observable. Keeping every template qty-bearing here sidesteps it rather
    # than re-deriving the two-step "pick template, then decide whether to
    # sample" dance for a generator this small.
    qty = rng.choices([1, 2, 3, 5, 10], [0.6, 0.15, 0.1, 0.1, 0.05])[0]
    qty_txt = "1 contract" if qty == 1 else rng.choice(
        [f"{qty} contracts", f"{qty} lots", f"{qty}x"])

    templates = [
        "{dir} {s}, {d}, {q}.",
        "{dir} {s}, {d} out, {q}.",
        "go {dir} {s} {d} for {q}.",
        "i want to go {dir} on {s}, {d}, {q}.",
        "{dir} {s} {d} for {q}.",
    ]
    user = rng.choice(templates).format(dir=direction, s=sym_txt, d=dte_txt, q=qty_txt)
    if rng.random() < 0.4:
        user = user[0].upper() + user[1:]

    obj = {"symbol": root, "asset_class": "FUTURES", "strategy": strat_id,
           "expiration_days": dte, "quantity": qty}
    return {"conversations": [
        {"role": "system", "content": SYSTEM},
        {"role": "user", "content": user},
        {"role": "assistant", "content": params_block(obj)},
    ]}


def make_futures_options_extraction(rng: random.Random) -> dict:
    """
    "Options on {root} futures" -> covered_futures_call.

    Production defect: "I want options on ES futures, 30 days out, 1
    contract." came back as strategy=futures_option, which is not a
    catalogue id -- the model was never taught this phrasing. The nearest
    (only) catalogue entry for an option written on a futures contract is
    covered_futures_call ("Covered Futures Call (FOP)", category Futures),
    so that is the target here.

    The phrase names the root AND says "futures" in the same breath, so this
    is decisive regardless of whether the root is otherwise ambiguous (ES,
    CL) -- unlike a bare "long ES", there is no asset-class question to ask
    when the utterance already states which asset class it means.
    """
    root = rng.choice(FUTURES)
    sym_txt = phrase_symbol(rng, root, "FUTURES")
    dte_txt, dte = phrase_dte(rng)
    qty = rng.choices([1, 2, 3, 5, 10], [0.6, 0.15, 0.1, 0.1, 0.05])[0]
    qty_txt = "1 contract" if qty == 1 else rng.choice([f"{qty} contracts", f"{qty} lots"])

    templates = [
        "I want options on {s} futures, {d} out, {q}.",
        "options on {s} futures, {d}, {q}",
        "set up options on the {s} future, {d} out, {q}.",
        "I'd like a futures option on {s}, {d}, {q}.",
        "price options on {s} futures for {d}, {q}.",
        "futures options on {s}, {d}, {q}",
    ]
    user = rng.choice(templates).format(s=sym_txt, d=dte_txt, q=qty_txt)

    obj = {"symbol": root, "asset_class": "FUTURES", "strategy": "covered_futures_call",
           "expiration_days": dte, "quantity": qty}
    return {"conversations": [
        {"role": "system", "content": SYSTEM},
        {"role": "user", "content": user},
        {"role": "assistant", "content": params_block(obj)},
    ]}


def make_share_purchase_refusal(rng: random.Random) -> dict:
    """
    Buying shares outright -> decline, don't invent an option strategy.

    Production defect: "Buy 100 shares of ES stock, Eversource, 30 days."
    came back as strategy=long_call, quantity=100. Buying shares is not a
    long call -- quantity=100 there is a share count, not a contract count,
    and folding it into long_call's `quantity` field would silently price
    100 CONTRACTS (10,000 deltas' worth of shares) for a request that named
    no option at all.

    This calculator prices options and futures positions, not outright stock
    purchases -- there is no catalogue id for "buy N shares" and inventing
    one (or coercing it into covered_call, which needs a share count the
    schema has no field for) would misrepresent what was asked. The correct
    behaviour is the same one make_refusal and make_unknown_strategy already
    teach: decline plainly and, since the trader clearly wants exposure to
    the named underlying, offer the two catalogue strategies that actually
    take a share position as a leg (covered_call, protective_put) as the
    nearest useful next step.
    """
    pool = list(EQUITIES) + AMBIGUOUS_ROOTS
    sym = rng.choice(pool)
    names = AMBIGUOUS_EQUITY_NAMES.get(sym) or EQUITY_NAMES.get(sym) or [sym.lower()]
    name_txt = rng.choice(names)
    n = rng.choice([10, 25, 50, 100, 200])
    dte_txt, _ = phrase_dte(rng)

    templates = [
        f"Buy {n} shares of {sym} stock, {dte_txt}.",
        f"Buy {n} shares of {sym} stock, {name_txt}, {dte_txt}.",
        f"I want to buy {n} shares of {name_txt}.",
        f"purchase {n} shares of {sym}",
        f"buy {n} shares of {name_txt}, {dte_txt}",
    ]
    user = rng.choice(templates)
    reply = rng.choice([
        f"This calculator prices options and futures positions, not outright share "
        f"purchases. If you want defined exposure to {sym}, I can set up a covered "
        f"call or a protective put instead — would either of those work?",
        "Buying shares outright isn't something this tool prices — it models options "
        "and futures positions. A covered call or protective put uses a share "
        "position as one leg, if that helps.",
        f"I can't price a straight stock purchase. If you want to express a view on "
        f"{sym} through the calculator, tell me an options strategy — covered call, "
        f"protective put, a spread — and I'll set it up.",
    ])
    return {"conversations": [
        {"role": "system", "content": SYSTEM},
        {"role": "user", "content": user},
        {"role": "assistant", "content": reply},
    ]}


def make_ambiguous_root_clarification(rng: random.Random) -> dict:
    """
    A bare, non-decisive mention of ES or CL -> ask futures-or-options first.

    This is the direct fix for the production defect where "Long ES, 30 days,
    1 contract." was answered with strategy=long_put instead of being asked
    about. ES and CL each name both a futures root and a live equity
    (Eversource Energy, Colgate-Palmolive); nothing in a bare "Long ES" or
    "calendar spread on CL" says which one is meant, so the model should ask
    -- exactly the four-turn shape make_clarification already uses elsewhere
    (user request, assistant question, user reply, assistant params), not a
    new shape. The reply vocabulary is "futures" or "options" because that is
    what the backend's own disambiguation prompt offers; it is deliberately
    never "stock" or "shares" here, and it is deliberately never something
    already-decisive like "e-mini" or a contract code -- those are handled by
    make_bare_futures_direction / the FUTURES_NAMES contract-code entries and
    must produce params with NO question, or the model learns to ask about
    utterances that already answered it.

    Two request shapes are covered:

      - direction ("long"/"short" + bare root): resolves to the futures
        outright on a "futures" reply. On an "options" reply the resolution
        is long_call for "long" and long_put for "short" -- the common
        retail reading of a bullish/bearish options view -- which is safe
        here specifically BECAUSE the trader has now confirmed "options",
        unlike the production defect where the model guessed a strategy
        without that confirmation at all.

      - calendar spread (root, no direction word): resolves to
        futures_calendar or calendar_spread, whichever asset class the reply
        names. No direction ambiguity here since the strategy id itself
        doesn't encode calls vs. puts.
    """
    root = rng.choice(AMBIGUOUS_ROOTS)
    equity_name = rng.choice(AMBIGUOUS_EQUITY_NAMES[root])
    pattern = rng.choice(["direction", "calendar"])
    dte_txt, dte = phrase_dte(rng)

    ask = rng.choice([
        f"Futures or options — are you trading the {root} futures contract, "
        f"or an options position on {equity_name}?",
        f"Do you mean {root} futures, or options on {equity_name}?",
        "Futures or options — which did you mean?",
    ])

    # As in make_extraction: pick the template FIRST, and only sample a
    # quantity other than 1 when that template can actually say it. Every
    # template below is qty-silent, so quantity is always 1 here -- this
    # branch is about the futures-vs-options question, not about size, and
    # inventing a stated-nowhere quantity is exactly the bug make_extraction's
    # own comment documents (25% quantity accuracy from doing this once).
    qty = 1

    if pattern == "direction":
        direction = rng.choice(["long", "short"])
        user = rng.choice([
            f"{direction.capitalize()} {root}, {dte_txt}.",
            f"{direction} {root} {dte_txt}",
            f"i want to go {direction} {root}, {dte_txt}",
        ])
        follow = rng.choice(["futures", "options"])
        if follow == "futures":
            strat = "futures_long" if direction == "long" else "futures_short"
            cls = "FUTURES"
        else:
            strat = "long_call" if direction == "long" else "long_put"
            cls = "EQUITY"
        obj = {"symbol": root, "asset_class": cls, "strategy": strat,
               "expiration_days": dte, "quantity": qty}
    else:
        user = rng.choice([
            f"calendar spread on {root}, {dte_txt}",
            f"set up a calendar spread on {root} for {dte_txt}",
            f"{root} calendar, {dte_txt}",
        ])
        follow = rng.choice(["futures", "options"])
        if follow == "futures":
            obj = {"symbol": root, "asset_class": "FUTURES", "strategy": "futures_calendar",
                   "expiration_days": dte, "quantity": qty}
        else:
            obj = {"symbol": root, "asset_class": "EQUITY", "strategy": "calendar_spread",
                   "expiration_days": dte, "quantity": qty}

    return {"conversations": [
        {"role": "system", "content": SYSTEM},
        {"role": "user", "content": user},
        {"role": "assistant", "content": ask},
        {"role": "user", "content": follow},
        {"role": "assistant", "content": params_block(obj)},
    ]}


def make_clarification(rng: random.Random, strategies: list[dict]) -> dict:
    """
    Missing information -> ONE question, then the params.

    Teaching the model to ask rather than to guess is the point. A confident
    default for an unstated expiry silently prices a position the user did not
    ask for, and on this site the number it produces looks exactly as
    authoritative as a correct one.

    Three things can be missing, and "asset_class" is the newest: ES and CL
    are both futures roots AND live equities (Eversource Energy,
    Colgate-Palmolive), so a bare, non-decisive mention of one of them is
    genuinely ambiguous between "the E-mini/WTI future" and "an options
    position on the stock" -- there is no default that is not a guess. This
    is the fix for the production defect where "Long ES, 30 days, 1
    contract." was read as evidence for long_put: the bare word "long" is not
    a strategy name, it is a direction, and on an ambiguous root the honest
    response is to ask which asset class before picking a strategy at all.
    The question and the reply vocabulary are deliberately futures-or-OPTIONS
    (never "stock" or "equity"), matching what the backend's own
    disambiguation prompt now offers -- training on a reply vocabulary the
    backend cannot produce would teach a resolution step that never fires.
    """
    s = rng.choice(strategies)
    is_fut = s["category"] == "Futures"
    cls = "FUTURES" if is_fut else "EQUITY"
    sym = rng.choice(FUTURES if is_fut else EQUITIES)
    name = rng.choice(ALIASES.get(s["id"], [s["name"].lower()]))
    missing = rng.choices(["dte", "symbol", "asset_class"], [0.42, 0.42, 0.16])[0]

    if missing == "asset_class":
        return make_ambiguous_root_clarification(rng)

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
             f"data provider covers ES (E-mini S&P), NQ (E-mini Nasdaq), CL "
             f"(crude oil), GC (gold) and ZB (30-year bond). Would one of "
             f"those work?")
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
    #
    # Four additions target production defects 1-3 (defect 4, the broadened
    # futures roots, and defect 5's asset-class disambiguation are folded
    # into the existing extraction/modification/clarification generators
    # instead of new ones -- see FUTURES and make_clarification above). Each
    # gets a small slice, not a large one: the distribution warning in the
    # brief is specific -- enough rows to learn the behaviour, not so many
    # that the model starts asking about, or second-guessing, symbols that
    # were never ambiguous in the first place.
    mix = [
        (0.50, make_extraction),
        (0.14, make_clarification),
        (0.12, make_modification),
        (0.05, make_refusal),
        (0.035, make_chitchat),
        (0.025, make_unknown_strategy),
        (0.02, make_unsupported_future),
        (0.035, make_bare_futures_direction),   # defect 1: bare long/short on an unambiguous root
        (0.025, make_futures_options_extraction),  # defect 2: "options on {root} futures"
        (0.02, make_share_purchase_refusal),    # defect 3: buying shares is not a strategy
    ]
    weights = [w for w, _ in mix]
    fns = [f for _, f in mix]

    # Functions taking (rng, strategies); everything else takes just rng.
    needs_strategies = (make_extraction, make_clarification, make_modification)

    rows, seen = [], set()
    attempts = 0
    while len(rows) < args.n and attempts < args.n * 40:
        attempts += 1
        fn = rng.choices(fns, weights)[0]
        row = fn(rng, strategies) if fn in needs_strategies else fn(rng)
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
