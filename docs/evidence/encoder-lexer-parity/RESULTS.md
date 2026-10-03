# Deployed lexer <-> trainer lex() parity, 2026-10-02

Probe: backend/tests/encoder_lexer_parity.cpp (target test_encoder_lexer_parity)
Gate:  scripts/check_encoder_lexer_parity.py

## Result
utterances: 754   literals: 3381
rows differing on ORDER/COUNT, SPAN or VALUE: 0
literal counts matching: 754   span mismatches: 0   value mismatches: 0
TAG divergences: 0

PARITY: the deployed lexer finds the same literals as the trainer's on 754/754
utterances (3381 literals: order, span, value).

## What is load-bearing, and what is not
The pair head is indexed BY LITERAL POSITION, so ORDER/COUNT, SPAN and VALUE must
agree. The TAG must not: reconstruct() applies a map BY NAME and the tag predicate
in MAPS gates candidate generation during TRAINING only -- rewriting all 3,381 tags
to 'bare' leaves both languages byte-identical (see ../encoder-reconstruct-parity).
The tag is compared anyway and counted, because a divergence in it is worth knowing.

## Two representation differences that are CONVERSIONS, not disagreements
1. The trainer bakes a k/m suffix into the value (v *= 1000); NumericLiteral keeps
   value BEFORE the multiplier with scale beside it, so M4 can be reasoned about.
   The comparison multiplies, and the self-test asserts a DROPPED scale is caught.
2. LiteralTag has SIX values (Untagged, Money, Percent, Years, Months, Days) against
   the trainer's EIGHT -- 'weeks' and 'quarters' are absent.

## THE ZERO TAG DIVERGENCES ARE A PROPERTY OF THE CORPUS, NOT OF THE TWO LEXERS
Tag histogram over the 754 holdout utterances:
  money 1708, percent 939, years 499, bare 120, months 58, days 57
  weeks 0, quarters 0  <- the two the deployed enum lacks

So the corpus cannot exhibit the divergence. Measured on a CONSTRUCTED utterance,
'draw it down over 6 weeks and then 3 quarters at 5.5%':
  python: ['6'=6:weeks, '3'=3:quarters, '5.5%'=5.5:percent]
  cpp:    6@18 tag=Untagged, 3@35 tag=Untagged, 5.5@49 tag=Percent
The spans and values still agree, so the three load-bearing properties hold even on
the case the holdout cannot show. This is the same lesson this project records against
strategy's 100%: a corpus that cannot exhibit a failure is not a control for it.

## Comparator self-test
9 passed / 0 failed
