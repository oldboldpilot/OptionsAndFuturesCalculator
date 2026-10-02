#!/usr/bin/env python3
"""WordPiece tokenizer for the tiny encoder, trained on the corpus itself.

@author Olumuyiwa Oluwasanmi

WHY A TOKENIZER OF OUR OWN. The weights a pretrained encoder would bring
(MiniLM, ModernBERT) are not reachable from this environment, so there is no
vocabulary to inherit either. The target is 4,096 entries, and these corpora do
not fill it: the trainer stops at 2,661 pieces for the mortgage corpus and 689
for the strategy one, because there are no more pieces worth a merge. That is the
right size anyway: the embedding table is the largest single block of a model this
small (V x d, a third of the mortgage network at d=128), and a 30k vocabulary
would be most of the network spent on words these corpora never contain.

WHY OFFSETS ARE LOAD-BEARING. The per-literal heads read the hidden states of
the tokens a numeric literal occupies. The lexer yields CHARACTER spans; only
the tokenizer's offset mapping can turn those into token indices, and it must
do so on the exact string that was encoded, special tokens included.
`span_to_tokens` is that conversion, and `tensorize()` in encoder_corpus.py
refuses (rather than guesses) when a literal maps to no token.

THE TRAPS, MEASURED ON `tokenizers` 0.23 (see `--selftest`):
  * The segment separator is the literal text " [SEP] " inside the string, not a
    pair-encoding call. A pair encode restarts offsets per sequence, and a
    literal in the later user turn would then point into the wrong segment.
    Special tokens are matched in the RAW text (before lowercasing), so
    "[SEP]" survives the normalizer, and its offsets are real.
  * The special tokens the post-processor ADDS ([CLS] at the front and the
    closing [SEP]) carry offset (0, 0). `span_to_tokens` must skip them or every
    literal that starts at character 0 would claim the [CLS] token.
  * Lowercasing is deliberate (TSLA == tsla, "Meta" == "meta"), but it is a
    property of this vocabulary, not of the corpus: the tokenizer is stored in
    the checkpoint beside the weights so the pair cannot drift apart.
  * A WORDPIECE TRAINED ON A CLOSED SET OF NUMBERS HAS NO PIECE FOR AN UNSEEN
    ONE. The strategy corpus states its expiries from 14 values, so the digits 3,
    7 and 9 never occur as a NON-INITIAL character and `##3`, `##7`, `##9` were
    never created: "73 days" became [UNK] and 987 of 2,000 random numbers
    contained one. The mortgage corpus, whose amounts are all different, had none.
    A model cannot point at what the tokenizer turned into [UNK], so `train`
    seeds the trainer with every two-digit string (`DIGIT_SEED`); the seed lines
    change the vocabulary only, never a training example.
  * Numbers are NOT delexicalised. The class heads sometimes need the digits
    ("3 months" -> 90 days) and the literal head gets magnitude and unit tag as
    explicit features instead (encoder_corpus.mag_bucket / TAGS), which a
    placeholder token would have destroyed.
"""
from __future__ import annotations

import argparse
import collections
import statistics
import sys
from pathlib import Path
from typing import Iterable, Sequence

from tokenizers import Encoding, Tokenizer, models, normalizers, pre_tokenizers, processors, trainers

PAD, UNK, CLS, SEP, MASK = "[PAD]", "[UNK]", "[CLS]", "[SEP]", "[MASK]"
SPECIALS = [PAD, UNK, CLS, SEP, MASK]
# Every two-digit string, twice (the trainer's min_frequency is 2). See `train`.
DIGIT_SEED = [" ".join(f"{i:02d}" for i in range(100))] * 2


class EncoderTokenizer:
    def __init__(self, tk: Tokenizer) -> None:
        self.tk = tk
        self.pad_id = tk.token_to_id(PAD)
        self.cls_id = tk.token_to_id(CLS)
        self.sep_id = tk.token_to_id(SEP)
        self.unk_id = tk.token_to_id(UNK)
        if self.pad_id != 0:
            raise ValueError("[PAD] must be id 0: tensorize() zero-fills padding")

    # ------------------------------------------------------------- build
    @classmethod
    def train(cls, texts: Iterable[str], vocab_size: int = 4096, min_frequency: int = 2,
              digits: str = "chunk") -> "EncoderTokenizer":
        tk = Tokenizer(models.WordPiece(unk_token=UNK, max_input_chars_per_word=64))
        tk.normalizer = normalizers.BertNormalizer(
            lowercase=True, strip_accents=True, clean_text=True, handle_chinese_chars=False)
        pre = [pre_tokenizers.BertPreTokenizer()]
        if digits == "single":
            pre.append(pre_tokenizers.Digits(individual_digits=True))
        tk.pre_tokenizer = pre_tokenizers.Sequence(pre)
        trainer = trainers.WordPieceTrainer(
            vocab_size=vocab_size, min_frequency=min_frequency, special_tokens=SPECIALS,
            continuing_subword_prefix="##", limit_alphabet=300)
        tk.train_from_iterator(list(texts) + DIGIT_SEED, trainer)
        tk.post_processor = processors.TemplateProcessing(
            single=f"{CLS} $A {SEP}",
            special_tokens=[(CLS, tk.token_to_id(CLS)), (SEP, tk.token_to_id(SEP))])
        return cls(tk)

    @classmethod
    def from_str(cls, s: str) -> "EncoderTokenizer":
        return cls(Tokenizer.from_str(s))

    @classmethod
    def load(cls, path: Path) -> "EncoderTokenizer":
        return cls(Tokenizer.from_file(str(path)))

    def to_str(self) -> str:
        return self.tk.to_str()

    def save(self, path: Path) -> None:
        self.tk.save(str(path))

    # ------------------------------------------------------------- use
    @property
    def vocab_size(self) -> int:
        return self.tk.get_vocab_size()

    def encode(self, text: str) -> Encoding:
        return self.tk.encode(text)

    @staticmethod
    def span_to_tokens(offsets: Sequence[tuple[int, int]], start: int, end: int
                       ) -> tuple[int, int] | None:
        """Token index range [a, b) covering characters [start, end), or None.

        Tokens with offset (0, 0) are the post-processor's [CLS]/[SEP] and are
        skipped; a real token never has zero width."""
        a = b = None
        for i, (s, e) in enumerate(offsets):
            if e <= s:
                continue
            if s < end and e > start:
                if a is None:
                    a = i
                b = i + 1
        return None if a is None else (a, b)


