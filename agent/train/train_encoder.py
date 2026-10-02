#!/usr/bin/env python3
"""Train the tiny encoder end to end and measure it the way that matters.

@author Olumuyiwa Oluwasanmi

THE NUMBER THAT MATTERS IS ROW ACCURACY: the fraction of held-out rows for
which the FULL params object is rebuilt exactly (every field, every array
element, at the gold's own decimal precision) from nothing but the model's
heads. Per-head accuracies are reported beside it because they say WHERE a
miss came from, but a high head accuracy with a low row accuracy is the usual
shape of a model that is not actually good: 99% on each of ten independent
fields is 90% on the row.

WHAT IS COMPUTED AND WHAT IS NOT. The model never outputs a number. Row
accuracy is scored through `encoder_corpus.reconstruct()`, which computes each
value in Decimal from a literal's text and a map, so the figure already
includes everything the serving layer would do. It is bounded above by the
ORACLE COVERAGE of the labels (printed first); the gap between the two is the
model's, the ceiling is the label scheme's.

DISCIPLINE ABOUT THE HELD-OUT SET.
  * No checkpoint is selected on validation: the weights saved are the FINAL
    epoch's. Per-epoch validation numbers are printed for diagnosis only.
  * `--dev-frac` carves a dev split off TRAIN for hyperparameter decisions, so
    a tuning loop never has to look at val. Headline numbers are produced by a
    run with `--dev-frac 0` or are labelled as dev numbers.
  * The decision threshold of the pair head is fixed at 0.5 and was not tuned.
  * Val is the SAME synthetic generator as train. These are in-distribution
    numbers. Nothing here measures robustness to how a real user phrases things.

SPEED. bf16 autocast is the default because this host has AMX: a transformer-
shaped matmul measured 26.2 ms fp32 against 6.1 ms bf16 (fwd+bwd, 4,480 x 128).
Losses are computed in fp32 and master weights stay fp32.

TRAPS KEPT HERE SO THEY ARE NOT RE-LEARNED:
  * Pair BCE is SUMMED over a literal's pairs and averaged over literals. A
    mean over pairs divides by ~100 and the head stops learning while the
    operation head converges.
  * The per-operation pair/class masks are applied to the TRAINING loss with the
    GOLD operation (so the head only has to separate slots within an operation)
    and to inference with the PREDICTED one. A wrong operation therefore costs
    the row, which is the honest accounting.
"""
from __future__ import annotations

import argparse
import collections
import json
import math
import random
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

import numpy as np
import torch
import torch.nn.functional as F

sys.path.insert(0, str(Path(__file__).resolve().parent))
import encoder_corpus as C  # noqa: E402
from encoder_model import (EncoderConfig, TinyEncoder, add_model_args, build,  # noqa: E402
                           cfg_kwargs, config_from_schema)
from encoder_tokenizer import EncoderTokenizer  # noqa: E402

NEG = -1e9


# ===========================================================================
# DATA
# ===========================================================================
@dataclass
class Data:
    t: dict                      # tensors from C.tensorize
    examples: list[C.Example]

    def __len__(self) -> int:
        return len(self.examples)


def make_data(examples: list[C.Example], tok: EncoderTokenizer, sch: C.Schema, max_len: int) -> Data:
    return Data(C.tensorize(examples, tok, sch, max_len), examples)


