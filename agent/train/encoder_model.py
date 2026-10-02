#!/usr/bin/env python3
"""A plain `torch.nn` BIDIRECTIONAL transformer encoder with the heads the
label builder (encoder_corpus.py) defines.

@author Olumuyiwa Oluwasanmi

WHY BIDIRECTIONAL IS THE WHOLE POINT. A causal decoder reads "$700" with only
the words BEFORE it and must carry the decision forward through its own
generated digits. An encoder reads the entire utterance at once, so the literal
"$700" is classified with the question that precedes it ("What do operating
expenses run per month?") AND with everything after it, in one forward pass.
There is no attention mask other than padding, and no KV cache: a request is one
batched matmul chain, which is also why the cost is independent of how many
parameters the answer has.

HEADS (what each reads, and why it reads that):
  op        CLS -> n_ops. `<NONE>` is class 0 (a request with no params), so
            "decline" is an ordinary class rather than a separate mechanism.
  conv      CLS -> one softmax per convention field (compound_frequency,
            method, symbol, expiration_days ...). Only fields with >= 2 classes
            get a head; a field with one value is a constant of the schema.
  pair      PER LITERAL -> a SIGMOID over every (slot, map) pair, not a softmax.
            One literal can fill several slots (a stated price is both
            `loan_amount` and `original_home_value`; 22% of mortgage rows), so a
            single-label "slot" head plus a single-label "map" head would cap row
            accuracy at ~78% before any learning. The input to the pair head is
            [mean of the literal's token states | CLS | tag embedding |
            magnitude embedding]: the CLS vector carries "which operation is
            this", which decides what a literal can mean, and the two
            embeddings are the lexer's own facts (unit tag, order of magnitude)
            that WordPiece fragments of "$1,202,100" make hard to recover.

Per-operation masks (which pairs / classes are admissible for an operation) are
applied by the CALLER, to the logits, in training with the gold operation and in
serving with the predicted one. The model itself is mask-free so that the
masking policy can be measured, not baked in.

POSITIONS: `--pos learned` (the requested default) or `--pos rope`. The option
exists because of a deployment fact found while building this, not because
learned positions were doubted. sensen's `MultiHeadAttention` builds a
`RotaryEmbedding` for EVERY instance and rotates q and k unconditionally
(backend/sensen/src/multi_head_attention.cppm:2598-2606, 2772-2781, line
numbers as of the sensen commit 044de4ba), with no switch for absolute
positions, and its bidirectional path (`AttnParams.causal = false`, :2496-2512)
is the diffusion one. Weights trained with learned absolute
position embeddings therefore have no home in the engine that serves everything
else in this repository. `rope` uses the split-half layout of
`RotaryEmbedding::apply_rotation_inplace` (rotary_embedding.cppm:318, pairs
(i, i + d/2), base 10000) so that the same weights could be loaded by it; that
agreement is from READING the source, not from a parity run against sensen.
Segment ids and the literal tag/magnitude embeddings are further inputs a sensen
serving path does not supply today.

PARAMETER COUNT is printed exactly by `python encoder_model.py ...` and broken
down by component; the embedding table is V x d and, at d=128, is a third of
the network.

NOT TESTED HERE: anything about long inputs. `max_len` is a hard position-table
limit; encoder_corpus.tensorize() refuses rather than truncates.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Sequence

import torch
import torch.nn as nn
import torch.nn.functional as F


@dataclass
class EncoderConfig:
    vocab_size: int
    n_ops: int
    n_pairs: int
    conv_sizes: dict[str, int] = field(default_factory=dict)   # ordered: field -> n classes
    max_len: int = 160
    d_model: int = 128
    n_layers: int = 3
    n_heads: int = 4
    ffn: str = "geglu"            # geglu | gelu
    ffn_mult: float = 0.0         # 0 -> equal-parameter default (8/3 d for geglu, 4 d for gelu)
    dropout: float = 0.1
    n_segments: int = 3
    n_tags: int = 8
    n_mag: int = 16
    tag_dim: int = 16
    lit_features: bool = True
    pos: str = "learned"          # learned | rope
    rope_base: float = 10000.0

    @property
    def d_ffn(self) -> int:
        if self.ffn_mult:
            return int(round(self.ffn_mult * self.d_model))
        return int(round(self.d_model * (8 / 3 if self.ffn == "geglu" else 4) / 8) * 8)

    def to_json(self) -> dict:
        return asdict(self)

    @classmethod
    def from_json(cls, d: dict) -> "EncoderConfig":
        return cls(**d)


def rope_tables(t: int, dh: int, base: float) -> tuple[torch.Tensor, torch.Tensor]:
    """cos/sin [t, dh/2], the table `RotaryEmbedding::precompute_freqs_cis` builds."""
    inv = base ** (-torch.arange(0, dh, 2, dtype=torch.float32) / dh)
    ang = torch.outer(torch.arange(t, dtype=torch.float32), inv)
    return ang.cos(), ang.sin()


def apply_rope(x: torch.Tensor, cos: torch.Tensor, sin: torch.Tensor) -> torch.Tensor:
    """Split-half rotation of [B, H, T, dh]: (x1, x2) -> (x1 cos - x2 sin, x1 sin + x2 cos),
    the scalar fallback of `RotaryEmbedding::apply_rotation_inplace`."""
    h = x.shape[-1] // 2
    x1, x2 = x[..., :h], x[..., h:]
    c, s = cos[: x.shape[-2]].to(x.dtype), sin[: x.shape[-2]].to(x.dtype)
    return torch.cat([x1 * c - x2 * s, x1 * s + x2 * c], dim=-1)


class Block(nn.Module):
    """Pre-norm self-attention + feed-forward. Pre-norm because it trains
    stably at a high learning rate without warm-up tuning, which is what a
    from-scratch model on a 20-minute budget needs."""

    def __init__(self, c: EncoderConfig) -> None:
        super().__init__()
        d = c.d_model
        if d % c.n_heads:
            raise ValueError("d_model must be divisible by n_heads")
        self.h = c.n_heads
        self.ln1 = nn.LayerNorm(d)
        self.qkv = nn.Linear(d, 3 * d)
        self.proj = nn.Linear(d, d)
        self.ln2 = nn.LayerNorm(d)
        self.geglu = c.ffn == "geglu"
        self.fc = nn.Linear(d, (2 if self.geglu else 1) * c.d_ffn)
        self.out = nn.Linear(c.d_ffn, d)
        self.drop = nn.Dropout(c.dropout)

    def forward(self, x: torch.Tensor, keep: torch.Tensor | None,
                rope: tuple[torch.Tensor, torch.Tensor] | None = None) -> torch.Tensor:
        b, t, d = x.shape
        q, k, v = self.qkv(self.ln1(x)).view(b, t, 3, self.h, d // self.h).permute(2, 0, 3, 1, 4)
        if rope is not None:
            q, k = apply_rope(q, *rope), apply_rope(k, *rope)
        # No causal mask: this is an encoder. `keep` only hides padding.
        a = F.scaled_dot_product_attention(q, k, v, attn_mask=keep)
        x = x + self.drop(self.proj(a.transpose(1, 2).reshape(b, t, d)))
        y = self.fc(self.ln2(x))
        if self.geglu:
            g, u = y.chunk(2, dim=-1)
            y = F.gelu(g) * u
        else:
            y = F.gelu(y)
        return x + self.drop(self.out(y))


class TinyEncoder(nn.Module):
    def __init__(self, c: EncoderConfig) -> None:
        super().__init__()
        self.c = c
        d = c.d_model
        if c.pos not in ("learned", "rope"):
            raise ValueError(f"unknown positional scheme {c.pos!r}")
        self.tok = nn.Embedding(c.vocab_size, d, padding_idx=0)
        self.pos = nn.Embedding(c.max_len, d) if c.pos == "learned" else None
        if c.pos == "rope":
            cos, sin = rope_tables(c.max_len, d // c.n_heads, c.rope_base)
            self.register_buffer("rope_cos", cos, persistent=False)
            self.register_buffer("rope_sin", sin, persistent=False)
        self.seg = nn.Embedding(c.n_segments, d)
        self.emb_ln = nn.LayerNorm(d)
        self.drop = nn.Dropout(c.dropout)
        self.blocks = nn.ModuleList(Block(c) for _ in range(c.n_layers))
        self.ln_f = nn.LayerNorm(d)
        self.pool = nn.Linear(d, d)
        self.op_head = nn.Linear(d, c.n_ops)
        self.conv_heads = nn.ModuleDict({n: nn.Linear(d, k) for n, k in c.conv_sizes.items()})
        self.tag = nn.Embedding(c.n_tags, c.tag_dim)
        self.mag = nn.Embedding(c.n_mag, c.tag_dim)
        d_lit = 2 * d + (2 * c.tag_dim if c.lit_features else 0)
        self.lit_mlp = nn.Linear(d_lit, d)
        self.pair_head = nn.Linear(d, c.n_pairs)
        self.apply(self._init)
        scale = 0.02 / math.sqrt(2 * c.n_layers)
        for blk in self.blocks:        # residual-branch outputs start small (GPT-2 style)
            nn.init.normal_(blk.proj.weight, std=scale)
            nn.init.normal_(blk.out.weight, std=scale)

    @staticmethod
    def _init(m: nn.Module) -> None:
        if isinstance(m, (nn.Linear, nn.Embedding)):
            nn.init.normal_(m.weight, std=0.02)
            if isinstance(m, nn.Linear) and m.bias is not None:
                nn.init.zeros_(m.bias)
        if isinstance(m, nn.Embedding) and m.padding_idx is not None:
            with torch.no_grad():
                m.weight[m.padding_idx].zero_()

    def forward(self, ids: torch.Tensor, seg: torch.Tensor, pad: torch.Tensor | None,
                lit_s: torch.Tensor, lit_e: torch.Tensor, lit_tag: torch.Tensor,
                lit_mag: torch.Tensor, lit_ok: torch.Tensor) -> dict[str, torch.Tensor]:
        """ids/seg [B,T]; pad [B,T] bool (True = real token) or None when there is
        no padding (batch 1, the serving shape); lit_* [B,L]."""
        b, t = ids.shape
        e = self.tok(ids) + self.seg(seg)
        rope = None
        if self.pos is not None:
            e = e + self.pos(torch.arange(t, device=ids.device))[None]
        else:
            rope = (self.rope_cos, self.rope_sin)
        x = self.drop(self.emb_ln(e))
        keep = None if pad is None else pad[:, None, None, :]
        for blk in self.blocks:
            x = blk(x, keep, rope)
        x = self.ln_f(x)
        cls = F.gelu(self.pool(x[:, 0]))
        out: dict[str, torch.Tensor] = {"op": self.op_head(cls)}
        for n, head in self.conv_heads.items():
            out["conv:" + n] = head(cls)
        # mean of the token states a literal occupies: [B,L,T] @ [B,T,D]
        p = torch.arange(t, device=ids.device)[None, None, :]
        m = ((p >= lit_s[..., None]) & (p < lit_e[..., None]) & lit_ok[..., None]).to(x.dtype)
        pooled = torch.bmm(m, x) / m.sum(-1, keepdim=True).clamp(min=1)
        feats = [pooled, cls[:, None, :].expand(-1, pooled.shape[1], -1)]
        if self.c.lit_features:
            feats += [self.tag(lit_tag.long()), self.mag(lit_mag.long())]
        h = F.gelu(self.lit_mlp(self.drop(torch.cat(feats, dim=-1))))
        out["pair"] = self.pair_head(h)
        return out

    def n_params(self) -> int:
        return sum(p.numel() for p in self.parameters())

    def breakdown(self) -> dict[str, int]:
        def n(*mods) -> int:
            return sum(p.numel() for m in mods for p in m.parameters())
        return {
            "token embedding": n(self.tok),
            "position embedding": n(self.pos) if self.pos is not None else 0,
            "segment embedding": n(self.seg), "embedding norm": n(self.emb_ln),
            f"{self.c.n_layers} transformer blocks": n(*self.blocks),
            "final norm + CLS pooler": n(self.ln_f, self.pool),
            "op head": n(self.op_head), "convention heads": n(self.conv_heads),
            "literal features (tag, magnitude)": n(self.tag, self.mag),
            "literal MLP + pair head": n(self.lit_mlp, self.pair_head),
        }


def build(cfg: EncoderConfig) -> TinyEncoder:
    return TinyEncoder(cfg)


def config_from_schema(schema_json: dict, vocab_size: int, **kw) -> EncoderConfig:
    conv = {n: len(schema_json["conv_vocab"][n]) for n in schema_json["conv_fields"]}
    return EncoderConfig(vocab_size=vocab_size, n_ops=len(schema_json["ops"]),
                         n_pairs=len(schema_json["pairs"]), conv_sizes=conv, **kw)


def add_model_args(ap: argparse.ArgumentParser) -> None:
    g = ap.add_argument_group("model")
    g.add_argument("--d-model", type=int, default=128)
    g.add_argument("--n-layers", type=int, default=3)
    g.add_argument("--n-heads", type=int, default=4)
    g.add_argument("--max-len", type=int, default=160,
                   help="position-table size; measured corpus maxima are 150 (mortgage) and "
                        "29 (strategy) WordPiece tokens")
    g.add_argument("--ffn", choices=["geglu", "gelu"], default="geglu")
    g.add_argument("--pos", choices=["learned", "rope"], default="learned",
                   help="positions: learned absolute (as specified) or RoPE (what sensen's "
                        "attention applies unconditionally)")
    g.add_argument("--dropout", type=float, default=0.1)
    g.add_argument("--no-lit-features", action="store_true",
                   help="drop the tag/magnitude embeddings from the literal head (ablation)")


def cfg_kwargs(a: argparse.Namespace) -> dict:
    return dict(d_model=a.d_model, n_layers=a.n_layers, n_heads=a.n_heads, max_len=a.max_len,
                ffn=a.ffn, dropout=a.dropout, lit_features=not a.no_lit_features, pos=a.pos)


def selftest() -> int:
    """`python encoder_model.py --selftest`: the properties the docstring claims."""
    torch.manual_seed(0)
    bad = n = 0

    def check(cond: bool, what: str) -> None:
        nonlocal bad, n
        n += 1
        bad += not cond
        print(f"  {'PASS' if cond else 'FAIL'}: {what}")

    def mk(pos: str) -> TinyEncoder:
        m = build(EncoderConfig(vocab_size=50, n_ops=5, n_pairs=7, conv_sizes={"f": 3}, max_len=32,
                                d_model=32, n_layers=2, n_heads=4, dropout=0.0, pos=pos))
        m.eval()
        return m

    def inputs(t: int, pad_to: int | None = None):
        ids = torch.randint(5, 50, (1, t))
        lit_s, lit_e = torch.tensor([[1, 4]]), torch.tensor([[2, 6]])
        kw = dict(ids=ids, seg=torch.zeros(1, t, dtype=torch.long), pad=None, lit_s=lit_s, lit_e=lit_e,
                  lit_tag=torch.tensor([[0, 6]]), lit_mag=torch.tensor([[3, 8]]),
                  lit_ok=torch.ones(1, 2, dtype=torch.bool))
        if pad_to:
            kw["ids"] = torch.cat([ids, torch.zeros(1, pad_to - t, dtype=torch.long)], 1)
            kw["seg"] = torch.zeros(1, pad_to, dtype=torch.long)
            kw["pad"] = torch.arange(pad_to)[None] < t
        return kw

    for pos in ("learned", "rope"):
        m = mk(pos)
        x = inputs(12)
        with torch.no_grad():
            base = m(**x)
            # BIDIRECTIONAL: a token at the END of the sequence moves the CLS vector and a
            # literal at the START. In a causal model both would be unchanged.
            y = dict(x, ids=x["ids"].clone())
            y["ids"][0, -1] = (int(y["ids"][0, -1]) % 40) + 6
            moved = m(**y)
            check(not torch.allclose(base["op"], moved["op"], atol=1e-6) and
                  not torch.allclose(base["pair"][:, 0], moved["pair"][:, 0], atol=1e-6),
                  f"[{pos}] a later token changes an earlier literal and CLS (no causal mask)")
            # PADDING does not leak: the same request padded to 20 gives the same logits.
            pad = m(**inputs_like(x, 20))
            check(torch.allclose(base["op"], pad["op"], atol=1e-5) and
                  torch.allclose(base["pair"], pad["pair"], atol=1e-5),
                  f"[{pos}] padding to 20 tokens leaves every head unchanged (batch 1 == padded batch)")
        parts = m.breakdown()
        check(sum(parts.values()) == m.n_params(), f"[{pos}] parameter breakdown sums to the total "
                                                   f"({m.n_params():,})")
    # RoPE: a score depends on the DISTANCE between positions, not on the positions
    cos, sin = rope_tables(40, 8, 10000.0)
    q, k = torch.randn(1, 1, 1, 8), torch.randn(1, 1, 1, 8)

    def score(i: int, j: int) -> float:
        qi = apply_rope(q.expand(1, 1, i + 1, 8).clone(), cos, sin)[..., i:i + 1, :]
        kj = apply_rope(k.expand(1, 1, j + 1, 8).clone(), cos, sin)[..., j:j + 1, :]
        return float((qi * kj).sum())

    check(abs(score(5, 2) - score(15, 12)) < 1e-4 and abs(score(5, 2) - score(5, 3)) > 1e-4,
          "RoPE score depends on the offset i - j only")
    check(abs(float(apply_rope(q.expand(1, 1, 7, 8).clone(), cos, sin)[0, 0, 6].norm() - q.norm())) < 1e-5,
          "RoPE preserves vector norm (it is a rotation)")
    print(f"\n{n - bad}/{n} checks passed")
    return 1 if bad else 0


def inputs_like(x: dict, pad_to: int) -> dict:
    t = x["ids"].shape[1]
    y = dict(x)
    y["ids"] = torch.cat([x["ids"], torch.zeros(1, pad_to - t, dtype=torch.long)], 1)
    y["seg"] = torch.zeros(1, pad_to, dtype=torch.long)
    y["pad"] = torch.arange(pad_to)[None] < t
    return y


def main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Print the exact parameter count of the encoder.")
    ap.add_argument("--schema", type=Path, help="schema.json written by encoder_corpus.py --out")
    ap.add_argument("--vocab", type=int, help="tokenizer vocabulary size")
    ap.add_argument("--selftest", action="store_true", help="run the built-in checks and exit")
    add_model_args(ap)
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not (a.schema and a.vocab):
        ap.error("--schema and --vocab are required unless --selftest")
    cfg = config_from_schema(json.loads(a.schema.read_text()), a.vocab, **cfg_kwargs(a))
    model = build(cfg)
    print(f"d_model={cfg.d_model} layers={cfg.n_layers} heads={cfg.n_heads} ffn={cfg.ffn}"
          f"({cfg.d_ffn}) vocab={cfg.vocab_size} max_len={cfg.max_len} pos={cfg.pos}")
    print(f"ops={cfg.n_ops} pairs={cfg.n_pairs} conv heads={cfg.conv_sizes}")
    parts = model.breakdown()
    for k, v in parts.items():
        print(f"  {k:36s} {v:>10,}")
    assert sum(parts.values()) == model.n_params(), "breakdown does not sum to the total"
    print(f"  {'TOTAL':36s} {model.n_params():>10,}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
