# The small-encoder mortgage assistant, SERVED on CPU end to end, 2026-10-02

Chain:  backend/src/modules/encoder_assistant.cppm/.cpp
Probe:  backend/tests/encoder_assistant_probe.cpp (target test_encoder_assistant_probe)
Scored by: scripts/check_encoder_reconstruct_parity.py (the same comparator that gated
           reconstruct against Python, so the scoring rule is not a new one)

## Result
rows: 600   rows differing: 0
decimal values compared: 5352  (5280 EXACT, 72 within one BigDecimal ulp of 1e-38)
600 utterances, 0 errored, 40 answered <NONE> -- matching the gold's 40 null-params rows

PARITY: the full C++ chain reproduces the gold params on 600/600 rows, from the model's
OWN predictions (not a fixture): lex -> tokenize -> encode -> mask -> reconstruct.

## Throughput, CPU, single thread of control
three repeat runs: 730 / 717 / 716 ms for 600 utterances
  = 821.9 / 836.8 / 838.0 utterances per second; median 717 ms = 836.8 u/s
  = about 1.2 ms per utterance
Binary mtime 20:49:07 against source 20:49:05, checked -- the identical 725 ms of an
earlier run was inside this spread, not a stale binary.

Against the DEPLOYED Qwen3-0.6B on the same surface: ParseOperation measures 2.18 s live
and ~64.8 tok/s decode at batch 1. This is a different kind of measurement (in-process,
no gRPC, no TLS, no Envoy) and must not be compared to 2.18 s directly -- CLAUDE.md
records that ~71 ms of that is network, TLS, Envoy and gRPC framing, which no model
change touches.

## Model
v2s1: 958,103 parameters, vocab 2,694, 29 operations, 109 (slot,map) pairs,
5 convention fields, 3 layers, d=128, 4 heads, ffn 344, RoPE, RMSNorm, SwiGLU, no bias.
GGUF 3,890,080 bytes carrying weights + schema_json (21,827 B) + tokenizer.ggml.*
holdout agent/dataset/data_mortgage/val.jsonl, val_sha256 1aa3ce94c344217e12f7...

## TWO DEFECTS THIS RUN FOUND, both mine, both invisible to the four fixture gates

1. A REFERENCE BOUND TO A TEMPORARY, segfaulting two rows in.
   FieldValue::as_array() returns std::optional<std::span<const BigDecimal>> BY VALUE, so
   `const auto& v = *fval.as_array()` referenced into an optional that died at the end of
   the statement; every later v[i] read through a dangling span. It crashed inside
   BigDecimal::to_string() on the first array-valued row (ComputePaybackPeriod's values).
   The reconstruct fixture probe took the span BY VALUE and so never hit it.

2. sensen's WordPiece DOES NOT PRE-MATCH SPECIAL TOKENS IN TEXT, and HuggingFace does.
   The trainer joins multi-turn rows with the literal string " [SEP] " and HuggingFace
   matches added tokens with a trie BEFORE WordPiece, giving the single id 3. sensen
   splits it into "[", "sep", "]" = ids 28, 130, 29.

   MEASURED: of the 600 RENDERED holdout utterances, 154 contain a literal [SEP] and
   ALL 154 tokenized differently from the trainer.

   IT WAS INVISIBLE TO THE TOKENIZER GATE, which is the part worth keeping. That gate
   scored 754/754 on ids AND spans -- over the USER TURNS, which contain no [SEP] at all.
   A corpus that cannot exhibit a failure is not a control for it, and this is the THIRD
   instance of that shape in one day (strategy's 100% could not show the single-label
   ceiling; the holdout has no weeks/quarters literal to show the 6-against-8 tag gap).

   IT COST EXACTLY ONE ROW OF 600 BEFORE THE FIX, AND THAT IS NOT REASSURANCE. Only row
   227 changed answer -- 'Amortize $1,131,700 at 5.88% over 30-year. [SEP]  [SEP] what if
   the rate is 6.38% instead?', where annual_rate went missing. The literal SPANS are
   unaffected by the mangling, so the extra tokens are usually noise the model shrugs
   off. A 599/600 resting on a tokenizer that disagrees on a quarter of its inputs is
   luck, not correctness, and was not quoted as a result.

   FIXED IN THE CALLER, not in sensen: EncoderAssistant::tokenize_with_specials splits on
   the marker, tokenizes each segment unframed, shifts every offset into the whole
   utterance's coordinates and emits the sep id with the span of the five characters it
   was written as -- verified against the trainer, which gives (43,48) and (50,55) on row
   227, while [CLS] and the final [SEP] carry (0,0). The real service receives its turns
   in SEPARATE ParseRequest fields, so segment boundaries are something it knows rather
   than recovers from text, and a general-purpose tokenizer stays free of one corpus's
   rendering convention.

   STILL OPEN: test_encoder_tokenizer_parity exercises sensen's RAW encode and therefore
   still reports 154 differing rows on the rendered text. That is correct -- it measures
   sensen, which makes no claim to HuggingFace's added-token prematching. Teaching
   sensen to prematch would make both agree and would remove the trap for the next
   caller; it is not done.

## The convention head, added for this run
The 5 convention classifiers were NOT exported at all before today, and sensen's encoder
had nowhere to put them. Measured cost of their absence: 49 of 600 rows (8.17%) -- NOT
the 123 rows that carry a convention value, because the schema's const_default already
supplies the conventional one for 74 of them. Quoting 123 would have overstated it 2.5x.

They are ONE tensor, encoder.convention.{weight,bias} of width 13, because all five read
the SAME pooled vector the operation head reads -- so five heads of widths 2/2/3/2/4 and
one of width 13 are the same arithmetic. The per-field slice boundaries are conv_vocab in
the SCHEMA and are deliberately NOT in sensen: the encoder emits the logits FLAT and the
caller slices, which is the same split that leaves the pair mask to the caller. The
exporter asserts the head's widths against conv_vocab, because a disagreement would read
one field's logits as another's and produce a plausible convention value.
