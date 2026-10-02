#!/usr/bin/env python3
r"""Train the tiny encoder end to end and measure it the way that matters.

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

THE PROBES EXIST BECAUSE OF THAT LAST BULLET. Each keeps the gold params and
changes only what the model reads, so a drop is attributable to that change:
  * NUMBER probe   the numbers a row points at get fresh digits (never seen), or are
                   resampled from train (seen, recombined).
  * FORMAT probe   the same numbers typed another way ("495000", "6.5 percent", "$250k",
                   "495,000 dollars"). Two rewrites are never trained on, so the probe
                   always contains formats the model has not seen.
  * ASK probe      a clarification dialogue's FIRST turn alone: can the layer above see
                   which field is missing and ask for it?
  * EXTRA sets     `--extra-val`, e.g. the strategy defect holdout, scored under the train
                   schema (an operation outside it counts as wrong).
`--augment-digits`, `--augment-format` and `--first-turn-examples` train against the first
three; each is off by default, and any run that uses one says so in its own header.

SPEED. bf16 autocast is the default because the task specified it, and no
precision mode is faster than plain fp32 (`--no-bf16`) for this model on this
host. The expectation came from a fair-looking microbenchmark (the host has AMX:
one transformer-shaped matmul, 4,480 x 128, measured 26.2 ms fp32 against 6.1 ms
bf16 forward+backward). A full training step of a 1 M parameter model does not
follow it: at d=128 the step is made of many small ops, not of big matmuls.
Measured as the calling thread's CPU time per 64-row mortgage batch at ONE
thread, which other jobs on the host cannot inflate (it stayed ~100% busy):
fp32 219 ms, bf16 autocast 244 ms, a bf16 replica with fp32 master weights
230 ms, weights cast to bf16 outright 223 ms (medians of 3 alternating rounds x
8 steps). Wall clock at 4 threads on the same busy host had autocast ~1.3x
slower (308-317 ms per step against 226-255 ms), so the direction held. Losses
are computed in fp32 and master weights stay fp32 in the default path. Accuracy
under the modes was NOT compared, so nothing here says bf16 training is harmless.

TRAPS KEPT HERE SO THEY ARE NOT RE-LEARNED:
  * Pair BCE is SUMMED over a literal's pairs and averaged over literals. A
    mean over pairs divides by ~100 and the head stops learning while the
    operation head converges.
  * The per-operation pair/class masks are applied to the TRAINING loss with the
    GOLD operation (so the head only has to separate slots within an operation)
    and to inference with the PREDICTED one. A wrong operation therefore costs
    the row, which is the honest accounting.

RUN IT (from agent/train/; the two corpora differ only in --op-key):
    python train_encoder.py --train ../dataset/data_mortgage/train.jsonl \
        --val ../dataset/data_mortgage/val.jsonl --op-key operation --out-dir OUT --epochs 10
    python train_encoder.py --train ../dataset/data/train.jsonl --val ../dataset/data/val.jsonl \
        --op-key strategy --out-dir OUT --epochs 10 --extra-val defect_holdout.jsonl
    python quantize_encoder.py --checkpoint OUT/encoder_fp32.pt --val <the same val> --out-dir OUT_Q
  `--limit N` and `--epochs` bound a smoke run (a tiny `--limit` also needs `--min-coverage 0
  --min-class-count 5`, because the class and pair vocabularies are counted over the rows it
  keeps). `--pos rope`, `--augment-digits P` and `--first-turn-examples F` are the
  options that depart from the specified recipe; each is off by default.
"""
from __future__ import annotations

import os
import re

# Must precede the torch import. On a shared host two PyTorch processes at 4 OpenMP
# threads each ran roughly 20x slower than one: spinning barriers steal the cores the other
# process is computing on (measured while another job was training; a 3,000-row
# smoke run went from 8.7 s to over 2.5 minutes without finishing). PASSIVE keeps
# the workers asleep between kernels. It is a default, not a mandate: export
# OMP_WAIT_POLICY=ACTIVE to override on a host that is yours alone.
os.environ.setdefault("OMP_WAIT_POLICY", "PASSIVE")

import argparse
import collections
import json
import math
import random
import sys
import time
from dataclasses import dataclass, replace
from decimal import Decimal
from pathlib import Path
from typing import Callable, Sequence

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


