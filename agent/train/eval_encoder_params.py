#!/usr/bin/env python3
"""Score the encoder on PARAMS EXACT-MATCH, the metric the decoders are scored on.

WHY THIS FILE EXISTS. `train_encoder_min.py` reports ROW accuracy -- every head
simultaneously correct. That is the right training signal and the WRONG number
to compare against this repository's recorded figures, which are "the emitted
params JSON matches gold" (95.0% for the strategy assistant; raw 414/560 and
served 431 for mortgage v20). The two should coincide, because the label routing
is invertible, but "should" is not a measurement and quoting ROW against 95.0%
would be comparing two different denominators -- the exact mistake CLAUDE.md
records three times over.

So this renders the model's predictions back through the SERVING path and diffs
the result against gold:

    (operation class, per-literal (slot,map) pairs, convention classes)
        -> encoder_corpus.reconstruct(...)   # computes values in Decimal
        -> encoder_corpus.params_match(...)  # every gold field at gold's precision

`reconstruct` is the serving-side half of the design and the only place a number
is computed -- from a literal's TEXT and a map id, never emitted by the model.
Its `lit_scores` argument arbitrates a scalar two literals both claim, which is
exactly what the per-literal sigmoid probabilities are for, so they are passed
rather than thresholded away.

IT ALSO REPORTS THE ORACLE CEILING on the same rows, by reconstructing from the
GOLD labels. Without that, a miss is ambiguous: it could be the model picking
the wrong pair, or the routing being unable to express the row at all. The gap
between `model` and `oracle` is the model's share; the gap between `oracle` and
100% is the routing's.
"""
from __future__ import annotations

import argparse
import collections
import json
import sys
from pathlib import Path

import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import encoder_corpus as ec  # noqa: E402
import train_encoder_min as tm  # noqa: E402


def load(ckpt_path: Path):
    ck = torch.load(ckpt_path, map_location="cpu", weights_only=False)
    sch = ec.Schema.from_json(ck["schema"])
    a = ck["args"]
    tm.Block.use_sdpa = (a.get("attn", "sdpa") == "sdpa")
    model = tm.TinyEncoder(
        ck["vocab"], a["d_model"], a["layers"], a["heads"], a["ffn"],
        a["max_len"], len(sch.ops), len(sch.pairs), ck["conv_sizes"], drop=0.0)
    model.load_state_dict(ck["model"])
    model.eval()
    dtype = torch.bfloat16 if a.get("pure_bf16") else torch.float32
    if dtype is torch.bfloat16:
        model = model.to(dtype)
    from tokenizers import Tokenizer
    tok = Tokenizer.from_file(str(ckpt_path.with_suffix(".tokenizer.json")))
    return model, sch, tok, a, dtype


@torch.no_grad()
def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("checkpoint")
    ap.add_argument("--val", required=True)
    ap.add_argument("--op-key", default="operation")
    ap.add_argument("--question-mode", default="real")
    ap.add_argument("--thresh", type=float, default=0.5)
    ap.add_argument("--bs", type=int, default=32)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--show-misses", type=int, default=12)
    args = ap.parse_args()
    torch.set_num_threads(args.threads)

    ck = Path(args.checkpoint)
    model, sch, tok, targs, dtype = load(ck)
    n_par = sum(p.numel() for p in model.parameters())

    # Labels for the val split, built against the checkpoint's OWN schema. A
    # schema rebuilt from val would silently renumber the pairs and every
    # prediction would index the wrong slot.
    dias = ec.load_dialogues(Path(args.val), None)
    facts = [ec.make_facts(d, args.op_key, args.question_mode) for d in dias]
    exs = [ec.finish(f, ec.label_facts(f, sch), sch) for f in facts]

    max_lits = targs["max_lits"]
    max_len = targs["max_len"]

    m_ok = o_ok = n_gold = 0
    op_ok = 0
    by_op = collections.Counter()
    by_op_n = collections.Counter()
    bad_field = collections.Counter()
    misses = []

    for s in range(0, len(exs), args.bs):
        chunk = exs[s:s + args.bs]
        ids, pad, pool, _py, lit_ok, op_y, _cy, _o = tm.make_batch(
            chunk, tok, sch, max_len, max_lits, "cpu", dtype)
        op_l, pair_l, conv_l = model(ids, pad, pool)
        op_hat = op_l.float().argmax(-1)
        prob = pair_l.float().sigmoid()
        conv_hat = [c.float().argmax(-1) for c in conv_l]

        for i, ex in enumerate(chunk):
            if ex.gold is None:
                continue
            n_gold += 1
            op = sch.ops[ex.op]
            by_op_n[op] += 1
            op_ok += int(op_hat[i].item() == ex.op)

            lit_pairs, lit_scores = [], []
            for j in range(len(ex.lits)):
                if j >= max_lits or not bool(lit_ok[i, j]):
                    lit_pairs.append([])
                    lit_scores.append({})
                    continue
                row = prob[i, j]
                hits = (row > args.thresh).nonzero(as_tuple=True)[0].tolist()
                lit_pairs.append(hits)
                lit_scores.append({int(p): float(row[p]) for p in hits})

            conv = {f: int(conv_hat[ci][i].item())
                    for ci, f in enumerate(sch.conv_fields)}

            pred = ec.reconstruct(sch, int(op_hat[i].item()), ex.lits,
                                  lit_pairs, conv, lit_scores)
            ok, bad = ec.params_match(pred, ex.gold)
            m_ok += int(ok)
            if ok:
                by_op[op] += 1
            else:
                for b in bad:
                    bad_field[(op, b)] += 1
                if len(misses) < 4000:
                    misses.append((op, bad, ex.text[:130]))

            # oracle: same reconstruction, gold labels
            oracle = ec.reconstruct(sch, ex.op, ex.lits, ex.pairs, ex.conv)
            o_ok += int(ec.params_match(oracle, ex.gold)[0])

    g = max(n_gold, 1)
    print(f"checkpoint        : {ck.name}")
    print(f"val               : {args.val}")
    print(f"parameters        : {n_par:,} ({n_par/1e6:.2f}M)   dtype {dtype}")
    print(f"rows with gold    : {n_gold}")
    print()
    print(f"PARAMS EXACT-MATCH (model)  : {m_ok}/{n_gold} = {100*m_ok/g:.2f}%")
    print(f"PARAMS EXACT-MATCH (oracle) : {o_ok}/{n_gold} = {100*o_ok/g:.2f}%"
          "   <- ceiling of the label routing")
    print(f"operation class only        : {op_ok}/{n_gold} = {100*op_ok/g:.2f}%")
    print()
    print(f"gap attributable to the MODEL   : {100*(o_ok-m_ok)/g:.2f} points")
    print(f"gap attributable to the ROUTING : {100*(n_gold-o_ok)/g:.2f} points")
    print()
    print("--- per-operation params exact-match ---")
    for op, n in sorted(by_op_n.items(), key=lambda kv: -kv[1]):
        print(f"  {by_op[op]:>5}/{n:<5} {100*by_op[op]/n:6.2f}%  {op}")
    if bad_field:
        print()
        print("--- fields most often wrong ---")
        for (op, f), c in bad_field.most_common(20):
            print(f"  {c:>5}  {op}.{f}")
    if misses and args.show_misses:
        print()
        print(f"--- miss sample ({min(args.show_misses,len(misses))} of {len(misses)}) ---")
        for op, bad, text in misses[: args.show_misses]:
            print(f"  [{op}] wrong: {bad}")
            print(f"      {text}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
