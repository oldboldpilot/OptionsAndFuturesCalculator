#!/usr/bin/env python3
"""Post-training int8 for the tiny encoder, and the benchmark that says whether
it was worth doing.

@author Olumuyiwa Oluwasanmi

WHAT IS MEASURED, AND THE SHAPE IT IS MEASURED AT. Serving is BATCH 1: one
request, one sequence, no padding, so the benchmark feeds `pad=None` exactly as
`encoder_model.TinyEncoder` documents. Latency is the median of >= 200 timed
forward passes over distinct real validation requests (their natural lengths,
not one repeated shape), and `tokens/s` is total tokens over total time across
those same passes. Every variant is also scored for ACCURACY on the whole
validation split, because a quantised model that is faster and wrong has not
been measured, only sped up.

VARIANTS
  fp32              the trained weights.
  bf16 autocast     the training configuration (fp32 weights, bf16 matmuls).
  bf16 weights      the network converted to bf16 outright.
  int8 Linear       `torch.ao.quantization.quantize_dynamic` over `nn.Linear`:
                    int8 weights, activations quantised on the fly. This is the
                    requested baseline and the artifact saved.
  int8 Linear+Emb   the same plus the embedding table (weight-only int8). Added
                    because the embedding is the largest single block of a model
                    this small (V x d), so quantising only the Linear layers
                    leaves most of the bytes alone.

TRAPS, KEPT HERE SO THEY ARE NOT RE-LEARNED:
  * WARM EVERY SHAPE FIRST. oneDNN builds a primitive per input shape, and a
    batch-1 benchmark over real requests sees ~100 distinct lengths. Timing the
    first sight of each shape measures primitive creation, not inference. One
    untimed pass over the benchmark inputs precedes the timed passes.
  * A SHARED HOST LIES. Two PyTorch processes at 4 OpenMP threads each on 4 cores
    ran more than 20x slower than one (spin-waiting barriers), and while another
    job is running a latency figure describes the contention. Busy CPU is sampled
    from /proc/stat before and after each variant and recorded beside its number;
    `--wait-quiet` blocks until the host is idle, and a figure measured under load
    is marked CONTENDED, never silently quoted.
  * `torch.ao.quantization` is DEPRECATED in this torch (2.14: "will be removed
    in 2.10" and it still ships). It is used because it was asked for and works;
    the migration target is torchao's `quantize_`, which is not installed here and
    was not tested.
  * The saved int8 artifact is verified by RELOADING it and requiring bit-identical
    logits. `state_dict()` of a dynamically quantised module carries packed
    parameters, and an artifact that was never reloaded has not been shown to be one.
"""
from __future__ import annotations

import os

# Must precede the torch import. See the shared-host trap above; PASSIVE keeps
# OpenMP workers asleep between kernels instead of spinning on a core another
# process needs. `--omp-wait default` is honoured by re-exec-free means: the env
# var is only a default.
os.environ.setdefault("OMP_WAIT_POLICY", "PASSIVE")

import argparse
import copy
import io
import json
import statistics
import sys
import time
import warnings
from pathlib import Path
from typing import Sequence

import torch
import torch.nn as nn

sys.path.insert(0, str(Path(__file__).resolve().parent))
import encoder_corpus as C  # noqa: E402
import train_encoder as T  # noqa: E402
from encoder_model import TinyEncoder, build  # noqa: E402

warnings.filterwarnings("ignore", category=DeprecationWarning)
warnings.filterwarnings("ignore", category=UserWarning, module="torch.ao")


# ===========================================================================
# HOST QUIETNESS
# ===========================================================================
def _cpu_times() -> tuple[int, int]:
    with open("/proc/stat") as fh:
        f = [int(x) for x in fh.readline().split()[1:]]
    idle = f[3] + f[4]
    return sum(f), idle


def busy_fraction(window: float = 0.5) -> float:
    """Fraction of ALL cores busy over `window` seconds (this process included,
    so call it while this process is idle)."""
    t0, i0 = _cpu_times()
    time.sleep(window)
    t1, i1 = _cpu_times()
    return 1.0 - (i1 - i0) / max(1, t1 - t0)


def wait_quiet(limit: float, timeout: float) -> float:
    t_end = time.time() + timeout
    b = busy_fraction()
    while b > limit and time.time() < t_end:
        time.sleep(2.0)
        b = busy_fraction()
    return b


# ===========================================================================
# VARIANTS
# ===========================================================================
def int8_linear(model: TinyEncoder) -> nn.Module:
    import torch.ao.quantization as q
    return q.quantize_dynamic(copy.deepcopy(model).eval(), {nn.Linear}, dtype=torch.qint8)


