# sensen WordPiece tokenizer <-> trainer parity, 2026-10-02

Probe: backend/tests/encoder_tokenizer_parity.cpp (target test_encoder_tokenizer_parity)
Gate:  scripts/check_encoder_tokenizer_parity.py
Export: agent/train/export_encoder_tokenizer.py (vocab.txt) and
        agent/train/export_encoder_gguf.py (tokenizer.ggml.* inside the GGUF)

## Inputs
checkpoint  v2s1/encoder_fp32.pt -- vocab_size 2694, tok.weight (2694, 128), lit_features False
corpus      agent/dataset/data_mortgage/val.jsonl (val_sha256 1aa3ce94c344217e12f7...)
utterances  754 user turns from 600 rows, 28,303 tokens, non_ascii 0
fixture     sha256 d15689b83f81605bac13211111649643a2b842461e26d853a0750081c1298361

## The trainer's normaliser, read from its own tokenizer.json
lowercase                 true
strip_accents             true
handle_chinese_chars      false   <- sensen defaults TRUE
max_input_chars_per_word  64      <- sensen defaults 100
continuing_subword_prefix ##
post_processor            [CLS] A [SEP]

## Result -- BOTH routes
route A, vocab.txt with the four values stated in the probe:
  PARITY: 754/754 utterances (ids and spans); 1,508 no-text sentinels accepted
route B, the GGUF's own tokenizer.ggml.* via Tokenizer::fromParser:
  PARITY: 754/754 utterances (ids and spans)
route A vs route B: BYTE-IDENTICAL -- so the GGUF carried the normaliser and did
  not fall back to sensen's defaults. Running one route alone proves nothing,
  because neither divergent default can bite on an all-ASCII corpus of short words.

## Mutation arm -- does sensen actually READ the keys?
A GGUF written from a checkpoint whose normalizer.lowercase is false:
  the two routes DIFFER (so the key is read, not ignored)
  the gate reports 576 of 754 rows differing
  the failure is exactly right: cpp ids start [1, ...] -- [UNK] -- because 'Show'
  with a capital S is absent from an all-lowercase vocabulary
  178 rows still MATCH (the already-lowercase utterances), and offsets still match
  754/754 -- so a normaliser defect breaks 76% of rows and leaves spans intact,
  which is how it would be misread as a model problem

## Character offsets against byte offsets
HuggingFace reports CHARACTER offsets; sensen's TokenizeResult::offsets index a
string_view, i.e. UTF-8 BYTES. Probed concretely on 'cafe\u0301 100': HF puts '100'
at chars 5:8, where the bytes are 6:9. This holdout is 100% ASCII so the two
coincide on it -- a measurement about the corpus, not an identity. The comparator
therefore CONVERTS, and its self-test carries a multi-byte row that fails without
the conversion.

## The no-text sentinel is asymmetric, deliberately
HF spells 'no text' as (0,0); sensen spells it TokenSpan::kNone precisely because
(0,0) is a zero-width span AT character 0 that passes a containment test whenever a
literal starts there. So python (0,0) on a special is fine and a C++ (0,0) is a
DISAGREEMENT -- the self-test pins both directions.

## Comparator self-test
10 passed / 0 failed

## A separate finding, measured: the literal TAG is unused at serving time
reconstruct() applies a map by NAME (UNARY_FN[name](lit['value'])); the tag
predicate in MAPS gates CANDIDATE GENERATION during training only. Rewriting all
3,381 literal tags in the reconstruct fixture to 'bare' leaves both sides
byte-identical -- python tagged vs python tag-stripped is BYTE-IDENTICAL, and C++
on the tag-stripped fixture still matches python on 600/600. So the deployed C++
LiteralTag having 6 values against the trainer's 8 ('weeks'/'quarters' absent) is
NOT a serving blocker; what the engine's lexer must reproduce is the literal's
SPAN, its VALUE and its ORDER.
