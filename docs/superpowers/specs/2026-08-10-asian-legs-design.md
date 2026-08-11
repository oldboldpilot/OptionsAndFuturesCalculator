# Asian options as strategy legs — design

@author Olumuyiwa Oluwasanmi

## Where this starts

Asian averaging already exists in this product, on one surface only. The
`Exercise & Averaging` panel prices a SINGLE contract through
`sensen.finance.Finance/PriceOptionTree`, backed by `options.cppm`'s Hull-White
grid search (`price_option_double(..., asian_type, M_avg_states, lambda)`).

It is not reachable from a strategy. `calculator.proto`'s `Leg` (line 26) has
`action`, `type`, `strike`, `expiration_days`, `quantity`, `premium`,
`implied_volatility`, `contract_multiplier` — and no averaging field. So an
Asian can be priced and cannot be traded.

There is an existing asymmetry worth naming: `assistant.proto`'s `StrategyParams`
ALREADY carries `asian_type` and `exercise_type`, extracted from the trader's own
words by `assistant_verification.cppm`. The assistant can therefore parse "Asian
call on SPY" into a field the calculator has no way to accept. This work closes
that gap from the calculator's side.

## The thing that makes this not a plumbing job

An average-price Asian's terminal payoff is `max(A - K, 0)` where `A` is the
REALIZED AVERAGE over the averaging window. A vanilla's is `max(S_T - K, 0)` in
the TERMINAL SPOT.

Those are two different random variables. Every payoff-derived surface in this
app — the payoff ladder, `PnLMatrix`, the 3D P&L surface, the probability
distribution, and the `max_profit` / `max_loss` / `break_even` triple that is
read off the curve — is parameterised on terminal spot. Plotting an Asian leg on
that axis does not produce an approximate answer. It produces a confident wrong
one, and it looks exactly like a right one.

`Var(A) < Var(S_T)` for the same sigma, which is why an average-price Asian is
strictly cheaper than its vanilla twin. That inequality is also the
discriminating test below.

## What v1 does, and what it refuses

**Prices.** An Asian leg is priced through the same tree the panel already uses.
`LegRisk.model_price` and the position's value are real numbers.

**Refuses, specifically and by name:**

| output | behaviour when any leg is Asian |
| --- | --- |
| payoff curve, `max_profit`, `max_loss`, `break_even` | refuse |
| `PnLMatrix`, P&L surface, probability distribution | refuse |
| net Greeks and per-leg Greeks | refuse — see below |

The Greeks row is the dangerous one. `options.cppm` returns no closed-form
Greeks for an Asian on this tree; the existing panel already says so in the UI.
A net delta that silently sums a zero for the Asian leg understates the hedge
ratio of the whole position, parses fine, and is wrong in the direction that
costs money. Greeks must be ABSENT, not zero. This is the same defect class as
the `?? 0` expiry bug that reached production: a failed lookup rendered as a
plausible number.

Refusal copy follows the existing guard idiom ("Every leg expires today. The
payoff model prices remaining time value, so it needs at least one day to
expiry — pick a later expiration."): say what cannot be computed, why, and what
the user can do.

**Explicitly out of scope for v1**, so nobody infers it: plotting an all-Asian
strategy against average price with a relabelled axis. It is exact and it is
tempting; it is also a second payoff axis in a UI that currently has one, and it
should be its own decision.

## Contract change

`calculator.proto`, `message Leg`:

```proto
  // Averaging style. Zero value is NOT_ASIAN, so every existing client that
  // never sets this field keeps its current wire meaning and its current
  // answers -- the field is additive, not a version break.
  enum AsianType { NOT_ASIAN = 0; AVERAGE_PRICE = 1; AVERAGE_STRIKE = 2; }
  AsianType asian_type = 9;

  // Averaging grid states. Zero means "engine default" (50), matching the
  // panel. Bounded 10..200 by the service, as the panel already bounds it.
  int32 averaging_states = 10;
```

A LOCAL enum, deliberately, rather than importing `sensen.finance.AsianType`.
`calculator.proto` has no imports at all today (it declares `package calculator`
and nothing else); adding a cross-package import pulls `finance.proto` into the
generated browser bundle and into Envoy's `grpc_json_transcoder` services list,
which is a deployment change dressed up as a schema change. The two enums have
identical zero-values and identical orderings, and the mapping is one line in
the service.

## Tasks

Each task ends with an independently testable deliverable and a commit.

### Task 1 — proto + engine pricing

- `backend/proto/calculator.proto`: the two fields above.
- `backend/src/modules/calculator_service.cpp`: map `Leg.asian_type` onto
  `sensen::AsianType`, pass `averaging_states`, price through the tree.
- Validate `averaging_states` in 10..200 when Asian; refuse otherwise.

**Discriminating test** (`backend/tests/`): price a 30-day ATM call twice,
identical inputs, `NOT_ASIAN` vs `AVERAGE_PRICE`. Assert the Asian is strictly
cheaper by a real margin. Break it by ignoring `asian_type` on the way into the
pricer — the two prices become equal and the assertion must fail. Report both.

### Task 2 — refuse what cannot be computed

- Payoff/matrix/surface/probability: refuse when any leg is Asian, with the
  specific message, not an empty array and not a zero-filled curve.
- Greeks: absent for an Asian leg and for any strategy containing one.

**Discriminating tests**: (a) a strategy with one Asian leg returns the refusal
and NO payoff points — break by letting it fall through to the vanilla payoff
walk and the test must fail on receiving points; (b) net Greeks are absent —
break by summing with a zero default and the test must fail on delta being
present and understated. A vanilla-only strategy must be byte-identical to
today: that is the regression guard on the whole change.

### Task 3 — frontend

- `OptionTicket`: averaging selector on option legs, defaulting to Vanilla.
- `PositionLegs`: badge an Asian leg so a position cannot be misread.
- Panels render the engine's refusal through the existing empty-state, NOT the
  red "Unavailable" error branch — this is a modelling limit, not a failure.
- Store tests in the existing Vitest harness, hand-built responses per the
  suite's standing rule.

### Task 4 — assistant wiring

`StrategyParams.asian_type` already exists and is already extracted. Carry it
into the calculator leg it builds. Note the training-set limit honestly: the
model was not taught Asian strategies, so this path is only as good as the
extractor, and the extractor is regex-shaped.

## Gate

A vanilla-only strategy must produce identical output to the current build.
Everything else is additive. If that identity does not hold, the change is wrong
regardless of how the Asian numbers look.
