#!/usr/bin/env python3
"""Diff two `eval_grpc_mortgage.py --json-out` runs ROW BY ROW.

A totals line ("served 431 against 435") says how many rows moved and nothing
about which, and the two directions cancel: this repository already records a
19-row served regression that `raw_exact` could not see because the row set was
byte-identical in aggregate. Pair the rows and the question becomes answerable --
which operations, which outcomes, and whether anything moved in BOTH directions.

Usage: diff_eval_rows.py <a.json> <b.json>
"""
import collections
import json
import sys


def load(path: str) -> tuple[dict, dict]:
    d = json.load(open(path))
    rows = {r["row"]: r for r in d["row_verdicts"]}
    return d, rows


def main() -> None:
    a_doc, a = load(sys.argv[1])
    b_doc, b = load(sys.argv[2])
    for doc, name in ((a_doc, sys.argv[1]), (b_doc, sys.argv[2])):
        print(f'{name}: label={doc["label"]} raw={doc["raw_exact"]}/{doc["raw_total"]} '
              f'served={doc["served_exact"]} asked={doc["asked_ok"]}/{doc["asked_total"]} '
              f'answered={doc["answered_ok"]}/{doc["answered_total"]} errors={doc["errors"]}')
    print(f'holdout sha equal: {a_doc["holdout"] == b_doc["holdout"]}')

    common = sorted(set(a) & set(b))
    print(f'\npaired rows: {len(common)} (a-only {len(set(a)-set(b))}, b-only {len(set(b)-set(a))})')

    for field in ("raw_exact", "served_exact", "outcome"):
        moved = [i for i in common if a[i][field] != b[i][field]]
        print(f'\n=== {field}: {len(moved)} rows differ ===')
        by_op = collections.Counter(a[i]["operation"] for i in moved)
        for op, n in by_op.most_common():
            print(f'  {op:34s} {n}')
        for i in moved[:25]:
            print(f'  row {i:4d} {a[i]["operation"]:30s} '
                  f'A={a[i][field]!s:14s} B={b[i][field]!s:14s}')
            if field == "outcome":
                print(f'           A refusal: {a[i]["refusal_reason"]!r} {a[i]["refusal_message"][:70]!r}')
                print(f'           B refusal: {b[i]["refusal_reason"]!r} {b[i]["refusal_message"][:70]!r}')
                print(f'           utterance: {a[i]["utterance"][:100]!r}')
        if len(moved) > 25:
            print(f'  ... and {len(moved) - 25} more')


if __name__ == "__main__":
    main()
