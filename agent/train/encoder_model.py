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

SERVABLE (`--servable`). The defaults above (LayerNorm, GeGLU, biases, a segment
embedding, a CLS pooler, tag/magnitude features) are NOT what sensen can load:
`text_encoder.cppm` reuses `TransformerBlock`, which is RMSNorm + SwiGLU with no
biases, and its GGUF loader refuses any other FFN activation. `--servable` builds
exactly that stack, with RoPE, and drops every input and head the engine would have
to grow: its token ids are the only input, the heads are single Linears. What it does
NOT decide is the engine's side of the pairing; see RoPE LAYOUT below. The defaults are
kept as the specified recipe and so that the checkpoints already trained still load
(`EncoderConfig.from_json` fills the new fields with the old behaviour).

RoPE LAYOUT. The pairs rotated together are (i, i + d_head/2) here and in sensen
(`RotaryEmbedding::apply_rotation_inplace`, scalar fallback: `data[i]` with
`data[i + half_dim]`). A trainer that rotates ADJACENT pairs (x[0::2], x[1::2], the
GPT-J layout) produces q/k weights whose channels are permuted relative to that, and
the engine would compute different attention scores from them unless the exporter
reorders the q and k output channels of every head (new i <- old 2i, new i + d/2 <-
old 2i + 1). Same frequencies either way; only the pairing differs.

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
    ffn: str = "geglu"            # geglu | swiglu | gelu
    ffn_mult: float = 0.0         # 0 -> equal-parameter default (8/3 d for geglu/swiglu, 4 d for gelu)
    dropout: float = 0.1
    n_segments: int = 3
    n_tags: int = 8
    n_mag: int = 16
    tag_dim: int = 16
    lit_features: bool = True
    pos: str = "learned"          # learned | rope
    rope_base: float = 10000.0
    # --- the pieces `--servable` switches off (see the SERVABLE paragraph of the docstring)
    norm: str = "layernorm"       # layernorm | rmsnorm
    bias: bool = True             # biases on every Linear of the transformer stack
    emb_norm: bool = True         # a norm over the summed embeddings
    segments: bool = True         # segment embedding (first user / question / reply)
    pooler: bool = True           # CLS -> Linear -> GELU before the op and class heads
    lit_mlp: bool = True          # [pooled | CLS | tag | mag] -> Linear -> GELU before the pair head

    @property
    def gated(self) -> bool:
        return self.ffn in ("geglu", "swiglu")

    @property
    def d_ffn(self) -> int:
        if self.ffn_mult:
            return int(round(self.ffn_mult * self.d_model))
        return int(round(self.d_model * (8 / 3 if self.gated else 4) / 8) * 8)

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


class RMSNorm(nn.Module):
    """x * w / sqrt(mean(x^2) + eps): no mean subtraction, no bias. The reduction runs
    in fp32 whatever the activation dtype, because under bf16 a mean of squares
    accumulated at 8 mantissa bits is where a norm quietly stops being one."""

    def __init__(self, d: int, eps: float = 1e-6) -> None:
        super().__init__()
        self.weight = nn.Parameter(torch.ones(d))
        self.eps = eps

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x32 = x.float()
        n = x32 * torch.rsqrt(x32.pow(2).mean(-1, keepdim=True) + self.eps)
        return (n * self.weight.float()).to(x.dtype)


def make_norm(c: EncoderConfig) -> nn.Module:
    return RMSNorm(c.d_model) if c.norm == "rmsnorm" else nn.LayerNorm(c.d_model)


