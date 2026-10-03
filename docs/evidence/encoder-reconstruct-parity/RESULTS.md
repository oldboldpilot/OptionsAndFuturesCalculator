# encoder_reconstruct C++ <-> Python parity, 2026-10-02

Gate: scripts/check_encoder_reconstruct_parity.py
Probe: backend/tests/encoder_reconstruct_parity.cpp (target test_encoder_reconstruct_parity)
Fixture generator: agent/train/make_reconstruct_parity_fixture.py

## Inputs
checkpoint   v2s1/encoder_fp32.pt (vocab 2,650)
corpus       agent/dataset/data_mortgage/val.jsonl
schema.json  sha256 407c5dacb3a09ca4044660cb49d77651c42d61c12c3386de79fc8b5eec1b01c6  21827 bytes
fixture.json sha256 acc1cafbd4f9d905fdf834404ce3a200fea32f23c5536b7b0eaf0aae6bbb43a1  333981 bytes
python.ndjson sha256 040c6689d3c5466514943610e46cd28777e67a9dda74b223276f1a7e5131a0b9  162286 bytes
cpp.ndjson   sha256 52a4352176b0971d3c2bc1ad1837d0a20cd1d838478fda7bcf195dd103bd4b01  362262 bytes

## Oracle control
ORACLE CONTROL: Python reconstruct on GOLD pairs matches gold on 600/600 rows

## Coverage of the 600 rows
operations        28 of 28 in the schema
(slot,map) pairs  109 of 109 in the schema
fields            116
field kinds       num 4576, cat 205, arr 78
null-params rows  40 (op == <NONE>), agreeing on both sides

## Result
rows: python=600 cpp=600
rows differing: 0
decimal values compared: 5352  (5280 EXACT, 72 equal within one BigDecimal ulp of 1e-38)
PARITY: C++ encoder_reconstruct matches Python reconstruct on 600/600 rows

max |python - cpp| : 6.666667e-39, at row 5 / rate
All 72 are the field 'rate' and all are a NON-TERMINATING annual/12:
Python runs at getcontext().prec = 60; BigDecimal is fixed at 38 places and
TRUNCATES (34 of 72 differ from the correctly-rounded 38-place form, which is
the signature of truncation rather than round-to-nearest). Both are far beyond
the 6 decimal places the corpus labels carry.

## Mutation arm (CCACHE_DISABLE=1)
encoder_reconstruct.cppm:863  divide(BigDecimal(12)) -> divide(BigDecimal(10))
  rows differing: 112, all 'rate', residuals ~1e-3 (35 orders above the ulp bound)
  decimal values compared: 5240 (5240 EXACT, 0 within ulp) -- with /10 the
  division terminates, which independently confirms the 72 ulp cases are the /12 tail
Restored: rows differing 0, 5280 EXACT / 72 within ulp

## Comparator self-test
13 passed / 0 failed -- including 'ten ulps (1e-37) still FAILS'

## Engine
CCACHE_DISABLE=1 ninja calculator_engine -> rc=0, 30,728,656 bytes

## The per-operation pair mask (encoder_reconstruct::maskPairsToOperation)

op_pairs admits a median of 5 pairs of 109 per operation (min 2, max 16).

It is a strict SUBSET of "every pair whose slot the operation has" in 19 of 28
operations -- same field, a MAP never observed for that operation. ComputeRate
admits 3 of the 7 pairs on its own fields; ComputePeriods 3 of 8. op_pairs-only
is empty for every operation, so op_pairs is always a subset, never a superset.

                                  no --mask   --mask
  plain fixture                   0           0        <- never removes a correct pair
  same-slot wrong-map distractor  123         0        <- removes exactly the wrong ones

[inject] distractor plantable in 370 rows; 230 rows admit no such pair (their
op_pairs already equals every pair on their own fields), so the arm cannot reach
them. Of the 370, 123 changed; on the rest reconstruct's arbitration still
resolved to the correct pair.

THE FIRST VERSION OF THIS ARM MEASURED NOTHING: it planted the lowest GLOBALLY
inadmissible pair id and changed 0 of 600 rows, because reconstruct already
discards a pair whose SLOT is not a field of the named operation. Reading that 0
as "the mask is redundant" would have been the wrong conclusion from a correct
number.

The 12.67% figure (600/600 with the mask, 524/600 without) was measured in PYTHON
against real model predictions. It is NOT this synthetic injection and the two
must not be conflated.