def get_batch(d: Data, idx: torch.Tensor, n_pairs: int, labels: bool = True,
              pad_free: bool = False) -> dict:
    """Rows `idx` -> model inputs (+ labels). Sliced to the batch's own longest
    sequence and literal count, so padding is bounded by length bucketing.
    `pad_free` (batch 1) passes no padding mask, the serving shape."""
    t = d.t
    n = t["length"][idx]
    T = int(n.max())
    nl = t["lit_ok"][idx].sum(1)
    L = max(1, int(nl.max()))
    out = dict(
        ids=t["ids"][idx, :T].long(), seg=t["seg"][idx, :T].long(),
        pad=None if pad_free else (torch.arange(T)[None, :] < n[:, None]),
        lit_s=t["lit_s"][idx, :L].long(), lit_e=t["lit_e"][idx, :L].long(),
        lit_tag=t["lit_tag"][idx, :L].long(), lit_mag=t["lit_mag"][idx, :L].long(),
        lit_ok=t["lit_ok"][idx, :L])
    if labels:
        out["op"] = t["op"][idx]
        out["conv"] = t["conv"][idx]
        y = torch.zeros(len(idx), L, n_pairs)
        ptr = t["pair_ptr"]
        for k, r in enumerate(idx.tolist()):
            a, b = int(ptr[r]), int(ptr[r + 1])
            if b > a:
                y[k, t["pair_lit"][a:b], t["pair_id"][a:b]] = 1.0
        out["pair"] = y
    return out


def model_inputs(b: dict) -> dict:
    return {k: b[k] for k in ("ids", "seg", "pad", "lit_s", "lit_e", "lit_tag", "lit_mag", "lit_ok")}


def make_batches(lengths: torch.Tensor, bs: int, shuffle: bool, rng: random.Random
                 ) -> list[torch.Tensor]:
    """Length-bucketed batches: sort inside shuffled mega-chunks so padding is
    small without every epoch seeing the same batch composition."""
    idx = list(range(len(lengths)))
    if shuffle:
        rng.shuffle(idx)
    chunk = bs * 50
    batches = []
    for i in range(0, len(idx), chunk):
        part = sorted(idx[i:i + chunk], key=lambda j: int(lengths[j]))
        batches += [torch.tensor(part[k:k + bs]) for k in range(0, len(part), bs)]
    if shuffle:
        rng.shuffle(batches)
    return batches


@dataclass
class Masks:
    pair: torch.Tensor                      # [n_ops, n_pairs] bool
    conv: dict[str, torch.Tensor]           # field -> [n_ops, n_classes] bool


def make_masks(sch: C.Schema) -> Masks:
    pm = torch.zeros(sch.n_ops, sch.n_pairs, dtype=torch.bool)
    for op, ids in sch.op_pairs.items():
        pm[sch.op_index()[op], ids] = True
    cm = {}
    for name in sch.conv_fields:
        m = torch.zeros(sch.n_ops, len(sch.conv_vocab[name]), dtype=torch.bool)
        for op, ids in sch.conv_op_mask[name].items():
            m[sch.op_index()[op], ids] = True
        cm[name] = m
    return Masks(pm, cm)


# ===========================================================================
# LOSS
# ===========================================================================
def compute_loss(out: dict, b: dict, sch: C.Schema, masks: Masks, w: tuple[float, float, float]
                 ) -> tuple[torch.Tensor, dict]:
    op = b["op"]
    l_op = F.cross_entropy(out["op"].float(), op)
    l_conv = out["op"].new_zeros(()).float()
    n_heads = 0
    for i, name in enumerate(sch.conv_fields):
        tgt = b["conv"][:, i]
        ok = tgt != C.IGNORE
        if not ok.any():
            continue
        logits = out["conv:" + name].float()[ok].masked_fill(~masks.conv[name][op[ok]], NEG)
        l_conv = l_conv + F.cross_entropy(logits, tgt[ok])
        n_heads += 1
    l_conv = l_conv / max(1, n_heads)
    valid = b["lit_ok"][..., None] & masks.pair[op][:, None, :]
    bce = F.binary_cross_entropy_with_logits(out["pair"].float(), b["pair"], reduction="none")
    l_pair = (bce * valid).sum() / b["lit_ok"].sum().clamp(min=1)
    loss = w[0] * l_op + w[1] * l_conv + w[2] * l_pair
    return loss, dict(op=float(l_op.detach()), conv=float(l_conv.detach()),
                      pair=float(l_pair.detach()))


