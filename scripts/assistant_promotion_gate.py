#!/usr/bin/env python3
"""Refuse to promote an assistant model on a pooled score alone.

    scripts/assistant_promotion_gate.py <holdout.jsonl> <candidate.json> <baseline.json> \
        [--train train.jsonl ...]

@author Olumuyiwa Oluwasanmi

WHY THIS IS A SCRIPT AND NOT A LINE IN A CHECKLIST. The requirement it enforces
was ALREADY WRITTEN DOWN -- docs/guides/ASSISTANT_EVALUATION.md, checklist item
8, "compare against the deployed model measured the same way in the same
session" -- and on 2026-09-14 it was skipped anyway, on a retrain, by someone
who had read the file. A pooled 82.0% was reported, the feature the retrain
existed for was never scored apart from it, and a `ComputeXnpv` regression of
6/9 -> 1/9 stayed invisible until the deployed model was probed by hand. A rule
nothing executes is a rule that gets skipped exactly when it matters.

WHAT IT REFUSES, and why each one is a defect a pooled score cannot show:

  MISSING BASELINE      a candidate measured against nothing. 82% means nothing
                        without the number it replaces; the model of record
                        scored 78.7% on a DIFFERENT holdout, which is not a
                        comparison.

  CONTAMINATION         a holdout row that is in EITHER model's training set.
                        Pass --train once per model being compared. This is not
                        hypothetical and it is not rare: on 2026-09-14 a v14
                        holdout was checked disjoint from v14's train and
                        compared against v13 anyway -- and 364 of its 600 rows
                        were byte-identical members of v13's train split. The
                        baseline had memorised 61% of the test, so the gate
                        reported eight regressions that were mostly the
                        baseline's memory. CLAUDE.md records the same defect at
                        304/600 on an earlier pair. A contaminated comparison
                        does not fail loudly; it fails in the older model's
                        favour, which is the direction that blocks a good
                        candidate.

  PER-OPERATION         an operation the baseline serves and the candidate does
  REGRESSION            not. 27 operations inside one score: ComputeXnpv going
                        6/9 -> 1/9 moves the pooled figure by 0.9 points and
                        reads as noise. This is the check that would have
                        caught it without anyone thinking to look.

  NEW ZERO              an operation with holdout rows and not one correct
                        answer, which the baseline did serve. Strictly a
                        special case of the above, reported separately because
                        it is the one that makes a feature unreachable.

It does NOT decide the trade-off. It puts both columns on screen and refuses to
call a regression a rounding error. Whether +17 rows that fail SAFE are worth
-5 that fail SILENTLY is a judgement, and the script's job is to make sure
nobody makes it by accident.
"""
import json
import re
import sys
from pathlib import Path

# A regression smaller than this is reported but does not refuse: one row on a
# nine-row operation is inside the noise of a single decode.
TOLERANCE = 1


def gold_ops(holdout: Path) -> list[str]:
    ops = []
    for line in holdout.read_text().splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        final = next((t.get("content", "") for t in reversed(row.get("conversations", []))
                      if t.get("role") == "assistant"), "")
        m = re.search(r'"operation"\s*:\s*"([^"]+)"', final)
        ops.append(m.group(1) if m else "")
    return ops


