#!/usr/bin/env python3
"""Measure the SINGLE-LABEL ceiling directly: how many rows contain a literal
that fills more than one (slot, map) pair.
@author Olumuyiwa Oluwasanmi
"""
import sys, collections
sys.path.insert(0, ".")
import encoder_corpus as C

def run(tag, train, val, op_key):
    _, _, sch, xtr, xva, _, _ = C.prepare(train, val, op_key, "real", 20, 8, None)
    slots = sorted({s for s, _ in sch.pairs}); maps = sorted({m for _, m in sch.pairs})
    for name, xs in (("val", xva), ("train", xtr)):
        multi_rows = multi_lits = tot_lits = 0
        hist = collections.Counter()
        for e in xs:
            bad = False
            for ps in e.pairs:
                tot_lits += 1; hist[len(ps)] += 1
                if len(ps) > 1: multi_lits += 1; bad = True
            if bad: multi_rows += 1
        n = len(xs)
        if name == "val":
            print(f"==== {tag} ====")
            print(f"  pairs {sch.n_pairs}  slots {len(slots)}  maps {len(maps)}"
                  f"  slots*maps={len(slots)*len(maps)}"
                  f"  {'BIJECTIVE' if sch.n_pairs == len(slots)*len(maps) else 'NOT bijective'}")
        print(f"  [{name}] rows {n}  literals {tot_lits}  hist {dict(sorted(hist.items()))}")
        print(f"  [{name}] literals filling >1 pair {multi_lits};  rows affected {multi_rows}/{n}"
              f" = {100*multi_rows/max(1,n):.2f}%   SINGLE-LABEL CEILING"
              f" {(n-multi_rows)}/{n} = {100*(n-multi_rows)/max(1,n):.2f}%")
    print()

from pathlib import Path
run("MORTGAGE", Path("../dataset/data_mortgage/train.jsonl"), Path("../dataset/data_mortgage/val.jsonl"), "operation")
run("STRATEGY", Path("../dataset/data/train.jsonl"), Path("../dataset/data/val.jsonl"), "strategy_type")