# ===========================================================================
# PREDICTION AND DECODING
# ===========================================================================
@torch.no_grad()
def predict(model: TinyEncoder, d: Data, sch: C.Schema, bs: int = 128, bf16: bool = True) -> dict:
    """Run the model over every row. Returns CPU float32 logits per head."""
    model.eval()
    n = len(d)
    L = d.t["lit_ok"].shape[1]
    op = torch.zeros(n, sch.n_ops)
    pair = torch.zeros(n, L, sch.n_pairs)
    conv = {name: torch.zeros(n, len(sch.conv_vocab[name])) for name in sch.conv_fields}
    for idx in make_batches(d.t["length"], bs, False, random.Random(0)):
        b = get_batch(d, idx, sch.n_pairs, labels=False)
        with torch.autocast("cpu", dtype=torch.bfloat16, enabled=bf16):
            out = model(**model_inputs(b))
        op[idx] = out["op"].float()
        pair[idx, : out["pair"].shape[1]] = out["pair"].float()
        for name in sch.conv_fields:
            conv[name][idx] = out["conv:" + name].float()
    return dict(op=op, pair=pair, conv=conv)


def decode_row(sch: C.Schema, masks: Masks, ex: C.Example, pred: dict, r: int, op: int,
               threshold: float) -> dict | None:
    """One row's logits -> params, under operation `op`'s masks."""
    if op == 0:
        return None
    nl = len(ex.lits)
    prob = torch.sigmoid(pred["pair"][r, :nl])
    allowed = masks.pair[op]
    sel = (prob > threshold) & allowed[None, :]
    lit_pairs = [torch.nonzero(sel[i]).flatten().tolist() for i in range(nl)]
    scores = [{p: float(prob[i, p]) for p in lit_pairs[i]} for i in range(nl)]
    conv = {}
    for name in sch.conv_fields:
        m = masks.conv[name][op]
        if m.any():
            conv[name] = int(pred["conv"][name][r].masked_fill(~m, NEG).argmax())
    return C.reconstruct(sch, op, ex.lits, lit_pairs, conv, scores)


