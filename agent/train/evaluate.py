#!/usr/bin/env python3
"""
Measures whether the fine-tune actually does the job.

Exact match on the parsed params object, not on the string: two serialisations
of the same object are the same answer, and scoring text would report a failure
that is only key ordering. Field-level accuracy is reported alongside, because
"symbol right, expiry wrong" and "everything wrong" are different problems and a
single number hides which one you have.
"""

import argparse
import json
import re
from pathlib import Path
import torch
from transformers import AutoModelForCausalLM, AutoTokenizer


def parse_params(text):
    m = re.search(r"<params>(.*?)</params>", text, re.S)
    if not m:
        return None
    try:
        return json.loads(m.group(1))
    except Exception:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True)
    ap.add_argument("--val", required=True)
    ap.add_argument("--n", type=int, default=200)
    a = ap.parse_args()

    tok = AutoTokenizer.from_pretrained(a.model)
    model = AutoModelForCausalLM.from_pretrained(a.model, dtype=torch.bfloat16, device_map="cuda")
    model.eval()

    rows = [json.loads(line) for line in Path(a.val).read_text().splitlines() if line.strip()][
        : a.n
    ]
    exact = total = 0
    fields = {k: 0 for k in ["symbol", "asset_class", "strategy", "expiration_days", "quantity"]}
    non_param_ok = non_param_total = 0
    bad = []

    for r in rows:
        convo = r["conversations"]
        # Feed everything up to the final assistant turn; that turn is the target.
        prompt_turns, target = convo[:-1], convo[-1]["content"]
        # transformers 5.x returns a BatchEncoding here, not a bare tensor, so
        # it is expanded into generate() rather than passed positionally.
        enc = tok.apply_chat_template(
            prompt_turns, add_generation_prompt=True, return_tensors="pt", return_dict=True
        ).to("cuda")
        n_in = enc["input_ids"].shape[-1]
        with torch.no_grad():
            out = model.generate(
                **enc, max_new_tokens=96, do_sample=False, pad_token_id=tok.eos_token_id
            )
        gen = tok.decode(out[0][n_in:], skip_special_tokens=True)

        want, got = parse_params(target), parse_params(gen)
        if want is None:
            # A question or a refusal: score as correct if it also emitted no
            # params. Emitting parameters where the gold asks a question is the
            # failure that matters -- it acts on a request it should clarify.
            non_param_total += 1
            if got is None:
                non_param_ok += 1
            continue
        total += 1
        if got == want:
            exact += 1
        else:
            bad.append((prompt_turns[-1]["content"], want, got))
        for k in fields:
            if got and got.get(k) == want.get(k):
                fields[k] += 1

    print(f"params exact-match : {exact}/{total} = {exact / max(total, 1):.1%}")
    for k, v in fields.items():
        print(f"  {k:16} {v}/{total} = {v / max(total, 1):.1%}")
    print(
        f"non-params correct : {non_param_ok}/{non_param_total} "
        f"= {non_param_ok / max(non_param_total, 1):.1%}"
    )
    print("\nfirst mismatches:")
    for u, w, g in bad[:5]:
        print(f"  user: {u[:70]}")
        print(f"    want {w}")
        print(f"    got  {g}")


if __name__ == "__main__":
    main()