def int8_linear_embedding(model: TinyEncoder) -> nn.Module:
    import torch.ao.quantization as q
    spec = {nn.Linear: q.default_dynamic_qconfig, nn.Embedding: q.float_qparams_weight_only_qconfig}
    m = copy.deepcopy(model).eval()
    # `padding_idx` embeddings are rejected by the weight-only converter; the pad
    # row is zero and masked, so clearing the attribute changes nothing here.
    for mod in m.modules():
        if isinstance(mod, nn.Embedding):
            mod.padding_idx = None
    return q.quantize_dynamic(m, spec, dtype=torch.qint8)


def bf16_weights(model: TinyEncoder) -> nn.Module:
    return copy.deepcopy(model).eval().to(torch.bfloat16)


def state_bytes(model: nn.Module) -> int:
    buf = io.BytesIO()
    torch.save(model.state_dict(), buf)
    return buf.getbuffer().nbytes


# ===========================================================================
# BENCHMARK
# ===========================================================================
def single_inputs(d: T.Data, n_pairs: int, n: int) -> list[dict]:
    """Batch-1, padding-free inputs for the first `n` distinct requests."""
    out = []
    for i in range(min(n, len(d))):
        b = T.get_batch(d, torch.tensor([i]), n_pairs, labels=False, pad_free=True)
        out.append(T.model_inputs(b))
    return out


@torch.inference_mode()
def bench(model: nn.Module, inputs: list[dict], autocast: bool, n_timed: int, passes_warm: int = 1
          ) -> dict:
    ac = lambda: torch.autocast("cpu", dtype=torch.bfloat16, enabled=autocast)  # noqa: E731
    for _ in range(passes_warm):                # one untimed pass over EVERY shape
        for x in inputs:
            with ac():
                model(**x)
    times, toks = [], []
    k = 0
    while len(times) < n_timed:
        x = inputs[k % len(inputs)]
        k += 1
        t0 = time.perf_counter()
        with ac():
            model(**x)
        times.append(time.perf_counter() - t0)
        toks.append(int(x["ids"].shape[1]))
    s = sorted(times)
    q = lambda p: s[min(len(s) - 1, int(p * (len(s) - 1)))]  # noqa: E731
    return dict(n=len(times), median_ms=1e3 * statistics.median(times), mean_ms=1e3 * statistics.mean(times),
                p95_ms=1e3 * q(0.95), p99_ms=1e3 * q(0.99), min_ms=1e3 * s[0],
                tok_per_s=sum(toks) / sum(times), mean_tokens=statistics.mean(toks))


def score(model: nn.Module, d: T.Data, sch: C.Schema, masks: T.Masks, autocast: bool) -> dict:
    res = T.evaluate(model, d, sch, masks, bf16=autocast)
    return dict(row_acc=res["row_acc"], op_acc=res["op_acc"], field_acc=res["field_acc"],
                pair_f1=res["pair"]["f1"], row_acc_params=res["row_acc_params_rows"])


