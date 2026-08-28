# Integration handoff — mortgagefvcalculator.com → `sensen.finance.Finance`

**Audience:** the developers of `https://mortgagefvcalculator.com` (a Lovable app).
**Subject:** consuming the deployed `sensen.finance.Finance` gRPC service for every
mortgage, TVM and cash-flow figure the site displays.

This document is the *site-specific* handoff: your endpoint, your key, your RPCs,
and the traps that will actually bite a JavaScript client. It does not restate the
general contract — two documents already own that, and they are the source of truth
where this one is silent:

| Read | For |
| --- | --- |
| [`docs/API_USAGE.md`](API_USAGE.md) | **How to reach the API at all** — transports, error codes, quota mechanics, the rate limit, the numeric rule. Read §1, §3 and §6 before writing code. |
| [`docs/FINANCE_API.md`](FINANCE_API.md) | **What the functions compute**, plus the full auth model (§8) and quota model (§9). |
| [`clients/mortgagefv/README.md`](../clients/mortgagefv/README.md) | The ready-made client package for this site — vendored proto, generated stubs, a runnable example. Start there rather than hand-rolling. |

@author Olumuyiwa Oluwasanmi

---

## 1. Endpoint and transports

```
POST https://api.optionsandfuturescalculator.com/sensen.finance.Finance/<Method>
```

One host, one port, one path shape. Envoy fronts the engine and routes by path
prefix with a catch-all route, so every method on the service is reachable and
adding an RPC needs no proxy change.

