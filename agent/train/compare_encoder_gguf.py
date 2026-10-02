#!/usr/bin/env python3
"""Compare sensen's pair-head encode() against PyTorch over a SWEEP of utterances.

All three outputs are compared -- trunk-derived operation logits, per-literal pair logits,
and the SELECTED SET. A logit comparison alone would pass with the threshold rule wrong,
and the selected set is the whole point of the multi-label head.

@author Olumuyiwa Oluwasanmi
"""
import struct, sys
from pathlib import Path
import numpy as np, torch
sys.path.insert(0, ".")
from encoder_model import build, EncoderConfig

ck_path, bin_path = sys.argv[1], sys.argv[2]
# A corpus whose label space is single-label in practice (strategy: 2 pairs, no literal filling two) has
# NOTHING multi-label to show, and requiring it everywhere would fail a correct run. It is required only
# where the corpus admits it -- which is the distinction that kept this defect hidden in the first place.
require_multi = "--require-multi" in sys.argv[3:]
ck = torch.load(ck_path, map_location="cpu", weights_only=False)
cfg = EncoderConfig(**ck["config"]); model = build(cfg); model.load_state_dict(ck["state_dict"]); model.eval()
threshold = float(ck["meta"]["args"]["threshold"])

buf = Path(bin_path).read_bytes(); o = 0
def u32():
    global o
    v = struct.unpack_from("<I", buf, o)[0]; o += 4; return v
def f32(n):
    global o
    a = np.frombuffer(buf, dtype=np.float32, count=n, offset=o).copy(); o += 4 * n; return a
def u32a(n):
    global o
    a = np.frombuffer(buf, dtype=np.uint32, count=n, offset=o).copy(); o += 4 * n; return a

n_utt = u32()
worst_op = worst_pair = 0.0
scale_op = scale_pair = 0.0
sel_total = sel_match = 0
multi = empty = lits_total = 0
op_argmax_ok = True
for _ in range(n_utt):
    seed, n_tok, d = u32(), u32(), u32()
    trunk = f32(n_tok * d)
    n_lit, n_pairs, n_ops = u32(), u32(), u32()
    op_logits = f32(n_ops)
    lits = []
    for _ in range(n_lit):
        first, count = u32(), u32()
        pl = f32(n_pairs)
        sel = u32a(u32())
        lits.append((first, count, pl, sel))
    ids = torch.tensor([[(i * 37 + 5 + seed * 101) % (cfg.vocab_size - 2) + 1 for i in range(n_tok)]])
    lit_s = torch.tensor([[f for f, c, _, _ in lits]])
    lit_e = torch.tensor([[f + c for f, c, _, _ in lits]])
    z = torch.zeros_like(lit_s)
    with torch.no_grad():
        out = model(ids, torch.zeros_like(ids), None, lit_s, lit_e, z, z,
                    torch.ones_like(lit_s, dtype=torch.bool))
    po = out["op"][0].numpy()
    worst_op = max(worst_op, float(np.abs(po - op_logits).max()))
    scale_op = max(scale_op, float(np.abs(po).max()))
    op_argmax_ok &= int(po.argmax()) == int(op_logits.argmax())
    pp = out["pair"][0].numpy()
    for i, (first, count, pl, sel) in enumerate(lits):
        worst_pair = max(worst_pair, float(np.abs(pp[i] - pl).max()))
        scale_pair = max(scale_pair, float(np.abs(pp[i]).max()))
        want = np.flatnonzero(1.0 / (1.0 + np.exp(-pp[i].astype(np.float64))) >= threshold).astype(np.uint32)
        sel_total += 1
        sel_match += int(np.array_equal(want, sel))
        lits_total += 1
        multi += int(len(sel) > 1)
        empty += int(len(sel) == 0)
assert o == len(buf), f"{o} != {len(buf)} -- the dump and this reader disagree"

print(f"{n_utt} utterances, {lits_total} literals, threshold {threshold}")
# RELATIVE, because these are unnormalised logits. sensen's own 1e-3 device-agreement bound is stated on
# quantities of order 1, and an absolute bound on a logit of magnitude 26 is a 4e-5 relative bound in
# disguise -- a tolerance that tightens as the model gets more confident, which is the wrong direction.
rel_op = worst_op / max(1e-30, scale_op)
rel_pair = worst_pair / max(1e-30, scale_pair)
print(f"[operation] worst |PyTorch - sensen| {worst_op:.3e} on a scale of {scale_op:.3g} "
      f"= {rel_op:.1e} relative; argmax agrees everywhere: {op_argmax_ok}")
print(f"[pair logits] worst |PyTorch - sensen| {worst_pair:.3e} on a scale of {scale_pair:.3g} "
      f"= {rel_pair:.1e} relative  (bound: 1e-3 relative)")
print(f"[SELECTED SET] identical on {sel_match}/{sel_total} literals")
print(f"[multi-label] {multi} literals select >1 pair, {empty} select none"
      + ("  (REQUIRED: this corpus has multi-pair literals)" if require_multi else
         "  (not required: this corpus is single-label in practice)"))
ok = op_argmax_ok and rel_op < 1e-3 and rel_pair < 1e-3 and sel_match == sel_total and (multi > 0 or not require_multi)
print("RESULT:", "AGREE" if ok else "DISAGREE")
sys.exit(0 if ok else 1)

