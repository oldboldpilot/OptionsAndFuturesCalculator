#!/usr/bin/env python3
"""Export the trainer's WordPiece tokenizer into sensen's vocab.txt format and verify parity.

@author Olumuyiwa Oluwasanmi

WHY THIS SCRIPT EXISTS:
The engine (sensen) must tokenize an utterance EXACTLY as the Python trainer does,
or the encoder points at the wrong literal. In the tiny encoder architecture, the model
never emits numbers directly; instead, per-literal heads point at numeric literals
discovered by a separate lexer in the raw input text. This script exports the trainer's
WordPiece tokenizer (stored in the checkpoint as ck['tokenizer']) into the vocab.txt format
loaded by `backend/sensen/src/tokenizer.cppm` via `Tokenizer::fromVocabTxt`, and generates
a parity fixture of tokenized real utterances from `agent/dataset/data_mortgage/val.jsonl`
to prove token ID and span agreement against a C++ probe.

CRITICAL CORRECTNESS FINDING: OFFSETS ARE THE WHOLE POINT
=========================================================
The pair head points AT a numeric literal found by a separate lexer, and the literal
is matched to tokens by span coordinates (character or byte range [start, end)).

* PYTHON SIDE:
  Hugging Face `tokenizers` (via `EncoderTokenizer`) returns per-token CHARACTER offsets
  (Unicode codepoint indices into the Python `str`). Special tokens added by the
  TemplateProcessing post-processor ([CLS] at front, [SEP] at end) carry offset (0, 0).
  A literal at character 0 must not claim [CLS] (see `EncoderTokenizer.span_to_tokens`).

* C++ / SENSEN SIDE:
  sensen's `tokenizer.cppm` returns per-token UTF-8 BYTE offsets into the input UTF-8
  byte stream (`TokenSpan.begin`, `TokenSpan.end` into `std::string_view text`).
  Tokens without input text (such as framing [CLS]/[SEP] and [PAD]) carry the default
  sentinel span `TokenSpan{kNone, kNone}`, which fails containment tests.

* THE FINDING:
  Python offsets are CHARACTER offsets; sensen offsets are BYTE offsets.
  - On pure ASCII text (where 1 character == 1 UTF-8 byte), character and byte offsets
    numerically coincide.
  - On ANY non-ASCII utterance (e.g. accents 'é', em-dashes '—', unicode quotes, or currency
    symbols like '€' / '£'), a multi-byte character causes byte offsets to advance faster
    than character offsets. Every non-ASCII utterance following a multi-byte sequence
    silently mismatches if character and byte offsets are mixed!
  - In `agent/dataset/data_mortgage/val.jsonl`, all 600 rows (754 user turns) are 100% ASCII,
    meaning character and byte offsets coincide on this holdout. However, this is an empirical
    property of this particular corpus, NOT an algorithmic identity. Sensen's C++ lexer
    must emit BYTE spans into the UTF-8 input to match sensen's tokenizer BYTE offsets.

REFUSAL ON TOKEN BOUNDARY STRADDLING:
=====================================
sensen's `tokensInSpan()` in `backend/sensen/src/text_encoder.cppm` requires every token
overlapping a literal to fall WHOLLY inside the literal span:
    `wholly_inside = t.start >= literal.start && t.end <= literal.end;`
If a token boundary straddles a literal boundary, sensen returns `TokenStraddlesLiteral`
and refuses execution immediately. Because of this invariant, a tokenizer disagreement
(or offset unit mismatch) shows up as an explicit REFUSAL, not a silent wrong answer or
corrupted financial parameter.

VOCABULARY FORMAT:
==================
sensen's `Tokenizer::fromVocabTxt` loads a standard BERT vocab.txt:
- Exactly one token per line, UTF-8 encoded.
- The token ID IS the 0-based line number.
- No blank lines in the middle (refuses rather than shifts IDs).
- No duplicate tokens (refuses rather than creates ambiguous IDs).
- Required special tokens: [PAD] (id 0), [UNK] (id 1), [CLS] (id 2), [SEP] (id 3),
  with optional [MASK] (id 4).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Sequence

import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from encoder_tokenizer import EncoderTokenizer, PAD, UNK, CLS, SEP, MASK

# THERE IS NO DEFAULT CHECKPOINT, deliberately. A first version defaulted to
# `mort_aug` (vocab 3,345) while the model being served is the 3-seed-stable
# `v2s1` run (vocab 2,694, `tok.weight (2694, 128)`), so it exported a vocab.txt
# for a DIFFERENT tokenizer and the fixture it produced would have proved
# agreement with weights nobody ships. A vocabulary is not a detail that can be
# defaulted: the token ID is the line number, so one extra entry renumbers every
# token after it and the engine then embeds the wrong rows with no error
# anywhere. The checkpoint must be named.
DEFAULT_CHECKPOINTS: list[Path] = []


def load_tokenizer(
    checkpoint_path: Path | None = None,
    tokenizer_path: Path | None = None,
) -> tuple[EncoderTokenizer, str]:
    """Load EncoderTokenizer from a checkpoint (.pt), tokenizer.json, or fallback path."""
    if checkpoint_path is not None:
        if not checkpoint_path.exists():
            raise FileNotFoundError(f"Checkpoint not found: {checkpoint_path}")
        ck = torch.load(checkpoint_path, map_location="cpu", weights_only=False)
        if "tokenizer" not in ck:
            raise KeyError(f"Checkpoint {checkpoint_path} contains no 'tokenizer' key")
        tok_str = ck["tokenizer"]
        return EncoderTokenizer.from_str(tok_str), f"checkpoint {checkpoint_path}"

    if tokenizer_path is not None:
        if not tokenizer_path.exists():
            raise FileNotFoundError(f"Tokenizer file not found: {tokenizer_path}")
        if tokenizer_path.is_dir():
            tokenizer_path = tokenizer_path / "tokenizer.json"
        return EncoderTokenizer.load(tokenizer_path), f"file {tokenizer_path}"

    for cand in DEFAULT_CHECKPOINTS:
        if cand.exists():
            ck = torch.load(cand, map_location="cpu", weights_only=False)
            if "tokenizer" in ck:
                return EncoderTokenizer.from_str(ck["tokenizer"]), f"default checkpoint {cand}"

    raise FileNotFoundError(
        "No tokenizer source found. Pass --checkpoint <path/to/encoder_fp32.pt> "
        "or --tokenizer <path/to/tokenizer.json>"
    )


def export_vocab_txt(tok: EncoderTokenizer, out_path: Path) -> int:
    """Export WordPiece vocabulary to vocab.txt in sensen's required format.

    Format: exactly one token per line; the token ID IS the line number (0-based).
    Checks that special tokens [PAD], [UNK], [CLS], [SEP] exist with their expected IDs.
    """
    vocab_size = tok.vocab_size
    vocab_dict = tok.tk.get_vocab()

    id_to_token: list[str | None] = [None] * vocab_size
    for token, idx in vocab_dict.items():
        if idx < 0 or idx >= vocab_size:
            raise ValueError(f"Token {token!r} has index {idx} outside [0, {vocab_size})")
        if id_to_token[idx] is not None:
            raise ValueError(
                f"Duplicate token index {idx}: already occupied by {id_to_token[idx]!r}, "
                f"new token {token!r}"
            )
        id_to_token[idx] = token

    for idx, token in enumerate(id_to_token):
        if token is None:
            raise ValueError(
                f"Gap in vocabulary: token index {idx} has no entry (vocab_size={vocab_size})"
            )
        if "\n" in token or "\r" in token:
            raise ValueError(f"Token index {idx} contains newline characters: {token!r}")
        if not token:
            raise ValueError(f"Token index {idx} is empty string")

    # Verify sensen-required special tokens
    specials = {
        PAD: tok.pad_id,
        UNK: tok.unk_id,
        CLS: tok.cls_id,
        SEP: tok.sep_id,
    }
    for name, expected_id in specials.items():
        actual_id = vocab_dict.get(name)
        if actual_id is None:
            raise ValueError(f"Required special token {name!r} not found in vocabulary")
        if actual_id != expected_id:
            raise ValueError(
                f"Special token {name!r} id mismatch: dict has {actual_id}, tokenizer has {expected_id}"
            )

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        for token in id_to_token:
            f.write(f"{token}\n")

    # Verify round-trip file validation matching sensen's parseVocabTxt rules
    content = out_path.read_text(encoding="utf-8")
    lines = content.splitlines()
    if len(lines) != vocab_size:
        raise RuntimeError(
            f"Written vocab.txt has {len(lines)} lines, expected {vocab_size}"
        )
    for i, line in enumerate(lines):
        if not line:
            raise RuntimeError(
                f"line {i+1} is empty: a vocab.txt id is its line number, so a blank line "
                "cannot be skipped without renumbering every token after it"
            )

    return vocab_size


def analyze_offset_semantics(tok: EncoderTokenizer) -> dict[str, str | bool]:
    """Demonstrate and report character vs byte offset semantics."""
    test_str = "café 100"
    enc = tok.encode(test_str)
    cafe_spans = []
    num_offsets = (-1, -1)
    for t, o in zip(enc.tokens, enc.offsets):
        if o[1] > o[0]:
            if o[0] >= 0 and o[1] <= 4:
                cafe_spans.append((t, o))
            elif "100" in t:
                num_offsets = o

    char_len = len(test_str)
    byte_len = len(test_str.encode("utf-8"))

    # For 'café 100':
    # Character indices: 'c'(0), 'a'(1), 'f'(2), 'é'(3), ' '(4), '1'(5), '0'(6), '0'(7) -> len 8
    # UTF-8 bytes: 'c'(0), 'a'(1), 'f'(2), \xc3\xa9 (3,4), ' '(5), '1'(6), '0'(7), '0'(8) -> len 9
    # If Python offset for '100' is (5, 8), it is CHARACTER offsets.
    # If it were (6, 9), it would be BYTE offsets.
    python_uses_chars = num_offsets == (5, 8)
    cafe_desc = "+".join(f"{t}@{o[0]}:{o[1]}" for t, o in cafe_spans) if cafe_spans else "none"

    return {
        "python_uses_character_offsets": python_uses_chars,
        "sensen_uses_byte_offsets": True,
        "test_str": test_str,
        "char_len": str(char_len),
        "byte_len": str(byte_len),
        "cafe_offsets": cafe_desc,
        "num_offsets": f"{num_offsets[0]}:{num_offsets[1]}",
    }


def extract_utterances(
    val_path: Path, source: str = "user_turns", turns_mode: str = "all"
) -> list[str]:
    """Extract real utterances from val.jsonl.

    source:
      - 'user_turns': raw utterances spoken by the user in the conversations.
      - 'rendered': full dialogue rendered as `first [SEP] question [SEP] later`.
    turns_mode (for user_turns):
      - 'all': every user turn across all rows (754 utterances across 600 rows).
      - 'first': the first user turn of each row (600 utterances).
    """
    if not val_path.exists():
        raise FileNotFoundError(f"Validation dataset not found: {val_path}")

    utterances: list[str] = []
    if source == "rendered":
        import encoder_corpus as C

        dialogues = C.load_dialogues(val_path)
        for d in dialogues:
            utterances.append(C.render(d).text)
        return utterances

    with open(val_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            user_turns = [
                c["content"] for c in row.get("conversations", []) if c.get("role") == "user"
            ]
            if not user_turns:
                continue
            if turns_mode == "first":
                utterances.append(user_turns[0])
            else:
                utterances.extend(user_turns)

    return utterances


def build_parity_fixture(
    tok: EncoderTokenizer, utterances: list[str]
) -> tuple[list[dict], int]:
    """Tokenize each utterance and produce fixture records with {text, ids, offsets}."""
    records: list[dict] = []
    total_tokens = 0
    for text in utterances:
        enc = tok.encode(text)
        ids = list(enc.ids)
        offsets = [list(span) for span in enc.offsets]
        records.append({
            "text": text,
            "ids": ids,
            "offsets": offsets,
        })
        total_tokens += len(ids)

    return records, total_tokens


def main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument(
        "--checkpoint",
        type=Path,
        default=None,
        help="Path to trained checkpoint (encoder_fp32.pt) carrying ck['tokenizer']",
    )
    ap.add_argument(
        "--tokenizer",
        type=Path,
        default=None,
        help="Path to tokenizer.json (alternative to --checkpoint)",
    )
    ap.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Path to dump sensen-compatible vocab.txt",
    )
    ap.add_argument(
        "--val",
        type=Path,
        default=Path("agent/dataset/data_mortgage/val.jsonl"),
        help="Path to validation JSONL (default: agent/dataset/data_mortgage/val.jsonl)",
    )
    ap.add_argument(
        "--check",
        nargs="?",
        const="default",
        default=None,
        help="Run parity harness against val.jsonl utterances and emit JSON fixture. "
             "Optional value is the fixture output file path.",
    )
    ap.add_argument(
        "--fixture-out",
        type=Path,
        default=None,
        help="Explicit output path for the JSON parity fixture (alternative to --check <path>)",
    )
    ap.add_argument(
        "--source",
        choices=["user_turns", "rendered"],
        default="user_turns",
        help="Utterance source from val.jsonl: 'user_turns' (raw user turns) or 'rendered' "
             "(rendered dialogue text with [SEP] separators)",
    )
    ap.add_argument(
        "--turns",
        choices=["all", "first"],
        default="all",
        help="For user_turns: 'all' (all 754 user turns across 600 rows) or 'first' (600 first turns)",
    )

    args = ap.parse_args(argv)

    if args.out is None and args.check is None and args.fixture_out is None:
        ap.error("Specify --out to export vocab.txt, and/or --check to generate the parity fixture.")

    # Load tokenizer
    tok, source_desc = load_tokenizer(args.checkpoint, args.tokenizer)
    print(f"[tokenizer] Loaded {source_desc}: vocab_size={tok.vocab_size}")

    # Offset semantics probe & report
    offset_info = analyze_offset_semantics(tok)
    print(
        f"[offsets] Probing offset coordinate system on 'café 100': "
        f"offsets={offset_info['cafe_offsets']}, {offset_info['num_offsets']}"
    )
    if offset_info["python_uses_character_offsets"]:
        print(
            "  * FINDING: Python Hugging Face tokenizers uses Unicode CHARACTER offsets."
        )
    else:
        print("  * FINDING: Python tokenizers did not match expected character offset test.")
    print("  * FINDING: sensen Tokenizer (tokenizer.cppm) uses UTF-8 BYTE offsets.")
    print(
        "  * CRITICAL: On ASCII text, character and byte offsets are identical.\n"
        "              On non-ASCII text, multi-byte sequences cause character and byte offsets\n"
        "              to diverge. Sensen's C++ lexer spans MUST be byte offsets into UTF-8,\n"
        "              or any non-ASCII utterance will mismatch.\n"
        "  * REFUSAL BEHAVIOR: Sensen's `tokensInSpan()` enforces that literal spans strictly\n"
        "                      contain whole tokens without straddling (TextEncoderError::TokenStraddlesLiteral).\n"
        "                      Therefore, a tokenizer or offset disagreement shows up as a REFUSAL,\n"
        "                      never a wrong answer or corrupted parameter."
    )

    # STEP 2: Export vocab.txt
    if args.out is not None:
        exported_n = export_vocab_txt(tok, args.out)
        print(f"[export] Dumped {exported_n} vocabulary tokens to {args.out}")
        print(f"  * Vocab format: BERT vocab.txt (one token per line, ID = 0-based line number)")
        print(f"  * Special tokens: [PAD]={tok.pad_id}, [UNK]={tok.unk_id}, [CLS]={tok.cls_id}, [SEP]={tok.sep_id}")

    # STEP 3: Parity harness behind --check
    if args.check is not None or args.fixture_out is not None:
        # Determine fixture output path
        if args.fixture_out is not None:
            fixture_path = args.fixture_out
        elif args.check != "default" and args.check is not None:
            fixture_path = Path(args.check)
        elif args.out is not None:
            fixture_path = args.out.with_suffix(".val_fixture.json")
        else:
            fixture_path = Path("agent/train/encoder_val_fixture.json")

        utterances = extract_utterances(args.val, source=args.source, turns_mode=args.turns)
        non_ascii = sum(1 for u in utterances if not u.isascii())
        print(
            f"[check] Extracted {len(utterances)} utterances from {args.val} "
            f"(source={args.source}, turns={args.turns}, non_ascii={non_ascii})"
        )

        records, total_tokens = build_parity_fixture(tok, utterances)
        fixture_str = json.dumps(records, indent=1) + "\n"
        fixture_bytes = fixture_str.encode("utf-8")
        fixture_sha256 = hashlib.sha256(fixture_bytes).hexdigest()

        fixture_path.parent.mkdir(parents=True, exist_ok=True)
        fixture_path.write_bytes(fixture_bytes)

        print(f"[summary] number of utterances: {len(utterances)}")
        print(f"[summary] total tokens: {total_tokens}")
        print(f"[summary] fixture sha256: {fixture_sha256}")
        print(f"[summary] fixture written to: {fixture_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
