#!/usr/bin/env python3
"""Emit a mortgage-assistant evaluation as Prolog facts, DERIVED from the run.

    scripts/emit_assistant_facts.py <val.jsonl> <eval.json> [out.pl]

@author Olumuyiwa Oluwasanmi

Same contract as the sibling repository's emit-workflow-facts.mjs: the facts
are read out of the artefacts the run actually produced and out of
finance.proto itself, never retyped. A rule base maintained by hand beside the
thing it describes has the identical drift problem it exists to detect -- and
in this project that drift has a body count: a hand-written operation list in
ANOTHER repository once refused thirteen of the twenty-seven operations the
engine served.

What the facts describe, and why each one earns its place:

  label_space(Op)          the 27 operations derived from finance.proto
  excluded_field(Op, F)    a field the operation's request message declares and
                           that operation DISCARDS -- OP_EXCLUDED_FIELDS, the
                           mirror of mortgage_verification.cppm's
                           kOperationExcludedFields
  holdout_row(I, Op)       gold operation for held-out row I
  correct(I)               row I matched gold exactly (raw params)
  emitted(I, Op)           the operation the model actually named on row I
  emitted_field(I, F)      a key the model put in its <params> block
  refusal_shape(S, N)      how the service refused, and how often

  down_payment_row(I)      a held-out row for make_down_payment_extraction --
                           the feature the 2026-09-14 retrain existed for. The
                           loan is NOT the stated price and must be DERIVED,
                           which is what the previous model could not do.
  stated_price(I, V)       the gross figure the utterance names
  gold_loan(I, V)          what the loan must be (price minus the down payment)
  emitted_loan(I, V)       what the model actually put in the loan slot

The QUESTIONS are in assistant_check.cpp. Every one is written so that a
healthy run returns NO solutions, because a coverage defect is invisible to any
aggregate score: 400/508 says nothing about whether one operation is 0/11.
"""
import hashlib
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "agent" / "dataset"))

import build_mortgage_dataset as G  # noqa: E402