def render_texts(path: Path, op_key: str, question_mode: str = "real") -> list[str]:
    import encoder_corpus as C
    return [C.render(d, question_mode).text for d in C.load_dialogues(path)]


def length_stats(tok: EncoderTokenizer, texts: Sequence[str]) -> dict:
    lens = sorted(len(tok.encode(t).ids) for t in texts)
    unk = sum(tok.encode(t).ids.count(tok.unk_id) for t in texts[:2000])
    q = lambda p: lens[min(len(lens) - 1, int(p * (len(lens) - 1)))]  # noqa: E731
    return dict(n=len(lens), mean=statistics.mean(lens), p50=q(0.5), p95=q(0.95), p99=q(0.99),
                max=lens[-1], unk_in_first_2000=unk)


def selftest() -> None:
    tok = EncoderTokenizer.train(["What's the payment on a $420,000 loan at 6.5% over 30 years?",
                                  "Over how many years?", "30-year"] * 40, vocab_size=300)
    s = "payment on $420,000 at 6.5% [SEP] Over how many years? [SEP] 30-year"
    enc = tok.encode(s)
    toks = list(zip(enc.tokens, enc.offsets))
    assert toks[0] == (CLS, (0, 0)) and toks[-1] == (SEP, (0, 0)), toks
    assert [t for t, _ in toks].count(SEP) == 3
    i = s.index("420,000")
    a, b = tok.span_to_tokens(enc.offsets, i, i + 7)
    assert "".join(t.replace("##", "") for t, _ in toks[a:b]) == "420,000", toks[a:b]
    j = s.index("30-year")
    a, b = tok.span_to_tokens(enc.offsets, j, j + 2)
    assert "".join(t.replace("##", "") for t, _ in toks[a:b]) == "30", toks[a:b]
    # a literal at character 0 must not claim [CLS]
    enc0 = tok.encode("6.5% at the start")
    a, b = tok.span_to_tokens(enc0.offsets, 0, 4)
    assert enc0.tokens[a] != CLS, enc0.tokens
    # a corpus whose numbers come from a closed set must still tokenize an unseen one
    closed = EncoderTokenizer.train(["spread 7 days", "spread 14 days", "spread 30 days", "spread 365 days",
                                     "spread 180 days"] * 40, vocab_size=300)
    check = closed.encode("spread 73 days")
    assert closed.unk_id not in check.ids, check.tokens
    assert closed.unk_id not in closed.encode("spread 9347281 days").ids
    # round trip through the string form the checkpoint stores
    tok2 = EncoderTokenizer.from_str(tok.to_str())
    assert tok2.encode(s).ids == enc.ids
    print("selftest ok")


def main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--train", type=Path)
    ap.add_argument("--val", type=Path)
    ap.add_argument("--op-key", default="operation")
    ap.add_argument("--vocab-size", type=int, default=4096)
    ap.add_argument("--min-frequency", type=int, default=2)
    ap.add_argument("--digits", choices=["chunk", "single"], default="chunk",
                    help="'single' splits every digit (longer sequences, no unseen-number pieces)")
    ap.add_argument("--question-mode", choices=["real", "placeholder"], default="real")
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        selftest()
        return 0
    if not a.train:
        ap.error("--train is required unless --selftest")
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    texts = render_texts(a.train, a.op_key, a.question_mode)
    tok = EncoderTokenizer.train(texts, a.vocab_size, a.min_frequency, a.digits)
    print(f"trained WordPiece on {len(texts)} rendered train texts: vocab {tok.vocab_size}, "
          f"digits={a.digits}")
    for name, path in (("train", a.train), ("val", a.val)):
        if path:
            st = length_stats(tok, render_texts(path, a.op_key, a.question_mode))
            print(f"  {name:5s} tokens/row: mean {st['mean']:.1f}  p50 {st['p50']}  p95 {st['p95']}  "
                  f"p99 {st['p99']}  max {st['max']}   [UNK] in first 2000 rows: "
                  f"{st['unk_in_first_2000']}")
    import random
    rng = random.Random(0)
    unk = sum(tok.unk_id in tok.encode(f"a spread of {rng.randrange(1, 10 ** rng.randrange(1, 8))} days").ids
              for _ in range(2000))
    print(f"  random numbers that tokenize to [UNK]: {unk}/2000")
    demo = texts[0]
    enc = tok.encode(demo)
    print("example:", demo[:120])
    print("        ", " ".join(enc.tokens[:40]))
    if a.out:
        tok.save(a.out)
        print(f"saved {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