@torch.inference_mode()
def parse_request(model: torch.nn.Module, tok: EncoderTokenizer, sch: C.Schema, masks: Masks,
                  utterance: str, prior_clarification: str = "", prior_question: str = "",
                  threshold: float = 0.5, bf16: bool = False) -> dict | None:
    """RAW STRINGS IN, PARAMS OUT: what a serving layer would call, with the same
    three inputs `ParseRequest` carries. `prior_clarification` with a
    `prior_question` is the reply to a question this service asked; without one it
    is a revision (the contract sends no params block back, so the middle segment
    is empty). Returns None for a request the model declines (class <NONE>).

    Batch 1, no padding mask: the serving shape quantize_encoder.py benchmarks.
    Raises ValueError for an input longer than the model's position table, which a
    server must turn into a refusal rather than a truncated parse."""
    if prior_clarification:
        d = C.Dialogue("clarify" if prior_question else "revise", utterance,
                       prior_question or None, prior_clarification, None)
    else:
        d = C.Dialogue("single", utterance, None, None, None)
    r = C.render(d, sch.question_mode)
    lits = [dict(start=l.start, end=l.end, text=l.text, tag=l.tag, value=str(l.value),
                 mag=C.mag_bucket(l.value), seg=sg) for l, sg in zip(r.lits, r.lit_seg)]
    ex = C.Example(r.text, r.q_start, r.u2_start, d.kind, lits, 0, [[] for _ in lits], {}, None)
    data = Data(C.tensorize([ex], tok, sch, model.c.max_len), [ex])
    b = get_batch(data, torch.tensor([0]), sch.n_pairs, labels=False, pad_free=True)
    with torch.autocast("cpu", dtype=torch.bfloat16, enabled=bf16):
        out = model(**model_inputs(b))
    pred = dict(op=out["op"].float(), pair=out["pair"].float(),
                conv={n: out["conv:" + n].float() for n in sch.conv_fields})
    return decode_row(sch, masks, ex, pred, 0, int(pred["op"][0].argmax()), threshold)


# ===========================================================================
# EVALUATION
# ===========================================================================
def evaluate(model: TinyEncoder, d: Data, sch: C.Schema, masks: Masks, threshold: float = 0.5,
             bf16: bool = True, collect_failures: int = 0, pred: dict | None = None) -> dict:
    pred = pred or predict(model, d, sch, bf16=bf16)
    n = len(d)
    op_pred = pred["op"].argmax(1)
    op_prob = torch.softmax(pred["op"], 1)
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
            if not ok and len(failures) < collect_failures:
                # a decline the model answered with params: the failure a serving
                # layer can least afford, and the one a params-only list hides
                failures.append(dict(text=ex.text, gold=None, bad=["<op>"], op_pred=sch.ops[p_op],
                                     op_gold=sch.ops[0], op_conf=float(op_prob[r, p_op]), pred=None))
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
                                 op_pred=sch.ops[p_op], op_gold=name, op_conf=float(op_prob[r, p_op]),
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


