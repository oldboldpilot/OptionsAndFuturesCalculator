#!/usr/bin/env python3
"""Train the smallest encoder that can serve the two assistants.

WHY AN ENCODER AT ALL. `agent/analysis/extractability.py` measured that these
corpora are EXTRACTIVE: a gold parameter value is almost always recoverable
from a numeric literal in the utterance under a transformation the verifier
already enumerates, or it is a small-cardinality constant.
`agent/train/encoder_corpus.py` then routes 99.99% of train fields and 100.00%
of val fields that way. So the model never needs to emit a number -- it names
an operation and points at literals. One forward pass replaces the ~128
sequential decoder passes a mortgage answer currently costs.

WHY THIS FILE IS "_min". It is a deliberately small, self-contained trainer
written to get a real number on the board quickly: tokenizer, model, loop and
evaluation in one file, importing only the label builder. A fuller harness with
a separate model module and a quantisation/benchmark path is a sibling concern;
this one's job is to be unarguable about whether the architecture learns.

THREE DESIGN POINTS THAT ARE NOT ARBITRARY:

1. THE PER-LITERAL HEAD IS MULTI-LABEL, NOT SOFTMAX. `Example.pairs` is
   `list[list[int]]`: one literal legitimately fills several slots at once --
   `loan_amount` and `original_home_value` are the SAME stated amount in
   22.6% of train rows. A softmax would force the model to pick one and be
   wrong on the other by construction. Sigmoid + BCE per (literal, pair).

2. POSITIONS ARE RoPE, NOT LEARNED ABSOLUTE. sensen's serving attention applies
   RoPE unconditionally with no off switch (`multi_head_attention.cppm`
   ~:2768-2783). Training with learned absolute embeddings would produce weights
   the serving path cannot run. Since the model is trained from scratch there is
   no reason to prefer absolute, so this matches the engine instead of asking
   the engine to change.

3. LITERAL VECTORS COME FROM CHARACTER OFFSETS. The labels are in character
   coordinates on purpose (see `Example`'s docstring) so they never depend on a
   tokenizer. A literal's vector is the mean of the token vectors whose offset
   span overlaps `[lit.start, lit.end)`. A literal that maps to zero tokens is
   a real failure, not something to paper over -- it is counted and reported.
"""
from __future__ import annotations

import argparse
import json
import math
import random
import statistics
import sys
import time
from pathlib import Path

import torch
import torch.nn as nn
import torch.nn.functional as F

sys.path.insert(0, str(Path(__file__).resolve().parent))
import encoder_corpus as ec  # noqa: E402

IGNORE = -100


# =========================================================================
# TOKENIZER -- WordPiece trained on the corpus, because offsets are required
# =========================================================================
def build_tokenizer(texts: list[str], vocab_size: int, path: Path):
    """`tokenizers` gives character offsets natively, which is the one feature
    this task cannot do without. A hand-rolled whitespace splitter would lose
    the sub-word sharing that lets a 4k vocab cover this domain."""
    from tokenizers import Tokenizer, decoders, models, pre_tokenizers, trainers

    tok = Tokenizer(models.WordPiece(unk_token="[UNK]"))
    # Digits split individually: the slots differ by MAGNITUDE far more than by
    # lexical shape, so 495,000 and 49,500 must not share one token.
    tok.pre_tokenizer = pre_tokenizers.Sequence([
        pre_tokenizers.Punctuation(),
        pre_tokenizers.Digits(individual_digits=True),
        pre_tokenizers.Whitespace(),
    ])
    tok.decoder = decoders.WordPiece()
    trainer = trainers.WordPieceTrainer(
        vocab_size=vocab_size,
        special_tokens=["[PAD]", "[UNK]", "[CLS]", "[SEP]"],
        min_frequency=2,
    )
    tok.train_from_iterator(texts, trainer=trainer)
    tok.save(str(path))
    return tok


def encode(tok, text: str, max_len: int):
    enc = tok.encode(text)
    ids = enc.ids[: max_len - 1]
    offs = enc.offsets[: max_len - 1]
    cls = tok.token_to_id("[CLS]")
    return [cls] + ids, [(-1, -1)] + offs