# ===========================================================================
# EVALUATION
# ===========================================================================
def evaluate(model: TinyEncoder, d: Data, sch: C.Schema, masks: Masks, threshold: float = 0.5,
             bf16: bool = True, collect_failures: int = 0, pred: dict | None = None) -> dict:
    pred = pred or predict(model, d, sch, bf16=bf16)
    n = len(d)
    op_pred = pred["op"].argmax(1)
    op_gold = d.t["op"]
    res: dict = {"n": n}
    has = op_gold != 0
    res["op_acc"] = float((op_pred == op_gold).float().mean())
    res["op_acc_params_rows"] = float((op_pred[has] == op_gold[has]).float().mean()) if has.any() else 0.0
    res["none_rows"] = int((~has).sum())
    res["none_rows_correct"] = int(((op_pred == 0) & ~has).sum())

    # ---- convention heads, gold-op masks (isolates the head from the op head)
    conv_acc = {}
    for i, name in enumerate(sch.conv_fields):
        tgt = d.t["conv"][:, i]
        ok = tgt != C.IGNORE
        if ok.any():
            lg = pred["conv"][name][ok].masked_fill(~masks.conv[name][op_gold[ok]], NEG)
            conv_acc[name] = (float((lg.argmax(1) == tgt[ok]).float().mean()), int(ok.sum()))
    res["conv_acc"] = conv_acc

    # ---- pair head, gold-op masks, over literals of rows that have params
    tp = fp = fn = lit_exact = lit_n = slot_exact = map_hit = map_n = 0
    for r, ex in enumerate(d.examples):
        if ex.gold is None:
            continue
        nl = len(ex.lits)
        prob = torch.sigmoid(pred["pair"][r, :nl])
        sel = (prob > threshold) & masks.pair[int(op_gold[r])][None, :]
        for i in range(nl):
            got = set(torch.nonzero(sel[i]).flatten().tolist())
            gold = set(ex.pairs[i])
            tp += len(got & gold)
            fp += len(got - gold)
            fn += len(gold - got)
            lit_n += 1
            lit_exact += got == gold
            slot_exact += {sch.pairs[p][0] for p in got} == {sch.pairs[p][0] for p in gold}
            for p in gold:
                if any(sch.pairs[q][0] == sch.pairs[p][0] for q in got):
                    map_n += 1
                    map_hit += p in got
    prec = tp / max(1, tp + fp)
    rec = tp / max(1, tp + fn)
    res["pair"] = dict(literals=lit_n, exact_set_acc=lit_exact / max(1, lit_n),
                       slot_set_acc=slot_exact / max(1, lit_n),
                       map_acc_given_slot=map_hit / max(1, map_n),
                       precision=prec, recall=rec, f1=2 * prec * rec / max(1e-9, prec + rec))

    # ---- end to end
    ok_all = ok_gold_op = n_params = 0
    by_op: dict[str, list[int]] = collections.defaultdict(lambda: [0, 0])
    by_kind: dict[str, list[int]] = collections.defaultdict(lambda: [0, 0])
    fields_ok = fields_n = 0
    bad_fields: collections.Counter = collections.Counter()
    failures = []
    for r, ex in enumerate(d.examples):
        p_op = int(op_pred[r])
        got = decode_row(sch, masks, ex, pred, r, p_op, threshold)
        ok, bad = C.params_match(got, ex.gold)
        ok_all += ok
        if ex.gold is None:
            by_kind[ex.kind + ":none"][0] += ok
            by_kind[ex.kind + ":none"][1] += 1
            continue
        n_params += 1
        name = sch.ops[int(op_gold[r])]
        by_op[name][0] += ok
        by_op[name][1] += 1
        by_kind[ex.kind][0] += ok
        by_kind[ex.kind][1] += 1
        fields_n += len(ex.gold)
        fields_ok += len(ex.gold) - (len(ex.gold) if got is None else len(bad))
        for b in bad:
            bad_fields[(name, b)] += 1
        got_g = decode_row(sch, masks, ex, pred, r, int(op_gold[r]), threshold)
        ok_gold_op += C.params_match(got_g, ex.gold)[0]
        if not ok and len(failures) < collect_failures:
            failures.append(dict(text=ex.text, gold=ex.gold, bad=bad,
                                 op_pred=sch.ops[p_op], op_gold=name,
                                 pred=None if got is None else {k: _jsonable(v) for k, v in got.items()}))
    res["row_acc"] = ok_all / max(1, n)
    res["row_acc_params_rows"] = sum(v[0] for v in by_op.values()) / max(1, n_params)
    res["row_acc_given_gold_op"] = ok_gold_op / max(1, n_params)
    res["field_acc"] = fields_ok / max(1, fields_n)
    res["by_op"] = {k: (v[0], v[1]) for k, v in by_op.items()}
    res["by_kind"] = {k: (v[0], v[1]) for k, v in by_kind.items()}
    res["bad_fields"] = bad_fields.most_common(15)
    res["failures"] = failures
    return res


def _jsonable(v):
    if isinstance(v, list):
        return [_jsonable(x) for x in v]
    if isinstance(v, C.Missing):
        return "MISSING"
    return str(v) if not isinstance(v, (str, bool, int, float)) else v


def print_eval(res: dict, title: str, ceiling: float | None = None) -> None:
    print(f"\n==== {title}  (n={res['n']}) ====")
    print(f"  operation accuracy, all rows           {100 * res['op_acc']:7.2f}%   "
          f"(rows with params {100 * res['op_acc_params_rows']:.2f}%, "
          f"{res['none_rows_correct']}/{res['none_rows']} no-params rows)")
    p = res["pair"]
    print(f"  pair head, per literal (gold-op masks) exact set {100 * p['exact_set_acc']:.2f}%   "
          f"slot-set {100 * p['slot_set_acc']:.2f}%   map|slot {100 * p['map_acc_given_slot']:.2f}%   "
          f"P/R/F1 {100 * p['precision']:.2f}/{100 * p['recall']:.2f}/{100 * p['f1']:.2f}   "
          f"({p['literals']} literals)")
    for name, (acc, n) in res["conv_acc"].items():
        print(f"  convention head {name:24s}    {100 * acc:7.2f}%   (n={n})")
    print(f"  field accuracy (gold fields reproduced) {100 * res['field_acc']:6.2f}%")
    print(f"  ROW ACCURACY, all fields correct       {100 * res['row_acc']:7.2f}%   "
          f"(rows with params {100 * res['row_acc_params_rows']:.2f}%; "
          f"with the gold operation supplied {100 * res['row_acc_given_gold_op']:.2f}%)"
          + (f"   oracle ceiling {100 * ceiling:.2f}%" if ceiling is not None else ""))
    kinds = "  ".join(f"{k} {a}/{b} ({100 * a / max(1, b):.1f}%)" for k, (a, b) in sorted(res["by_kind"].items()))
    print(f"  by dialogue kind: {kinds}")