def wilson(k: int, n: int, z: float = 1.96) -> tuple[float, float]:
    """95% Wilson interval for k successes of n: with n ~ 1,200 a headline of 97% is
    +-1 point, which is the resolution any ablation here has to beat."""
    if n == 0:
        return 0.0, 0.0
    p = k / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return c - h, c + h


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
    lo, hi = wilson(round(res["row_acc"] * res["n"]), res["n"])
    print(f"  ROW ACCURACY, all fields correct       {100 * res['row_acc']:7.2f}%   "
          f"[95% CI {100 * lo:.2f}-{100 * hi:.2f}]  "
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
# THE CLAIM THE DESIGN RESTS ON: "the model never emits a number"
# ===========================================================================
_NUMSPAN = re.compile(r"^(\d[\d,]*)(\.\d+)?(%?)$")


def build_value_pool(examples: list[C.Example]) -> dict[frozenset, list[str]]:
    """For every set of (slot, map) pairs a literal carries, the number spans the
    TRAIN rows wrote there: the empirical distribution a same-distribution
    resample draws from."""
    pool: dict[frozenset, set[str]] = collections.defaultdict(set)
    for ex in examples:
        for lit, pids in zip(ex.lits, ex.pairs):
            span = ex.text[lit["start"]:lit["end"]]
            if pids and _NUMSPAN.match(span):
                pool[frozenset(pids)].add(span)
    return {k: sorted(v) for k, v in pool.items()}


def perturb_example(ex: C.Example, sch: C.Schema, rng: random.Random, mode: str,
                    pool: dict[frozenset, list[str]] | None = None) -> C.Example | None:
    """A copy of `ex` whose POINTED-AT literals carry different numbers.

    The labels (operation, pairs, classes) are left alone and the expected params
    are recomputed from them by `reconstruct`, so "did the model still find the
    right literals and maps" has an exact answer. Literals labelled NONE keep their
    text (a superseded "30 days" or an "S&P 500" must stay what it was).

      one / all   FRESH DIGITS: same digit count, decimals and comma grouping,
                  every digit random. The number is one the generator never wrote
                  (a 47-year term, a 3.1% rate that no corpus row has).
      pool        SAME-DISTRIBUTION RESAMPLE: every pointed-at literal takes the
                  text some train row wrote for the same (slot, map) set. Numbers
                  are in range and from the generator's own value sets, so what
                  moves is the COMBINATION, not the support.
    The gap between the two is the finding: a model that holds up under `pool` and
    falls under `all` is reading the range of a value, not just its context.

    TWO LIMITS OF THIS PROBE, both found by reading its failures:
      * It preserves digit count, so it never moves a number into a new order of
        magnitude. A model that uses magnitude as a slot cue ("a two-digit percent is a
        tax bracket") is therefore NOT caught by `one`/`all`; it surfaced in `pool`, where
        a 10.81% rate came back MISSING.
      * It trusts the labels' pointers. A revision that restates a value in words ("same
        but next month" after "30 days") has its label on the first-turn literal only
        because the two values are equal; resample that literal and the expected params
        follow it while the model, correctly, follows the words. On the strategy corpus
        that is 6 of 852 rows (0.7%), read one by one, and the same six (operation, field)
        failures appear for every model trained here, which is how it was recognised as
        the probe's floor rather than a model's."""
    if ex.gold is None:
        return None
    idx = [i for i, ps in enumerate(ex.pairs) if ps]
    if not idx:
        return None
    if mode == "one":
        idx = [rng.choice(idx)]
    hit_slots: set[str] = set()
    for i in idx:
        hit_slots |= {sch.pairs[p][0] for p in ex.pairs[i]}
    # A class label for a field whose literal just changed would now be WRONG
    # ("30 days" -> "34 days" while the class stays 30): the pointer answers it.
    conv = {k: v for k, v in ex.conv.items() if k not in hit_slots}
    new_span: dict[int, str] = {}
    for i in idx:
        l = ex.lits[i]
        span = ex.text[l["start"]:l["end"]]
        m = _NUMSPAN.match(span)
        if not m:
            return None
        if mode == "pool":
            options = [c for c in (pool or {}).get(frozenset(ex.pairs[i]), []) if c != span]
            if not options:
                return None
            new_span[i] = rng.choice(options)
            continue
        ip, dp, tail = m.group(1), m.group(2) or "", m.group(3)
        digits = len(ip.replace(",", ""))
        for _ in range(50):
            ni = rng.randrange(10 ** (digits - 1) if digits > 1 else 0, 10 ** digits)
            new_ip = f"{ni:,}" if "," in ip else str(ni)
            new_dp = "." + "".join(str(rng.randrange(10)) for _ in dp[1:]) if dp else ""
            if new_ip + new_dp + tail != span:
                new_span[i] = new_ip + new_dp + tail
                break
        else:
            return None
    # rebuild the two user segments around the replacements, keeping the question
    def rewrite(base: str, base_off: int, lit_ids: list[int]) -> str:
        for i in sorted(lit_ids, reverse=True):
            l = ex.lits[i]
            base = base[:l["start"] - base_off] + new_span[i] + base[l["end"] - base_off:]
        return base

    n_first = sum(1 for l in ex.lits if l["seg"] == 0)
    if ex.kind == "single":
        first = rewrite(ex.text, 0, [i for i in new_span])
        later, question = None, ""
    else:
        first = rewrite(ex.text[:ex.q_start - len(C.SEP)], 0, [i for i in new_span if i < n_first])
        later = rewrite(ex.text[ex.u2_start:], ex.u2_start, [i for i in new_span if i >= n_first])
        question = ex.text[ex.q_start:ex.u2_start - len(C.SEP)]
    text = first if later is None else first + C.SEP + question + C.SEP + later
    q_start = len(first) + len(C.SEP) if later is not None else len(first)
    u2_start = q_start + len(question) + len(C.SEP) if later is not None else len(first)
    again = C.lex(first) + ([] if later is None else [
        _shifted(l, u2_start) for l in C.lex(later)])
    if len(again) != len(ex.lits) or any(a.tag != l["tag"] for a, l in zip(again, ex.lits)):
        return None
    lits = [dict(l, start=a.start, end=a.end, text=a.text, value=str(a.value), mag=C.mag_bucket(a.value))
            for a, l in zip(again, ex.lits)]
    for i in range(len(lits)):
        if i not in new_span and lits[i]["value"] != ex.lits[i]["value"]:
            return None                       # an untouched literal must keep its value
    gold = C.reconstruct(sch, ex.op, lits, ex.pairs, conv)
    if gold is None or any(isinstance(v, C.Missing) or (isinstance(v, list) and any(
            isinstance(x, C.Missing) for x in v)) for v in gold.values()):
        return None
    return C.Example(text, q_start, u2_start, ex.kind, lits, ex.op, ex.pairs, conv, gold)


def _shifted(l: C.Lit, off: int) -> C.Lit:
    return C.Lit(l.value, l.tag, l.start + off, l.end + off, l.text)


def probe_unseen_numbers(model: TinyEncoder, examples: list[C.Example], tok: EncoderTokenizer,
                         sch: C.Schema, masks: Masks, max_len: int, bf16: bool, seed: int = 0,
                         pool: dict[frozenset, list[str]] | None = None) -> dict:
    """Row accuracy when the digits are ones the model has never been shown (`one`,
    `all`), or are in-distribution but recombined (`pool`), against its accuracy on
    the SAME rows with their original digits."""
    out = {}
    for mode in ("one", "all") + (("pool",) if pool else ()):
        rng = random.Random(seed)
        orig, pert = [], []
        for ex in examples:
            p = perturb_example(ex, sch, rng, mode, pool)
            if p is not None:
                orig.append(ex)
                pert.append(p)
        if not pert:
            continue
        try:
            a = evaluate(model, make_data(orig, tok, sch, max_len), sch, masks, bf16=bf16)
            b = evaluate(model, make_data(pert, tok, sch, max_len), sch, masks, bf16=bf16)
        except ValueError as e:                # a perturbed row longer than the position table
            out[mode] = dict(error=str(e))
            continue
        out[mode] = dict(n=len(pert), original=a["row_acc"], fresh_digits=b["row_acc"],
                         pair_f1_original=a["pair"]["f1"], pair_f1_fresh=b["pair"]["f1"],
                         bad_fields=b["bad_fields"][:8])
    return out


def first_turn_example(ex: C.Example) -> C.Example | None:
    """The FIRST TURN of a clarification dialogue as its own training row.

    Without it the model never sees an incomplete request: every row it trains on
    ends in the params turn, so on a first turn it picks the wrong operation about
    as often as not (the ask probe measured 52.7% before this existed). The labels
    are the final row's restricted to what the first turn states: the same
    operation, the pairs on the first turn's literals, and NOTHING on the slot the
    reply supplies, so the missing field stays empty and the layer above can ask.
    Every clarification question in these corpora is about a pointer field (rate,
    term, LTV cap, operating expenses), so the class labels carry over unchanged."""
    if ex.kind != "clarify" or ex.gold is None:
        return None
    n1 = sum(1 for l in ex.lits if l["seg"] == 0)
    first = ex.text[: ex.q_start - len(C.SEP)]
    return C.Example(first, len(first), len(first), "single", [dict(l) for l in ex.lits[:n1]], ex.op,
                     [list(p) for p in ex.pairs[:n1]], dict(ex.conv), None)


def ask_probe(model: TinyEncoder, dialogues: list[C.Dialogue], sch: C.Schema, tok: EncoderTokenizer,
              masks: Masks, max_len: int, bf16: bool, threshold: float = 0.5) -> dict:
    """Would the serving layer know to ASK? On the FIRST turn of every clarification
    dialogue (the reply withheld) the encoder should leave exactly the fields the
    reply supplies EMPTY, so the layer above can turn "no value for `periods`" into
    "Over how many years?" -- the missing-field logic the decoder contract already
    has (`UnstatedField` in mortgage_verification.cppm).

    By default nothing in training shows the model a first turn alone, so this is a
    measurement of whether abstention comes for free from pointing, which it can for
    a pointer slot (no literal -> no pointer -> the field is MISSING) and CANNOT for
    a class head (`symbol` always returns one of its 20 classes; there is no ABSENT
    class). `--first-turn-examples` adds such rows to training and the probe then
    measures what that bought, not abstention for free.
    Rows whose reply supplies no pointer slot are not counted, because for them
    there is nothing this probe can see."""
    full, trunc, expected = [], [], []
    for d in dialogues:
        if d.kind != "clarify" or d.gold is None:
            continue
        ex_full = C.build_examples([C.make_facts(d, sch.op_key, sch.question_mode)], sch)[0]
        op_name = sch.ops[ex_full.op]
        need = set()
        for lit, pids in zip(ex_full.lits, ex_full.pairs):
            if lit["seg"] == 2:
                for pid in pids:
                    slot = sch.pairs[pid][0]
                    need.add(slot[:-len(C.ARRAY_SUFFIX)] if slot.endswith(C.ARRAY_SUFFIX) else slot)
        # a field with a class or a constant to fall back on is never "missing"
        need = {n for n in need if n not in sch.conv_fields and n not in sch.const_default
                and n not in sch.op_default.get(op_name, {})}
        if not need:
            continue
        r = C.render(C.Dialogue("single", d.first, None, None, None))
        lits = [dict(start=l.start, end=l.end, text=l.text, tag=l.tag, value=str(l.value),
                     mag=C.mag_bucket(l.value), seg=0) for l in r.lits]
        trunc.append(C.Example(r.text, r.q_start, r.u2_start, "single", lits, ex_full.op,
                               [[] for _ in lits], {}, None))
        expected.append((need, ex_full.op))
    if not trunc:
        return {}
    data = make_data(trunc, tok, sch, max_len)
    pred = predict(model, data, sch, bf16=bf16)
    op_pred = pred["op"].argmax(1)
    ok = op_ok = exact_missing = 0
    for r, (need, gold_op) in enumerate(expected):
        got = decode_row(sch, masks, trunc[r], pred, r, int(op_pred[r]), threshold)
        missing = set() if got is None else {k for k, v in got.items() if isinstance(v, C.Missing) or (
            isinstance(v, list) and any(isinstance(x, C.Missing) for x in v))}
        op_ok += int(op_pred[r]) == gold_op
        exact_missing += missing == need
        ok += int(op_pred[r]) == gold_op and missing == need
    n = len(expected)
    return dict(n=n, asks_correctly=ok / n, op_correct=op_ok / n, missing_set_exact=exact_missing / n)


# ===========================================================================
# FORMAT PROBE: the same request, typed another way
# ===========================================================================
# Real users do not write "$495,000". Each rewrite keeps the VALUE of every number (so the
# gold params are untouched) and changes only the surface the lexer and the tokenizer see.
# Validation comes from the generator that wrote train, so it contains exactly one way of
# writing each amount and rate (the word "percent" occurs in 0 of 27,764 mortgage user
# segments, "250k" in 0, a bare amount without "$" or commas in about 2%) and cannot say how
# the model copes with the others.
#
# TWO SETS, because `--augment-format` trains on the first and a probe of the same rewrites
# would then measure what was trained, not what generalises: FORMAT_REWRITES are the ones the
# augmentation applies; HELD_OUT_REWRITES are never trained on, so the model is always unseen
# on them whatever the flags.
FORMAT_REWRITES: dict[str, Callable[[str], str]] = {
    "drop the $ sign": lambda s: s.replace("$", ""),
    "drop thousands commas": lambda s: re.sub(r"(?<=\d),(?=\d{3}(?!\d))", "", s),
    "'6.5%' -> '6.5 percent'": lambda s: re.sub(r"(\d)\s*%", r"\1 percent", s),
    "'$250,000' -> '$250k'": lambda s: re.sub(r"\$(\d{1,3}),000(?![\d,])", r"$\1k", s),
}
HELD_OUT_REWRITES: dict[str, Callable[[str], str]] = {
    "'$495,000' -> '495,000 dollars'": lambda s: re.sub(r"\$(\d[\d,]*(?:\.\d+)?)", r"\1 dollars", s),
    "'$495,000' -> 'USD 495,000'": lambda s: s.replace("$", "USD "),
}
FORMAT_PERTURBATIONS = {**FORMAT_REWRITES, **HELD_OUT_REWRITES}


def format_example(d: C.Dialogue, sch: C.Schema, rng: random.Random) -> C.Example | None:
    """One TRAINING row retyped: each trained-on rewrite is applied with probability 1/2 (at
    least one is forced), the row is relabelled from scratch by the label builder, and the
    result is kept ONLY if those labels still reconstruct the gold params. A rewrite can
    produce a (slot, map) pair the vocabulary dropped as too rare ("6.5 percent" read as a
    bare number would need `M2' /100`); such a row would teach the model to point at nothing,
    so it is refused here rather than trained on."""
    names = [n for n in FORMAT_REWRITES if rng.random() < 0.5] or [rng.choice(list(FORMAT_REWRITES))]
    d2 = d
    for n in names:
        fn = FORMAT_REWRITES[n]
        d2 = replace(d2, first=fn(d2.first), later=None if d2.later is None else fn(d2.later))
    if d2 == d:
        return None
    ex = C.build_examples([C.make_facts(d2, sch.op_key, sch.question_mode)], sch)[0]
    if ex.gold is not None:
        ok, _ = C.params_match(C.reconstruct(sch, ex.op, ex.lits, ex.pairs, ex.conv), ex.gold)
        if not ok:
            return None
    return ex


def probe_formats(model: TinyEncoder, dialogues: list[C.Dialogue], sch: C.Schema, tok: EncoderTokenizer,
                  masks: Masks, max_len: int, bf16: bool) -> dict:
    """Row accuracy on the rows a rewrite actually changes, before and after it. Only the
    USER segments are rewritten; the gold params are the original ones, so a model that
    reads the number correctly in its new spelling still scores. Rewrites that change no
    row (the strategy corpus has no `$`) are omitted rather than reported as 100%, and a
    retyped row longer than `max_len` is left out of BOTH sides and counted in `too_long`
    (a model with a hard position table cannot be asked about it)."""
    out = {}
    base = C.build_examples([C.make_facts(d, sch.op_key, sch.question_mode) for d in dialogues], sch)
    for name, fn in FORMAT_PERTURBATIONS.items():
        orig, pert, too_long = [], [], 0
        for d, ex in zip(dialogues, base):
            d2 = replace(d, first=fn(d.first), later=None if d.later is None else fn(d.later))
            if d2 == d:
                continue
            ex2 = C.build_examples([C.make_facts(d2, sch.op_key, sch.question_mode)], sch)[0]
            if len(tok.encode(ex2.text).ids) > max_len:
                too_long += 1                    # a retyped row the position table cannot hold
                continue
            orig.append(ex)
            pert.append(ex2)
        if not pert:
            continue
        try:
            a = evaluate(model, make_data(orig, tok, sch, max_len), sch, masks, bf16=bf16)
            b = evaluate(model, make_data(pert, tok, sch, max_len), sch, masks, bf16=bf16)
        except ValueError as e:
            out[name] = dict(error=str(e))
            continue
        out[name] = dict(n=len(pert), too_long=too_long, original=a["row_acc"], retyped=b["row_acc"],
                         op_original=a["op_acc"], op_retyped=b["op_acc"],
                         bad_fields=b["bad_fields"][:6])
    return out


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
    ap.add_argument("--no-bf16", action="store_true",
                    help="plain fp32, autocast off. Measured FASTER than autocast bf16 on this "
                         "host for this model (see SPEED in the module docstring)")
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
    ap.add_argument("--extra-val", type=Path, action="append", default=[],
                    help="additional jsonl files to score once at the end under the TRAIN "
                         "schema (e.g. agent/train/defect_holdout.jsonl); repeatable")
    ap.add_argument("--augment-digits", type=float, default=0.0, metavar="P",
                    help="each epoch, give this fraction of train rows fresh digits in the literals "
                         "they point at (labels unchanged). Added after the unseen-number probe "
                         "measured a drop without it; 0 = off, the specified recipe")
    ap.add_argument("--augment-format", type=float, default=0.0, metavar="P",
                    help="each epoch, retype this fraction of train rows with the FORMAT_REWRITES "
                         "(no $, no thousands commas, 'percent', '250k') and relabel them. Added "
                         "after the format probe measured a collapse without it; 0 = off, the "
                         "specified recipe")
    ap.add_argument("--first-turn-examples", type=float, default=0.0, metavar="F",
                    help="each epoch, add the first turn ALONE of this fraction of the clarification "
                         "dialogues as extra rows (the reply withheld, its slot left empty). 0 = off, "
                         "the specified recipe; the ask probe is what motivated it")
    ap.add_argument("--print-failures", type=int, default=0)
    ap.add_argument("--no-probe-unseen", dest="probe_unseen", action="store_false",
                    help="skip the unseen-number probe")
    ap.add_argument("--no-probe-ask", dest="probe_ask", action="store_false",
                    help="skip the first-turn abstention probe")
    ap.add_argument("--no-probe-format", dest="probe_format", action="store_false",
                    help="skip the retyped-numbers probe")
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

    # ---- optional dev split (cut BEFORE the tokenizer sees any text, so the dev
    # rows influence neither the vocabulary nor the weights; the label space
    # (class values, pair vocabulary) is derived from all of train, which is
    # a property of the generator and not something the dev score can leak)
    xdev: list[C.Example] = []
    if a.dev_frac > 0:
        perm = list(range(len(xtr)))
        random.Random(a.seed).shuffle(perm)
        k = int(len(xtr) * a.dev_frac)
        xdev = [xtr[i] for i in perm[:k]]
        xtr_all, dtr_all = xtr, dtr
        xtr = [xtr_all[i] for i in perm[k:]]
        dtr = [dtr_all[i] for i in perm[k:]]            # --augment-format retypes the DIALOGUE
        print(f"[data] dev split: {len(xdev)} rows held out of train")

    # ---- tokenizer (fit split only)
    tok_texts = [e.text for e in xtr]
    if a.augment_format > 0:
        # the retyped rows contain words ("percent") and pieces ("##k") the plain corpus never does
        r_tok = random.Random(a.seed + 7)
        tok_texts += [fe.text for fe in (format_example(dtr[i], sch, r_tok)
                                         for i in r_tok.sample(range(len(xtr)), min(len(xtr), 4000)))
                      if fe is not None]
    tok = EncoderTokenizer.train(tok_texts, a.vocab_size, 2, a.digits)
    tok.save(a.out_dir / "tokenizer.json")
    print(f"[tokenizer] vocab {tok.vocab_size}")
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
    print(f"[model] stack: norm={cfg.norm} bias={cfg.bias} pos={cfg.pos}   inputs/heads: "
          f"segments={cfg.segments} emb_norm={cfg.emb_norm} lit_features={cfg.lit_features} "
          f"pooler={cfg.pooler} lit_mlp={cfg.lit_mlp}"
          + ("   (--servable: what sensen's TransformerBlock can load)" if a.servable else ""))
    for k, v in model.breakdown().items():
        print(f"           {k:36s} {v:>10,}")
    departures = [f"--pos {a.pos}"] if a.pos != "learned" else []
    departures += [f"--{n.replace('_', '-')} {getattr(a, n)}" for n in
                   ("augment_digits", "augment_format", "first_turn_examples") if getattr(a, n) > 0]
    departures += ["--digits single"] if a.digits == "single" else []
    departures += ["--question-mode placeholder"] if a.question_mode == "placeholder" else []
    print("[recipe] " + (", ".join(departures) if departures else
                         "as specified: learned positions, no augmentation, real question text"))
    masks = make_masks(sch)
    opt = torch.optim.AdamW(param_groups(model, a.weight_decay), lr=a.lr, betas=(0.9, 0.98))
    clarify_rows = [e for e in xtr if e.kind == "clarify" and e.gold is not None]
    n_ft = int(a.first_turn_examples * len(clarify_rows))
    steps_per_epoch = math.ceil((len(dtrain) + n_ft) / a.batch_size)
    total = steps_per_epoch * a.epochs
    rng = random.Random(a.seed)
    step = 0
    history = []
    t_train = time.time()
    for epoch in range(1, a.epochs + 1):
        model.train()
        t_ep = time.time()
        if a.augment_digits > 0 or a.augment_format > 0 or n_ft > 0:
            r_aug = random.Random(a.seed * 1009 + epoch)
            ex_epoch = list(xtr)
            n_aug = n_fmt = 0
            if a.augment_format > 0:                 # first: it rewrites the text the digits then change
                for i in r_aug.sample(range(len(xtr)), int(a.augment_format * len(xtr))):
                    fe = format_example(dtr[i], sch, r_aug)
                    if fe is not None and len(tok.encode(fe.text).ids) <= a.max_len:
                        ex_epoch[i] = fe
                        n_fmt += 1
            if a.augment_digits > 0:
                for i in r_aug.sample(range(len(xtr)), int(a.augment_digits * len(xtr))):
                    pe = perturb_example(ex_epoch[i], sch, r_aug, r_aug.choice(("one", "all")))
                    if pe is not None:
                        ex_epoch[i] = pe
                        n_aug += 1
            if n_ft:
                ex_epoch += [first_turn_example(e) for e in r_aug.sample(clarify_rows, n_ft)]
            dtrain = make_data(ex_epoch, tok, sch, a.max_len)
            print(f"[augment] epoch {epoch}: {n_fmt} rows retyped, {n_aug} rows carry fresh digits, "
                  f"{n_ft} first-turn rows added", flush=True)
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
    # ---- contamination: val inputs that are byte-identical to a train input. This
    # repository has already paid for a holdout that was partly its model's
    # own training set (304 of 600 rows), so the overlap is measured, not assumed.
    seen = {e.text for e in xtr} | {e.text for e in xdev}
    fresh = [e for e in xva if e.text not in seen]
    overlap = dict(n_val=len(xva), n_identical_to_train=len(xva) - len(fresh))
    if 0 < len(fresh) < len(xva):
        overlap["row_acc_on_disjoint_rows"] = evaluate(
            model, make_data(fresh, tok, sch, a.max_len), sch, masks, a.threshold, bf16)["row_acc"]
    print(f"\n[contamination] {overlap['n_identical_to_train']}/{len(xva)} val inputs are byte-identical "
          f"to a train input"
          + (f"; row accuracy on the {len(fresh)} disjoint rows: "
             f"{100 * overlap['row_acc_on_disjoint_rows']:.2f}%" if "row_acc_on_disjoint_rows" in overlap
             else ""))
    extra = {}
    for path in a.extra_val:
        fx = [C.make_facts(dl, sch.op_key, sch.question_mode) for dl in C.load_dialogues(path)]
        # A row whose class of record is not in the label space cannot be represented at
        # all: it is counted as WRONG, not dropped from the denominator and not a crash.
        known = [f for f in fx if f.op is None or f.op in sch.op_index()]
        xx = C.build_examples(known, sch)
        cov_x = C.oracle_coverage(xx, sch)
        ex_res = evaluate(model, make_data(xx, tok, sch, a.max_len), sch, masks, a.threshold, bf16,
                          collect_failures=50)
        n_all = len(fx)
        n_ok = round(ex_res["row_acc"] * len(xx))
        # `oracle_coverage` counts rows WITH params only; a decline is representable (class
        # <NONE>), so it belongs in the ceiling. The first run of this block left them out
        # and printed a ceiling two rows too low.
        n_decl = sum(1 for x in xx if x.gold is None)
        ceiling_rows = cov_x.rows_ok + n_decl
        extra[path.name] = dict(n=n_all, n_unrepresentable_operation=n_all - len(known),
                                oracle_coverage=ceiling_rows / max(1, n_all) if n_all else 0.0,
                                row_acc=n_ok / max(1, n_all), op_acc=ex_res["op_acc"],
                                failures=ex_res["failures"])
        print(f"\n==== EXTRA SET {path.name} (n={n_all}, scored under the TRAIN schema) ====")
        print(f"  rows whose operation is not in the label space: {n_all - len(known)} (counted wrong)")
        print(f"  oracle ceiling {100 * ceiling_rows / max(1, n_all):.2f}% ({ceiling_rows}/{n_all})   "
              f"operation accuracy {100 * ex_res['op_acc']:.2f}%   ROW ACCURACY "
              f"{100 * n_ok / max(1, n_all):.2f}% ({n_ok}/{n_all})")
        for f in ex_res["failures"][:20]:
            print("   FAIL", json.dumps(dict(text=f["text"], op_gold=f["op_gold"], op_pred=f["op_pred"],
                                              op_conf=round(f["op_conf"], 3), bad=f["bad"]), default=str)[:320])
        wrong = [f["op_conf"] for f in ex_res["failures"] if f["op_pred"] != f["op_gold"]]
        if wrong:
            sure = sum(c >= 0.99 for c in wrong)
            print(f"  operation head confidence on its {len(wrong)} wrong rows: min {min(wrong):.3f}  "
                  f"median {sorted(wrong)[len(wrong) // 2]:.3f}  {sure} of them >= 0.99"
                  + (" (confidently wrong: no confidence threshold can refuse those)" if sure else ""))
    probe = {}
    if a.probe_unseen:
        probe = probe_unseen_numbers(model, xva, tok, sch, masks, a.max_len, bf16,
                                     pool=build_value_pool(xtr + xdev))
        print("\n==== NUMBER PROBE (val rows; the numbers a row points at are replaced, labels unchanged, "
              "expected params recomputed) ====")
        what = {"one": "ONE literal gets fresh random digits   ",
                "all": "EVERY pointed-at literal gets fresh digits",
                "pool": "EVERY pointed-at literal resampled from train (in-distribution)"}
        for mode, r in probe.items():
            if "error" in r:
                print(f"  {mode}: {r['error']}")
                continue
            print(f"  {what[mode]}: {r['n']} rows  row acc {100 * r['original']:.2f}% -> "
                  f"{100 * r['fresh_digits']:.2f}%   (pair F1 {100 * r['pair_f1_original']:.2f}% -> "
                  f"{100 * r['pair_f1_fresh']:.2f}%)")
            print(f"      fields that break: " + ", ".join(f"{o}.{f} x{c}" for (o, f), c in r["bad_fields"][:5]))
    ask = {}
    if a.probe_ask:
        ask = ask_probe(model, dva, sch, tok, masks, a.max_len, bf16, a.threshold)
        if ask:
            print(f"\n==== ASK PROBE (first turn of {ask['n']} validation clarification dialogues whose "
                  f"reply supplies a pointer slot; the reply is withheld) ====")
            print(f"  operation right {100 * ask['op_correct']:.2f}%   the set of EMPTY fields is exactly "
                  f"the one the reply fills {100 * ask['missing_set_exact']:.2f}%   both "
                  f"{100 * ask['asks_correctly']:.2f}%")
        else:
            print("\n==== ASK PROBE: no clarification dialogue in val has a pointer slot in its reply; "
                  "nothing to measure (class heads cannot abstain) ====")
    fmt = {}
    if a.probe_format:
        fmt = probe_formats(model, dva, sch, tok, masks, a.max_len, bf16)
        if fmt:
            print("\n==== FORMAT PROBE (the same val rows with their numbers typed another way; "
                  "values and gold unchanged) ====")
            for name, r in fmt.items():
                if "error" in r:
                    print(f"  {name}: {r['error']}")
                    continue
                tag = "trained" if (a.augment_format > 0 and name in FORMAT_REWRITES) else "UNSEEN "
                print(f"  [{tag}] {name:32s} {r['n']:5d} rows  row acc {100 * r['original']:.2f}% -> "
                      f"{100 * r['retyped']:.2f}%   (operation {100 * r['op_original']:.2f}% -> "
                      f"{100 * r['op_retyped']:.2f}%)   breaks: "
                      + ", ".join(f"{o}.{f} x{c}" for (o, f), c in r["bad_fields"][:3])
                      + (f"   ({r['too_long']} rows over max_len left out)" if r["too_long"] else ""))
    if a.print_failures:
        for f in res["failures"][: a.print_failures]:
            print("  FAIL", json.dumps(f, default=str)[:600])

    # CPU seconds of the WHOLE process (every thread): the one cost figure that does not
    # depend on what else the host was running. Wall minutes on a shared host describe the
    # contention as much as the job.
    cpu_s = time.process_time()
    meta = dict(args={k: (str(v) if isinstance(v, Path) else v) for k, v in vars(a).items()},
                n_params=n_params, train_minutes=train_min, total_minutes=(time.time() - t_start) / 60,
                process_cpu_seconds=cpu_s, load_average_at_end=os.getloadavg(),
                oracle_row_coverage=dict(train=ctr.row_cov, val=cva.row_cov),
                train_sha256=C.sha256_file(a.train), val_sha256=C.sha256_file(a.val),
                question_mode=a.question_mode, op_key=a.op_key)
    save_checkpoint(a.out_dir / "encoder_fp32.pt", model, cfg, sch, tok, meta)
    out = dict(meta=meta, history=history, probe=probe, extra_val=extra, ask_probe=ask, format_probe=fmt,
               contamination=overlap,
               final={k: v for k, v in res.items() if k != "failures"})
    (a.out_dir / "metrics.json").write_text(json.dumps(out, indent=1, default=str))
    with open(a.out_dir / "failures.jsonl", "w") as fh:
        for f in res["failures"]:
            fh.write(json.dumps(f, default=str) + "\n")
    print(f"\n[done] training {train_min:.1f} min wall, total {(time.time() - t_start) / 60:.1f} min wall; "
          f"process CPU {cpu_s / 60:.1f} min (all threads), load average at end "
          f"{os.getloadavg()[0]:.1f}; checkpoint {a.out_dir / 'encoder_fp32.pt'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
