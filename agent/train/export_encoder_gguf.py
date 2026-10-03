#!/usr/bin/env python3
"""Export a trained tiny encoder to a GGUF that sensen's `text_encoder.cppm` loads.

@author Olumuyiwa Oluwasanmi

    python3 export_encoder_gguf.py --checkpoint OUT/encoder_fp32.pt --out model.gguf

WHY THIS EXISTS. `src/text_encoder.cppm` can load an encoder from GGUF and its own
header says what nobody had produced: "NOT checked: a GGUF produced by
src/gguf_exporter.cppm or by a PyTorch trainer". This is that producer, and it is
the last link between a model the trainer measures and a model the engine serves.

THE HEAD MISMATCH THIS SCRIPT USED TO REFUSE IS FIXED IN THE LOADER, and how it was
diagnosed is worth more than the fix. The trainer learns ONE MULTI-LABEL head over
(slot, map) PAIRS. sensen's loader used to offer only two SINGLE-label softmax heads it
argmaxed independently, so a literal filling several parameters -- a stated price that
is both `loan_amount` AND `original_home_value` -- had no representation at all.
Measured on the corpora (agent/dataset, 2026-10-02):

    corpus     pairs  slots x maps     literals filling >1 pair     single-label ceiling
    strategy       2   2 x  1 =    2   0                            1500/1500 = 100.00%
    mortgage     109  97 x 11 = 1067   158 of 3,381, on 139 rows     461/600 =  76.83%

**STRATEGY SCORING 100% WAS NOT EVIDENCE THE DESIGN WORKED.** Its label space is
ACCIDENTALLY single-label -- one map, and no literal that fills two slots -- so the
defect was unreachable from it in principle. A corpus that cannot exhibit a failure is
not a control for it, and reading 100% as a pass is what kept the hole open while the
mortgage ceiling was written down as a property of mortgage.

So the fix is `HeadKind::LiteralPair` and `encoder.pair.weight {d, n_pairs}` in
sensen -- an independent sigmoid per pair against a threshold carried in the file,
multi-label, mutually exclusive with the slot and map heads. The servable pair head is
a single `nn.Linear(d, n_pairs)`, the same shape as the slot and map heads, so this
needed no new input and no MLP. **`--heads pair` is now the default and loses nothing.**

`--heads slot_map` keeps the old lossy rewrite for a loader predating `encoder.pair`,
with its bijectivity proof and its refusals intact: exact only when
`n_pairs == n_slots * n_maps` AND `n_maps == 1`, which strategy satisfies (2 == 2 x 1)
and mortgage does not (109 != 1067). It is never chosen for you.

THE THRESHOLD IS READ FROM THE CHECKPOINT, NEVER GUESSED. `meta.args.threshold` is the
value the trainer scored row accuracy at, and a serving threshold this script picked
would be the `layer_norm_rms_epsilon` defect again: nothing fails, a different model
serves. sensen refuses a pair-head file that carries no threshold.

DIMENSION ORDER. GGUF records `{in, out}` while torch holds `[out, in]`; the gguf
writer reverses on write, so a torch `weight` goes in verbatim. The fused projections
are split the way `Block.forward` consumes them, which is the only thing that makes
this correct rather than plausible: `qkv.weight` is `[3d, d]` viewed as `[3, h, dh]`
so it is q, k, v in thirds; `fc.weight` is `[2*ffn, d]` and `g, u = y.chunk(2)` makes
the FIRST half gate and the second up.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import torch

try:
    import gguf
except ImportError:
    print("the `gguf` package is required: pip install gguf", file=sys.stderr)
    raise

ARCH = "sensen-encoder"
# GGUFParser classifies by SUBSTRING, so a name containing any of these would be
# silently read as that family. text_encoder.cppm static_asserts the same list.
_FAMILIES = ("llama", "qwen", "deepseek", "mistral", "mixtral", "gemma", "phi",
             "starcoder")
assert not any(f in ARCH for f in _FAMILIES), f"{ARCH} would be misclassified"


def slot_map_vocab(schema: dict) -> tuple[list[str], list[str]]:
    """The slot and map vocabularies, sorted, so an index is a function of the schema."""
    pairs = [tuple(p) for p in schema["pairs"]]
    return sorted({p[0] for p in pairs}), sorted({p[1] for p in pairs})


def factorisation_is_exact(schema: dict) -> tuple[bool, str]:
    """`n_pairs == n_slots * n_maps` iff every (slot, map) cell is a real pair."""
    pairs = [tuple(p) for p in schema["pairs"]]
    slots, maps = slot_map_vocab(schema)
    n_p, n_s, n_m = len(pairs), len(slots), len(maps)
    if n_p != n_s * n_m:
        return False, (f"{n_p} pairs but {n_s} slots x {n_m} maps = {n_s * n_m}: the pair "
                       f"index is not a bijection onto the slot x map grid, so a slot head "
                       f"and a map head cannot reproduce the pair head's scores. sensen's "
                       f"loader also argmaxes each head ONCE, so a literal carrying two "
                       f"pairs loses one -- 139 of 600 mortgage rows, a 76.83% ceiling. "
                       f"sensen HAS an `encoder.pair.weight {{d, n_pairs}}` tensor now: "
                       f"drop --heads slot_map and the whole rewrite goes away.")
    if len(set(pairs)) != n_p:
        return False, "the schema repeats a (slot, map) pair"
    return True, f"{n_p} pairs == {n_s} slots x {n_m} maps: exact"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--checkpoint", type=Path, required=True,
                    help="encoder_fp32.pt written by train_encoder.py")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--pooling", default="cls", choices=["cls", "mean", "none"])
    ap.add_argument("--literal-reduction", default="mean_logits",
                    choices=["mean_logits", "first_token"])
    ap.add_argument("--heads", default="pair", choices=["pair", "slot_map"],
                    help="pair (default): write the MULTI-LABEL encoder.pair head the "
                         "trainer actually learned -- no rewrite, no loss. slot_map: the "
                         "lossy single-label rewrite, for a loader predating encoder.pair; "
                         "exact only when n_pairs == n_slots x n_maps and n_maps == 1.")
    ap.add_argument("--threshold", type=float, default=None,
                    help="the pair-selection probability threshold. DEFAULT: the one the "
                         "checkpoint was scored at (meta.args.threshold). Pass it only to "
                         "override that deliberately.")
    ap.add_argument("--dtype", default="f32", choices=["f32", "f16"])
    ap.add_argument("--norm-eps", type=float, default=1e-6,
                    help="RMSNorm epsilon. 1e-6 is encoder_model.RMSNorm's constructor "
                         "default and EncoderConfig carries no epsilon field, so this is "
                         "the value the trained model used. Do not change it to make a "
                         "comparison pass.")
    a = ap.parse_args()

    ck = torch.load(a.checkpoint, map_location="cpu", weights_only=False)
    sd = ck["state_dict"]
    cfg = ck["config"]
    schema = ck["schema"] if isinstance(ck["schema"], dict) else json.loads(ck["schema"])
    args = ck["meta"]["args"] if "meta" in ck else {}

    d = int(cfg["d_model"])
    n_layers = int(cfg["n_layers"])
    n_heads = int(cfg["n_heads"])
    # d_ffn is a @property on EncoderConfig, so asdict() omits it. Read it from the
    # TENSOR instead of recomputing the formula: fc.weight is [2*d_ffn, d] for a gated
    # FFN, so the file itself states its own width and the two cannot disagree.
    _fc = sd["blocks.0.fc.weight"]
    gated = cfg.get("ffn") in ("geglu", "swiglu")
    d_ffn = int(_fc.shape[0] // (2 if gated else 1))
    _formula = int(round(d * (8 / 3 if gated else 4) / 8) * 8)
    if d_ffn != _formula:
        print(f"[note] d_ffn {d_ffn} from the tensor, {_formula} from the formula; "
              f"trusting the tensor")
    max_len = int(cfg["max_len"])
    n_ops = int(cfg["n_ops"])

    # ---- the refusals, before a single byte is written -----------------------------
    if cfg.get("ffn") != "swiglu":
        print(f"REFUSED: ffn is {cfg.get('ffn')!r}; the loader requires ffn_activation "
              f"\"swiglu\" and reuses sensen's TransformerBlock, which has no other "
              f"activation. Retrain with --servable.", file=sys.stderr)
        return 2
    if cfg.get("norm") != "rmsnorm":
        print(f"REFUSED: norm is {cfg.get('norm')!r}; sensen's TransformerBlock is "
              f"RMSNorm. Retrain with --servable.", file=sys.stderr)
        return 2
    if cfg.get("pos") != "rope":
        print(f"REFUSED: positions are {cfg.get('pos')!r}; sensen applies RoPE "
              f"unconditionally with no off switch, so learned absolute positions "
              f"produce weights the serving path cannot run. Retrain with --servable.",
              file=sys.stderr)
        return 2
    if cfg.get("bias"):
        print("REFUSED: the transformer stack has biases; sensen's TransformerBlock is "
              "bias-free and the loader's tensor set names no attn/ffn bias. Retrain "
              "with --servable.", file=sys.stderr)
        return 2
    slots, maps = slot_map_vocab(schema)
    pairs = [tuple(p) for p in schema["pairs"]]
    s_ix = {s: i for i, s in enumerate(slots)}

    # ---- the pair head is read once, whichever path is taken -----------------------
    if "pair_head.weight" not in sd:
        print("REFUSED: the checkpoint has no pair_head.weight, so it was trained "
              "--heads slot_map. Export it by writing its own slot and map heads, or "
              "retrain with the default (pair) heads.", file=sys.stderr)
        return 2
    w_pair = sd["pair_head.weight"].float()            # [n_pairs, lit_in]
    b_pair = sd["pair_head.bias"].float()              # [n_pairs]
    lit_in = w_pair.shape[1]
    if lit_in != d:
        print(f"REFUSED: the pair head reads {lit_in} features, not d={d}. The loader "
              f"feeds the literal's pooled hidden state straight to the head, so "
              f"--servable (no literal MLP, no tag/magnitude features) is required.",
              file=sys.stderr)
        return 2
    if w_pair.shape[0] != len(pairs):
        print(f"REFUSED: the pair head has {w_pair.shape[0]} rows and the schema names "
              f"{len(pairs)} pairs; the checkpoint and its schema disagree.", file=sys.stderr)
        return 2

    # ---- the THRESHOLD, read from the checkpoint rather than chosen here ------------
    trained_threshold = args.get("threshold") if isinstance(args, dict) else None
    threshold = a.threshold if a.threshold is not None else trained_threshold
    if a.heads == "pair":
        if threshold is None:
            print("REFUSED: no pair-selection threshold. The checkpoint records no "
                  "meta.args.threshold and none was passed, and a threshold this script "
                  "invented is the layer_norm_rms_epsilon defect -- nothing fails and a "
                  "different model serves. Pass --threshold explicitly.", file=sys.stderr)
            return 2
        if not (0.0 < float(threshold) < 1.0):
            print(f"REFUSED: threshold {threshold} is not strictly inside (0, 1); sensen "
                  f"refuses it too (0 selects every pair, 1 selects none).", file=sys.stderr)
            return 2
        src = "--threshold" if a.threshold is not None else "the checkpoint's meta.args.threshold"
        print(f"[threshold] pair_threshold = {float(threshold):g}, from {src}")

    # ---- the LOSSY slot_map rewrite: opt-in only, with its proof intact -------------
    # pair p scores w_pair[p] . h + b_pair[p]. Under the bijection each p is exactly one
    # (slot, map) cell, so putting w_pair[p] on the SLOT row recovers every pair score
    # when the map head is constant -- which it is whenever n_maps == 1. For n_maps > 1
    # the sum of two marginals cannot in general reproduce n_s*n_m independent rows.
    #
    # ALL OF IT IS SKIPPED ON THE DEFAULT PATH. The pair head is written as itself, so
    # there is no factorisation to be exact and nothing to prove.
    w_slot = b_slot = w_map = b_map = None
    if a.heads == "slot_map":
        ok, why = factorisation_is_exact(schema)
        print(f"[factorisation] {why}")
        if not ok:
            print(f"REFUSED: {why}", file=sys.stderr)
            return 3
        if len(maps) != 1:
            print(f"REFUSED: {len(maps)} maps. The bijection holds but a slot row plus a map "
                  f"row is a SUM of two marginals, which cannot reproduce "
                  f"{len(pairs)} independent pair scores unless one factor is constant. "
                  f"Only n_maps == 1 is exact here.", file=sys.stderr)
            return 3
        # A literal filling several pairs loses all but one under sensen's single argmax, and
        # that loss is INVISIBLE in this script's own arithmetic -- the rewrite below is exact
        # on the SCORES and the cap is in the DECODE. So it is counted here, from the schema,
        # rather than left for a holdout run to discover.
        multi = sum(1 for _, cnt in
                    [(k, sum(1 for s2, _ in pairs if s2 == k)) for k in {s3 for s3, _ in pairs}]
                    if cnt > 1)
        if multi:
            print(f"[warn] {multi} slot(s) appear in more than one pair; sensen argmaxes the "
                  f"slot head ONCE, so a literal filling two of them loses one. This is the "
                  f"76.83% mortgage ceiling, and --heads pair does not have it.")
        w_slot = torch.zeros(len(slots), d)
        b_slot = torch.zeros(len(slots))
        for p, (s, m) in enumerate(pairs):
            w_slot[s_ix[s]] = w_pair[p]
            b_slot[s_ix[s]] = b_pair[p]
        w_map = torch.zeros(len(maps), d)              # one map: argmax is 0 regardless
        b_map = torch.zeros(len(maps))

        # ---- verify the rewrite reproduces the pair scores, on random inputs ------------
        h = torch.randn(256, d)
        pair_scores = h @ w_pair.T + b_pair                    # [256, n_pairs]
        slot_scores = h @ w_slot.T + b_slot                    # [256, n_slots]
        recon = torch.stack([slot_scores[:, s_ix[s]] for s, _ in pairs], dim=1)
        worst = float((pair_scores - recon).abs().max())
        print(f"[rewrite] worst |pair score - (slot row) score| over 256 random states: {worst:.3e}")
        if worst > 1e-6:
            print("REFUSED: the rewrite does not reproduce the pair head.", file=sys.stderr)
            return 4
        agree = int((pair_scores.argmax(1) == recon.argmax(1)).sum())
        print(f"[rewrite] argmax agrees on {agree}/256 random states")
        if agree != 256:
            print("REFUSED: the rewrite changes the argmax.", file=sys.stderr)
            return 4

    # ---- write ---------------------------------------------------------------------
    ftype = gguf.GGMLQuantizationType.F32 if a.dtype == "f32" else gguf.GGMLQuantizationType.F16
    npdt = np.float32 if a.dtype == "f32" else np.float16
    w = gguf.GGUFWriter(str(a.out), ARCH)
    w.add_string("general.name", f"sensen tiny encoder ({args.get('op_key', '?')})")

    def kv_u32(k, v): w.add_uint32(f"{ARCH}.{k}", int(v))
    kv_u32("context_length", max_len)
    kv_u32("embedding_length", d)
    kv_u32("block_count", n_layers)
    kv_u32("attention.head_count", n_heads)
    kv_u32("feed_forward_length", d_ffn)
    kv_u32("operation_count", n_ops)
    # EXACTLY ONE per-literal head: sensen refuses a file declaring both, because two
    # incompatible descriptions of one label space leave which serves to read order.
    kv_u32("slot_count", len(slots) if a.heads == "slot_map" else 0)
    kv_u32("map_count", len(maps) if a.heads == "slot_map" else 0)
    kv_u32("pair_count", len(pairs) if a.heads == "pair" else 0)
    if a.heads == "pair":
        # Required by the loader IFF pair_count > 0, and REFUSED when it is 0.
        w.add_float32(f"{ARCH}.pair_threshold", float(threshold))
    # eps IS NOT A CONFIG FIELD, and asserting a value here is how this went wrong.
    # `RMSNorm.__init__` defaults to 1e-6 and EncoderConfig carries no epsilon, so the
    # trained model's norms ALWAYS use 1e-6; writing 1e-5 put the sensen trunk
    # 1.809e-01 away from PyTorch's on identical ids, because three norms a block over
    # three blocks plus the final one compounds a factor-of-ten epsilon. Nothing
    # refuses a wrong value either -- the loader reads exactly one eps key and the
    # parser would otherwise default it -- so it just serves a different model.
    # --norm-eps exists for a model whose RMSNorm default ever changes.
    w.add_float32(f"{ARCH}.attention.layer_norm_rms_epsilon", float(a.norm_eps))
    print(f"[eps] layer_norm_rms_epsilon = {a.norm_eps:g} (RMSNorm's own default)")
    w.add_float32(f"{ARCH}.rope.freq_base", float(cfg.get("rope_base", 10000.0)))
    w.add_bool(f"{ARCH}.attention.causal", False)      # MUST be false: bidirectional
    w.add_string(f"{ARCH}.pooling", a.pooling)
    w.add_string(f"{ARCH}.literal_reduction", a.literal_reduction)
    w.add_string(f"{ARCH}.ffn_activation", "swiglu")
    # sort_keys=True and the compact separators are DELIBERATE: the string must be
    # byte-reproducible for the same schema, so two exports of one checkpoint produce
    # identical bytes.
    blob = json.dumps(schema, separators=(',', ':'), sort_keys=True)
    w.add_string(f"{ARCH}.schema_json", blob)
    print(f"[schema] schema_json = {len(blob):,} bytes ({len(schema['pairs'])} pairs, {len(schema['ops'])} ops)")

    def put(name: str, t: torch.Tensor) -> None:
        arr = t.detach().contiguous().to(torch.float32).numpy().astype(npdt)
        w.add_tensor(name, arr, raw_dtype=ftype)

    put("token_embd.weight", sd["tok.weight"])
    for i in range(n_layers):
        p = f"blocks.{i}."
        put(f"blk.{i}.attn_norm.weight", sd[p + "ln1.weight"])
        qkv = sd[p + "qkv.weight"].float()             # [3d, d]: q, k, v in thirds
        put(f"blk.{i}.attn_q.weight", qkv[0 * d:1 * d])
        put(f"blk.{i}.attn_k.weight", qkv[1 * d:2 * d])
        put(f"blk.{i}.attn_v.weight", qkv[2 * d:3 * d])
        put(f"blk.{i}.attn_output.weight", sd[p + "proj.weight"])
        put(f"blk.{i}.ffn_norm.weight", sd[p + "ln2.weight"])
        fc = sd[p + "fc.weight"].float()               # [2*ffn, d]: gate FIRST, then up
        put(f"blk.{i}.ffn_gate.weight", fc[0:d_ffn])
        put(f"blk.{i}.ffn_up.weight", fc[d_ffn:2 * d_ffn])
        put(f"blk.{i}.ffn_down.weight", sd[p + "out.weight"])
    put("output_norm.weight", sd["ln_f.weight"])
    put("encoder.operation.weight", sd["op_head.weight"])
    put("encoder.operation.bias", sd["op_head.bias"])
    if a.heads == "pair":
        put("encoder.pair.weight", w_pair)
        put("encoder.pair.bias", b_pair)
    else:
        put("encoder.slot.weight", w_slot)
        put("encoder.slot.bias", b_slot)
        put("encoder.map.weight", w_map)
        put("encoder.map.bias", b_map)

    w.write_header_to_file()
    w.write_kv_data_to_file()
    w.write_tensors_to_file()
    w.close()

    size = a.out.stat().st_size
    head_desc = (f"pairs={len(pairs)} (MULTI-LABEL, threshold {float(threshold):g})"
                 if a.heads == "pair" else f"slots={len(slots)} maps={len(maps)} (single-label)")
    print(f"[written] {a.out}  {size:,} bytes  arch={ARCH}  d={d} layers={n_layers} "
          f"heads={n_heads} ffn={d_ffn} ops={n_ops} schema={len(blob):,} bytes {head_desc}")
    side = a.out.with_suffix(".slotmap.json")
    side.write_text(json.dumps({"heads": a.heads, "slots": slots, "maps": maps,
                                "pairs": [list(p) for p in pairs],
                                "pair_threshold": (float(threshold) if a.heads == "pair" else None)},
                               indent=1))
    print(f"[written] {side}  the index order the engine must agree with "
          f"({'pair' if a.heads == 'pair' else 'slot/map'})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