# =========================================================================
# MODEL
# =========================================================================
class Rope(nn.Module):
    """Rotary positions, matching what sensen's attention applies anyway."""

    def __init__(self, head_dim: int, max_len: int, base: float = 10000.0):
        super().__init__()
        inv = 1.0 / (base ** (torch.arange(0, head_dim, 2).float() / head_dim))
        t = torch.arange(max_len).float()
        f = torch.outer(t, inv)
        self.register_buffer("cos", f.cos()[None, None, :, :], persistent=False)
        self.register_buffer("sin", f.sin()[None, None, :, :], persistent=False)

    def forward(self, x):  # x: [B, H, T, D]
        t = x.shape[2]
        x1, x2 = x[..., 0::2], x[..., 1::2]
        c, s = self.cos[:, :, :t, :], self.sin[:, :, :t, :]
        o1, o2 = x1 * c - x2 * s, x1 * s + x2 * c
        return torch.stack((o1, o2), dim=-1).flatten(-2)


class Block(nn.Module):
    """Pre-norm, BIDIRECTIONAL. No causal mask anywhere -- that is the point."""

    def __init__(self, d: int, h: int, ffn: int, rope: Rope, drop: float):
        super().__init__()
        self.h, self.dh = h, d // h
        self.n1, self.n2 = nn.LayerNorm(d), nn.LayerNorm(d)
        self.qkv = nn.Linear(d, 3 * d)
        self.proj = nn.Linear(d, d)
        self.fc1, self.fc2 = nn.Linear(d, ffn), nn.Linear(ffn, d)
        self.rope = rope
        self.drop = nn.Dropout(drop)

    def forward(self, x, pad_mask):
        B, T, D = x.shape
        y = self.n1(x)
        q, k, v = self.qkv(y).split(D, dim=-1)
        shp = lambda z: z.view(B, T, self.h, self.dh).transpose(1, 2)
        q, k, v = self.rope(shp(q)), self.rope(shp(k)), shp(v)
        # pad_mask: True where PAD. Only padding is masked; all real tokens see
        # each other in both directions.
        am = pad_mask[:, None, None, :]
        y = F.scaled_dot_product_attention(q, k, v, attn_mask=~am)
        y = self.proj(y.transpose(1, 2).reshape(B, T, D))
        x = x + self.drop(y)
        return x + self.drop(self.fc2(F.gelu(self.fc1(self.n2(x)))))