def atom(s: str) -> str:
    """Prolog atom: lower_snake, quoted if it cannot be bare."""
    t = "".join(ch if (ch.isalnum() or ch == "_") else "_" for ch in str(s))
    if not t or not t[0].isalpha():
        t = "x_" + t
    return t.lower()


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    val_path, eval_path = Path(sys.argv[1]), Path(sys.argv[2])
    out_path = Path(sys.argv[3]) if len(sys.argv) > 3 else Path("/tmp/claude-1000/assistant-facts.pl")

    gold = []
    for line in val_path.read_text().splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        # The gold <params> live in the final assistant turn.
        op = ""
        for turn in reversed(row.get("conversations", [])):
            if turn.get("role") == "assistant":
                m = turn.get("content", "")
                i = m.find('"operation"')
                if i >= 0:
                    j = m.find('"', m.find(":", i) + 1)
                    k = m.find('"', j + 1)
                    op = m[j + 1:k]
                break
        gold.append(op)

    # The down-payment feature, identified from the ROW rather than asserted:
    # an utterance naming a down payment, on an operation whose loan slot must
    # therefore be derived. Scoring it apart from the pooled figure is the whole
    # point -- 17/29 on the feature is invisible inside 465/567.
    raw_rows = [json.loads(l) for l in val_path.read_text().splitlines() if l.strip()]
    dp: dict[int, tuple[str, str]] = {}
    for i, row in enumerate(raw_rows):
        utt = next((t.get("content", "") for t in row.get("conversations", [])
                    if t.get("role") == "user"), "")
        if not re.search(r"\b(down|deposit)\b", utt, re.I):
            continue
        final = next((t.get("content", "") for t in reversed(row.get("conversations", []))
                      if t.get("role") == "assistant"), "")
        # make_down_payment_extraction emits ONLY these two operations. Other
        # generators mention a down payment legitimately (closing costs,
        # rent-vs-buy) and deriving a loan is not the feature there -- without
        # this filter the row count went 29 -> 61 and the fact stopped meaning
        # what its name says.
        m_op = re.search(r'"operation"\s*:\s*"([^"]+)"', final)
        if not m_op or m_op.group(1) not in ("ComputePayment", "ComputeAmortization"):
            continue
        m_loan = re.search(r'"(?:loan_amount|present_value)"\s*:\s*"([\d.]+)"', final)
        if not m_loan:
            continue
        # The gross price is the LARGEST money figure the utterance states.
        money = [x.replace(",", "") for x in re.findall(r"\$([\d,]+)", utt)]
        if not money:
            continue
        price = f"{max(float(x) for x in money):.2f}"
        if price == m_loan.group(1):
            continue  # nothing to derive; not a feature row
        dp[i] = (price, m_loan.group(1))

    res = json.loads(eval_path.read_text())
    failures = {f["row"]: f for f in res.get("failures", [])}

    # THE FACTS AND THE RESULTS MUST DESCRIBE THE SAME ROWS.
    #
    # Every fact here is keyed by ROW INDEX, and an index only means anything
    # within one holdout file. Each corpus revision reshuffles the split, so
    # pairing a fresh val.jsonl with an older eval.json produces a fact base
    # that parses, solves, and answers about the wrong rows -- operation
    # confusions attributed to rows that never had that gold.
    #
    # Caught by doing exactly that on 2026-09-16: the new val.jsonl
    # (849e8a34...) against v17's results (af88ac9e...). Nothing complained,
    # because nothing was checking. `eval_grpc_mortgage.py` already records the
    # holdout's sha256 in its `--json-out`, so the check costs one comparison.
    #
    # REFUSES rather than warns, for the reason --assert-disjoint-from refuses:
    # a warning printed above a table of findings is read as a caveat, and the
    # findings are quoted anyway.
    holdout_sha = hashlib.sha256(val_path.read_bytes()).hexdigest()
    recorded = (res.get("holdout") or {}).get("sha256")
    if recorded and recorded != holdout_sha:
        raise SystemExit(
            f"holdout mismatch -- these describe different rows:\n"
            f"  {val_path}  sha256 {holdout_sha[:16]}...\n"
            f"  {eval_path} was scored on sha256 {recorded[:16]}...\n"
            f"Every fact is keyed by row index, and an index only means "
            f"something within one holdout. Re-score the model on this "
            f"holdout, or emit facts from the one it was scored on."
        )

    L = []
    L.append("% GENERATED by scripts/emit_assistant_facts.py -- do not edit.")
    L.append(f"% holdout: {val_path}  sha256 {holdout_sha[:16]}...")
    L.append(f"% eval   : {eval_path}")
    L.append("")

    for op in sorted(G.OPERATIONS):
        L.append(f"label_space({atom(op)}).")
    L.append("")
    for op, fields in sorted(G.OP_EXCLUDED_FIELDS.items()):
        for f in sorted(fields):
            L.append(f"excluded_field({atom(op)}, {atom(f)}).")
    L.append("")

    emitted_ops = set()
    for i, op in enumerate(gold):
        if not op:
            L.append(f"holdout_row({i}, non_params).")
        else:
            L.append(f"holdout_row({i}, {atom(op)}).")
        f = failures.get(i)
        if f is None:
            L.append(f"correct({i}).")
            if op:
                L.append(f"emitted({i}, {atom(op)}).")
                emitted_ops.add(op)
            continue
        got = f.get("got") or {}
        got_op = got.get("operation", "") if isinstance(got, dict) else ""
        if got_op:
            L.append(f"emitted({i}, {atom(got_op)}).")
            emitted_ops.add(got_op)
        if isinstance(got, dict):
            for key in sorted(k for k in got if k != "operation"):
                L.append(f"emitted_field({i}, {atom(key)}).")
    L.append("")
    for i, (price, loan) in sorted(dp.items()):
        L.append(f"down_payment_row({i}).")
        L.append(f"stated_price({i}, '{price}').")
        L.append(f"gold_loan({i}, '{loan}').")
        f = failures.get(i)
        got = (f.get("got") or {}) if f else {}
        emitted = None
        if isinstance(got, dict):
            emitted = got.get("loan_amount") or got.get("present_value")
        if f is None:
            emitted = loan
        if emitted is not None:
            L.append(f"emitted_loan({i}, '{emitted}').")
    L.append("")
    for shape, n in sorted(res.get("refusal_shapes", {}).items()):
        L.append(f"refusal_shape({atom(shape)}, {n}).")
    L.append("")
    L.append(f"raw_exact({res.get('raw_exact', 0)}).")
    L.append(f"raw_total({res.get('raw_total', 0)}).")

    out_path.write_text("\n".join(L) + "\n")
    print(f"wrote {out_path}  "
          f"{len(G.OPERATIONS)} operations, {len(gold)} holdout rows, "
          f"{len(failures)} failures, {len(emitted_ops)} distinct operations emitted, "
          f"{len(dp)} down-payment rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