| Transport | `content-type` | Works here? |
| --- | --- | --- |
| **gRPC-Web** | `application/grpc-web+proto` or `application/grpc-web-text`, plus `x-grpc-web: 1` | ✅ — what your browser client should use |
| **JSON** (Envoy's gRPC-JSON transcoder) | `application/json` | ✅ — for server-side callers and for curl |
| Native gRPC (HTTP/2) | — | ❌ **does not survive the ingress on this hostname** |

The gRPC-Web runtime sets `content-type` and `x-grpc-web` itself. **Do not set them
by hand** — you will fight the runtime and lose.

### Why native gRPC fails, and what its failure looks like

Railway's HTTP edge terminates HTTP/2 and speaks HTTP/1.1 to the container. gRPC
carries its status **entirely in HTTP/2 trailers — including on success** — and
HTTP/1.1 has nowhere to put them, so the status is dropped. The call computes the
right answer and then the client waits forever for a `grpc-status` that no longer
exists.

The symptoms, so you recognise them instead of filing a backend bug:

- `grpc-status: 2` with `grpc-message: Missing :te header`, or
- a hang that the client library eventually reports as **`Stream removed`**.

Neither means the method is missing and neither means the engine crashed. If you
genuinely need native framing from a server-side process, a native gRPC listener
exists behind Railway's TCP proxy — it is opt-in via `GRPC_NATIVE_PORT` and is
configured in `backend/start.sh`, which also documents the TLS-or-fail-closed
policy on that port. It is never `api.optionsandfuturescalculator.com`.

For a static site calling from the browser, none of this matters: use gRPC-Web.

### CORS

Already open and already correct for you. Preflights from
`https://mortgagefvcalculator.com` are answered with that origin echoed back, and
both `x-api-key` and `authorization` are in Envoy's `allow_headers`. `grpc-status`
and `grpc-message` are in `access-control-expose-headers`, which is what lets your
browser client see *why* a call failed rather than only *that* it failed. No server
change is needed for you to start sending the key.

---

## 2. Authentication

Send this header on **every** call:

```
x-api-key: pk_live_mfv_93a2802945beb32cc7f248e2eaa8a549d33c278480d8b522
```

| Property | Value |
| --- | --- |
| Type | `publishable` |
| Tier | `partner` — 2400 req/min, 1,200,000 compute-units/hr, **per caller** |
| Bound origins | `https://mortgagefvcalculator.com`, `https://www.mortgagefvcalculator.com` |
| Scope | `finance` |

### This key is safe in your page source

That is not a concession, it is the design. A publishable key's security is its
**origin binding**, not secrecy. A browser sets `Origin` on cross-origin requests
and cannot be made to lie about it by the page it is on, so a key lifted out of
your HTML and pasted into another site stops working there. Ship it in the bundle;
do not build a proxy to hide it.

Two corollaries worth internalising:

- The server stores **only the SHA-512** of this key, never the key itself. The
  loader refuses any registry entry that is not a 128-character hex digest — a
  guard that exists specifically to catch a plaintext key pasted where its hash
  belongs, which would otherwise silently never match and look like a bad key.
- Never send a **secret** key (`sk_live_…`) from a browser. A secret key arriving
  *with* an `Origin` header is treated as leaked: refused outright and logged
  loudly, because nothing legitimate produces that combination. You have not been
  issued one and do not need one.

### Rollout state: Observe mode, and the follow-up

`FINANCE_REQUIRE_KEY` is currently **unset**, which is Observe mode:

| `FINANCE_REQUIRE_KEY` | Behaviour |
| --- | --- |
| unset / `0` / `observe` | ← **today.** Everything served. Keys are recognised and metered to their own bucket; unkeyed callers are **not** refused, just metered as anonymous. Refusals logged as `would-deny`. |
| `1` / `warn` | Everything still served; `would-deny` logged at error level. |
| `2` / `enforce` | Refusals are real. |

So today, sending the key changes *nothing you can see* — and that is exactly why
you should send it from your first commit. Two reasons:

1. **Quota isolation, immediately.** Every unkeyed caller collapses into **one
   shared `~anonymous` bucket **per replica** — not per-user, not per-site, and
   `railway.json` sets `numReplicas: 2`, so the ceiling is twice the table value
   and which replica you land on decides whether you are refused. Unkeyed,
   your traffic competes with anonymous visitors of
   optionsandfuturescalculator.com and with any third party pointing at this host.
   A recognised key gets a bucket keyed to *its own* id at the `partner` tier.
2. **The flip to `enforce` becomes a no-op.** Flipping the switch is the planned
   follow-up once your traffic is observed arriving with the key. If you are
   already sending it, that deploy is invisible to you; if you are not, it is an
   outage. The switch will not be thrown until the `would-deny` logs are clean —
   but do not make yourself the reason it stays off.

---

## 3. A complete call, verified live

Sent to production, returned **HTTP 200**:

```http
POST /sensen.finance.Finance/ComputeHomeNpv HTTP/2
Host: api.optionsandfuturescalculator.com
content-type: application/json
x-api-key: pk_live_mfv_93a2802945beb32cc7f248e2eaa8a549d33c278480d8b522
Origin: https://mortgagefvcalculator.com
```

```json
{"property_price":"500000","down_payment":"100000","closing_costs_buy":"12000",
 "loan_amount":"400000","loan_annual_rate":"0.065","loan_term_years":30,
 "monthly_taxes_ins_hoa":"750","monthly_maintenance":"400",
 "annual_appreciation_rate":"0.03","selling_closing_cost_percent":"0.06",
 "monthly_rent_saved":"2600","annual_rent_increase":"0.03",
 "annual_discount_rate":"0.07","holding_period_years":10}
```

Response:

```json
{"netPresentValue":-28177.98,"internalRateOfReturn":0.04874,
 "futureSalePrice":671958.19,"futureEquity":332853.68}
```

Three things to notice in that exchange, because each is a rule and not an accident:

- **Every money and rate input is a quoted string.** `"500000"`, not `500000`.
  Only the two genuinely-integer fields (`loan_term_years`, `holding_period_years`)
  are bare numbers. See §5.
- **The request used `snake_case`; the response came back `lowerCamelCase`.**
  Proto-JSON accepts either spelling on input and always *emits* lowerCamelCase.
  Do not assume the shape you sent is the shape you get back — parse
  `netPresentValue`, not `net_present_value`.
- **`HomeNpvResponse` is entirely `double`.** These four values are safe as plain
  JS numbers. That is not true of most responses on this service — see §5.

---

## 4. The RPC surface you need

Generated from `backend/proto/finance.proto`. `str` is a **decimal string**;
`i32` is a plain JSON integer; `dbl` is a `double`. A field marked **required**
is refused by name if omitted (§7); everything else defaults to zero when
genuinely absent.

Two enums appear below:

```proto
enum AnnuityTiming { END_OF_PERIOD = 0; BEGINNING_OF_PERIOD = 1; }
enum RefinanceRequest.ClosingCostType { PAID_IN_CASH = 0; ROLLED_INTO_LOAN = 1; }
```

Both zero values are chosen so that a caller who says nothing gets the safe
reading: payments at period end, and *the loan did not silently grow*.

### 4.1 Time value of money

`rate` here is **per period** — see §6, this is the single most costly field on the
whole surface to get wrong.

| RPC | Request fields | Returns |
| --- | --- | --- |
| `ComputePayment` | `rate` str **req** · `periods` i32 · `present_value` str **req** · `future_value` str · `timing` AnnuityTiming | `DecimalResponse { value str }` |
| `ComputePresentValue` | `rate` str **req** · `periods` i32 · `payment` str **req** · `future_value` str · `timing` | `DecimalResponse` |
| `ComputeFutureValue` | `rate` str **req** · `periods` i32 · `payment` str **req** · `present_value` str · `timing` | `DecimalResponse` |
| `ComputeRate` | `periods` i32 · `payment` str **req** · `present_value` str **req** · `future_value` str · `timing` · `guess` str | `DecimalResponse` |
| `ComputePeriods` | `rate` str **req** · `payment` str **req** · `present_value` str **req** · `future_value` str · `timing` | `DecimalResponse` |
| `ComputeFutureValueDetailed` | `annual_rate` str **req** · `years` i32 · `annual_contribution` str **req** · `current_principal` str **req** · `annual_inflation_rate` str · `compound_frequency` i32 **req, non-zero** | `FutureValueDetailedResponse { nominal_fv, inflation_adjusted_fv, total_contributions, total_interest_earned — all str }` |

`ComputeFutureValueDetailed` refuses a zero `compound_frequency` rather than
picking one: the compounding frequency changes the answer materially, so no
default is invented.

**Sign convention:** `ComputePayment` returns a **negative** value —
`-1798.651575458257198999` for $300,000 at 6% over 30 years. That is the Excel
cash-flow convention (money leaving the borrower), not a bug. Negate it for display.

### 4.2 Amortization

| RPC | Request fields | Returns |
| --- | --- | --- |
| `ComputeAmortization` | `loan_amount` str **req** · `annual_rate` str **req** · `term_months` i32 · `monthly_overpayment` str · `pmi_annual_rate` str · `original_home_value` str | `AmortizationResponse { schedule[], summary }` |
| `ComputeDetailedAmortization` | same, plus `annual_tax_rate` str | `DetailedAmortizationResponse { schedule[], summary }` |

```
AmortizationRow  { period i32, start_balance, scheduled_payment, extra_payment,
                   interest_paid, principal_paid, pmi_paid, end_balance }   — all str
MortgageSummary  { total_principal_paid, total_interest_paid, total_pmi_paid,
                   total_payments_paid — all str; actual_term_months i32 }
```

The detailed variants add `tax_savings` per row and `total_tax_savings` to the
summary. `original_home_value` is what PMI drops off against — the home's value,
not the loan. `term_months` is capped at 1200: one row is allocated per period, so
an unbounded term is a denial of service dressed as a mortgage.

`ComputeAmortizationBatch` also exists but **is deliberately not on this list.**
Every one of its inputs is `double`, on purpose, because it returns
summaries-only comparison grids rather than cent-exact figures. Do not use it for
anything a user reads as money.

### 4.3 Refinance, payoff, recast, HELOC

| RPC | Request fields | Returns |
| --- | --- | --- |
| `ComputeRefinance` | `current_loan_balance` **req** · `current_monthly_payment` **req** (P&I only) · `current_annual_rate` **req** · `current_remaining_months` i32 · `property_value` **req** · `new_annual_rate` **req** · `new_term_years` i32 · `closing_costs` **req** · `closing_cost_type` enum · `cash_out_amount` · `current_pmi_monthly` · `new_pmi_monthly` · `pmi_drop_off_ltv` · `payments_per_year` i32 | `RefinanceResponse` — see below |
| `ComputePayoffTiming` | `current_loan_balance` **req** · `annual_rate` **req** · `current_monthly_payment` **req** · `extra_monthly_payment` · `payments_per_year` i32 | `{ original_months_remaining i32, new_months_remaining i32, months_saved i32, total_interest_saved str }` |
| `ComputeMortgageRecast` | `current_loan_balance` **req** · `current_monthly_payment` **req** · `lump_sum_payment` · `annual_rate` **req** · `remaining_months` i32 · `payments_per_year` i32 | `{ new_monthly_payment str, monthly_savings str }` |
| `ComputeHeloc` | `home_value` **req** · `current_mortgage_balance` **req** · `max_ltv_rate` **req** (`0.80` = 80%) · `drawn_amount` · `annual_rate` **req** · `repayment_term_years` i32 · `payments_per_year` i32 | `{ available_equity, draw_period_payment (interest only), repayment_period_payment (fully amortizing) — all str }` |

```
RefinanceResponse {
  new_loan_amount            str
  new_monthly_payment        str    // P&I only
  monthly_savings_initial    str    // (old P&I + PMI) − (new P&I + PMI)
  current_loan_pmi_drop_off_months  i32   // 0 = no PMI / already below; −1 = never reached
  new_loan_pmi_drop_off_months      i32
  payoff_date_shift_months          i32
  simple_break_even_months          i32
  cash_flow_break_even_months       i32
  equity_adjusted_break_even_months i32
  total_savings_over_life           dbl   // ← double, see §5
}
```

Note the three separate break-even figures. They answer different questions and
disagree legitimately; pick one deliberately rather than showing whichever is
smallest. And note the `−1` sentinel on the PMI drop-off months: it means *never
reached*, not *zero months*. Rendering it raw produces "PMI drops off in −1
months" on somebody's screen.

Every "monthly payment" input on this group is **P&I only** — PMI goes in its own
field. Passing a full PITI figure where P&I is expected produces a plausible wrong
answer, not an error.

### 4.4 Home ownership models

| RPC | Request fields | Returns |
| --- | --- | --- |
| `ComputeHomeFutureValue` | `current_property_value` **req** · `annual_appreciation_rate` **req** · `current_loan_balance` **req** · `annual_mortgage_rate` **req** · `current_monthly_payment` **req** (P&I) · `target_years` i32 · `payments_per_year` i32 | `{ future_property_value dbl, future_loan_balance str, future_equity str }` |
| `ComputeRentVsBuy` | `property_price` **req** · `down_payment` **req** · `annual_home_appreciation` **req** · `current_monthly_rent` **req** · `annual_rent_increase` **req** · `annual_investment_return` **req** · `years` i32 · **plus exactly ONE shape** — see below | `{ total_cost_of_buying dbl, total_cost_of_renting dbl, is_buying_better bool, buying_advantage dbl }` + the `_exact` strings on the amortising shape only |
| `ComputeHomeNpv` | `property_price` · `down_payment` · `closing_costs_buy` · `loan_amount` · `loan_annual_rate` · `loan_term_years` i32 · `monthly_taxes_ins_hoa` · `monthly_maintenance` · `annual_appreciation_rate` · `selling_closing_cost_percent` (`0.06` = 6% of sale price) · `monthly_rent_saved` · `annual_rent_increase` · `annual_discount_rate` — **all twelve strings required** | `{ net_present_value dbl, internal_rate_of_return dbl, future_sale_price dbl, future_equity dbl }` |

### `ComputeRentVsBuy` carries TWO shapes, and a zero is not a silence

The request message holds both the legacy composite and the amortising inputs,
and a caller must supply **exactly one** of them:

| shape | fields | model |
| --- | --- | --- |
| legacy | `monthly_piti_and_maintenance` (the all-in monthly cost) | the original, computed in `double` |
| amortising | `loan_annual_rate` · `loan_term_years` · `loan_amount` · `monthly_taxes_ins_maintenance` (non-debt carrying costs only) · `closing_costs_buy` · `selling_cost_percent` · `annual_inflation_rate` | amortises the loan; also fills the `_exact` decimal strings |

**A field is read as "not this shape" when it is absent OR zero, and the two
are equivalent on purpose.** This service has two kinds of caller and they
spell the same intent differently:

- a JSON caller through the transcoder OMITS what it does not use, so the field
  arrives as `""`;
- the mortgage assistant CANNOT omit — `mortgage_verification.cppm`'s G2b
  refuses a missing key — so it emits every declared field and says "not this
  shape" with the value `0`.

Testing presence alone reads the first correctly and the second backwards.
It did, until 2026-08-27: **every** request the assistant could emit set both
shape flags and was refused `INVALID_ARGUMENT` as self-contradictory. Send
either shape; do not send both, and do not expect a zeroed field to select one.

The decision is a total function over five field signals (absent, zero,
negative, positive, malformed) with a fixed precedence — malformed outranks
negative outranks both-shapes outranks a shape — and all twenty cells of its
input space are asserted by name in section 24 of
`backend/tests/test_finance_service_validation.cpp`.

**The RESPONSE differs by shape too, and the difference is deliberate.** Fields
1–4 are always populated and keep their `double` type and field numbers, because
replacing a double with a string at the same number is wire-breaking — an old
client decodes the new bytes as garbage rather than raising. Everything else is
populated **only on the amortising path**:

| field | type | legacy | amortising |
| --- | --- | --- | --- |
| `totalCostOfBuying` / `totalCostOfRenting` | `double` | ✓ | ✓ |
| `isBuyingBetter` | `bool` | ✓ | ✓ |
| `buyingAdvantage` | `double` | ✓ | ✓ |
| `totalCostOfBuyingExact` / `totalCostOfRentingExact` / `buyingAdvantageExact` | `string` | **empty** | ✓ |
| `ownerTerminalWealth` / `renterTerminalWealth` | `string` | **empty** | ✓ |
| `finalLoanBalance` / `homeSalePrice` / `sellingCosts` | `string` | **empty** | ✓ |
| `totalPrincipalPaid` / `totalInterestPaid` / `totalRentPaid` | `string` | **empty** | ✓ |
| `realBuyingAdvantage` / `realOwnerTerminalWealth` / `realRenterTerminalWealth` | `string` | **empty** | ✓ (zero inflation ⇒ equals the nominal) |

The legacy model computes in `double`, so rendering it to eighteen places would
invent digits it never had. **An empty `_exact` string is the honest statement
that this path has no exact companion — not a missing value.** Branch on
`buyingAdvantageExact !== ""` if you need to know which model answered; do not
branch on the status, which is 200 either way.

Verified live on 2026-08-28, both shapes through the public ingress:

```
amortising  buyingAdvantage 70977.1811034778
            buyingAdvantageExact "70977.181103477791935949"
legacy      buyingAdvantage  9132.293106093437
            buyingAdvantageExact ""
```

The `real*` fields are the inflation-adjusted companions, deflated by
`(1 + annual_inflation_rate) ^ years`. Send `annual_inflation_rate` as `"0"` (or
omit it) and they equal their nominal counterparts; that is why they are safe to
read unconditionally on the amortising path.

Two refusals worth knowing: a request whose every field is the convention zero
gets `carries neither` rather than an answer computed from a loan the service
invented, and a negative `monthly_piti_and_maintenance` is refused rather than
priced.

`ComputeHomeNpv` has the strictest input contract on the surface: every one of its
twelve decimal fields is required, and `loan_term_years` and
`holding_period_years` must each be `1..100`. `monthly_rent_saved` is the *imputed*
rent the purchase displaces — the number that makes the NPV a comparison rather
than a cost tally.

`ComputeHomeFutureValue`'s mixed return type is not sloppiness; §5 explains it.

### 4.5 Rental and cash flow

| RPC | Request fields | Returns |
| --- | --- | --- |
| `ComputeRentalRoi` | `property_value` **req** · `total_cash_invested` **req** · `periodic_gross_rent` **req** · `periodic_operating_expenses` **req** · `periodic_mortgage_payment` · `periods_per_year` i32 | `{ net_operating_income (before debt service), annual_cash_flow (after), cash_on_cash_return, cap_rate, gross_rent_multiplier — all str }` |
| `ComputeNpv` | `rate` dbl · `values` repeated dbl | `DoubleResponse { value dbl }` |
| `ComputeIrr` | `values` repeated dbl · `guess` dbl | `DoubleResponse` |
| `ComputeXnpv` | `rate` dbl · `values` repeated dbl · `dates` repeated dbl | `DoubleResponse` |
| `ComputeXirr` | `values` repeated dbl · `dates` repeated dbl · `guess` dbl (`rate` is ignored) | `DoubleResponse` |

The four cash-flow RPCs share `DatedCashFlowRequest` between XNPV and XIRR, so the
message carries fields each ignores: `rate` is XNPV-only, `guess` is XIRR-only.
Setting the wrong one is silently harmless, which also means it will not warn you.

`ComputeNpv`/`Irr`/`Xnpv`/`Xirr` take and return **doubles**, not strings — sensen
genuinely computes them in double, and §5 explains why widening them would be a
lie rather than a courtesy.

---

## 5. Trap 1 — money is a decimal `string`, and JS `number` **is** float64

This is the one that produces wrong numbers on a page with no error anywhere.

Every money and rate field on the mortgage surface is a `string` on the wire:
`"500000"`, not `500000`. The moment you `parseFloat` it you are in IEEE-754
float64, and the loss **compounds** — over a 360-period amortization the schedule
stops closing. On the live service `start − principal − end` is exactly `0E-18` on
every one of the 360 rows, which is only achievable if the value never passes
through a float.

```ts
// WRONG — corrupts the figure before you have used it once, silently, HTTP 200.
const payment = parseFloat(res.getValue());
const annual  = payment * 12;
const total   = schedule.reduce((a, r) => a + parseFloat(r.getInterestPaid()), 0);
```

```ts
// RIGHT — exact end to end; round ONLY at the display edge.
import Decimal from 'decimal.js';

const payment = new Decimal(res.getValue());       // "-1798.651575458257198999", exact
const annual  = payment.times(12);                 // still exact — no float touched it
const total   = schedule.reduce(
  (a, r) => a.plus(new Decimal(r.getInterestPaid())), new Decimal(0));

element.textContent = payment.negated().toFixed(2); // "1798.65" — round here, and only here
```

Four working rules:

1. Parse every `string` field into `Decimal` **on receipt**. Do not let a raw money
   string sit in state where someone later `+`s it and gets string concatenation
   or a coerced float.
2. Do all arithmetic in `Decimal`.
3. Format with `.toFixed(2)` (or feed the already-rounded string to
   `Intl.NumberFormat`) only at render time.
4. Requests carry the same rule back: `req.setRate('0.005')`, not
   `req.setRate(0.005)`. On the JSON surface: `"rate": "0.005"`, not
   `"rate": 0.005`.

Chart *pixels* can be floats — a chart axis is not a ledger. Every number actually
printed on the page must come from the `Decimal` path.

### Why the split is per-field, and not "money = string" as a slogan

A field is a `string` where the engine computes in `BigDecimal` — an exact
`__int128` scaled by 1e18, eighteen decimal places — and a `double` where the
engine **genuinely computes in `double`**. Widening a double to a string would
claim a precision the engine never had; narrowing a BigDecimal to a double
discards precision it actually has. Every field carries a comment in the proto
saying which it is and why.

`ComputeHomeFutureValue` is the cleanest illustration, and worth reading twice
because it looks inconsistent until you know why:

```
HomeFutureValueResponse {
  future_property_value  double   ← single pow(): appreciation compounded once
  future_loan_balance    string   ← amortization recursion: rounding compounds
  future_equity          string   ← exact subtraction of the two above
}
```

The property value is one `std::pow()` call — there is no recursion for error to
accumulate through, so a double is the engine's honest precision. The loan balance
comes out of an amortization where each period's rounding feeds the next, which is
exactly the case a decimal string exists to protect. Same message, same call, two
different answers to "how exact is this number", because they are two different
computations.

Consequence for your code: **do not write one generic response mapper that treats
every field the same way.** Wrap the strings in `Decimal`; use the doubles as
plain numbers. Wrapping a double in `Decimal` is not harmless — it dresses up
float64 noise as eighteen significant places.

The doubles you will meet on this surface: `RefinanceResponse.total_savings_over_life`,
`HomeFutureValueResponse.future_property_value`, all of `RentVsBuyResponse`, all of
`HomeNpvResponse`, and everything returned by `ComputeNpv`/`Irr`/`Xnpv`/`Xirr` and
`ComputeAmortizationBatch`.

---

## 6. Trap 2 — `rate` on the TVM calls is **per period**, not annual

`ComputePayment`, `ComputePresentValue`, `ComputeFutureValue` and `ComputePeriods`
take `rate` as the rate **for one period**. For 6% per year paid monthly:

```ts
req.setRate('0.005');    // 0.06 / 12  ✅
req.setPeriods(360);
```

```ts
req.setRate('0.06');     // ❌ 6% PER MONTH — a 72%/yr loan
req.setPeriods(360);
```

The mortgage-specific RPCs are the other convention: they take `annual_rate` (or
`current_annual_rate`, `new_annual_rate`, `loan_annual_rate`,
`annual_mortgage_rate`) **plus** `payments_per_year`, and do the division
themselves.

**Mixing these up produces a plausible number, not an error.** There is no
validation that can catch it — 0.06 is a perfectly legal per-period rate, and
0.005 is a perfectly legal annual one. The service refuses malformed input, not
input that is well-formed and wrong. The defence is in your code:

- Keep the annual rate as the single value in your app state, and convert at the
  call boundary — never store both and hope they stay in sync.
- Name the variable for its period (`monthlyRate`, not `rate`).
- Assert against a known answer in a test: $300,000 at 6%/30yr must return
  `-1798.651575458257198999`. If a refactor swaps the convention, that assertion
  is the only thing standing between it and production.

Field-by-field: the "which fields take which" column is in §4. `payments_per_year`
is `12` monthly, `26` bi-weekly; the same convention appears as `periods_per_year`
on `ComputeRentalRoi`.

---

## 7. Trap 3 — required fields now refuse **by name**

Your first hand-rolled call will probably fail like this:

```json
HTTP 400
{"code":3,"message":"closing_costs_buy is required and was not supplied","details":[]}
```

**That is the feature, not a regression.** Here is what it replaced.

Envoy's gRPC-JSON transcoder silently ignores unknown JSON fields, and proto3
scalars have **no field presence** — an absent field and one explicitly set to its
default are indistinguishable on the wire. So before this hardening, measured
against production:

| Request | Response | HTTP |
| --- | --- | --- |
| `{"present_value":"300000","rate":"0.005","periods":360}` | `-1798.651575458257198999` | 200 |
| `{"presentvalue":"300000", …}` (one typo) | `0.000000000000000000` | 200 |
| `{}` | `0.000000000000000000` | 200 |
| `{"present_value":"300000","periods":360}` (**no rate**) | **`-833.333333333333333333`** | 200 |

The last row is the dangerous one. A zero looks broken and gets investigated. But
`-833.33` is principal ÷ periods — a **confidently wrong 0%-interest mortgage**,
under HTTP 200, that would have shipped straight onto a page. That is precisely
the defect the refusal exists to prevent, and it is why a 400 naming your field is
better news than a 200.

Now: a field the computation cannot proceed without — a rate, a principal, a price
— is refused by name if it arrives empty. Fields that are **genuinely optional**
still default to zero, because omitting them is a real question and not a mistake:

```
future_value · guess · monthly_overpayment · pmi_annual_rate · original_home_value
annual_tax_rate · annual_inflation_rate · cash_out_amount · current_pmi_monthly
new_pmi_monthly · pmi_drop_off_ltv · drawn_amount · extra_monthly_payment
lump_sum_payment · periodic_mortgage_payment
```

Note the empty-string distinction this buys you: because money fields are
*strings*, `""` means **absent** while `"0"` means **an explicit zero**. A caller
who means "$0 overpayment" can and should say `"0"`. That difference is impossible
with a numeric field, and is one more reason these are strings.

### Use generated stubs, not hand-written JSON

This is the practical conclusion, and it is worth more than any amount of care:

**A typo'd field name is indistinguishable from an omission.** The transcoder drops
`presentvalue` on the floor and proto3 cannot tell the server that
`present_value` was never set versus set to `""`. Server-side required-checks catch
the fields someone remembered to mark — a *compile-time field-name check* catches
the entire class before it reaches the wire.

`clients/mortgagefv/` already contains generated stubs (`FinanceClient`,
`finance_pb`) built from the vendored proto with a pinned toolchain, plus
`scripts/gen_proto.sh` to regenerate both files from one invocation. Copy that
directory in. If you must use JSON — for a server-side call or a curl probe —
then **assert on the answer, not on HTTP 200**: a zero, or a suspiciously round
figure, means check your field names first.

---

## 8. Errors

Full table in [`docs/API_USAGE.md`](API_USAGE.md) §3. What matters for you:

| Code | Meaning | Your move |
| --- | --- | --- |
| `3` `INVALID_ARGUMENT` | Bad input — malformed decimal, a required field omitted (named in the message), a magnitude or rate outside what the engine can price. Surfaces as **HTTP 400** on the JSON path. | Fix the input. Not retryable. Show the message; it names the field. |
| `7` `PERMISSION_DENIED` | We know who you are, and no — wrong origin, wrong scope, or a secret key from a browser. | Check the `Origin` your page actually sends. |
| `8` `RESOURCE_EXHAUSTED` | Quota exceeded. Message carries a real `retry in <N>s` computed from the bucket's own refill rate. | Honour the retry-after. Do **not** tight-retry. |
| `9` `FAILED_PRECONDITION` | The maths has no answer — a payment below the periodic interest, a solver that cannot converge. | Surface it. Never substitute a default; a wrong number is worse than a refusal. |
| `12` `UNIMPLEMENTED` | **Your proto is newer than the server.** | Re-check the pinned commit in the vendored proto header. |
| `16` `UNAUTHENTICATED` | No / bad / expired / revoked key. Cannot occur today (Observe mode); will once `enforce` lands. | Handle it now so the flip is a no-op. |

### The 429 that is not a gRPC error at all

**A bare HTTP 429 with no `grpc-status` is Envoy's local rate limit**, not the
quota system. It is:

- **10 requests/second sustained, burst 100** (`max_tokens: 100`,
  `tokens_per_fill: 10`, `fill_interval: 1s`), marked with `x-local-rate-limit: true`;
- **site-wide and completely key-independent** — shared with every caller of every
  service on this host;
- **below every quota tier.** Your `partner` tier is 2400 req/min = 40 req/s, so
  above 10 req/s *the proxy limit is the real constraint and a bigger tier will
  not raise it.*

Because it carries no gRPC status, grpc-web surfaces it as a **transport error** —
it will arrive in your code looking like a network failure, not a rate limit.

**So: if your client reports a network error under load, check for 429 before
debugging anything else.** Most gRPC-Web error handling never looks, and the
resulting bug hunt goes looking for a backend fault that is not there.

Design for it: batch what a single user action fires, cache aggressively (these are
pure functions of their inputs — identical inputs give identical outputs forever,
so caching is trivially safe), debounce slider and text-input recalculation, and
back off on 429 rather than retrying immediately. Do not load-test production to
find the limit; it is 10 req/s, take it as given.

---

## 9. Hardening you can rely on

You do not need to sanitise inputs before sending them. The six home-finance RPCs
carry adversarial-input guards, each written against a defect **measured on a live
engine**, and each refusing in **69–98 ms** rather than by timing out:

| Guard | What it prevented |
| --- | --- |
| `payments_per_year` capped at **366** (daily, leap year) and floored above 0 | It had a floor but no ceiling. `new_term_years=100, payments_per_year=20,000,000` burned **11.53 s of CPU for one request**, charged only 101 compute units against a 120,000/hr budget — the price was computed from `years×12`, not `years×payments_per_year`. Larger values overflowed the int32 product outright: `payments_per_year=2,000,000,000` returned `payoff_date_shift_months = -1863463212`, a wrong answer rather than an error. |
| Decimal **magnitude** capped at 15 integer digits, checked on the **raw wire string** | `BigDecimal` is exact but not unbounded — `__int128` scaled by 1e18 caps out around 1.7e20, and neither its string constructor nor `multiply()`/`pow()` detect overflow; they wrap silently. A 200-digit `current_loan_balance` came back as a **NEGATIVE `new_loan_amount`** from positive input; a 30-digit principal produced a `new_loan_amount` unrelated to it. The check runs on the string *before* a `BigDecimal` is ever constructed, because an already-overflowed value cannot reliably report that it overflowed. |
| Per-period rate must be **greater than −100%**, plus a *joint* rate×term compounding check | `annual_rate = -1` made `ComputePayoffTiming` return `original_months_remaining = 0` — "this loan is already paid off" — for a loan that never amortises, because `log(0)/log(1−1)` reduces through IEEE floating point to exactly `0.0` instead of raising. Separately, a rate and a term that are each individually plausible can compound into a factor no fixed-point type holds (6% compounded *annually* over 1200 periods); neither field alone catches that, only their product does. |

The shape of all three is the same and it is the service's general posture: **it
refuses rather than guessing.** A malformed decimal, an unstated compounding
frequency, a bond with neither yield nor price, a ragged batch, a term over 1200
months, a loan whose payment cannot cover its interest — each produces an error,
never a plausible-looking number.

Treat every refusal as correct behaviour and surface it to the user. **Do not fall
back to a default.** On a mortgage calculator the failure mode that matters is not
a blank field; it is a confident wrong number on somebody's largest financial
decision.

---

## 10. Verifying your integration

Adapted from [`docs/API_USAGE.md`](API_USAGE.md) §8 for this site:

1. `GET https://api.optionsandfuturescalculator.com/healthz` → `200 ok`.
   **Note the `z`.** `docs/API_USAGE.md` §8 currently writes this as `/health`,
   which is wrong: `backend/envoy.yaml` matches the exact path `/healthz` and
   direct-responds `200 ok`. Any other path — `/health` included — falls through
   to the catch-all gRPC route and is answered as a malformed gRPC call, so a
   liveness check pointed at `/health` will look broken while the service is
   perfectly healthy.
2. Call `ComputePayment` with `rate="0.005"`, `periods=360`,
   `present_value="300000"`. Assert the value is exactly
   `-1798.651575458257198999` — matching the closed-form annuity payment, not
   merely that *a* response arrived. This assertion is also your regression guard
   against the §6 per-period/annual mix-up.
3. Send `{"rate":"12x3","periods":360,"presentValue":"300000"}`. Assert
   `grpc-status: 3` with `rate is not a decimal number: "12x3"`, and that your
   client **surfaces** it rather than substituting a default.
4. Omit a required field — drop `closing_costs_buy` from the §3 `ComputeHomeNpv`
   body. Assert `closing_costs_buy is required and was not supplied`.
5. Assert a returned decimal string survives your client round-trip **with no
   precision loss**. This failure is silent, so it must be asserted rather than
   eyeballed: take `res.getValue()`, run it through your state layer and your
   formatter, and compare the string.
6. Confirm your key is arriving: send a call with `x-api-key` and one without,
   and check that the backend logs attribute them to different buckets. In Observe
   mode both succeed, so this is the *only* way to catch a key that is silently
   not being sent — and catching it now is the whole point of §2.

7. **Rent-vs-buy: assert the SHAPE you get back, not just a 200.** Send the same
   scenario twice — once as the legacy composite, once as the amortising inputs —
   and assert that the first returns an empty `buyingAdvantageExact` and the
   second a non-empty one. Both are 200, so a status check cannot tell them
   apart, and silently getting the legacy model when you asked for the
   amortising one means your loan is not being amortised at all: the composite
   is treated as the entire monthly cost of owning, which understates it by the
   whole P&I payment.

   Then assert the refusals, because they are the ones that will bite:

   ```
   both shapes   -> grpc-status 3, "cannot be combined"
   neither shape -> grpc-status 3, "carries neither"
   ```

   Sending a zeroed field expecting it to select a shape lands on the second of
   those. **A zero and an absent field mean the same thing here** (§4.4), so a
   request whose every field is a convention zero is refused rather than answered
   from a loan the service invented for you.

There is also a standing gate you can borrow rather than reinvent:

```bash
python3 scripts/probe_finance_service.py https://api.optionsandfuturescalculator.com
```

Every case in it is checked against something derived independently of the engine
— put-call parity, price/yield inversion, schedule closure, the closed-form
annuity formula — because a check that compares the engine to its own previous
output only detects a crash, and this service's failure mode is a **wrong number**.

---

## 11. Contact points in this repo

| Thing | Where |
| --- | --- |
| The contract | `backend/proto/finance.proto` (canonical) · `clients/mortgagefv/proto/finance.proto` (vendored, header-only diff) |
| Ready-made client | `clients/mortgagefv/` — stubs, `gen_proto.sh`, runnable example |
| Transport / errors / quota / numerics | `docs/API_USAGE.md` |
| What each function computes, auth and quota models | `docs/FINANCE_API.md` |
| Required-field and adversarial guards | `backend/src/modules/finance_service.cpp` |
| Key verification, hashing, origin binding | `backend/src/modules/api_key.cpp` |
| Native gRPC listener, TLS policy | `backend/start.sh` |
| Rate limit, CORS, transcoder config | `backend/envoy.yaml` |