class Block(nn.Module):
    """Pre-norm self-attention + feed-forward. Pre-norm because it trains
    stably at a high learning rate without warm-up tuning, which is what a
    from-scratch model on a 20-minute budget needs."""

    def __init__(self, c: EncoderConfig) -> None:
        super().__init__()
        d = c.d_model
        if d % c.n_heads:
            raise ValueError("d_model must be divisible by n_heads")
        if c.ffn not in ("geglu", "swiglu", "gelu") or c.norm not in ("layernorm", "rmsnorm"):
            raise ValueError(f"unknown ffn {c.ffn!r} or norm {c.norm!r}")
        self.h = c.n_heads
        self.ln1 = make_norm(c)
        self.qkv = nn.Linear(d, 3 * d, bias=c.bias)
        self.proj = nn.Linear(d, d, bias=c.bias)
        self.ln2 = make_norm(c)
        self.kind = c.ffn
        self.fc = nn.Linear(d, (2 if c.gated else 1) * c.d_ffn, bias=c.bias)   # gate and up fused
        self.out = nn.Linear(c.d_ffn, d, bias=c.bias)
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
        if self.kind == "gelu":
            y = F.gelu(y)
        else:
            g, u = y.chunk(2, dim=-1)
            y = (F.silu(g) if self.kind == "swiglu" else F.gelu(g)) * u
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
        self.seg = nn.Embedding(c.n_segments, d) if c.segments else None
        self.emb_ln = make_norm(c) if c.emb_norm else None
        self.drop = nn.Dropout(c.dropout)
        self.blocks = nn.ModuleList(Block(c) for _ in range(c.n_layers))
        self.ln_f = make_norm(c)
        self.pool = nn.Linear(d, d) if c.pooler else None
        self.op_head = nn.Linear(d, c.n_ops)
        self.conv_heads = nn.ModuleDict({n: nn.Linear(d, k) for n, k in c.conv_sizes.items()})
        self.tag = nn.Embedding(c.n_tags, c.tag_dim) if c.lit_features else None
        self.mag = nn.Embedding(c.n_mag, c.tag_dim) if c.lit_features else None
        # with the literal MLP the head reads [pooled | CLS | tag | mag]; without it, a single
        # Linear reads [pooled | tag | mag] (just `pooled` when the features are off too)
        d_feat = d + (2 * c.tag_dim if c.lit_features else 0)
        self.lit_mlp = nn.Linear(d_feat + d, d) if c.lit_mlp else None
        self.pair_head = nn.Linear(d if c.lit_mlp else d_feat, c.n_pairs)
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
        e = self.tok(ids)
        if self.seg is not None:
            e = e + self.seg(seg)
        rope = None
        if self.pos is not None:
            e = e + self.pos(torch.arange(t, device=ids.device))[None]
        else:
            rope = (self.rope_cos, self.rope_sin)
        x = self.drop(e if self.emb_ln is None else self.emb_ln(e))
        keep = None if pad is None else pad[:, None, None, :]
        for blk in self.blocks:
            x = blk(x, keep, rope)
        x = self.ln_f(x)
        cls = x[:, 0] if self.pool is None else F.gelu(self.pool(x[:, 0]))
        out: dict[str, torch.Tensor] = {"op": self.op_head(cls)}
        for n, head in self.conv_heads.items():
            out["conv:" + n] = head(cls)
        # mean of the token states a literal occupies: [B,L,T] @ [B,T,D]
        p = torch.arange(t, device=ids.device)[None, None, :]
        m = ((p >= lit_s[..., None]) & (p < lit_e[..., None]) & lit_ok[..., None]).to(x.dtype)
        pooled = torch.bmm(m, x) / m.sum(-1, keepdim=True).clamp(min=1)
        feats = [pooled]
        if self.lit_mlp is not None:
            feats.append(cls[:, None, :].expand(-1, pooled.shape[1], -1))
        if self.c.lit_features:
            feats += [self.tag(lit_tag.long()), self.mag(lit_mag.long())]
        h = self.drop(torch.cat(feats, dim=-1))
        if self.lit_mlp is not None:
            h = F.gelu(self.lit_mlp(h))
        out["pair"] = self.pair_head(h)
        return out

    def n_params(self) -> int:
        return sum(p.numel() for p in self.parameters())

    def breakdown(self) -> dict[str, int]:
        def n(*mods) -> int:
            return sum(p.numel() for m in mods if m is not None for p in m.parameters())
        return {
            "token embedding": n(self.tok),
            "position embedding": n(self.pos),
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
    g.add_argument("--ffn", choices=["geglu", "swiglu", "gelu"], default="geglu")
    g.add_argument("--norm", choices=["layernorm", "rmsnorm"], default="layernorm")
    g.add_argument("--no-bias", action="store_true", help="no biases in the transformer stack")
    g.add_argument("--no-emb-norm", action="store_true", help="no norm over the summed embeddings")
    g.add_argument("--no-segments", action="store_true", help="no segment embedding")
    g.add_argument("--no-pooler", action="store_true", help="op and class heads read CLS directly")
    g.add_argument("--no-lit-mlp", action="store_true",
                   help="the pair head is one Linear over the pooled literal (no MLP, no CLS)")
    g.add_argument("--servable", action="store_true",
                   help="the architecture sensen's TransformerBlock can load: RMSNorm + SwiGLU, no "
                        "biases, RoPE, and none of the extra inputs and heads (segments, tag and "
                        "magnitude features, pooler, literal MLP). Overrides the options above")
    g.add_argument("--pos", choices=["learned", "rope"], default="learned",
                   help="positions: learned absolute (as specified) or RoPE (what sensen's "
                        "attention applies unconditionally)")
    g.add_argument("--dropout", type=float, default=0.1)
    g.add_argument("--no-lit-features", action="store_true",
                   help="drop the tag/magnitude embeddings from the literal head (ablation)")


def cfg_kwargs(a: argparse.Namespace) -> dict:
    kw = dict(d_model=a.d_model, n_layers=a.n_layers, n_heads=a.n_heads, max_len=a.max_len,
              ffn=a.ffn, dropout=a.dropout, lit_features=not a.no_lit_features, pos=a.pos,
              norm=a.norm, bias=not a.no_bias, emb_norm=not a.no_emb_norm,
              segments=not a.no_segments, pooler=not a.no_pooler, lit_mlp=not a.no_lit_mlp)
    if a.servable:
        kw.update(norm="rmsnorm", ffn="swiglu", bias=False, pos="rope", emb_norm=False,
                  segments=False, pooler=False, lit_mlp=False, lit_features=False)
    return kw


def selftest() -> int:
    """`python encoder_model.py --selftest`: the properties the docstring claims."""
    torch.manual_seed(0)
    bad = n = 0

    def check(cond: bool, what: str) -> None:
        nonlocal bad, n
        n += 1
        bad += not cond
        print(f"  {'PASS' if cond else 'FAIL'}: {what}")

    SERVABLE = dict(norm="rmsnorm", ffn="swiglu", bias=False, pos="rope", emb_norm=False,
                    segments=False, pooler=False, lit_mlp=False, lit_features=False)

    def mk(pos: str) -> TinyEncoder:
        kw = SERVABLE if pos == "servable" else dict(pos=pos)
        m = build(EncoderConfig(vocab_size=50, n_ops=5, n_pairs=7, conv_sizes={"f": 3}, max_len=32,
                                d_model=32, n_layers=2, n_heads=4, dropout=0.0, **kw))
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

    for pos in ("learned", "rope", "servable"):
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
    # the servable stack really is what sensen's TransformerBlock holds: no bias anywhere in
    # it, RMSNorm weights only, and a SwiGLU FFN with the same shapes (so the same parameter
    # count) as the GeGLU one it replaces
    m = mk("servable")
    stack = [n for n, _ in m.named_parameters() if n.startswith(("blocks", "ln_f"))]
    check(stack and not any(n.endswith("bias") for n in stack),
          f"[servable] the transformer stack has no bias parameter ({len(stack)} tensors)")
    check(all(isinstance(mod, RMSNorm) for n, mod in m.named_modules() if n.endswith(("ln1", "ln2", "ln_f"))),
          "[servable] every norm is an RMSNorm")
    cfg_g = EncoderConfig(vocab_size=50, n_ops=5, n_pairs=7, d_model=32, n_layers=2, n_heads=4, ffn="geglu")
    cfg_s = EncoderConfig(vocab_size=50, n_ops=5, n_pairs=7, d_model=32, n_layers=2, n_heads=4, ffn="swiglu")
    check(build(cfg_g).n_params() == build(cfg_s).n_params(), "SwiGLU and GeGLU cost the same parameters")
    old_cfg = EncoderConfig.from_json({k: v for k, v in cfg_g.to_json().items()
                                       if k not in ("norm", "bias", "emb_norm", "segments", "pooler", "lit_mlp")})
    check(old_cfg.norm == "layernorm" and old_cfg.bias and old_cfg.segments and old_cfg.pooler,
          "a config saved before these options existed loads with the old behaviour")
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
