# Session Log: 2026-07-30 — Per-Leg Clocks, Matrix Date Axis, Locale-Free Dates

@author Olumuyiwa Oluwasanmi

Three defects in the P&L path, all in `backend/src/modules/calculator_service.cpp`,
all found by pulling one thread. Each was invisible while every position had a
single expiry, and each produced confident, wrong output rather than an error.

## 1. Every Leg Was Priced On One Clock

`value_at()` took a single `years_remaining` and applied it to every leg.
`payoff_at_expiry()` settled every leg at once. Correct while all legs shared an
expiry; wrong the moment two did not.

For a same-strike calendar spread the two legs then had identical
`(S, K, sigma, T)` with opposite direction. Every term cancelled and the whole
position collapsed to a flat line at the net debit — at every price and every
date. The signature is `max_profit == max_loss == -premium`.

Fixed by replacing both functions with `value_at_elapsed()`, which walks the
legs and gives each its own remaining maturity: Black-Scholes while it has time
left, intrinsic once its expiry has passed.

This is the same defect class as the Greeks bug fixed on 2026-07-29 — a value
hoisted out of the leg loop that stopped being loop-invariant when the input
space widened. Fixing `action_greeks` did not fix this, because the P&L path
had its own copy.

### The date the curve is drawn at

Per-leg clocks alone do not fix a calendar spread, and this is the part worth
remembering. At the FAR expiry both legs are intrinsic and a same-strike
calendar still collapses to a flat line. The tent shape lives at the NEAR
expiry, where the short leg has settled and the long leg still carries time
value.

So the curve is now evaluated at `curve_days()` — the earliest leg expiry —
rather than `horizon_days()`. For a single-expiry position the two are equal and
nothing changes. `StrategyResponse.curve_days_to_expiration` reports which date
was used, because a client that labels the curve "at expiration" without reading
it will mislabel every multi-expiry position.

Everything derived from that curve moved with it: `max_profit`, `max_loss`,
breakevens, and — critically — the terminal distribution behind PoP, EV and
VaR/CVaR. `risk_figures()` pairs each density with the P&L at the same grid
point, so a distribution on a different clock would weight the right P&Ls by the
wrong probabilities.

### The assumption being made

Past a leg's expiry we value it against the underlying price at the EVALUATION
date, not the unknowable price on the day it actually settled. Every payoff
diagram of this kind makes the same single-path assumption. It is stated in the
code, in the proto, and in the panel's tooltip rather than left implicit.

### Verified

Against live SPY, ATM 739 call, IV 0.147, short 30d / long 60d, both legs given
the same real ATM premium so that any curvature must come from the differing
clocks:

| | max_profit | max_loss | curve drawn at |
| --- | --- | --- | --- |
| Before | −0.00 | −0.00 | 60d (far) |
| After | 1378.78 | 0.00 | 30d (near) |

Hand-derived independently: BS(739, 739, r=0.05, sigma=0.147, T=30/365) =
13.9726, so the long leg contributes (13.9726 − 6.26) × 100 = 771.26 and the
short leg (0 − 6.26) × −1 × 100 = 626.00, totalling 1397.26 against a strike of
exactly 739. The engine reports slightly less because the price grid lands at
738.96, not 739.

## 2. The Matrix Date Axis Ran Backwards

`MatrixCell` carries two views of one axis — `days_to_expiration` and
`date_str` — and they pointed in opposite directions. The date was computed as
`now + dte`, where `dte` is days REMAINING. So the first column was labelled
"30 days to expiry" and dated 30 days in the future; the last was labelled
"0 days" and dated today. Every cell in the grid named the wrong day except the
midpoint.

Fixed by dating from `elapsed = horizon - dte`. The smoke client now asserts
that more days remaining implies an earlier date, which is enough to catch a
reversal.

## 3. Dates Carried A Thousands Separator

`date_str` was built with a `std::ostringstream`, which carries the **global**
locale. `logger`'s initialisation sets that to `en_US.UTF-8`
(`cpp23-logger/logger.cppm:2007`) so console output handles UTF-8. That locale
groups thousands, so the year was emitted as `2,026` and every cell in the grid
carried `2,026-07-30` — a date string no client can parse, read straight into
the frontend's date axis at `useCalculatorStore.ts:406`.

Fixed with `std::format`, which is locale-independent unless asked otherwise —
the right property for a wire value in ISO-8601, which this is. `<sstream>` is
gone from the translation unit.

Worth carrying forward: **a logging library changed process-global state, and
the damage surfaced three layers away in a protobuf field.** `date_str` was the
only stream-formatted output in `backend/src`, so the blast radius was one
field, but the next stream-formatted number will be silently wrong too. Prefer
`std::format` for anything that goes on a wire.

## Frontend

- `scripts/gen_proto.sh` regenerated; `getCurveDaysToExpiration()` now exists.
- `useCalculatorStore` carries `inputs.curveDays`, falling back to the horizon
  when the field reads 0 — proto3 cannot distinguish an unset double from a
  backend predating the field, and 0 would collapse the distribution.
- `ProbabilityCurve` models the density over `curveDays`, not `days`. Otherwise
  the shaded region and the PoP printed beside it answer the same question two
  different ways.
- `PayoffLadder`'s chip names the date instead of asserting "at expiry", and
  explains the settled-leg assumption on hover.
- `StrategyMetrics` shows `near → horizon` when the legs differ.

## Verification At Close

- `cmake --build backend/build --target calculator_engine smoke_client` — 0 errors.
- `cd frontend && npm run build` — clean, 33 static pages.
- Smoke test, all four RPCs live: SPY 738.63 from Alpaca; 3M CMT 3.83% published
  / 3.7938% continuous, `as_of 2026-07-29`; matrix axis `30d @ 2026-07-30 -> 0d
  @ 2026-08-29`; calendar spread `curve_at=30d`, max_profit 1378.78.

## Known Outstanding (unchanged)

- `scripts/code_policy_check.sh` fails on `-ffast-math`, but every hit is in
  gitignored build output (`backend/build/**`), downloaded dependencies
  (`_deps/fastjson-src`), or SGEE documentation that merely *says* "no
  `-ffast-math`". No tracked first-party source carries the flag. The checker's
  scope is the defect, not the code.
- This repo compiles 255 sensen modules and imports 2.
- No dividend-yield term in the pricing path.
- `StrategyResponse.matrix` is filled correctly but not yet rendered; the UI
  still shows only the one-dimensional curve.
