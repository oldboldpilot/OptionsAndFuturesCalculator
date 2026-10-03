#!/usr/bin/env python3
"""Generate the fixture and Python oracle for the encoder_reconstruct parity gate.

@author Olumuyiwa Oluwasanmi

WHAT THIS PRODUCES
------------------
Three files, into --out:

  schema.json   the label schema, exactly as export_encoder_gguf.py writes it into
                the GGUF as `sensen-encoder.schema_json` -- so C++ reads the same
                bytes the engine will read, not a second hand-written copy.
  fixture.json  one object per holdout row: {row, op, lits, lit_pairs, conv}. These
                are the GOLD labels, i.e. what a PERFECT model would emit. Using
                gold rather than model predictions is deliberate: this gate is about
                reconstruct(), and feeding it predictions would confound a C++ port
                defect with a model error.
  python.ndjson the Python oracle -- reconstruct() run on each fixture row.

THE ORACLE CONTROL
------------------
Before writing anything it asserts that Python's reconstruct() on the GOLD pairs
reproduces the GOLD params on every row. If that fails, the fixture is wrong and
no C++ result read against it would mean anything -- so it REFUSES rather than
emitting a fixture that would make C++ look broken (or, worse, make a broken C++
look correct). A gate whose oracle is unverified is not a gate.

USAGE
-----
  make_reconstruct_parity_fixture.py --ckpt <encoder_fp32.pt> --out <dir>

The checkpoint supplies the schema (it is built with the corpus and stored in the
checkpoint), and --corpus defaults to the same holdout the trainer validated on.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import encoder_corpus as ec  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ckpt", required=True, help="encoder_fp32.pt holding the schema")
    ap.add_argument("--corpus", default="agent/dataset/data_mortgage/val.jsonl")
    ap.add_argument("--out", required=True)
    ap.add_argument("--question-mode", default="",
                    help="optional assertion; the schema's own value is what is used")
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()

    import torch

    ck = torch.load(args.ckpt, map_location="cpu", weights_only=False)
    raw = ck["schema"]
    schema_obj = raw if isinstance(raw, dict) else json.loads(raw)
    # from_json takes the parsed DICT, not a JSON string.
    sch = ec.Schema.from_json(schema_obj)

    # The question mode is a property OF THE SCHEMA, not a flag: it decides how
    # make_facts renders a clarification turn, and a CLI default disagreeing with
    # the checkpoint would silently build facts the schema's pair ids do not
    # describe. Taken from the schema, with the flag only able to confirm it.
    q_mode = sch.question_mode
    if args.question_mode and args.question_mode != q_mode:
        print(f"REFUSED: --question-mode {args.question_mode!r} disagrees with the "
              f"schema's own {q_mode!r}")
        return 2

    dialogues = ec.load_dialogues(Path(args.corpus), limit=args.limit or None)
    facts = [ec.make_facts(d, sch.op_key, q_mode) for d in dialogues]
    examples = ec.build_examples(facts, sch)

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    # ------------------------------------------------------------------ oracle
    # Python reconstruct() on GOLD must reproduce GOLD. Counted in both
    # directions so a vacuous pass is impossible: a run that scored 0 rows would
    # otherwise report "0 mismatches".
    checked = ok = 0
    bad: list[int] = []
    for i, ex in enumerate(examples):
        got = ec.reconstruct(sch, ex.op, ex.lits, ex.pairs, ex.conv)
        checked += 1
        match, _ = ec.params_match(got, ex.gold)
        if match:
            ok += 1
        else:
            bad.append(i)
    if checked == 0:
        print("REFUSED: 0 rows -- nothing was lexed. Check --corpus and its JSONL key "
              "(it is `conversations`, not `messages`).")
        return 2
    print(f"ORACLE CONTROL: Python reconstruct on GOLD pairs matches gold on {ok}/{checked} rows")
    if ok != checked:
        print(f"REFUSED: {len(bad)} rows do not reproduce their own gold; "
              f"first few: {bad[:10]}")
        return 2

    # ----------------------------------------------------------------- outputs
    (out / "schema.json").write_text(
        json.dumps(schema_obj, separators=(",", ":"), sort_keys=True))

    fixture = [{
        "row": i,
        "op": ex.op,
        "lits": ex.lits,
        "lit_pairs": ex.pairs,
        "conv": ex.conv,
    } for i, ex in enumerate(examples)]
    (out / "fixture.json").write_text(
        json.dumps(fixture, separators=(",", ":"), sort_keys=True))

    with (out / "python.ndjson").open("w") as fh:
        for i, ex in enumerate(examples):
            params = ec.reconstruct(sch, ex.op, ex.lits, ex.pairs, ex.conv)
            fh.write(json.dumps({"row": i, "params": params},
                                separators=(",", ":"), sort_keys=True,
                                default=str) + "\n")

    nonnull = sum(1 for ex in examples if ex.gold is not None)
    print(f"rows {len(examples)}  non-null params {nonnull}")
    print(f"wrote {out}/schema.json, fixture.json, python.ndjson")
    print("\nNext:")
    print(f"  ./backend/build/test_encoder_reconstruct_parity {out}/schema.json "
          f"{out}/fixture.json > {out}/cpp.ndjson")
    print(f"  python3 scripts/check_encoder_reconstruct_parity.py "
          f"{out}/python.ndjson {out}/cpp.ndjson --expect-rows {len(examples)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