def per_op(ops: list[str], result: dict) -> dict[str, tuple[int, int]]:
    """operation -> (correct, total)."""
    failed = {f["row"] for f in result.get("failures", [])}
    out: dict[str, list[int]] = {}
    for i, op in enumerate(ops):
        if not op:
            continue
        slot = out.setdefault(op, [0, 0])
        slot[1] += 1
        if i not in failed:
            slot[0] += 1
    return {k: (v[0], v[1]) for k, v in out.items()}


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    holdout, cand_p, base_p = (Path(a) for a in sys.argv[1:4])
    trains = [Path(a) for a in sys.argv[5::2]] if "--train" in sys.argv[4:] else []
    rest = sys.argv[4:]
    trains = [Path(rest[i + 1]) for i, a in enumerate(rest) if a == "--train" and i + 1 < len(rest)]

    for p in (holdout, cand_p, base_p):
        if not p.exists() or p.stat().st_size == 0:
            print(f"REFUSING: {p} is missing or empty.\n"
                  f"  A candidate measured against nothing is not a comparison. Serve the\n"
                  f"  DEPLOYED model on this same holdout and score it the same way first.",
                  file=sys.stderr)
            return 2

    ops = gold_ops(holdout)

    # Restrict to rows no model under comparison was trained on. Restricting is
    # the right remedy rather than refusing outright: the clean subset is still
    # a valid measurement, and it is the only one both columns can be read from.
    keep = set(range(len(ops)))
    if trains:
        def convo_keys(path: Path) -> set[str]:
            out = set()
            for line in path.read_text().splitlines():
                if line.strip():
                    out.add(json.dumps(json.loads(line).get("conversations"),
                                       sort_keys=True, ensure_ascii=False))
            return out
        seen = set()
        for t in trains:
            if not t.exists():
                print(f"REFUSING: --train {t} does not exist.", file=sys.stderr)
                return 2
            seen |= convo_keys(t)
        hold = [json.dumps(json.loads(l).get("conversations"), sort_keys=True,
                           ensure_ascii=False)
                for l in holdout.read_text().splitlines() if l.strip()]
        keep = {i for i, k in enumerate(hold) if k not in seen}
        dropped = len(hold) - len(keep)
        print(f"contamination: {dropped}/{len(hold)} holdout rows appear in a training "
              f"set under comparison; scoring the {len(keep)} clean rows.\n")
        if not keep:
            print("REFUSING: no holdout row is clean for every model compared.",
                  file=sys.stderr)
            return 2
    else:
        print("WARNING: no --train given, so contamination was NOT checked. A row the\n"
              "         BASELINE memorised counts in its favour and reads as a candidate\n"
              "         regression. Pass --train once per model.\n")
    ops = [op if i in keep else "" for i, op in enumerate(ops)]
    cand = per_op(ops, json.loads(cand_p.read_text()))
    base = per_op(ops, json.loads(base_p.read_text()))

    print(f"{'operation':34s} {'baseline':>10s} {'candidate':>11s}   delta")
    regressions, new_zeros = [], []
    for op in sorted(set(cand) | set(base)):
        bc, bt = base.get(op, (0, 0))
        cc, ct = cand.get(op, (0, 0))
        d = cc - bc
        flag = ""
        if d < -TOLERANCE:
            regressions.append((op, bc, bt, cc, ct))
            flag = "  <-- REGRESSION"
        if cc == 0 and ct > 0 and bc > 0:
            new_zeros.append((op, bc, bt, ct))
            flag = "  <-- NEW ZERO"
        print(f"{op:34s} {bc:5d}/{bt:<4d} {cc:6d}/{ct:<4d} {d:+6d}{flag}")

    bsum = (sum(c for c, _ in base.values()), sum(t for _, t in base.values()))
    csum = (sum(c for c, _ in cand.values()), sum(t for _, t in cand.values()))
    print(f"\n{'POOLED':34s} {bsum[0]:5d}/{bsum[1]:<4d} {csum[0]:6d}/{csum[1]:<4d} "
          f"{csum[0] - bsum[0]:+6d}")

    if not regressions and not new_zeros:
        print("\nPASS -- no operation regressed beyond tolerance.")
        return 0

    print("\nREFUSING PROMOTION.")
    for op, bc, bt, ct in new_zeros:
        print(f"  {op}: baseline {bc}/{bt} -> candidate 0/{ct}. The operation became "
              f"unreachable; that is not a rounding error in the pooled figure.")
    for op, bc, bt, cc, ct in regressions:
        if any(op == z[0] for z in new_zeros):
            continue
        print(f"  {op}: {bc}/{bt} -> {cc}/{ct}.")
    print("\n  Promote anyway ONLY with the trade stated explicitly: what the candidate\n"
          "  gains, what it loses, and WHICH DIRECTION each failure takes. A refusal the\n"
          "  user sees beats a plausible answer to a question nobody asked.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
