#!/usr/bin/env python3
"""MULTI-LABEL vs SINGLE-LABEL decode, same weights, same holdout.

This is the error rate the head redesign is worth, measured rather than derived from the
schema. The single-label arm is TOP-1 PER LITERAL within the operation mask -- the BEST a
pair of independent single-label argmax heads could do -- so the gap it shows is a LOWER
BOUND on what the old serving contract cost.

@author Olumuyiwa Oluwasanmi
"""
import collections, sys, torch
sys.path.insert(0, ".")
import encoder_corpus as C
import train_encoder as T

ck_path = sys.argv[1]
model, cfg, sch, tok, meta = T.load_checkpoint(__import__("pathlib").Path(ck_path))
model.eval()
masks = T.make_masks(sch)
thr = float(meta["args"]["threshold"])

fva = [C.make_facts(d, sch.op_key, sch.question_mode) for d in C.load_dialogues(__import__("pathlib").Path(meta["args"]["val"]))]
xva = C.build_examples([f for f in fva if f.op is None or f.op in sch.op_index()], sch)
d = T.make_data(xva, tok, sch, int(meta["args"]["max_len"]))
pred = T.predict(model, d, sch, bf16=False)
op_pred = pred["op"].argmax(1)

def decode(ex, r, op, single):
    if op == 0:
        return None
    nl = len(ex.lits)
    prob = torch.sigmoid(pred["pair"][r, :nl])
    allowed = masks.pair[op]
    if single:
        # exactly one pair per literal: the argmax inside the mask. A literal whose best
        # admissible probability is below the threshold selects nothing, as multi-label would.
        masked = prob.masked_fill(~allowed[None, :], -1.0)
        best = masked.argmax(1)
        lit_pairs = [([int(best[i])] if float(masked[i, best[i]]) > thr else []) for i in range(nl)]
    else:
        sel = (prob > thr) & allowed[None, :]
        lit_pairs = [torch.nonzero(sel[i]).flatten().tolist() for i in range(nl)]
    scores = [{p: float(prob[i, p]) for p in lit_pairs[i]} for i in range(nl)]
    conv = {}
    for name in sch.conv_fields:
        m = masks.conv[name][op]
        if m.any():
            conv[name] = int(pred["conv"][name][r].masked_fill(~m, T.NEG).argmax())
    return C.reconstruct(sch, op, ex.lits, lit_pairs, conv, scores)

rows = {False: 0, True: 0}
params_rows = {False: 0, True: 0}
n = n_params = 0
lost_by_op = collections.Counter()
multi_rows = 0
for r, ex in enumerate(d.examples):
    n += 1
    op = int(op_pred[r])
    has_multi = any(len(ps) > 1 for ps in ex.pairs)
    multi_rows += has_multi
    res = {}
    for single in (False, True):
        got = decode(ex, r, op, single)
        ok = C.params_match(got, ex.gold)[0]
        rows[single] += ok
        res[single] = ok
    if ex.gold is not None:
        n_params += 1
        for single in (False, True):
            params_rows[single] += res[single]
    if res[False] and not res[True]:
        lost_by_op[sch.ops[int(d.t["op"][r])] if ex.gold is not None else "<NONE>"] += 1

print(f"holdout {meta['args']['val']}  n={n} rows ({n_params} with params, {n - n_params} declines)")
print(f"rows containing a literal that fills >1 pair: {multi_rows}")
print()
print(f"{'arm':<28}{'rows':>12}{'row acc':>11}{'ERROR RATE':>13}{'params rows':>14}")
for single, name in ((False, "MULTI-LABEL (shipped)"), (True, "single-label (old head)")):
    acc = rows[single] / n
    pa = params_rows[single] / max(1, n_params)
    print(f"{name:<28}{rows[single]:>6}/{n:<5}{100*acc:>10.2f}%{100*(1-acc):>12.2f}%"
          f"{params_rows[single]:>8}/{n_params:<5}  ({100*pa:.2f}%)")
print()
print(f"rows the single-label decode LOSES: {rows[False] - rows[True]}"
      f"  = {100*(rows[False]-rows[True])/n:.2f}% of the holdout")
for op, k in lost_by_op.most_common(12):
    print(f"    {op:<34}{k}")
