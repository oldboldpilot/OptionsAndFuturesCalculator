"""How many label-precision matches are AMBIGUOUS (several maps fit)?

A rounding-consistent match is real, but if six maps all round to the same
6-dp label then attributing the value to one of them is arbitrary, and the
EXTRACTED count built on it is inflated. Count: unique-map golds vs multi-map
golds, and for the rate family, which cadence wins on MINIMUM error.
"""
import collections, sys
sys.path.insert(0, __import__("os").path.dirname(__file__))
from extractability import (lex, UNARY, iter_rows, row_parts, flatten,
                            at_label_precision)
from decimal import Decimal, InvalidOperation

uniq = multi = exact_n = 0
amb_hist = collections.Counter()
rate_win = collections.Counter()
ratelike = ('rate',)

for _, row in iter_rows(sys.argv[1]):
    utt, gold = row_parts(row)
    if not gold: continue
    lits = lex(utt)
    for field, val in flatten(gold):
        exact = set(); approx = {}
        for lit in lits:
            for name, pred, fn in UNARY:
                if not pred(lit.tag): continue
                try: got = fn(lit.value)
                except (InvalidOperation, ZeroDivisionError, OverflowError): continue
                if got == val: exact.add(name)
                elif at_label_precision(got, val):
                    err = abs(got - val)
                    if name not in approx or err < approx[name]: approx[name] = err
        if exact:
            exact_n += 1; continue
        if not approx: continue
        amb_hist[len(approx)] += 1
        if len(approx) == 1: uniq += 1
        else: multi += 1
        if field in ratelike:
            best = min(approx.items(), key=lambda kv: kv[1])[0]
            rate_win[best] += 1

print(f"exact matches                  : {exact_n}")
print(f"label-precision, ONE map fits  : {uniq}")
print(f"label-precision, SEVERAL fit   : {multi}")
print()
print("how many maps fit, histogram:")
for k, c in sorted(amb_hist.items()): print(f"   {k} map(s): {c}")
print()
print("for field 'rate', cadence winning on MINIMUM error:")
for k, c in rate_win.most_common(): print(f"   {c:>6}  {k}")