class TinyEncoder(nn.Module):
    def __init__(self, vocab, d, layers, heads, ffn, max_len,
                 n_ops, n_pairs, conv_sizes, drop=0.1):
        super().__init__()
        self.emb = nn.Embedding(vocab, d, padding_idx=0)
        rope = Rope(d // heads, max_len)
        self.blocks = nn.ModuleList(Block(d, heads, ffn, rope, drop) for _ in range(layers))
        self.norm = nn.LayerNorm(d)
        self.op_head = nn.Linear(d, n_ops)
        self.pair_head = nn.Linear(d, n_pairs)          # multi-label, see docstring
        self.conv_heads = nn.ModuleList(nn.Linear(d, n) for n in conv_sizes)

    def forward(self, ids, pad_mask, lit_pool):
        x = self.emb(ids)
        for b in self.blocks:
            x = b(x, pad_mask)
        x = self.norm(x)
        cls = x[:, 0]
        # lit_pool: [B, L, T] row-normalised selector over token positions
        lit = torch.bmm(lit_pool, x)
        return (self.op_head(cls),
                self.pair_head(lit),
                [h(cls) for h in self.conv_heads])


# =========================================================================
# BATCHING
# =========================================================================
def make_batch(exs, tok, sch, max_len, max_lits, device):
    B = len(exs)
    ids = torch.zeros(B, max_len, dtype=torch.long)
    pad = torch.ones(B, max_len, dtype=torch.bool)
    pool = torch.zeros(B, max_lits, max_len)
    pair_y = torch.zeros(B, max_lits, len(sch.pairs))
    lit_ok = torch.zeros(B, max_lits, dtype=torch.bool)
    op_y = torch.zeros(B, dtype=torch.long)
    conv_y = torch.full((B, len(sch.conv_fields)), IGNORE, dtype=torch.long)
    orphan = 0

    for i, ex in enumerate(exs):
        t_ids, offs = encode(tok, ex.text, max_len)
        n = len(t_ids)
        ids[i, :n] = torch.tensor(t_ids)
        pad[i, :n] = False
        op_y[i] = ex.op
        for j, lit in enumerate(ex.lits[:max_lits]):
            sel = [k for k in range(1, n)
                   if offs[k][1] > lit["start"] and offs[k][0] < lit["end"]]
            if not sel:
                orphan += 1
                continue
            lit_ok[i, j] = True
            w = 1.0 / len(sel)
            for k in sel:
                pool[i, j, k] = w
            for pid in ex.pairs[j]:
                pair_y[i, j, pid] = 1.0
        for ci, f in enumerate(sch.conv_fields):
            if f in ex.conv:
                conv_y[i, ci] = ex.conv[f]

    to = lambda t: t.to(device)
    return (to(ids), to(pad), to(pool), to(pair_y), to(lit_ok), to(op_y),
            to(conv_y), orphan)


def loss_of(out, pair_y, lit_ok, op_y, conv_y, pair_pos_weight):
    op_logits, pair_logits, conv_logits = out
    l_op = F.cross_entropy(op_logits, op_y)
    # Masking by boolean INDEXING would flatten to [n_selected] and `pos_weight`
    # (shape [n_pairs]) could not broadcast against it. Reduce manually instead,
    # keeping the pair axis last so the weight still lines up.
    m = lit_ok.unsqueeze(-1).float()
    if lit_ok.any():
        per = F.binary_cross_entropy_with_logits(
            pair_logits, pair_y, pos_weight=pair_pos_weight, reduction="none")
        denom = m.expand_as(per).sum().clamp(min=1.0)
        l_pair = (per * m).sum() / denom
    else:
        l_pair = pair_logits.sum() * 0.0
    l_conv = pair_logits.sum() * 0.0
    nc = 0
    for ci, lg in enumerate(conv_logits):
        y = conv_y[:, ci]
        if (y != IGNORE).any():
            l_conv = l_conv + F.cross_entropy(lg, y, ignore_index=IGNORE)
            nc += 1
    if nc:
        l_conv = l_conv / nc
    return l_op + l_pair + l_conv, l_op, l_pair, l_conv


@torch.no_grad()
def evaluate(model, exs, tok, sch, max_len, max_lits, device, bs, thresh=0.5):
    """Reports the number that matters: ROW accuracy, where every head must be
    right at once. Per-head accuracy flatters a multi-head model."""
    model.eval()
    op_ok = rows = row_ok = 0
    lit_tot = lit_ok_n = 0
    conv_tot = conv_ok = 0
    for s in range(0, len(exs), bs):
        chunk = exs[s:s + bs]
        ids, pad, pool, pair_y, lit_ok, op_y, conv_y, _ = make_batch(
            chunk, tok, sch, max_len, max_lits, device)
        op_l, pair_l, conv_l = model(ids, pad, pool)
        op_hat = op_l.argmax(-1)
        op_ok += (op_hat == op_y).sum().item()
        pred = (pair_l.sigmoid() > thresh)
        for i in range(len(chunk)):
            ok = bool(op_hat[i] == op_y[i])
            for j in range(max_lits):
                if not lit_ok[i, j]:
                    continue
                lit_tot += 1
                same = bool((pred[i, j] == (pair_y[i, j] > 0.5)).all())
                lit_ok_n += int(same)
                ok = ok and same
            for ci in range(len(sch.conv_fields)):
                if conv_y[i, ci] == IGNORE:
                    continue
                conv_tot += 1
                c = bool(conv_l[ci][i].argmax() == conv_y[i, ci])
                conv_ok += int(c)
                ok = ok and c
            rows += 1
            row_ok += int(ok)
    model.train()
    return {
        "op_acc": op_ok / max(rows, 1),
        "literal_exact": lit_ok_n / max(lit_tot, 1),
        "conv_acc": conv_ok / max(conv_tot, 1),
        "ROW_acc": row_ok / max(rows, 1),
        "n_rows": rows,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--train", required=True)
    ap.add_argument("--val", required=True)
    ap.add_argument("--op-key", default="operation")
    ap.add_argument("--question-mode", default="real")
    ap.add_argument("--d-model", type=int, default=128)
    ap.add_argument("--layers", type=int, default=3)
    ap.add_argument("--heads", type=int, default=4)
    ap.add_argument("--ffn", type=int, default=256)
    ap.add_argument("--vocab", type=int, default=4096)
    ap.add_argument("--max-len", type=int, default=192)
    ap.add_argument("--max-lits", type=int, default=16)
    ap.add_argument("--epochs", type=int, default=6)
    ap.add_argument("--bs", type=int, default=32)
    ap.add_argument("--lr", type=float, default=3e-4)
    ap.add_argument("--limit", type=int, default=0)
    # Defaults mirror encoder_corpus.py's own, so the schema this trains against
    # is the schema that script reports coverage for.
    ap.add_argument("--min-class-count", type=int, default=20)
    ap.add_argument("--min-pair-count", type=int, default=8)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--bf16", action="store_true")
    ap.add_argument("--seed", type=int, default=20261002)
    ap.add_argument("--out", default="encoder_min.pt")
    args = ap.parse_args()

    torch.set_num_threads(args.threads)
    torch.manual_seed(args.seed)
    random.seed(args.seed)
    device = "cpu"

    print("building labels ...", flush=True)
    tr_d = ec.load_dialogues(Path(args.train), args.limit or None)
    va_d = ec.load_dialogues(Path(args.val), None)
    tr_f = [ec.make_facts(d, args.op_key, args.question_mode) for d in tr_d]
    va_f = [ec.make_facts(d, args.op_key, args.question_mode) for d in va_d]
    sch = ec.build_schema(tr_f, args.op_key, args.question_mode,
                          args.min_class_count, args.min_pair_count)
    fin = lambda fs: [ec.finish(f, ec.label_facts(f, sch), sch) for f in fs]
    tr = fin(tr_f)
    va = fin(va_f)
    print(f"  train {len(tr)}  val {len(va)}  ops {len(sch.ops)} "
          f"pairs {len(sch.pairs)} conv {len(sch.conv_fields)}", flush=True)

    out = Path(args.out)
    tok = build_tokenizer([e.text for e in tr], args.vocab,
                          out.with_suffix(".tokenizer.json"))
    vocab = tok.get_vocab_size()

    conv_sizes = [len(sch.conv_vocab[f]) for f in sch.conv_fields]
    model = TinyEncoder(vocab, args.d_model, args.layers, args.heads, args.ffn,
                        args.max_len, len(sch.ops), len(sch.pairs), conv_sizes)
    n_par = sum(p.numel() for p in model.parameters())
    print(f"  vocab {vocab}  PARAMETERS {n_par:,} ({n_par/1e6:.2f}M)", flush=True)

    # A pair is positive on roughly one literal in len(pairs), so unweighted BCE
    # would be minimised by predicting all-zero. Weight the positive class.
    pos = torch.full((len(sch.pairs),), 8.0)

    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=0.01)
    steps = max(1, (len(tr) // args.bs)) * args.epochs
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=args.lr,
                                                total_steps=steps, pct_start=0.1)
    print(f"  steps {steps}  bf16={args.bf16}", flush=True)

    step = 0
    t0 = time.time()
    orphans = 0
    for ep in range(args.epochs):
        random.shuffle(tr)
        run = []
        for s in range(0, len(tr) - args.bs + 1, args.bs):
            batch = tr[s:s + args.bs]
            ids, pad, pool, pair_y, lit_ok, op_y, conv_y, orph = make_batch(
                batch, tok, sch, args.max_len, args.max_lits, device)
            orphans += orph
            ctx = (torch.autocast("cpu", dtype=torch.bfloat16)
                   if args.bf16 else torch.enable_grad())
            with ctx:
                o = model(ids, pad, pool)
                loss, l_op, l_pair, l_conv = loss_of(
                    o, pair_y, lit_ok, op_y, conv_y, pos)
            opt.zero_grad(set_to_none=True)
            loss.float().backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            opt.step()
            sched.step()
            run.append(loss.item())
            step += 1
            if step % 100 == 0:
                print(f"  ep{ep} step {step}/{steps} loss {statistics.mean(run[-100:]):.4f} "
                      f"(op {l_op.item():.3f} pair {l_pair.item():.3f} conv {l_conv.item():.3f}) "
                      f"{time.time()-t0:.0f}s", flush=True)
        m = evaluate(model, va, tok, sch, args.max_len, args.max_lits, device, args.bs)
        print(f"EPOCH {ep}: train_loss {statistics.mean(run):.4f}  "
              f"op {m['op_acc']:.4f}  literal {m['literal_exact']:.4f}  "
              f"conv {m['conv_acc']:.4f}  ROW {m['ROW_acc']:.4f}  "
              f"[{time.time()-t0:.0f}s]", flush=True)
        torch.save({"model": model.state_dict(), "schema": sch.to_json(),
                    "args": vars(args), "vocab": vocab,
                    "conv_sizes": conv_sizes, "metrics": m}, out)

    print(f"\norphan literals (mapped to zero tokens): {orphans}")
    print(f"saved {out}  parameters {n_par:,}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
