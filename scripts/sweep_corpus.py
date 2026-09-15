#!/usr/bin/env python3
"""Feed the derivation layer its own corpus and prove it corrupts nothing.

Two modes:

  sweep_corpus.py <val.jsonl> <out.tsv>      flatten a recorded corpus
  sweep_corpus.py --generate <n> <out.tsv>   sample CORPUS_MIX directly

THE GENERATED MODE IS THE GATE, and it is generated rather than checked in for
the reason `test_corpus_invariants.py` gives: a corpus is a pure function of
(seed, n, this generator), so a gate that samples `CORPUS_MIX` re-derives
itself whenever the corpus changes and cannot go stale beside it. A checked-in
fixture would assert the corpus of the day it was written.

WHAT IT PROVES, and why no accuracy number can. The sweep hands the layer the
GOLD params as though the model had emitted them perfectly, and reports every
field the layer then REWRITES. Rewriting a correct value is a served regression
by construction: the row still scores raw-exact and serves wrong, so `raw_exact`
is blind to it and only `served_exact` moves. That is exactly how a recency rule
keyed on percent alone cost 19 rows -- a HELOC reply of "75%" is a max-LTV, and
letting it supersede the stated 8.33% derived annual_rate = 0.7500 on rows the
model had answered perfectly.


Each ASSISTANT params turn becomes one row: the operation, everything the user
said BEFORE the latest turn, the latest turn, and the gold params. A row is
emitted per assistant answer, so a two-answer exchange contributes both its
first-turn and its revised state -- the revision is the case under test and the
opening turn is the control that must not move.

@author Olumuyiwa Oluwasanmi
"""
import json
import re
import sys

PARAMS = re.compile(r"<params>(.*?)</params>", re.S)


def esc(s: str) -> str:
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def rows_from(source) -> list[str]:
    out: list[str] = []
    for line in source:
            turns = (json.loads(line) if isinstance(line, str) else line).get(
                "conversations", [])
            users: list[str] = []
            # Was the PREVIOUS assistant turn an answer? Only then is the user's
            # next turn a revision -- a reply to a QUESTION supplies a missing
            # slot and supersedes nothing. `mortgage_assistant_service.cpp`
            # draws the same line on `prior_question`.
            prev_was_answer = False
            for t in turns:
                if t.get("role") == "user":
                    users.append(t.get("content", ""))
                elif t.get("role") == "assistant":
                    m = PARAMS.search(t.get("content", ""))
                    if not m:
                        prev_was_answer = False   # a question, not an answer
                        continue
                    if not users:
                        continue
                    try:
                        gold = json.loads(m.group(1))
                    except json.JSONDecodeError:
                        continue
                    op = gold.get("operation")
                    if not op:
                        continue
                    # `grounding_text` joins with a newline, and the service
                    # passes the LAST user turn as `latest`.
                    earlier = "\n".join(users[:-1])
                    latest = users[-1] if (len(users) > 1 and prev_was_answer) else ""
                    if not latest:
                        earlier = "\n".join(users)
                    out.append(
                        "\t".join([op, esc(earlier), esc(latest), esc(json.dumps(gold))])
                    )
                    prev_was_answer = True
    return out


def generated(n: int, seed: int = 20260914) -> list[dict]:
    """`n` rows sampled from the generator's own weighted mix."""
    import random
    from pathlib import Path

    sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "agent" / "dataset"))
    import build_mortgage_dataset as G  # noqa: PLC0415

    rng = random.Random(seed)
    fns = [f for _, f in G.CORPUS_MIX]
    weights = [w for w, _ in G.CORPUS_MIX]
    return [rng.choices(fns, weights=weights)[0](rng) for _ in range(n)]


def gate(binary: str, n: int) -> int:
    """Generate a corpus, sweep it, and return the sweep's own exit code."""
    import subprocess
    import tempfile

    with tempfile.NamedTemporaryFile("w", suffix=".tsv", delete=False) as tmp:
        tmp.write("\n".join(rows_from(generated(n))) + "\n")
        path = tmp.name
    proc = subprocess.run([binary, "--sweep", path], check=False)
    return proc.returncode


def main() -> int:
    argv = sys.argv[1:]
    if len(argv) >= 2 and argv[0] == "--gate":
        return gate(argv[1], int(argv[2]) if len(argv) > 2 else 500)
    if len(argv) == 3 and argv[0] == "--generate":
        rows = rows_from(generated(int(argv[1])))
        dest = argv[2]
    elif len(argv) == 2:
        with open(argv[0]) as f:
            rows = rows_from(list(f))
        dest = argv[1]
    else:
        print("usage: sweep_corpus.py <val.jsonl> <out.tsv>", file=sys.stderr)
        print("       sweep_corpus.py --generate <n> <out.tsv>", file=sys.stderr)
        return 2
    with open(dest, "w") as f:
        f.write("\n".join(rows) + "\n")
    print(f"{len(rows)} rows -> {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