def main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--checkpoint", type=Path, required=True, help="encoder_fp32.pt from train_encoder.py")
    ap.add_argument("--val", type=Path, required=True)
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--n-bench-inputs", type=int, default=300,
                    help="distinct validation requests the latency benchmark cycles over")
    ap.add_argument("--n-timed", type=int, default=600, help="timed forward passes per variant (>= 200)")
    ap.add_argument("--threads", type=int, nargs="+", default=[4, 1],
                    help="intra-op thread counts to benchmark; the first is the headline")
    ap.add_argument("--wait-quiet", type=float, default=0.0, metavar="SECONDS",
                    help="wait up to this long for the host to go idle before each variant")
    ap.add_argument("--quiet-limit", type=float, default=0.15,
                    help="busy-CPU fraction under which the host counts as quiet")
    ap.add_argument("--skip-accuracy", action="store_true")
    a = ap.parse_args(argv)
    if a.n_timed < 200:
        ap.error("--n-timed must be >= 200: a median of fewer is not a median")

    a.out_dir.mkdir(parents=True, exist_ok=True)
    model, cfg, sch, tok, meta = T.load_checkpoint(a.checkpoint)
    model.eval()
    masks = T.make_masks(sch)
    xva = C.build_examples([C.make_facts(dl, sch.op_key, sch.question_mode)
                            for dl in C.load_dialogues(a.val)], sch)
    dval = T.make_data(xva, tok, sch, cfg.max_len)
    inputs = single_inputs(dval, sch.n_pairs, a.n_bench_inputs)
    print(f"checkpoint {a.checkpoint}  params {model.n_params():,}  val rows {len(dval)}  "
          f"bench requests {len(inputs)} (mean {sum(x['ids'].shape[1] for x in inputs) / len(inputs):.1f} "
          f"tokens)  host cores {os.cpu_count()}", flush=True)

    variants: list[tuple[str, nn.Module, bool]] = [
        ("fp32", model, False),
        ("bf16 autocast", model, True),
        ("bf16 weights", bf16_weights(model), False),
        ("int8 Linear", int8_linear(model), False),
        ("int8 Linear+Emb", int8_linear_embedding(model), False),
    ]
    rows = []
    for name, m, ac in variants:
        row: dict = dict(variant=name)
        row["size_mb"] = state_bytes(m) / 1e6
        if not a.skip_accuracy:
            row.update(score(m, dval, sch, masks, ac))
        row["bench"] = {}
        for nt in a.threads:
            torch.set_num_threads(nt)
            before = wait_quiet(a.quiet_limit, a.wait_quiet) if a.wait_quiet else busy_fraction()
            r = bench(m, inputs, ac, a.n_timed)
            after = busy_fraction()
            r.update(busy_before=before, busy_after=after,
                     contended=max(before, after) > a.quiet_limit)
            row["bench"][nt] = r
        rows.append(row)
        h = row["bench"][a.threads[0]]
        print(f"  {name:16s} {row['size_mb']:6.2f} MB  "
              + (f"row acc {100 * row['row_acc']:.2f}%  " if "row_acc" in row else "")
              + f"{h['median_ms']:.2f} ms median @ {a.threads[0]}T"
              + ("  CONTENDED" if h["contended"] else ""), flush=True)
    torch.set_num_threads(a.threads[0])

    # ---- the saved artifact, reloaded and compared bit for bit
    q_model = variants[3][1]
    art = a.out_dir / "encoder_int8.pt"
    torch.save(dict(config=cfg.to_json(), state_dict=q_model.state_dict(), schema=sch.to_json(),
                    tokenizer=tok.to_str(), meta=dict(meta, quantization="dynamic int8 nn.Linear")), art)
    ck = torch.load(art, map_location="cpu", weights_only=False)
    fresh = int8_linear(build(cfg))
    fresh.load_state_dict(ck["state_dict"])
    with torch.inference_mode():
        diff = max(float((q_model(**x)["pair"] - fresh(**x)["pair"]).abs().max()) for x in inputs[:50])
        diff = max(diff, max(float((q_model(**x)["op"] - fresh(**x)["op"]).abs().max()) for x in inputs[:50]))
    print(f"\nint8 artifact {art} = {art.stat().st_size / 1e6:.3f} MB on disk; reload max |logit diff| "
          f"over 50 requests = {diff:g}")
    if diff != 0.0:
        print("ARTIFACT DOES NOT ROUND-TRIP", file=sys.stderr)

    # ---- table
    base = rows[0]["bench"][a.threads[0]]["median_ms"]
    print(f"\n{'variant':16s} {'size MB':>8s} {'row acc':>8s} {'op acc':>7s}"
          + "".join(f" | {nt}T ms med (p95)   tok/s " for nt in a.threads) + " speedup")
    for r in rows:
        line = f"{r['variant']:16s} {r['size_mb']:8.2f} "
        line += (f"{100 * r['row_acc']:7.2f}% {100 * r['op_acc']:6.2f}%" if "row_acc" in r else "       -       -")
        for nt in a.threads:
            b = r["bench"][nt]
            line += f" | {b['median_ms']:6.2f} ({b['p95_ms']:6.2f}) {b['tok_per_s']:8.0f}"
        line += f"   {base / r['bench'][a.threads[0]]['median_ms']:.2f}x"
        print(line)
    busy = max(max(b["busy_before"], b["busy_after"]) for r in rows for b in r["bench"].values())
    print(f"\nmax host busy fraction observed around the benchmarks: {100 * busy:.0f}%  "
          f"(quiet limit {100 * a.quiet_limit:.0f}%)  load average {os.getloadavg()}")
    (a.out_dir / "bench.json").write_text(json.dumps(
        dict(checkpoint=str(a.checkpoint), params=model.n_params(), rows=rows,
             load_average=os.getloadavg(), artifact_bytes=art.stat().st_size,
             artifact_reload_max_abs_diff=diff), indent=1, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main())
