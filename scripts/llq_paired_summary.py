#!/usr/bin/env python3
"""Summarise an N-arm paired LLQ probe sweep, and REFUSE a table that is not a
valid comparison.

Two gates run before any rate is printed as meaningful, and both exist because
this sweep has already produced a confident wrong answer without them:

  * EQUAL SLICE COUNTS ACROSS ARMS. Identical generated tokens with a different
    number of kernel calls is a different amount of WORK, not a different weight
    store. That is exactly how a QKV-fusion mismatch was caught -- byte-identical
    output, 170,072 calls against 198,744 -- after the rate alone had been read as
    "1.8x slower" and believed.
  * ONE TOKEN SHA PER ARM, AND THE SAME ONE ACROSS ARMS. An arm whose sha moves
    between rounds is not reproducible, and arms whose shas differ are not
    computing the same thing.

Usage: llq_paired_summary.py <outdir> <rounds> "<arm> <arm> ..."
"""
import json
import pathlib
import statistics
import sys


def main() -> int:
    out = pathlib.Path(sys.argv[1])
    rounds = int(sys.argv[2])
    arms = sys.argv[3].split()

    rows: dict[tuple[str, int], dict] = {}
    for r in range(1, rounds + 1):
        for arm in arms:
            p = out / f"probe_{arm}_r{r}.json"
            rows[(arm, r)] = json.loads(p.read_text()[len("RESULT "):])

    print(f'{"round":>5}' + "".join(f'{a + " tok/s":>17}' for a in arms))
    for r in range(1, rounds + 1):
        print(f'{r:>5}' + "".join(f'{rows[(a, r)]["decode_tok_s"]:>17.2f}' for a in arms))
    print(f'{"prefill":>5}' + "".join(f'{rows[(a, 1)]["prefill_tok_s"]:>17.1f}' for a in arms))

    base = arms[0]
    base_med = statistics.median(rows[(base, r)]["decode_tok_s"] for r in range(1, rounds + 1))
    print()
    for a in arms:
        med = statistics.median(rows[(a, r)]["decode_tok_s"] for r in range(1, rounds + 1))
        lo = min(rows[(a, r)]["decode_tok_s"] for r in range(1, rounds + 1))
        hi = max(rows[(a, r)]["decode_tok_s"] for r in range(1, rounds + 1))
        print(f'{a:>10}: median {med:7.2f} tok/s  spread {lo:.2f}..{hi:.2f}  '
              f'ratio vs {base} {med / base_med:6.3f}')

    counts = {a: rows[(a, 1)]["coverage_source_calls"] + rows[(a, 1)]["coverage_dense_calls"]
              for a in arms}
    equal = len(set(counts.values())) == 1
    print()
    print(f'slice calls per arm (source+dense): {counts}')
    print(f'SLICE COUNTS EQUAL ACROSS ARMS: {equal}')
    for a in arms:
        row = rows[(a, 1)]
        print(f'  {a:>10}: source={row["coverage_source_calls"]} dense={row["coverage_dense_calls"]}'
              f' llq_used={row["llq_used"]} qkv_fusion={row["qkv_fusion"]}')

    shas = {a: {rows[(a, r)]["token_sha"] for r in range(1, rounds + 1)} for a in arms}
    for a, sset in shas.items():
        print(f'{a}: {len(sset)} distinct token sha over {rounds} rounds: '
              f'{sorted(x[:16] for x in sset)}')
    stable = all(len(v) == 1 for v in shas.values())
    identical = stable and len({next(iter(v)) for v in shas.values()}) == 1
    print(f'BYTE IDENTITY across all arms: {identical}')

    for a in arms:
        if a == "dense":
            continue
        one = rows[(a, 1)]
        print(f'{a} image: kind={one["llq_adopted_kind"]} adopted={one["llq_weights_adopted"]} '
              f'{one["llq_source_bits_per_weight"]:.4f} -> {one["llq_bits_per_weight"]:.4f} '
              f'bits/weight ({one["dense_bytes_of_adopted"]} B -> {one["llq_bytes"]} B), '
              f'outliers={one["llq_outliers"]} base={one["llq_base_bits"]}')

    return 0 if (identical and equal) else 4


if __name__ == "__main__":
    sys.exit(main())
