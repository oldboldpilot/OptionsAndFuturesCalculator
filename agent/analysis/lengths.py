"""Input length (what an ENCODER processes, once) vs output length (what a
DECODER must generate, one sequential forward pass per token).

Decode on this stack is bandwidth-bound (CLAUDE.md: ~60 tok/s over 604MB of
Q8_0 weights). So the cost model is:
    decoder bytes read  ~= out_tokens x weight_bytes
    encoder bytes read  ~= 1          x weight_bytes
The ratio out_tokens is therefore the architectural speedup available BEFORE
any reduction in model size.
"""
import json, re, statistics, sys
P = re.compile(r"<params>(.*?)</params>", re.DOTALL)

def stats(name, path, limit=4000):
    ins, outs = [], []
    n = 0
    for line in open(path):
        if n >= limit: break
        row = json.loads(line); n += 1
        convs = row["conversations"]
        u = "\n".join(c["content"] for c in convs if c["role"] == "user")
        a = ""
        for c in convs:
            if c["role"] == "assistant":
                m = P.search(c["content"])
                if m: a = m.group(0)
        if not a: continue
        # Qwen3 tokenises one digit per token; JSON punctuation is ~1 token each.
        # chars/3.2 understates digit-dense JSON, so count digits separately.
        digits = sum(ch.isdigit() for ch in a)
        nondigit = len(a) - digits
        out_tok = digits + max(1, nondigit // 3)
        ins.append(max(1, len(u) // 4))      # English prose ~4 chars/token
        outs.append(out_tok)
    q = lambda xs, p: sorted(xs)[int(p*(len(xs)-1))]
    print(f"{name}  (n={len(ins)})")
    print(f"  input  tokens : mean {statistics.mean(ins):6.1f}  p50 {q(ins,.5):4d}  p95 {q(ins,.95):4d}  max {max(ins):4d}")
    print(f"  output tokens : mean {statistics.mean(outs):6.1f}  p50 {q(outs,.5):4d}  p95 {q(outs,.95):4d}  max {max(outs):4d}")
    print(f"  => decoder does ~{statistics.mean(outs):.0f}x the weight reads of one encoder pass")
    print()

stats("MORTGAGE", "/home/user/OptionsAndFuturesCalculator/agent/dataset/data_mortgage/val.jsonl")
stats("STRATEGY", "/home/user/OptionsAndFuturesCalculator/agent/dataset/data/val.jsonl")
