#!/usr/bin/env python3
"""Compare encoder_reconstruct.cppm (C++) against reconstruct() (Python) row by row.

@author Olumuyiwa Oluwasanmi

WHY THIS GATE EXISTS
--------------------
The small-encoder assistants never generate digits. The model names an operation,
points at numeric spans in the user's own text, and names a MAP; every parameter
VALUE is then computed on the serving side by reconstruct(). So the entire
arithmetic surface of those assistants is one function, and C++ drifting from the
trainer's Python is a wrong answer that parses, satisfies every bound and prices a
different loan -- the dangerous-failure class this repository documents.

WHAT IS COMPARED, AND WHAT DELIBERATELY IS NOT
----------------------------------------------
NOT the decimal TEXT. `sensen::BigDecimal::to_string()` emits exactly 38 fractional
places; Python's `Decimal` carries the lexical scale of its operands, so the same
495000 is "495000" on one side and "495000.000...0" on the other. Pinning that text
would violate this repository's own rule -- docs/FINANCE_API.md tells callers to
parse money as decimal strings of UNSPECIFIED length and "do not pin the count" --
and a consumer test that pinned 18 places is what the 18->38 cutover broke.

So the comparison is: the same rows, the same KEY SET per row, and per key the same
VALUE -- numerically for decimals, exactly for everything else, elementwise for
arrays. That catches a wrong number, a dropped field, an invented field, a wrong
operation name, a wrong convention class and a wrong array length. It cannot catch
a formatting difference, which is the serving layer's job and not this module's.

USAGE
-----
  check_encoder_reconstruct_parity.py <python.ndjson> <cpp.ndjson> [--expect-rows N]
  check_encoder_reconstruct_parity.py --self-test
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from decimal import Decimal, InvalidOperation, localcontext
from pathlib import Path


def _as_decimal(text: str) -> Decimal | None:
    """Decimal(text) or None if the string is not a decimal literal."""
    try:
        return Decimal(text)
    except (InvalidOperation, ValueError, ArithmeticError):
        return None


# One unit in the last place of sensen::BigDecimal's scale. BigDecimal is Int256
# scaled by 10^38, so 10^-38 is the smallest value it can represent at all -- this
# is DERIVED from the storage type, not a tolerance chosen to make a run pass.
BIGDECIMAL_ULP = Decimal(1).scaleb(-38)

# Counted so the report can say how much of the agreement is EXACT. A bound that
# swallows every comparison is indistinguishable from no comparison.
_stats: Counter = Counter()


def value_eq(a: object, b: object) -> tuple[bool, str]:
    """Compare one reconstructed value. Returns (equal, reason-when-not)."""
    if a is None and b is None:
        return True, ""
    if (a is None) != (b is None):
        return False, f"one side is null: python={a!r} cpp={b!r}"
    # bool must be checked before anything else: bool is a subclass of int.
    if isinstance(a, bool) or isinstance(b, bool):
        if not (isinstance(a, bool) and isinstance(b, bool)):
            return False, f"boolean against non-boolean: python={a!r} cpp={b!r}"
        return (a == b), "" if a == b else f"python={a!r} cpp={b!r}"
    if isinstance(a, list) or isinstance(b, list):
        if not (isinstance(a, list) and isinstance(b, list)):
            return False, f"array against non-array: python={type(a).__name__} cpp={type(b).__name__}"
        if len(a) != len(b):
            return False, f"array length {len(a)} against {len(b)}"
        for i, (x, y) in enumerate(zip(a, b)):
            ok, why = value_eq(x, y)
            if not ok:
                return False, f"element {i}: {why}"
        return True, ""
    if isinstance(a, str) and isinstance(b, str):
        da, db = _as_decimal(a), _as_decimal(b)
        if da is not None and db is not None:
            # BOTH are decimal literals: compare the NUMBER, not its scale.
            if da == db:
                _stats["exact"] += 1
                return True, ""
            # A NON-TERMINATING division is the one place the two CANNOT agree
            # digit for digit: Python's reconstruct() runs at getcontext().prec =
            # 60 while BigDecimal is fixed at 38 places and TRUNCATES, so an
            # annual rate over 12 differs in the tail and nowhere else. Admitted
            # only within ONE ULP of BigDecimal's own scale -- 10^-38, derived
            # from Int256/10^38 and not fitted to this run. Ten ulps still fails,
            # which is what keeps this a comparison: the smallest defect that
            # could matter here is a wrong map, a factor of 12.
            with localcontext() as ctx:
                ctx.prec = 120
                residual = abs(da - db)
            if residual <= BIGDECIMAL_ULP:
                _stats["within_one_ulp"] += 1

                return True, ""
            return False, f"python={a} cpp={b} (residual {residual:.3e} > 1 ulp {BIGDECIMAL_ULP:.0e})"
        if (da is None) != (db is None):
            return False, f"one side is a decimal and the other is not: python={a!r} cpp={b!r}"
        return (a == b), "" if a == b else f"python={a!r} cpp={b!r}"
    # Anything else (a bare JSON number, an object) is not a shape reconstruct emits.
    return False, f"unexpected value shapes: python={type(a).__name__} cpp={type(b).__name__}"


def load_ndjson(path: Path) -> dict[int, dict]:
    rows: dict[int, dict] = {}
    with path.open() as fh:
        for lineno, line in enumerate(fh, 1):
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"{path}:{lineno}: not JSON: {exc}") from exc
            if "row" not in obj:
                raise SystemExit(f"{path}:{lineno}: no 'row' key")
            rid = int(obj["row"])
            if rid in rows:
                raise SystemExit(f"{path}:{lineno}: duplicate row id {rid}")
            rows[rid] = obj
    return rows


def compare(py: dict[int, dict], cpp: dict[int, dict]) -> tuple[int, list[str], Counter]:
    """Returns (rows_differing, messages, per-field disagreement counter)."""
    msgs: list[str] = []
    by_field: Counter = Counter()
    differing = 0

    only_py = sorted(set(py) - set(cpp))
    only_cpp = sorted(set(cpp) - set(py))
    if only_py:
        msgs.append(f"rows present only in python: {only_py[:10]}{'...' if len(only_py) > 10 else ''}")
    if only_cpp:
        msgs.append(f"rows present only in cpp: {only_cpp[:10]}{'...' if len(only_cpp) > 10 else ''}")
    differing += len(only_py) + len(only_cpp)

    for rid in sorted(set(py) & set(cpp)):
        p, c = py[rid], cpp[rid]
        row_msgs: list[str] = []

        # An error on either side is a disagreement in itself: Python's oracle run
        # succeeded on all 600 rows, so a C++ error row is a C++ failure.
        if "error" in p or "error" in c:
            row_msgs.append(f"error reported: python={p.get('error')!r} cpp={c.get('error')!r}")
            by_field["<error>"] += 1
        else:
            pp, cc = p.get("params"), c.get("params")
            if pp is None or cc is None:
                if pp is not cc and not (pp is None and cc is None):
                    row_msgs.append(f"params null on one side only: python={pp is None} cpp={cc is None}")
                    by_field["<null-params>"] += 1
            else:
                missing = sorted(set(pp) - set(cc))
                extra = sorted(set(cc) - set(pp))
                for k in missing:
                    row_msgs.append(f"key missing from cpp: {k}")
                    by_field[k] += 1
                for k in extra:
                    row_msgs.append(f"key invented by cpp: {k}")
                    by_field[k] += 1
                for k in sorted(set(pp) & set(cc)):
                    ok, why = value_eq(pp[k], cc[k])
                    if not ok:
                        row_msgs.append(f"{k}: {why}")
                        by_field[k] += 1

        if row_msgs:
            differing += 1
            if len(msgs) < 400:
                msgs.append(f"row {rid}: " + "; ".join(row_msgs[:6]))
    return differing, msgs, by_field


# --------------------------------------------------------------------------- #
# Self-test: a comparator that cannot FAIL is not a comparator.
# --------------------------------------------------------------------------- #
def self_test() -> int:
    checks = 0
    failures = 0

    def check(name: str, cond: bool) -> None:
        nonlocal checks, failures
        checks += 1
        if not cond:
            failures += 1
            print(f"  FAIL  {name}")
        else:
            print(f"  ok    {name}")

    base = {
        0: {"row": 0, "params": {"operation": "ComputePayment", "present_value": "495000",
                                 "rate": "0.005625", "periods": "360", "discounted": True,
                                 "values": ["-1000", "500"]}},
    }

    def at38(x: str) -> str:
        # BigDecimal::to_string()'s exact shape: 38 fractional places. Needs a
        # precision context wide enough for 44 significant digits; the default 28
        # raises InvalidOperation on quantize, which is how this helper first failed.
        with localcontext() as ctx:
            ctx.prec = 80
            return str(Decimal(x).quantize(Decimal(1).scaleb(-38)))

    def cpp_from(mutate=None) -> dict:
        import copy
        d = copy.deepcopy(base)
        # the honest C++ rendering: 38 places on every decimal
        for k, v in list(d[0]["params"].items()):
            if isinstance(v, str) and _as_decimal(v) is not None:
                d[0]["params"][k] = at38(v)
            elif isinstance(v, list):
                d[0]["params"][k] = [at38(x) for x in v]
        if mutate:
            mutate(d[0]["params"])
        return d

    # The control that matters: 38 places must NOT be a disagreement.
    n, msgs, _ = compare(base, cpp_from())
    check("identical values at 38 places agree (the whole premise)", n == 0 and not msgs)

    # And the comparator must still be able to fail, in every direction.
    n, _, _ = compare(base, cpp_from(lambda p: p.update(present_value=at38("495000.01"))))
    check("a one-cent wrong money value is caught", n == 1)

    n, _, _ = compare(base, cpp_from(lambda p: p.pop("rate")))
    check("a dropped field is caught", n == 1)

    n, _, _ = compare(base, cpp_from(lambda p: p.update(invented="1")))
    check("an invented field is caught", n == 1)

    n, _, _ = compare(base, cpp_from(lambda p: p.update(operation="ComputeAmortization")))
    check("a wrong operation name is caught", n == 1)

    n, _, _ = compare(base, cpp_from(lambda p: p.update(discounted=False)))
    check("a flipped convention boolean is caught", n == 1)

    n, _, _ = compare(base, cpp_from(lambda p: p.update(discounted="true")))
    check("a boolean rendered as a string is caught", n == 1)

    n, _, _ = compare(base, cpp_from(lambda p: p["values"].pop()))
    check("a short array is caught", n == 1)

    n, _, _ = compare(base, cpp_from(lambda p: p["values"].__setitem__(0, "-1000.5")))
    check("a wrong array element is caught", n == 1)

    n, _, _ = compare(base, {})
    check("a missing row is caught", n == 1)

    # periods "360" against "360.0" is the SAME NUMBER and must pass; this is the
    # rule that makes the gate about values rather than about formatting.
    n, _, _ = compare(base, cpp_from(lambda p: p.update(periods="360.0")))
    check("360 against 360.0 agrees (scale is not pinned)", n == 0)

    # The ulp bound exists for a non-terminating division's tail and must not
    # reach one digit further than that.
    n, _, _ = compare(base, cpp_from(lambda p: p.update(rate=at38("0.005625") [:-1] + "1")))
    check("a difference in the 38th place alone is admitted (one ulp)", n == 0)

    with localcontext() as ctx:
        ctx.prec = 120
        ten_ulps = str(Decimal(at38("0.005625")) + Decimal(1).scaleb(-37))
    n, _, _ = compare(base, cpp_from(lambda p: p.update(rate=ten_ulps)))
    check("ten ulps (1e-37) still FAILS -- the bound is one ulp, not 'small'", n == 1)

    print(f"\nself-test: {checks - failures} passed / {failures} failed")
    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("python_ndjson", nargs="?")
    ap.add_argument("cpp_ndjson", nargs="?")
    ap.add_argument("--expect-rows", type=int, default=0,
                    help="REFUSE unless both sides carry exactly this many rows. "
                         "A gate that compares zero rows reports a pass.")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    if not args.python_ndjson or not args.cpp_ndjson:
        ap.error("both python.ndjson and cpp.ndjson are required")

    py = load_ndjson(Path(args.python_ndjson))
    cpp = load_ndjson(Path(args.cpp_ndjson))

    # POSITIVE CONTROL on the count. "0 rows differing" over 0 rows is the shape of
    # pass this session already produced once (a corpus gate reported "0 changed"
    # having lexed 0 segments, because the JSONL key was `conversations`).
    if not py or not cpp:
        print(f"REFUSED: python rows={len(py)} cpp rows={len(cpp)} -- nothing to compare")
        return 2
    if args.expect_rows and (len(py) != args.expect_rows or len(cpp) != args.expect_rows):
        print(f"REFUSED: expected {args.expect_rows} rows, got python={len(py)} cpp={len(cpp)}")
        return 2

    differing, msgs, by_field = compare(py, cpp)

    print(f"rows: python={len(py)} cpp={len(cpp)}")
    print(f"rows differing: {differing}")
    exact = _stats["exact"]
    ulp = _stats["within_one_ulp"]
    print(f"decimal values compared: {exact + ulp}  "
          f"({exact} EXACT, {ulp} equal within one BigDecimal ulp of 1e-38)")
    if ulp and exact == 0:
        print("REFUSED: every decimal needed the ulp bound -- that is not a comparison")
        return 2
    if by_field:
        print("\ndisagreements by field:")
        for k, v in by_field.most_common(40):
            print(f"  {v:5d}  {k}")
    if msgs:
        print("\nfirst disagreements:")
        for m in msgs[:40]:
            print(f"  {m}")

    if differing == 0:
        print(f"\nPARITY: C++ encoder_reconstruct matches Python reconstruct on {len(py)}/{len(py)} rows")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