def print_by_op(res: dict) -> None:
    print("  per-operation row accuracy (rows with params):")
    for name, (a, b) in sorted(res["by_op"].items(), key=lambda kv: kv[1][0] / max(1, kv[1][1])):
        print(f"    {a:>4}/{b:<4} {100 * a / max(1, b):6.1f}%  {name}")
    if res["bad_fields"]:
        print("  most frequent failing (operation, field):")
        for (op, f), c in res["bad_fields"][:10]:
            print(f"    {c:>4}  {op}.{f}")


# ===========================================================================
# CHECKPOINTS
# ===========================================================================
def save_checkpoint(path: Path, model: TinyEncoder, cfg: EncoderConfig, sch: C.Schema,
                    tok: EncoderTokenizer, meta: dict) -> None:
    torch.save(dict(config=cfg.to_json(), state_dict=model.state_dict(), schema=sch.to_json(),
                    tokenizer=tok.to_str(), meta=meta), path)


def load_checkpoint(path: Path):
    ck = torch.load(path, map_location="cpu", weights_only=False)
    cfg = EncoderConfig.from_json(ck["config"])
    model = build(cfg)
    model.load_state_dict(ck["state_dict"])
    sch = C.Schema.from_json(ck["schema"])
    return model, cfg, sch, EncoderTokenizer.from_str(ck["tokenizer"]), ck["meta"]


# ===========================================================================
# TRAINING
# ===========================================================================
def lr_at(step: int, total: int, warmup: int, peak: float, floor: float = 0.1) -> float:
    if step < warmup:
        return peak * (step + 1) / warmup
    prog = (step - warmup) / max(1, total - warmup)
    return peak * (floor + (1 - floor) * 0.5 * (1 + math.cos(math.pi * min(1.0, prog))))


def param_groups(model: torch.nn.Module, wd: float):
    decay, no_decay = [], []
    for n, p in model.named_parameters():
        (decay if p.ndim >= 2 and "tok." not in n and "pos." not in n and "seg." not in n
         and "tag." not in n and "mag." not in n else no_decay).append(p)
    return [dict(params=decay, weight_decay=wd), dict(params=no_decay, weight_decay=0.0)]


