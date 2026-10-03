#!/usr/bin/env python3
"""Compare sensen's WordPiece tokenizer against the Python trainer's, per utterance.

@author Olumuyiwa Oluwasanmi

WHY THIS GATE EXISTS
--------------------
The encoder's per-literal heads point AT a numeric literal, and a literal is matched
to tokens by SPAN. So a tokenizer disagreement does not degrade an answer gracefully:
either the model reads a different token sequence than it was trained on, or the
literal's span no longer lines up with token boundaries. The second case is at least
loud -- sensen's `tokensInSpan` requires every token overlapping a literal to lie
WHOLLY inside it and returns `TokenStraddlesLiteral` otherwise, verified in
backend/sensen/src/text_encoder.cppm -- but the first is silent.

CHARACTER OFFSETS AGAINST BYTE OFFSETS, AND WHY THE CONVERSION IS HERE
----------------------------------------------------------------------
HuggingFace `tokenizers` reports per-token offsets as UNICODE CHARACTER indices into
the Python `str`. sensen's `TokenizeResult::offsets` are indices into a
`std::string_view`, i.e. UTF-8 BYTE offsets. On pure ASCII the two numbers coincide,
and the mortgage holdout is 100% ASCII (754 of 754 utterances) -- so a naive
comparison PASSES on this corpus while comparing two different coordinate systems.
That is a measurement about the corpus, not an identity, and it is exactly the shape
of agreement this repository distrusts.

So this comparator CONVERTS: Python's character offsets are mapped to byte offsets
through the utterance's own UTF-8 encoding before anything is compared. The gate is
then correct for any input, and a non-ASCII utterance is a case it handles rather
than a case it cannot see. `--self-test` includes a multi-byte row that FAILS without
the conversion, so the conversion is proven to be load-bearing rather than assumed.

THE NO-TEXT SENTINEL
--------------------
A token with no source text ([CLS], [SEP], padding) carries `TokenSpan::kNone` in
sensen and `(0, 0)` in HuggingFace. Those are NOT the same statement: (0, 0) is a
zero-width span AT character 0, which passes a containment test whenever a literal
starts at character 0. The C++ probe emits kNone as [-1,-1], and this comparator
treats [-1,-1] and [0,0] as equal ONLY for a token the vocabulary says is special.
Anywhere else they are a disagreement.

USAGE
-----
  check_encoder_tokenizer_parity.py <fixture.json> <cpp.ndjson> <vocab.txt>
  check_encoder_tokenizer_parity.py --self-test
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

SPECIAL = ("[PAD]", "[UNK]", "[CLS]", "[SEP]", "[MASK]")


def char_to_byte_offsets(text: str) -> list[int]:
    """prefix[i] = byte length of text[:i]. One entry longer than the text."""
    prefix = [0]
    total = 0
    for ch in text:
        total += len(ch.encode("utf-8"))
        prefix.append(total)
    return prefix


def to_byte_span(text_prefix: list[int], span: list[int]) -> tuple[int, int]:
    a, b = span
    if a == 0 and b == 0:
        return (0, 0)  # the HuggingFace no-text sentinel; handled by the caller
    return (text_prefix[a], text_prefix[b])


def compare(py_rows: list[dict], cpp_rows: dict[int, dict], special_ids: set[int]
            ) -> tuple[int, list[str], Counter]:
    msgs: list[str] = []
    stats: Counter = Counter()
    differing = 0

    for i, pr in enumerate(py_rows):
        cr = cpp_rows.get(i)
        if cr is None:
            differing += 1
            msgs.append(f"row {i}: missing from the C++ output")
            continue
        if "error" in cr:
            differing += 1
            msgs.append(f"row {i}: C++ error: {cr['error']}")
            stats["<error>"] += 1
            continue

        row_msgs: list[str] = []
        py_ids, cpp_ids = pr["ids"], cr["ids"]
        if py_ids != cpp_ids:
            # Report the FIRST divergence rather than the whole sequence: the position
            # is what identifies the cause (a normaliser difference shows up at the
            # first affected word, a framing difference at index 0 or the last index).
            n = min(len(py_ids), len(cpp_ids))
            at = next((k for k in range(n) if py_ids[k] != cpp_ids[k]), n)
            row_msgs.append(f"ids differ at index {at} "
                            f"(python={py_ids[at:at + 3]} cpp={cpp_ids[at:at + 3]}); "
                            f"lengths {len(py_ids)} vs {len(cpp_ids)}")
            stats["ids"] += 1
        else:
            stats["ids_ok"] += 1

        prefix = char_to_byte_offsets(pr["text"])
        py_off, cpp_off = pr["offsets"], cr["offsets"]
        if len(py_off) != len(cpp_off):
            row_msgs.append(f"offset counts differ: {len(py_off)} vs {len(cpp_off)}")
            stats["offsets"] += 1
        else:
            for k, (po, co) in enumerate(zip(py_off, cpp_off)):
                tok_is_special = k < len(py_ids) and py_ids[k] in special_ids
                if list(co) == [-1, -1]:
                    # kNone. Legitimate ONLY where HuggingFace also says "no text",
                    # which it spells (0, 0), and only on a special token.
                    if tuple(po) == (0, 0) and tok_is_special:
                        stats["sentinel_ok"] += 1
                        continue
                    row_msgs.append(f"offset {k}: cpp says no-text (kNone) but python says "
                                    f"{tuple(po)} on a {'special' if tok_is_special else 'CONTENT'} token")
                    stats["offsets"] += 1
                    break
                if tuple(po) == (0, 0) and tok_is_special:
                    row_msgs.append(f"offset {k}: python says no-text but cpp gives a real span {tuple(co)}")
                    stats["offsets"] += 1
                    break
                want = to_byte_span(prefix, list(po))
                if tuple(co) != want:
                    row_msgs.append(f"offset {k}: python chars {tuple(po)} -> bytes {want}, "
                                    f"cpp {tuple(co)}")
                    stats["offsets"] += 1
                    break
            else:
                stats["offsets_ok"] += 1

        if row_msgs:
            differing += 1
            if len(msgs) < 400:
                msgs.append(f"row {i}: " + "; ".join(row_msgs))
    return differing, msgs, stats


def load_vocab_special_ids(path: Path) -> set[int]:
    ids: set[int] = set()
    with path.open(encoding="utf-8") as fh:
        for i, line in enumerate(fh):
            if line.rstrip("\n") in SPECIAL:
                ids.add(i)
    return ids


def self_test() -> int:
    checks = 0
    failures = 0

    def check(name: str, cond: bool) -> None:
        nonlocal checks, failures
        checks += 1
        print(("  ok    " if cond else "  FAIL  ") + name)
        if not cond:
            failures += 1

    special = {0, 1, 2, 3, 4}

    # An ASCII row where the two coordinate systems coincide.
    py = [{"text": "pay 100", "ids": [2, 9, 10, 3],
           "offsets": [[0, 0], [0, 3], [4, 7], [0, 0]]}]
    cpp = {0: {"row": 0, "ids": [2, 9, 10, 3],
               "offsets": [[-1, -1], [0, 3], [4, 7], [-1, -1]]}}
    n, msgs, _ = compare(py, cpp, special)
    check("an ASCII row agrees, with kNone accepted on the specials", n == 0 and not msgs)

    # THE ROW THAT MATTERS: 'é' is two UTF-8 bytes, so '100' is at chars 5:8 and
    # bytes 6:9. Without the conversion this row reads as a mismatch; with it, it
    # passes -- and a C++ side emitting the CHARACTER span must FAIL.
    py_nb = [{"text": "café 100", "ids": [2, 9, 11, 10, 3],
              "offsets": [[0, 0], [0, 2], [2, 4], [5, 8], [0, 0]]}]
    cpp_bytes = {0: {"row": 0, "ids": [2, 9, 11, 10, 3],
                     "offsets": [[-1, -1], [0, 2], [2, 5], [6, 9], [-1, -1]]}}
    n, msgs, _ = compare(py_nb, cpp_bytes, special)
    check("a MULTI-BYTE row agrees once char offsets are converted to bytes", n == 0)

    cpp_chars = {0: {"row": 0, "ids": [2, 9, 11, 10, 3],
                     "offsets": [[-1, -1], [0, 2], [2, 4], [5, 8], [-1, -1]]}}
    n, _, _ = compare(py_nb, cpp_chars, special)
    check("a C++ side emitting CHARACTER offsets on that row FAILS "
          "(the conversion is load-bearing, not decorative)", n == 1)

    n, _, _ = compare(py, {0: {"row": 0, "ids": [2, 9, 99, 3],
                               "offsets": [[-1, -1], [0, 3], [4, 7], [-1, -1]]}}, special)
    check("a single wrong token id is caught", n == 1)

    n, _, _ = compare(py, {0: {"row": 0, "ids": [2, 9, 10],
                               "offsets": [[-1, -1], [0, 3], [4, 7]]}}, special)
    check("a dropped [SEP] is caught", n == 1)

    n, _, _ = compare(py, {0: {"row": 0, "ids": [2, 9, 10, 3],
                               "offsets": [[-1, -1], [0, 3], [4, 6], [-1, -1]]}}, special)
    check("a wrong span on a content token is caught", n == 1)

    n, _, _ = compare(py, {0: {"row": 0, "ids": [2, 9, 10, 3],
                               "offsets": [[-1, -1], [-1, -1], [4, 7], [-1, -1]]}}, special)
    check("kNone on a CONTENT token is caught, not waved through", n == 1)

    # (0,0) from the C++ side on a special is a DISAGREEMENT, and the asymmetry is
    # deliberate. HuggingFace spells "no text" as (0, 0) by convention, so Python
    # saying it is fine. sensen spells it `TokenSpan::kNone` *specifically because*
    # (0, 0) is a zero-width span AT character 0 that passes a containment test
    # whenever a literal starts there -- tokenizer.cppm argues exactly this above
    # the struct. So a C++ (0, 0) means the sentinel was LOST somewhere between the
    # tokenizer and the probe, which is the one thing that comment exists to prevent.
    n, _, _ = compare(py, {0: {"row": 0, "ids": [2, 9, 10, 3],
                               "offsets": [[0, 0], [0, 3], [4, 7], [0, 0]]}}, special)
    check("(0,0) from C++ on a special FAILS -- sensen must carry kNone, not a "
          "zero-width span at character 0", n == 1)

    n, _, _ = compare(py, {}, special)
    check("a missing row is caught", n == 1)

    n, _, _ = compare(py, {0: {"row": 0, "error": "vocab mismatch"}}, special)
    check("a C++ per-row error is a disagreement", n == 1)

    print(f"\nself-test: {checks - failures} passed / {failures} failed")
    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fixture", nargs="?")
    ap.add_argument("cpp_ndjson", nargs="?")
    ap.add_argument("vocab", nargs="?")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if not (args.fixture and args.cpp_ndjson and args.vocab):
        ap.error("fixture.json, cpp.ndjson and vocab.txt are all required")

    py_rows = json.loads(Path(args.fixture).read_text())
    cpp_rows: dict[int, dict] = {}
    for line in Path(args.cpp_ndjson).read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        o = json.loads(line)
        cpp_rows[int(o["row"])] = o
    special_ids = load_vocab_special_ids(Path(args.vocab))

    if not py_rows or not cpp_rows:
        print(f"REFUSED: python rows={len(py_rows)} cpp rows={len(cpp_rows)} -- nothing compared")
        return 2
    if not special_ids:
        print("REFUSED: the vocab names none of [PAD]/[UNK]/[CLS]/[SEP]/[MASK] -- "
              "the sentinel rule would then accept kNone nowhere and reject every framed row")
        return 2

    non_ascii = sum(1 for r in py_rows if not r["text"].isascii())
    differing, msgs, stats = compare(py_rows, cpp_rows, special_ids)

    print(f"utterances: python={len(py_rows)} cpp={len(cpp_rows)}  non-ascii={non_ascii}")
    print(f"rows differing: {differing}")
    print(f"ids matching: {stats['ids_ok']}   offsets matching: {stats['offsets_ok']}   "
          f"no-text sentinels accepted: {stats['sentinel_ok']}")
    if non_ascii == 0:
        print("NOTE: every utterance is ASCII, so character and byte offsets coincide on this\n"
              "      corpus. The comparison converted anyway, and --self-test carries a\n"
              "      multi-byte row proving the conversion is what makes that sound.")
    if msgs:
        print("\nfirst disagreements:")
        for m in msgs[:30]:
            print(f"  {m}")

    if differing == 0:
        print(f"\nPARITY: sensen's tokenizer matches the trainer's on "
              f"{len(py_rows)}/{len(py_rows)} utterances (ids and spans)")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
