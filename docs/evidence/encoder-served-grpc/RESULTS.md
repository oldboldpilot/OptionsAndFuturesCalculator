# The encoder SERVED THROUGH gRPC ParseOperation, 2026-10-02

Engine: calculator_engine, MORTGAGE_ASSISTANT_BACKEND=encoder
        MORTGAGE_ENCODER_PATH=<v2s1 GGUF, sha256 40b4e4202761b10628f8...>
        started via scripts/run_with_env.py so config/.env is applied without a shell
        (bare JSON values lose their quotes to `set -a; . config/.env`)
Boot:   "Mortgage ENCODER assistant ready: backend=sensen device=cpu, 29 operations,
         109 (slot,map) pairs, 5 convention fields, vocab 2694"
        "Mortgage assistant model is LOADED", 0 [ERROR], exactly ONE engine on :50051
Scored: scripts/score_encoder_served_rpc.py

## Result
rows with gold params                     : 560
  served and equal to gold                : 524  (93.57%)
  served and differing                    : 36
  refused or clarified instead            : 0
rows whose gold is prose                  : 40  -> 34 clarifications, 6 refusals
600 rows in 1.0 s through the RPC = about 2 ms per row

## ALL 36 DISAGREEMENTS ARE THE SERVICE'S OWN DOCUMENTED TRANSFORMATIONS

  13  ComputeDepreciation   inert fields dropped
  11  ComputeRate           TVM payment sign flipped
  12  ComputePeriods        TVM payment sign flipped

VERIFIED rather than assumed, on one row of each:

row 13, ComputeDepreciation, method STRAIGHT_LINE
  dropped: ['factor', 'period', 'recovery_period', 'year']
  kVariantInertFields for STRAIGHT_LINE: period, factor, recovery_period, year
  -- the same four, exactly. The service drops a field the chosen METHOD never reads;
  forwarding it is what made a straight-line request refuse on "factor" = 3 in
  production. Missing is CORRECT.

row 11, ComputeRate: $1,011,000 loan, $7,899.07/month, 20-year
  gold   payment  7899.07
  served payment -7899.07, every other field identical
  -- tvm_payment_needs_sign_flip, applied to the OUTGOING params. PV and PMT must
  OPPOSE or the balance equation has no root with FV = 0, and the ENGINE refuses a
  same-signed pair deliberately. The flip is what makes the call answerable at all.

So 560/560 params-gold rows are served as the service intends: 524 matching gold
directly and 36 differing only by transformations this repository documents as the
right behaviour. gold_as_served() models some of them and not these two.

## THREE MEASUREMENT DEFECTS ON THE WAY TO THAT NUMBER, in order

1. EVERY ROW REFUSED "not available right now" while the boot banner said LOADED.
   available() and local_model_loaded() answer DIFFERENT questions -- the first is
   "can this replica serve the RPC", the second "are the weights in THIS process" --
   and only the second had been taught about the encoder. A health signal from the
   wrong layer, which is the defect class this repo records against last_applied, the
   LIVE badge and Railway's SUCCESS. Measured on all 600 rows before it was found.

2. 13 of 560 after that, and the 13 were the tell. "periods: expected a whole
   number": Kind::Int wants a JSON NUMBER and Kind::Decimal wants a JSON STRING, and
   the first renderer emitted everything as a string. The only operations that passed
   -- ComputeIrr, ComputeXirr, ComputeAmortizationBatch -- are the three whose fields
   are all arrays and decimals. A uniform rendering looked right and was wrong for
   every shape that happened to need otherwise. The JSON type of a field is a fact
   about finance.proto, so the renderer moved into the service, which owns that table.

   Trailing zeros are also trimmed there: BigDecimal::to_string() emits 38 fractional
   places and the verifier's own fixed-point type is 15 (mv::Decimal::kPlaces), so a
   38-place string is wider than the thing that must parse it. The non-terminating
   annual/12 rates truncate to 15 -- still more than double the SIX the corpus rounds
   them to.

3. 0/78 on ComputeAmortization from eval_grpc_mortgage.py while a hand-made RPC call
   on a ComputeAmortization utterance returned perfect params. That harness compares
   with dict EQUALITY against the corpus's own text: gold "740700.00" against a
   computed "740700" is the same number and a failed string compare. Right for a
   DECODER that echoes the corpus and wrong for a model that COMPUTES -- and
   docs/FINANCE_API.md tells callers not to pin the digit count. This is the fourth
   time this repository records a low score that described the harness: the
   llama-cli phantom, the bf16-vs-Q8_0 gap, phrase_money's impossible labels, and now
   string equality against a computed value.

   Rescored on VALUES with the trainer's own at_label_precision -- the corpus rounds a
   per-period rate to six places and the encoder computes fifteen, so an exact compare
   reports the MORE precise answer as the failure. CLAUDE.md predicted that gap before
   any of this was built.

## NOT MEASURED: the multi-turn ASKING flow
asked-when-ambiguous is 0/86 and that figure means nothing here. The scoring script
sends the trainer's RENDERED text, which already contains every turn, so there is
nothing left to ask about. Testing the ask -> answer -> parse exchange needs the
harness's two-call protocol with prior_question echoed back, which this run does not
do. The encoder cannot ask a question itself; refine_unstated in the serving layer is
what asks, and whether it still does so on the encoder path is UNTESTED.