def main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--train", type=Path, required=True)
    ap.add_argument("--val", type=Path, required=True)
    ap.add_argument("--op-key", default="operation")
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--epochs", type=int, default=8)
    ap.add_argument("--limit", type=int, default=None, help="use only the first N train rows")
    ap.add_argument("--batch-size", type=int, default=64)
    ap.add_argument("--lr", type=float, default=2e-3)
    ap.add_argument("--warmup", type=int, default=200)
    ap.add_argument("--weight-decay", type=float, default=0.01)
    ap.add_argument("--loss-weights", type=float, nargs=3, default=(1.0, 1.0, 1.0),
                    metavar=("OP", "CONV", "PAIR"))
    ap.add_argument("--no-bf16", action="store_true", help="fp32 autocast-off (slower on this host)")
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--threshold", type=float, default=0.5)
    ap.add_argument("--dev-frac", type=float, default=0.0,
                    help="hold this fraction of TRAIN out as a dev split and report it each "
                         "epoch (val is then evaluated only once, at the end)")
    ap.add_argument("--max-minutes", type=float, default=None,
                    help="stop after the epoch that crosses this wall-clock budget")
    ap.add_argument("--min-coverage", type=float, default=0.99,
                    help="refuse to train if the oracle row coverage of the labels is below this")
    ap.add_argument("--question-mode", choices=["real", "placeholder"], default="real")
    ap.add_argument("--min-class-count", type=int, default=20)
    ap.add_argument("--min-pair-count", type=int, default=8)
    ap.add_argument("--vocab-size", type=int, default=4096)
    ap.add_argument("--digits", choices=["chunk", "single"], default="chunk")
    ap.add_argument("--print-failures", type=int, default=0)
    add_model_args(ap)
    a = ap.parse_args(argv)

    torch.set_num_threads(a.threads)
    random.seed(a.seed)
    np.random.seed(a.seed)
    torch.manual_seed(a.seed)
    a.out_dir.mkdir(parents=True, exist_ok=True)
    bf16 = not a.no_bf16
    t_start = time.time()

    # ---- labels
    dtr, dva, sch, xtr, xva, st, vst = C.prepare(
        a.train, a.val, a.op_key, a.question_mode, a.min_class_count, a.min_pair_count, a.limit)
    ctr, cva = C.oracle_coverage(xtr, sch), C.oracle_coverage(xva, sch)
    print(f"[labels] ops {sch.n_ops}  pairs {sch.n_pairs}  conv heads "
          f"{ {n: len(v) for n, v in sch.conv_vocab.items()} }")
    print(f"[labels] ORACLE COVERAGE (the ceiling on row accuracy): train {100 * ctr.row_cov:.2f}%  "
          f"val {100 * cva.row_cov:.2f}%")
    if min(ctr.row_cov, cva.row_cov) < a.min_coverage:
        print(f"REFUSING to train: oracle coverage below --min-coverage {a.min_coverage}",
              file=sys.stderr)
        return 1
    (a.out_dir / "schema.json").write_text(json.dumps(sch.to_json(), indent=1))

    # ---- tokenizer (train split only)
    tok = EncoderTokenizer.train([e.text for e in xtr], a.vocab_size, 2, a.digits)
    tok.save(a.out_dir / "tokenizer.json")
    print(f"[tokenizer] vocab {tok.vocab_size}")

    # ---- optional dev split
    xdev: list[C.Example] = []
    if a.dev_frac > 0:
        perm = list(range(len(xtr)))
        random.Random(a.seed).shuffle(perm)
        k = int(len(xtr) * a.dev_frac)
        xdev = [xtr[i] for i in perm[:k]]
        xtr = [xtr[i] for i in perm[k:]]
        print(f"[data] dev split: {len(xdev)} rows held out of train")
    t0 = time.time()
    dtrain = make_data(xtr, tok, sch, a.max_len)
    dval = make_data(xva, tok, sch, a.max_len)
    ddev = make_data(xdev, tok, sch, a.max_len) if xdev else None
    lens = dtrain.t["length"].float()
    print(f"[data] tensorized in {time.time() - t0:.1f}s: train {len(dtrain)} rows, tokens/row "
          f"mean {lens.mean():.1f} max {int(lens.max())}, literals/row max "
          f"{dtrain.t['lit_ok'].shape[1]}")

    # ---- model
    cfg = config_from_schema(sch.to_json(), tok.vocab_size, **cfg_kwargs(a))
    model = build(cfg)
    n_params = model.n_params()
    print(f"[model] PARAMETERS: {n_params:,}  ({n_params / 1e6:.3f} M)   d={cfg.d_model} "
          f"layers={cfg.n_layers} heads={cfg.n_heads} ffn={cfg.ffn}({cfg.d_ffn}) "
          f"max_len={cfg.max_len} bf16={bf16} threads={a.threads}")
    for k, v in model.breakdown().items():
        print(f"           {k:36s} {v:>10,}")
    masks = make_masks(sch)
    opt = torch.optim.AdamW(param_groups(model, a.weight_decay), lr=a.lr, betas=(0.9, 0.98))
    steps_per_epoch = math.ceil(len(dtrain) / a.batch_size)
    total = steps_per_epoch * a.epochs
    rng = random.Random(a.seed)
    step = 0
    history = []
    t_train = time.time()
    for epoch in range(1, a.epochs + 1):
        model.train()
        t_ep = time.time()
        agg = collections.Counter()
        n_tok = 0
        for idx in make_batches(dtrain.t["length"], a.batch_size, True, rng):
            for g in opt.param_groups:
                g["lr"] = lr_at(step, total, a.warmup, a.lr)
            b = get_batch(dtrain, idx, sch.n_pairs)
            with torch.autocast("cpu", dtype=torch.bfloat16, enabled=bf16):
                out = model(**model_inputs(b))
            loss, parts = compute_loss(out, b, sch, masks, tuple(a.loss_weights))
            opt.zero_grad(set_to_none=True)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            opt.step()
            step += 1
            n_tok += int(b["ids"].numel())
            for k, v in parts.items():
                agg[k] += v
        secs = time.time() - t_ep
        nb = steps_per_epoch
        ev_set, ev_name = (ddev, "dev") if ddev is not None else (dval, "val")
        ev = evaluate(model, ev_set, sch, masks, a.threshold, bf16)
        history.append(dict(epoch=epoch, secs=secs, tok_per_s=n_tok / secs,
                            loss={k: v / nb for k, v in agg.items()}, eval_set=ev_name,
                            op_acc=ev["op_acc"], row_acc=ev["row_acc"],
                            pair_f1=ev["pair"]["f1"]))
        print(f"[epoch {epoch}/{a.epochs}] {secs:6.1f}s  {n_tok / secs:7.0f} padded tok/s  "
              f"loss op {agg['op'] / nb:.4f} conv {agg['conv'] / nb:.4f} pair {agg['pair'] / nb:.4f}  "
              f"| {ev_name}: op {100 * ev['op_acc']:.2f}%  pair-F1 {100 * ev['pair']['f1']:.2f}%  "
              f"ROW {100 * ev['row_acc']:.2f}%", flush=True)
        if epoch == 1:
            print(f"           projected total training time {secs * a.epochs / 60:.1f} min")
        if a.max_minutes and (time.time() - t_train) / 60 > a.max_minutes and epoch < a.epochs:
            print(f"[stop] --max-minutes {a.max_minutes} exceeded after epoch {epoch}; the "
                  f"learning-rate schedule did NOT finish annealing")
            break
    train_min = (time.time() - t_train) / 60

    # ---- final, once, on val (final-epoch weights; no selection)
    res = evaluate(model, dval, sch, masks, a.threshold, bf16, collect_failures=200)
    print_eval(res, f"FINAL VALIDATION ({'bf16 autocast' if bf16 else 'fp32'}, final-epoch weights)",
               cva.row_cov)
    print_by_op(res)
    if ddev is not None:
        print_eval(evaluate(model, ddev, sch, masks, a.threshold, bf16), "dev split (from train)")
    if a.print_failures:
        for f in res["failures"][: a.print_failures]:
            print("  FAIL", json.dumps(f, default=str)[:600])

    meta = dict(args={k: (str(v) if isinstance(v, Path) else v) for k, v in vars(a).items()},
                n_params=n_params, train_minutes=train_min, total_minutes=(time.time() - t_start) / 60,
                oracle_row_coverage=dict(train=ctr.row_cov, val=cva.row_cov),
                train_sha256=C.sha256_file(a.train), val_sha256=C.sha256_file(a.val),
                question_mode=a.question_mode, op_key=a.op_key)
    save_checkpoint(a.out_dir / "encoder_fp32.pt", model, cfg, sch, tok, meta)
    out = dict(meta=meta, history=history, final={k: v for k, v in res.items() if k != "failures"})
    (a.out_dir / "metrics.json").write_text(json.dumps(out, indent=1, default=str))
    with open(a.out_dir / "failures.jsonl", "w") as fh:
        for f in res["failures"]:
            fh.write(json.dumps(f, default=str) + "\n")
    print(f"\n[done] training {train_min:.1f} min, total {(time.time() - t_start) / 60:.1f} min; "
          f"checkpoint {a.out_dir / 'encoder_fp32.pt'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
