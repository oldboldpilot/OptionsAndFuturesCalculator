#!/usr/bin/env python3
"""Compare the DEPLOYED numeric lexer against the encoder trainer's lex().

@author Olumuyiwa Oluwasanmi

The encoder's per-literal pair head is indexed BY LITERAL POSITION, so the engine's
literal list must agree with the trainer's index for index or the model's prediction
is applied to a different number. Three things are load-bearing -- ORDER/COUNT, SPAN
and VALUE -- and the TAG is not, which is measured rather than assumed (see
docs/evidence/encoder-reconstruct-parity: rewriting all 3,381 tags to "bare" leaves
both languages byte-identical). The tag is still reported, because a divergence in it
is worth knowing even where it is not worth refusing.

Two representation differences are CONVERSIONS, not disagreements:
  - the trainer bakes a k/m suffix into the value; NumericLiteral keeps `value` before
    the multiplier and `scale` beside it, so the comparison multiplies;
  - LiteralTag has six values against the trainer's eight (`weeks`/`quarters` absent).

USAGE
  check_encoder_lexer_parity.py <fixture.json> <cpp.ndjson>
  check_encoder_lexer_parity.py --self-test
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from decimal import Decimal
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "agent" / "train"))

# LiteralTag's enumerator order in backend/src/modules/mortgage_verification.cppm.
# Written as the enum's own order rather than a lookup built from strings, because the
# probe emits the integer and a reordered enum must show up as a mismatch here.
CPP_TAG = ["bare", "money", "percent", "years", "months", "days"]


def compare(texts: list[str], cpp_rows: list[dict]) -> tuple[int, list[str], Counter]:
    import encoder_corpus as ec

    msgs: list[str] = []
    stats: Counter = Counter()
    differing = 0

    for i, text in enumerate(texts):
        if i >= len(cpp_rows):
            differing += 1
            msgs.append(f"row {i}: missing from the C++ output")
            continue
        py = ec.lex(text)
        cl = cpp_rows[i]["lits"]
        row_msgs: list[str] = []

        if len(py) != len(cl):
            # COUNT first and alone: with a different count every index after the
            # divergence is meaningless, so reporting per-literal diffs too would bury
            # the one fact that matters.
            row_msgs.append(f"literal COUNT differs: python {len(py)} cpp {len(cl)} "
                            f"(python {[p.text for p in py]}, cpp {[q['text'] for q in cl]})")
            stats["count"] += 1
        else:
            stats["count_ok"] += 1
            for k, (p, q) in enumerate(zip(py, cl)):
                if p.start != q["offset"]:
                    row_msgs.append(f"literal {k} SPAN: python {p.start} cpp {q['offset']}")
                    stats["span"] += 1
                # The suffix multiplier is carried separately on the C++ side.
                value = Decimal(q["text"].replace(",", "")) * q["scale"]
                if p.value != value:
                    row_msgs.append(f"literal {k} VALUE: python {p.value} cpp {value} "
                                    f"(text {q['text']!r} scale {q['scale']})")
                    stats["value"] += 1
                tag = CPP_TAG[q["tag"]] if 0 <= q["tag"] < len(CPP_TAG) else f"?{q['tag']}"
                if tag != p.tag:
                    stats["tag"] += 1
                    stats[f"tag:{p.tag}->{tag}"] += 1

        if row_msgs:
            differing += 1
            if len(msgs) < 200:
                msgs.append(f"row {i}: " + "; ".join(row_msgs[:4]))
    return differing, msgs, stats


def self_test() -> int:
    import encoder_corpus as ec

    checks = 0
    failures = 0

    def check(name: str, cond: bool) -> None:
        nonlocal checks, failures
        checks += 1
        print(("  ok    " if cond else "  FAIL  ") + name)
        if not cond:
            failures += 1

    text = "a $495,000 loan at 6.5% over 30 years"
    py = ec.lex(text)
    good = [{"text": p.text.lstrip("$").rstrip("%").strip(), "offset": p.start, "scale": 1,
             "tag": CPP_TAG.index(p.tag)} for p in py]
    n, msgs, _ = compare([text], [{"row": 0, "lits": good}])
    check("an exact C++ reproduction agrees", n == 0 and not msgs)

    bad = [dict(x) for x in good]
    bad[0]["offset"] += 1
    n, _, _ = compare([text], [{"row": 0, "lits": bad}])
    check("a one-character SPAN shift is caught", n == 1)

    bad = [dict(x) for x in good]
    bad[0]["text"] = "494000"
    n, _, _ = compare([text], [{"row": 0, "lits": bad}])
    check("a wrong VALUE is caught", n == 1)

    n, _, _ = compare([text], [{"row": 0, "lits": good[:-1]}])
    check("a dropped literal is caught", n == 1)

    n, _, _ = compare([text], [{"row": 0, "lits": good + [dict(good[0])]}])
    check("an extra literal is caught", n == 1)

    # The suffix conversion must be applied, not assumed away: "$250k" is 250000 to the
    # trainer and (250, scale 1000) to the deployed lexer. If the comparison forgot to
    # multiply, this would read as a value mismatch.
    t2 = "a $250k deposit"
    py2 = ec.lex(t2)
    cl2 = [{"text": "250", "offset": py2[0].start, "scale": 1000, "tag": CPP_TAG.index("money")}]
    n, _, _ = compare([t2], [{"row": 0, "lits": cl2}])
    check("value * scale is applied, so $250k agrees across the two representations", n == 0)

    n, _, _ = compare([t2], [{"row": 0, "lits": [{**cl2[0], "scale": 1}]}])
    check("a DROPPED scale is caught (250 against 250000)", n == 1)

    # A tag divergence is counted and must NOT fail the gate, because it is provably
    # unused at serving time. Asserting that explicitly stops a later reader from
    # 'tightening' it and turning weeks/quarters into a refusal.
    n, _, stats = compare([text], [{"row": 0, "lits": [{**good[0], "tag": 0}, *good[1:]]}])
    check("a TAG divergence is COUNTED but does not fail the gate",
          n == 0 and stats["tag"] == 1)

    n, _, _ = compare([text], [])
    check("a missing row is caught", n == 1)

    print(f"\nself-test: {checks - failures} passed / {failures} failed")
    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fixture", nargs="?")
    ap.add_argument("cpp_ndjson", nargs="?")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if not (args.fixture and args.cpp_ndjson):
        ap.error("fixture.json and cpp.ndjson are both required")

    texts = [r["text"] for r in json.loads(Path(args.fixture).read_text())]
    cpp_rows = [json.loads(l) for l in Path(args.cpp_ndjson).read_text().splitlines() if l.strip()]
    if not texts or not cpp_rows:
        print(f"REFUSED: {len(texts)} utterances and {len(cpp_rows)} C++ rows -- nothing compared")
        return 2

    differing, msgs, stats = compare(texts, cpp_rows)
    total_lits = sum(len(r["lits"]) for r in cpp_rows)

    print(f"utterances: {len(texts)}   literals: {total_lits}")
    print(f"rows differing on ORDER/COUNT, SPAN or VALUE: {differing}")
    print(f"  literal counts matching: {stats['count_ok']}   "
          f"span mismatches: {stats['span']}   value mismatches: {stats['value']}")
    print(f"  TAG divergences (counted, NOT a failure -- provably unused at serving "
          f"time): {stats['tag']}")
    for k, v in sorted(stats.items()):
        if k.startswith("tag:"):
            print(f"    {k[4:]}  {v}")
    if msgs:
        print("\nfirst disagreements:")
        for m in msgs[:25]:
            print(f"  {m}")

    if differing == 0:
        print(f"\nPARITY: the deployed lexer finds the same literals as the trainer's on "
              f"{len(texts)}/{len(texts)} utterances ({total_lits} literals: order, span, value)")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
