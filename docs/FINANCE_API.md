# Using `sensen.finance.Finance` from another site or service
@author Olumuyiwa Oluwasanmi

## Scope

This document covers the public API surface, wire conventions, validation rules, authentication, quotas, and integration patterns for the financial calculation service defined across:
- `backend/proto/finance.proto` (package `sensen.finance`, service `Finance`: 46 RPCs)
- `backend/src/modules/finance_service.cppm` and `backend/src/modules/finance_service.cpp` (service implementation, validation bounds, shape dispatch, and error mapping)
- `backend/src/modules/quota.cppm` and `backend/src/modules/quota.cpp` (quota policy, metering, and cost calculation)
- `backend/src/modules/api_key.cppm` and `backend/src/modules/api_key.cpp` (API key authentication, origin validation, and entitlement gating)
- Test coverage in `backend/tests/test_finance_service_validation.cpp`, `backend/src/smoke_client.cpp`, `backend/tests/test_mortgage_verification.cpp`, and `backend/tests/test_vendored_proto_drift.cpp`.

> [!NOTE]
> **Companion contract reference:**
> - [`docs/FINANCE_API.md`](FINANCE_API.md) (this document) is the practical integration guide and operational manual for external callers reaching `sensen.finance.Finance` over gRPC-Web and Envoy's gRPC-JSON transcoder. It details hostnames, protocol routing, CORS configuration, exact decimal formatting rules, authentication credentials, quota tiers, and worked call examples.
> - [`docs/api/GRPC_SURFACE.md`](api/GRPC_SURFACE.md) is the exhaustive wire contract specification and per-message field reference for all four canonical protobuf schemas (`calculator.proto`, `finance.proto`, `assistant.proto`, `mortgage_assistant.proto`). It defines every field tag, wire type, presence rule, numerical bound, and error mapping in detail across the entire system.

The sensen financial library is served at:

```
https://api.optionsandfuturescalculator.com
```

Two call styles reach the container through that URL, verified live. **Native
gRPC over HTTP/2 is not one of them** — see the note below the table.

| Caller | Protocol | `content-type` |
| --- | --- | --- |
| Browser / any JS frontend | gRPC-Web | `application/grpc-web-text` or `application/grpc-web+proto` |
| Backend service, any language | JSON, via Envoy's gRPC-JSON transcoder | `application/json` |

**Native gRPC does not survive the Railway ingress on this hostname.** A
native `grpc.secure_channel("api.optionsandfuturescalculator.com:443", ...)`
call fails with `Stream removed`, and no corresponding request appears in
`railway logs` — only gRPC-Web and the JSON transcoder actually reach the
container through the public custom domain. Native gRPC does work against the
engine directly (e.g. `localhost:50051` in local dev) and against Railway's
own TCP proxy (`*.proxy.rlwy.net:<port>`) — just not through
`api.optionsandfuturescalculator.com`. Backend/server-side callers should use
the JSON surface in §3 instead of a native gRPC stub.

There is no separate host, port or auth for gRPC-Web vs. JSON. Envoy fronts
the engine and routes by path prefix, so the same
`/sensen.finance.Finance/<Method>` path serves both.

**CORS is open.** A preflight from an arbitrary origin is answered with that
origin echoed back, so a third-party site can call this directly from the
browser with no proxy of its own:

```
$ curl -i -X OPTIONS https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputePayment \
    -H "Origin: https://some-other-site.example" \
    -H "Access-Control-Request-Method: POST"

HTTP/2 200
access-control-allow-origin: https://some-other-site.example
access-control-expose-headers: custom-header-1,grpc-status,grpc-message
```

`grpc-status` and `grpc-message` are exposed deliberately — without them a
browser client can see that a call failed but not why.

---

## 1. Get the contract

Everything a client needs is in one file:

```
backend/proto/finance.proto      package sensen.finance, service Finance
```

Copy it into your project. It has no imports, so nothing else travels with it.
This is the canonical copy — there is no published package for it (no npm
registry entry, no Buf Schema Registry) and none is planned for a single
consumer. **Vendor it**: commit the copy into your own project and note the
source commit hash in a comment at the top of the file, so a future update is
a deliberate "pull the file at a newer commit, bump the comment, regenerate"
rather than a silent drift from this repo's contract. `clients/mortgagefv/`
in this repo is a worked example of the whole pattern — vendored proto with a
commit-hash header, a cloned `gen_proto.sh`, generated stubs, and a runnable
example — for a real external consumer of this service.

---

## 2. Browser / TypeScript (gRPC-Web)

Generate a client the same way this repo does (see `scripts/gen_proto.sh`):

```bash
protoc \
  --proto_path=. \
  --plugin=protoc-gen-grpc-web="$(command -v protoc-gen-grpc-web)" \
  --js_out=import_style=commonjs,binary:./src/grpc \
  --grpc-web_out=import_style=typescript,mode=grpcwebtext:./src/grpc \
  finance.proto
```

Then call it:

```ts
import { FinanceClient } from './grpc/FinanceServiceClientPb';
import { PaymentRequest, AmortizationRequest } from './grpc/finance_pb';

const client = new FinanceClient('https://api.optionsandfuturescalculator.com');

// Monthly payment on a 300,000 loan at 6% nominal over 30 years.
const req = new PaymentRequest();
req.setRate('0.005');          // per PERIOD, not per year
req.setPeriods(360);
req.setPresentValue('300000');

client.computePayment(req, {}, (err, res) => {
  if (err) {
    console.error(err.code, err.message);   // grpc-status is exposed via CORS
    return;
  }
  console.log(res.getValue());  // "-1798.651575458257198999"
});
```

Note the payment is **negative**: it is a cash outflow, following the Excel
convention sensen implements. Flip the sign for display if you want a positive
number on screen.

---

## 3. Backend service (JSON, via the gRPC-JSON transcoder)

**Native gRPC over HTTP/2 does not survive the Railway ingress on the public
custom domain** — see the note at the top of this document. Envoy's
`grpc_json_transcoder` filter maps `POST /sensen.finance.Finance/<Method>`
with `content-type: application/json` onto the same service descriptor, so
any HTTP client in any language works with no generated stub at all. Field
names are proto-JSON lowerCamelCase, decimal fields are JSON *strings* (not
numbers — the same exactness reasons as §4 apply on the wire, not just in a
gRPC client), and `x-api-key` passes through the transcoder unchanged.

**Python**

```python
import requests

res = requests.post(
    "https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputeAmortization",
    json={
        "loanAmount": "300000", "annualRate": "0.06", "termMonths": 360,
        "monthlyOverpayment": "500",
    },
    headers={"content-type": "application/json"},
).json()
print(res["summary"]["actualTermMonths"])      # 212 -- retired early
print(res["summary"]["totalInterestPaid"])     # exact decimal string
```

**Go**

```go
body, _ := json.Marshal(map[string]any{
    "spot": 100, "strike": 100, "rate": 0.05, "volatility": 0.2, "yearsToExpiry": 1,
})
resp, _ := http.Post(
    "https://api.optionsandfuturescalculator.com/sensen.finance.Finance/PriceBlackScholes",
    "application/json", bytes.NewReader(body),
)
```

If your server-side language has a real gRPC stub and you specifically need
native framing rather than JSON, generate it from `finance.proto` and point
it at the engine directly (e.g. `localhost:50051` in local dev) or at
Railway's own TCP proxy — never at `api.optionsandfuturescalculator.com`,
which only the gRPC-Web and JSON-transcoder paths reach. `smoke_client`
demonstrates exactly this split: it works locally and against the Railway TCP
proxy, and fails with `Stream removed` against the custom domain.

---

## 4. The one thing that will bite you: string decimals

**Money and rates are `string`, not `double`.** This is not a quirk to work
around — it is the reason the numbers are right.

sensen computes in `BigDecimal`: an `__int128` scaled by 1e18, exact to
eighteen decimal places. Two consequences:

- Rounding to `double` compounds. Over a 360-period amortization the schedule
  stops closing. On the live service, `start - principal - end` is exactly
  `0E-18` on every one of the 360 rows; that is only achievable if the value
  never passes through a float.
- **In the browser, `double` would be lossy anyway.** JavaScript's `number` *is*
  an IEEE-754 float64. A `double` money field is degraded on the client before
  you write a line of code.

So: **do not `parseFloat()` these.** Use a decimal library:

```ts
import Decimal from 'decimal.js';
const payment = new Decimal(res.getValue());     // exact
const yearly  = payment.times(12);
```

Fields that ARE `double` are the ones sensen itself computes in double — bonds,
T-bills, futures, options, portfolio statistics. Those are safe to use as
numbers. The rule is simple: **`string` means exact decimal, `double` means the
engine's own precision.**

---

## 5. Inputs are validated, not coerced

The service refuses rather than guesses. Expect `INVALID_ARGUMENT` for:

- **A malformed decimal.** `"12x3"` is rejected (`backend/src/modules/finance_service.cpp:69-102`).
  (`BigDecimal`'s own parser skips non-digits and would read it as `123`; the service validates first.)
- **An unstated compounding frequency** on `ComputeFutureValueDetailed` (`backend/src/modules/finance_service.cpp:1270-1273`).
  It changes the answer materially, so no default is invented.
- **A bond with neither `yield` nor `price`.** Supply exactly one; the other is
  derived (`backend/src/modules/finance_service.cpp:2199-2201`). Supplying both would let them disagree.
- **A ragged batch.** Every repeated field in `ComputeAmortizationBatch` must be
  the same length (`backend/src/modules/finance_service.cpp:1556-1570`); truncating to the shortest would return a short list that
  looks complete.
- **A term over 1200 months.** One row is allocated per period, so an
  unbounded term is a denial of service dressed as a mortgage (`backend/src/modules/finance_service.cpp:1111-1114`).
- **A compound-growth rate/period exponent overflow.** Combinations where $(1 + r)^n$ would exceed
  40 natural log units ($\approx 2.35 \times 10^{17}$-fold growth) are rejected by `check_compound_growth_safe`
  (`backend/src/modules/finance_service.cpp:568-583`) and `check_compound_growth_safe_periods` (`:586-615`),
  preventing silent `__int128` overflow wrapping.
- **Opposite-sign TVM failure.** In `ComputeRate` and `ComputePeriods`, when $FV = 0$, $PV$ and $PMT$
  must have opposite signs. If both share the same sign, money flows only one way and no real solution exists;
  refused by `check_tvm_solvable` (`backend/src/modules/finance_service.cpp:4326-4339`).
- **Unsupported MACRS class.** Real-property MACRS classes (27.5-year residential and 39-year nonresidential)
  are refused by `sensen::macrs_supports` (`backend/src/modules/finance_service.cpp:2147-2156`) because real
  property requires the mid-month convention placing the property in service during a specific month,
  which `DepreciationRequest` does not carry.

Expect `FAILED_PRECONDITION` for:
- **Engine solver convergence failures.** Template helper `fail(r)` (`backend/src/modules/finance_service.cpp:216-219`)
  maps `std::unexpected<std::string>` engine calculation errors into `grpc::StatusCode::FAILED_PRECONDITION`.
  This applies to Newton-Raphson non-convergence (`ComputeRate`, `ComputePeriods`, `ComputeIrr`, `ComputeXirr`),
  a mortgage payment that does not cover periodic interest (`ComputePayoffTiming`, `backend/src/modules/finance_service.cpp:1852`),
  or unresolvable cash flows (`ComputeHomeNpv`, `backend/src/modules/finance_service.cpp:3294`).

Where a figure is genuinely not computable it is marked, not zeroed:
`ComputeHedge` returns `contracts_computed = false` (`backend/proto/finance.proto:605`) when no position size was
given, and `ComputePortfolioStats` returns `benchmark_supplied = false` (`backend/proto/finance.proto:994`) so you
can tell an absent beta from a measured beta of zero.

---

## 6. What is available

Roughly fifty functions. — *Correction: The service declares exactly 46 RPCs (`backend/proto/finance.proto:40-104`). The inventory below previously omitted eight home-finance and real estate methods (`ComputeRefinance`, `ComputePayoffTiming`, `ComputeMortgageRecast`, `ComputeHomeFutureValue`, `ComputeRentVsBuy`, `ComputeRentVsBuyBatch`, `ComputeHomeNpv`, `ComputeClosingCosts`) that were added to the contract; they are restored in place below.* See `backend/proto/finance.proto` for the full list.

| Area | RPCs |
| --- | --- |
| Time value of money | `ComputePayment` `ComputePresentValue` `ComputeFutureValue` `ComputeFutureValueDetailed` `ComputeInterestPayment` `ComputePrincipalPayment` `ComputeRate` `ComputePeriods` `ConvertInterestRate` `ComputeFisherRate` |
| Mortgages, HELOC, Refinance | `ComputeAmortization` `ComputeDetailedAmortization` (tax deductions) `ComputeAmortizationBatch` `ComputeHeloc` `ComputeRefinance` `ComputePayoffTiming` `ComputeMortgageRecast` |
| Cash flow | `ComputeNpv` `ComputeIrr` `ComputeXnpv` `ComputeXirr` `ComputePaybackPeriod` `ComputeCumulative` |
| Depreciation | `ComputeDepreciation` (SLN, SYD, DDB, MACRS) |
| Fixed income | `AnalyzeBond` (price, yield, duration, convexity) `AnalyzeTreasuryBill` (price + BEY/MMY/BDY) |
| Futures | `PriceFutures` `ValueFutures` `SimulateMarginAccount` `ComputeHedge` `ComputeCommoditySpread` |
| Real estate | `ComputeRentalRoi` `ComputeHomeFutureValue` `ComputeRentVsBuy` `ComputeRentVsBuyBatch` `ComputeHomeNpv` `ComputeClosingCosts` |
| State assumptions | `GetStateAssumptions` (open) `RefreshStateAssumptions` (**partner only — the one write on this service**) |
| Options | `PriceOptionTree` (American/Bermudan/Asian) `PriceBlackScholes` (11 Greeks) `PriceOptionMonteCarlo` `ComputeProbabilityTree` |
| Portfolio | `ComputePortfolioStats` `OptimizePortfolio` `ComputeRiskContributions` |

### The one write, and why it is gated differently

Everything else on this service computes a number from its arguments and
returns it. `RefreshStateAssumptions` overwrites fifty rows that fifty live
pages render, from a third-party feed, so it requires a **partner** credential
and `GetStateAssumptions` requires none (`backend/src/modules/finance_service.cpp:3080-3085`).

Partner rather than "authenticated" or "pro", and the distinction is
load-bearing rather than fussy. `data_year` lets a caller pin an ACS vintage,
and every bound the validator enforces is a PLAUSIBILITY bound that a decade-old
vintage satisfies — so pinning 2015 rewrites all fifty states with figures
nothing downstream can distinguish from current ones. The admin trigger is a
server-side call from an operator; a Pro subscriber is a customer of the
calculator, not an operator of it.

**It does not honour `PRO_GATE_MODE`.** Every other gate here is commercial
policy that Off/Warn may switch off. This one is an integrity control, and
honouring Off would turn a billing switch into a data-integrity switch.

A refusal from the job itself — an unusable ACS vintage, too few valid states —
arrives as `OK` with `ok: false` and a sentence in `error`, not as a transport
error (`backend/src/modules/finance_service.cpp:3105-3108`). A caller must be able to tell "the site is serving last week's numbers"
apart from "the RPC did not happen", and a status code collapses those. See
[State assumptions handoff](STATE_ASSUMPTIONS_HANDOFF.md).

---

### Extended home finance and real estate RPCs

Eight methods in `sensen.finance.Finance` provide mortgage refinancing, acceleration timing, recasting, real estate forecasting, rent-vs-buy comparison, and itemized closing cost analysis.

#### `ComputeRefinance`

Computes the monthly payment, initial savings, PMI drop-off schedules, date shifts, break-even horizons, and lifetime savings of refinancing an existing mortgage into a replacement loan (`backend/proto/finance.proto:58, 337-383`, `backend/src/modules/finance_service.cpp:1700-1809`).

Priced in quota via `quota::cost_amortization(refinance_charge_months(...))` (`:1710-1713`), walking $\max(\text{current\_remaining\_months}, \text{new\_term\_years} \times \text{ppy})$ periods.

**Request fields (`RefinanceRequest`):**

| Field | Wire Type | Units / Convention | Restriction / Default | Enforcing Line |
| --- | --- | --- | --- | --- |
| `current_loan_balance` | `string` | Dollars | Required decimal | `finance_service.cpp:1737` |
| `current_monthly_payment` | `string` | Dollars | Required decimal (**P&I only**, exclude PMI) | `finance_service.cpp:1738` |
| `current_annual_rate` | `string` | Decimal fraction (e.g. "0.065" for 6.5%) | Required decimal; rate/ppy $> -1.0$ | `finance_service.cpp:1739, 1764` |
| `current_remaining_months` | `int32` | Months | Required; $0 < n \le 1200$ | `finance_service.cpp:1714` |
| `property_value` | `string` | Dollars | Required decimal | `finance_service.cpp:1740` |
| `new_annual_rate` | `string` | Decimal fraction (e.g. "0.0525") | Required decimal; compound growth safe | `finance_service.cpp:1741, 1769` |
| `new_term_years` | `int32` | Years | Required; $0 < n \le 100$ | `finance_service.cpp:1719` |
| `closing_costs` | `string` | Dollars | Required decimal | `finance_service.cpp:1742` |
| `closing_cost_type` | `enum` | `PAID_IN_CASH = 0`, `ROLLED_INTO_LOAN = 1` | Default 0 (`PAID_IN_CASH`) | `finance_service.cpp:1726-1735` |
| `cash_out_amount` | `string` | Dollars | Optional; default "0" | `finance_service.cpp:1753` |
| `current_pmi_monthly` | `string` | Dollars | Optional; default "0" | `finance_service.cpp:1754` |
| `new_pmi_monthly` | `string` | Dollars | Optional; default "0" | `finance_service.cpp:1755` |
| `pmi_drop_off_ltv` | `string` | Ratio (e.g. "0.80" for 80%) | Optional; default "0" | `finance_service.cpp:1756` |
| `payments_per_year` | `int32` | Frequency | Required; $0 < n \le 366$ (12 = monthly) | `finance_service.cpp:1723, 4342` |

**Response fields (`RefinanceResponse`):**

| Field | Wire Type | Description |
| --- | --- | --- |
| `new_loan_amount` | `string` | Total new loan principal balance (includes rolled-in closing costs and cash out). |
| `new_monthly_payment` | `string` | New monthly payment (P&I only). |
| `monthly_savings_initial` | `string` | Initial monthly savings: `(current_P&I + current_PMI) - (new_P&I + new_PMI)`. |
| `current_loan_pmi_drop_off_months` | `int32` | Months until current loan balance hits PMI threshold (0 = no PMI / already below; -1 = never). |
| `new_loan_pmi_drop_off_months` | `int32` | Months until new loan balance hits PMI threshold (0 = no PMI / already below; -1 = never). |
| `payoff_date_shift_months` | `int32` | Net change in loan maturity date (`new_term_months - current_remaining_months`). |
| `simple_break_even_months` | `int32` | Simple break-even: `closing_costs / initial_monthly_savings` (-1 if no savings). |
| `cash_flow_break_even_months` | `int32` | Period where cumulative new cash outlays equal cumulative old cash outlays (-1 if never). |
| `equity_adjusted_break_even_months` | `int32` | Period where cumulative outlays adjusted for equity accumulation reach parity (-1 if never). |
| `total_savings_over_life` | `double` | Cumulative lifetime savings across the comparison horizon. Carried as `double` because the engine accumulates savings across a floating-point month-by-month loop (`backend/proto/finance.proto:379-382`). |

**Example call:**

```bash
curl -X POST https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputeRefinance \
  -H "content-type: application/json" \
  -d '{
    "current_loan_balance": "320000.00",
    "current_monthly_payment": "2130.00",
    "current_annual_rate": "0.06875",
    "current_remaining_months": 310,
    "property_value": "450000.00",
    "new_annual_rate": "0.0550",
    "new_term_years": 30,
    "closing_costs": "6500.00",
    "closing_cost_type": "PAID_IN_CASH",
    "payments_per_year": 12
  }'
```

**Refusals and failure modes:**
- `INVALID_ARGUMENT`:
  - `current_remaining_months <= 0 || > 1200` (`backend/src/modules/finance_service.cpp:1714-1718`).
  - `new_term_years <= 0 || > 100` (`:1719-1722`).
  - `payments_per_year <= 0 || > 366` (`:1723`).
  - `closing_cost_type` outside `{PAID_IN_CASH, ROLLED_INTO_LOAN}` (`:1734`).
  - Required decimal string missing, malformed, or out of magnitude (`:1737-1742`).
  - `current_annual_rate / payments_per_year <= -1.0` (`:1764-1768`).
  - `new_annual_rate` compound growth overflow (`:1769-1774`).

**Gotchas:**
- `current_monthly_payment` MUST be P&I only. Passing full PITI (including escrow) will overestimate initial and cumulative savings, returning plausible wrong numbers rather than a validation error.
- Break-even fields return `-1` if break-even never occurs; do not display `-1` directly as a positive month count.

---

#### `ComputePayoffTiming`

Calculates how extra monthly principal payments accelerate loan payoff, reducing the remaining term and saving lifetime interest (`backend/proto/finance.proto:59, 385-398`, `backend/src/modules/finance_service.cpp:1811-1858`).

Priced in quota via `quota::cost_default()` (`:1816`).

**Request fields (`PayoffTimingRequest`):**

| Field | Wire Type | Units / Convention | Restriction / Default | Enforcing Line |
| --- | --- | --- | --- | --- |
| `current_loan_balance` | `string` | Dollars | Required decimal | `finance_service.cpp:1818` |
| `annual_rate` | `string` | Decimal fraction (e.g. "0.06") | Required decimal; rate/ppy $> -1.0$ | `finance_service.cpp:1819, 1837` |
| `current_monthly_payment` | `string` | Dollars | Required decimal (**P&I only**) | `finance_service.cpp:1820` |
| `extra_monthly_payment` | `string` | Dollars | Optional; default "0"; cannot be negative | `finance_service.cpp:1824-1828` |
| `payments_per_year` | `int32` | Frequency | Required; $0 < n \le 366$ (12 = monthly) | `finance_service.cpp:1817, 4342` |

**Response fields (`PayoffTimingResponse`):**

| Field | Wire Type | Description |
| --- | --- | --- |
| `original_months_remaining` | `int32` | Months remaining without extra payment. |
| `new_months_remaining` | `int32` | Accelerated months remaining with extra payment. |
| `months_saved` | `int32` | Total term reduction in months (`original_months_remaining - new_months_remaining`). |
| `total_interest_saved` | `string` | Exact 18-decimal reduction in cumulative interest paid over the life of the loan. |

**Example call:**

```bash
curl -X POST https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputePayoffTiming \
  -H "content-type: application/json" \
  -d '{
    "current_loan_balance": "285000.00",
    "annual_rate": "0.0625",
    "current_monthly_payment": "1800.00",
    "extra_monthly_payment": "300.00",
    "payments_per_year": 12
  }'
```

**Refusals and failure modes:**
- `INVALID_ARGUMENT`:
  - `payments_per_year <= 0 || > 366` (`backend/src/modules/finance_service.cpp:1817`).
  - Required decimal missing, malformed, or exceeds magnitude (`:1818-1820`).
  - `extra_monthly_payment < 0`: `"extra_monthly_payment cannot be negative"` (`:1825-1828`).
  - `annual_rate / payments_per_year <= -1.0` (`:1837-1841`).
- `FAILED_PRECONDITION`:
  - If `current_monthly_payment` does not cover one period's interest ($\text{payment} \le \text{balance} \times \text{rate}/\text{ppy}$), the loan cannot amortize. The engine refuses via `fail(r)` with `"payment does not cover interest; loan will never be paid off"` (`:1850-1852`).

---

#### `ComputeMortgageRecast`

Calculates the reduced monthly payment resulting from applying a lump-sum principal paydown while keeping the remaining term and interest rate unchanged (`backend/proto/finance.proto:60, 400-412`, `backend/src/modules/finance_service.cpp:1860-1900`).

Priced in quota via `quota::cost_default()` (`:1865`).

**Request fields (`MortgageRecastRequest`):**

| Field | Wire Type | Units / Convention | Restriction / Default | Enforcing Line |
| --- | --- | --- | --- | --- |
| `current_loan_balance` | `string` | Dollars | Required decimal | `finance_service.cpp:1872` |
| `current_monthly_payment` | `string` | Dollars | Required decimal (**P&I only**) | `finance_service.cpp:1873` |
| `lump_sum_payment` | `string` | Dollars | Optional; default "0"; cannot be negative | `finance_service.cpp:1877-1883` |
| `annual_rate` | `string` | Decimal fraction (e.g. "0.05875") | Required decimal; compound growth safe | `finance_service.cpp:1878, 1888` |
| `remaining_months` | `int32` | Months | Required; $0 < n \le 1200$ | `finance_service.cpp:1866-1870` |
| `payments_per_year` | `int32` | Frequency | Required; $0 < n \le 366$ (12 = monthly) | `finance_service.cpp:1871, 4342` |

**Response fields (`MortgageRecastResponse`):**

| Field | Wire Type | Description |
| --- | --- | --- |
| `new_monthly_payment` | `string` | Lower re-amortized monthly payment (P&I only). |
| `monthly_savings` | `string` | Monthly cash flow reduction (`current_monthly_payment - new_monthly_payment`). |

**Example call:**

```bash
curl -X POST https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputeMortgageRecast \
  -H "content-type: application/json" \
  -d '{
    "current_loan_balance": "350000.00",
    "current_monthly_payment": "2200.00",
    "lump_sum_payment": "50000.00",
    "annual_rate": "0.065",
    "remaining_months": 280,
    "payments_per_year": 12
  }'
```

**Refusals and failure modes:**
- `INVALID_ARGUMENT`:
  - `remaining_months <= 0 || > 1200` (`backend/src/modules/finance_service.cpp:1866-1870`).
  - `payments_per_year <= 0 || > 366` (`:1871`).
  - Required decimal missing or malformed (`:1872-1878`).
  - `lump_sum_payment < 0`: `"lump_sum_payment cannot be negative; a recast only pays the balance down, and the engine would otherwise happily grow it"` (`:1879-1883`).
  - `annual_rate` compound growth overflow (`:1888-1892`).

---

#### `ComputeHomeFutureValue`

Projects home appreciation, remaining mortgage balance, and owner equity over a target forecast horizon (`backend/proto/finance.proto:86, 647-666`, `backend/src/modules/finance_service.cpp:2571-2618`).

Priced in quota via `quota::cost_default()` (`:2578`).

**Request fields (`HomeFutureValueRequest`):**

| Field | Wire Type | Units / Convention | Restriction / Default | Enforcing Line |
| --- | --- | --- | --- | --- |
| `current_property_value` | `string` | Dollars | Required decimal | `finance_service.cpp:2584` |
| `annual_appreciation_rate` | `string` | Decimal fraction (e.g. "0.04" for 4%) | Required decimal | `finance_service.cpp:2586` |
| `current_loan_balance` | `string` | Dollars | Required decimal | `finance_service.cpp:2588` |
| `annual_mortgage_rate` | `string` | Decimal fraction (e.g. "0.0625") | Required decimal; compound growth safe | `finance_service.cpp:2589, 2597` |
| `current_monthly_payment` | `string` | Dollars | Required decimal (**P&I only**) | `finance_service.cpp:2591` |
| `target_years` | `int32` | Years | Required; $0 < n \le 100$ | `finance_service.cpp:2579-2582` |
| `payments_per_year` | `int32` | Frequency | Required; $0 < n \le 366$ (12 = monthly) | `finance_service.cpp:2583, 4342` |

**Response fields (`HomeFutureValueResponse`):**

| Field | Wire Type | Description |
| --- | --- | --- |
| `future_property_value` | `double` | Projected market value at target year. **Deliberately `double`** (`:2607-2609`) because compound appreciation is calculated using floating-point `std::pow`. |
| `future_loan_balance` | `string` | Remaining mortgage principal at target year (clamped to "0" if loan is retired). |
| `future_equity` | `string` | Owner equity at target year (`future_property_value - future_loan_balance`). |

**Example call:**

```bash
curl -X POST https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputeHomeFutureValue \
  -H "content-type: application/json" \
  -d '{
    "current_property_value": "500000.00",
    "annual_appreciation_rate": "0.035",
    "current_loan_balance": "380000.00",
    "annual_mortgage_rate": "0.06",
    "current_monthly_payment": "2398.20",
    "target_years": 10,
    "payments_per_year": 12
  }'
```

**Refusals and failure modes:**
- `INVALID_ARGUMENT`:
  - `target_years <= 0 || > 100` (`backend/src/modules/finance_service.cpp:2579-2582`).
  - `payments_per_year <= 0 || > 366` (`:2583`).
  - Required decimal missing or malformed (`:2584-2592`).
  - `annual_mortgage_rate` compound growth overflow (`:2597-2602`).

---

#### `ComputeRentVsBuy`

Compares the net financial cost of buying a home versus renting over a defined holding horizon (`backend/proto/finance.proto:87, 668-765`, `backend/src/modules/finance_service.cpp:2620-2945`).

Priced in quota via `quota::cost_default()` (`:2943`).

##### Two Mutually Exclusive Request Shapes

`RentVsBuyRequest` supports two distinct models in a single protobuf message. Callers must supply **exactly one** shape:

1. **Legacy Composite Shape (`monthly_piti_and_maintenance`)**:
   - Carries the single all-in ownership cost `monthly_piti_and_maintenance` (debt service + escrow + maintenance combined).
   - Evaluates via the legacy floating-point model (`sensen::calculate_rent_vs_buy(...)`, `:2825`).
   - Populates only double fields 1–4 (`total_cost_of_buying`, `total_cost_of_renting`, `is_buying_better`, `buying_advantage`). String companion fields 5–18 remain empty because the legacy model computes in `double` and converting to string would invent non-existent precision (`backend/proto/finance.proto:741-744`).
2. **Granular Amortising Shape (Seven Debt and Carrying Fields)**:
   - Carries `loan_annual_rate`, `loan_term_years`, `loan_amount`, `monthly_taxes_ins_maintenance`, `closing_costs_buy`, `selling_cost_percent`, and `annual_inflation_rate`.
   - Separates amortizing debt service from non-debt carrying costs, tracks equity accumulation month by month, and calculates terminal net wealth for both buyer and renter.
   - Populates double fields 1–4, exact 18-decimal string companion fields 5–15, and inflation-deflated real wealth fields 16–18.

##### How the Service Decides the Shape

The decision is implemented by `decide_rent_vs_buy_shape` (`backend/src/modules/finance_service.cpp:535-566`), which evaluates a total $5 \times 5$ decision matrix over the classification signals of both shapes:

- `classify_decimal_field` (`:454-486`) inspects the composite string (`monthly_piti_and_maintenance`), returning `FieldSignal::Absent` (empty), `Zero` ("0", "0.00"), `Negative`, `Positive`, or `Malformed`.
- `join_group_signal` (`:506-528`) collapses the seven amortising fields into a single `FieldSignal` (`Absent`, `Zero`, `Positive`, or `Malformed`).
- `decide_rent_vs_buy_shape` evaluates the pair:
  1. If either signal is `Malformed` $\to$ `RefuseMalformed` (`:539-541`).
  2. If composite is `Negative` $\to$ `RefuseNegativeCost` (`:545-547`).
  3. If both composite and granular are `Positive` $\to$ `RefuseBothShapes` (`:553-555`, returns `INVALID_ARGUMENT`).
  4. If only composite is `Positive` (granular is `Absent` or `Zero`) $\to$ `RentVsBuyShape::Legacy` (`:556-558`).
  5. If only granular is `Positive` (composite is `Absent` or `Zero`) $\to$ `RentVsBuyShape::Amortising` (`:559-561`).
  6. If both are `Absent` or `Zero` $\to$ `RentVsBuyShape::RefuseNeitherShape` (`:565`, returns `INVALID_ARGUMENT`).

##### What Happens When Neither Shape Is Stated

When a request supplies neither shape (both composite and granular fields are absent, empty, or zero), the service returns `grpc::StatusCode::INVALID_ARGUMENT` (`:2808-2811`):
```
"supply either monthly_piti_and_maintenance (the legacy composite) or the amortising inputs (loan_annual_rate, loan_term_years, monthly_taxes_ins_maintenance); the request carries neither"
```
The service refuses rather than inventing a loan. Treating absent granular fields as zero would construct a 30-year 0% loan with zero carrying costs and return 200 OK with fabricated numbers.

**Request fields (`RentVsBuyRequest`):**

| Field | Wire Type | Units / Convention | Restriction / Default | Enforcing Line |
| --- | --- | --- | --- | --- |
| `property_price` | `string` | Dollars | Required decimal | `finance_service.cpp:2644` |
| `down_payment` | `string` | Dollars | Required decimal | `finance_service.cpp:2645` |
| `annual_home_appreciation` | `string` | Decimal fraction (e.g. "0.035") | Required decimal; compound growth safe | `finance_service.cpp:2646, 2731` |
| `current_monthly_rent` | `string` | Dollars | Required decimal | `finance_service.cpp:2648` |
| `annual_rent_increase` | `string` | Decimal fraction (e.g. "0.03") | Required decimal; compound growth safe | `finance_service.cpp:2649, 2738` |
| `annual_investment_return` | `string` | Decimal fraction (e.g. "0.07") | Required decimal; compound growth safe | `finance_service.cpp:2651, 2745` |
| `years` | `int32` | Years | Required; $0 < n \le 100$ | `finance_service.cpp:2640-2643` |
| **Shape A (Legacy):** | | | | |
| `monthly_piti_and_maintenance` | `string` | Dollars | Required for Legacy; mutually exclusive with Shape B | `finance_service.cpp:2701-2708, 2790-2799` |
| **Shape B (Amortising):** | | | | |
| `loan_annual_rate` | `string` | Decimal fraction | Optional; default "0"; compound growth safe | `finance_service.cpp:2841, 2877` |
| `loan_term_years` | `int32` | Years | Optional; omitted (0) defaults to **30 years**; $0 \le n \le 100$ | `finance_service.cpp:2866-2873` |
| `loan_amount` | `string` | Dollars | Optional; omitted defaults to `price - down` | `finance_service.cpp:2842, 2894` |
| `monthly_taxes_ins_maintenance` | `string` | Dollars (non-debt carrying) | Optional; default "0" | `finance_service.cpp:2843` |
| `closing_costs_buy` | `string` | Dollars | Optional; default "0" | `finance_service.cpp:2845` |
| `selling_cost_percent` | `string` | Decimal fraction | Optional; omitted defaults to **0.0**, NOT 0.06 | `finance_service.cpp:2859-2864` |
| `annual_inflation_rate` | `string` | Decimal fraction | Optional; default "0"; compound growth safe | `finance_service.cpp:2846, 2752` |

**Response fields (`RentVsBuyResponse`):**

| Field | Wire Type | Description |
| --- | --- | --- |
| `total_cost_of_buying` | `double` | Cumulative nominal cost of buying (always populated). |
| `total_cost_of_renting` | `double` | Cumulative nominal cost of renting (always populated). |
| `is_buying_better` | `bool` | True if buying is financially advantageous. |
| `buying_advantage` | `double` | Net financial advantage of buying (`renting_cost - buying_cost`). |
| `total_cost_of_buying_exact` | `string` | Exact 18-decimal total cost of buying (amortising shape only). |
| `total_cost_of_renting_exact` | `string` | Exact 18-decimal total cost of renting (amortising shape only). |
| `buying_advantage_exact` | `string` | Exact 18-decimal buying advantage (amortising shape only). |
| `owner_terminal_wealth` | `string` | Net owner wealth at horizon: `home_sale_price - debt - selling_costs`. |
| `renter_terminal_wealth` | `string` | Compound investment balance of down payment + monthly savings. |
| `final_loan_balance` | `string` | Outstanding mortgage balance at end of horizon. |
| `home_sale_price` | `string` | Future property sale price at horizon. |
| `selling_costs` | `string` | Transaction costs incurred at sale. |
| `total_principal_paid` | `string` | Cumulative mortgage principal paid over horizon. |
| `total_interest_paid` | `string` | Cumulative mortgage interest paid over horizon. |
| `total_rent_paid` | `string` | Cumulative rent paid over horizon. |
| `real_buying_advantage` | `string` | Inflation-deflated real buying advantage. |
| `real_owner_terminal_wealth` | `string` | Inflation-deflated real owner terminal wealth. |
| `real_renter_terminal_wealth` | `string` | Inflation-deflated real renter terminal wealth. |

**Example call (Amortising Shape):**

```bash
curl -X POST https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputeRentVsBuy \
  -H "content-type: application/json" \
  -d '{
    "property_price": "550000.00",
    "down_payment": "110000.00",
    "annual_home_appreciation": "0.035",
    "current_monthly_rent": "2800.00",
    "annual_rent_increase": "0.03",
    "annual_investment_return": "0.065",
    "years": 7,
    "loan_annual_rate": "0.0625",
    "loan_term_years": 30,
    "monthly_taxes_ins_maintenance": "950.00",
    "closing_costs_buy": "8500.00",
    "selling_cost_percent": "0.06",
    "annual_inflation_rate": "0.025"
  }'
```

**Refusals and failure modes:**
- `INVALID_ARGUMENT`:
  - `years <= 0 || > 100` (`backend/src/modules/finance_service.cpp:2640-2643`).
  - Required decimal missing or malformed (`:2644-2652`).
  - Compound growth overflow on any of appreciation, rent increase, investment return, inflation rate, or loan rate (`:2728-2757, 2877-2882`).
  - `monthly_piti_and_maintenance < 0` (`:2790-2792`).
  - Both shapes present (`:2793-2799`).
  - Neither shape present (`:2800-2811`).
  - `loan_term_years < 0 || > 100` (`:2870-2873`).
  - Underlying engine calculation failure (`:2901-2905`).

---

#### `ComputeRentVsBuyBatch`

Executes high-throughput sweeps of up to 1000 rent-vs-buy scenarios in a single network round trip (`backend/proto/finance.proto:88, 768-819`, `backend/src/modules/finance_service.cpp:2947-3058`).

##### Quota Charging and Bounds

- **Charged per scenario:** `CHARGE("ComputeRentVsBuyBatch", quota::cost_default() * static_cast<double>(n))` (`:2978`). A batch of 500 costs exactly 500 calls. The batch avoids ingress rate limiting (Envoy's 10 req/sec ceiling) and network round trips, but offers no quota discount.
- **Upper bound:** `kMaxScenarios = 1000` (`backend/src/modules/finance_service.cpp:2961`). Batches with $n > 1000$ are refused with `INVALID_ARGUMENT`:
  ```
  "scenarios has N entries; at most 1000 may be sent in one batch. Split the work rather than expecting a truncated answer."
  ```
  The service refuses rather than silently truncating.

##### Positional Results with Per-Scenario Errors

- The response array `results` matches `scenarios` in length and order: `results[i]` corresponds strictly to `scenarios[i]`.
- Each element is a `RentVsBuyBatchResult` containing a `oneof outcome`:
  - On success: `result` (`RentVsBuyResponse`) is set (`:3019`).
  - On error: `error` (string containing the exact refusal diagnostic) is set (`:3021`).
- A malformed scenario does **not** fail the entire batch. The other scenarios compute normally.

##### Parallel Execution Threshold

- Small batches are executed serially to eliminate task scheduling overhead: `kParallelThreshold = 64` (`backend/src/modules/finance_service.cpp:3042`).
- Batches with $n < 64$ run sequentially (`:3043-3048`).
- Batches with $n \ge 64$ execute across worker threads using `sensen::parallel::parallel_for` (`:3050-3056`). Response slots are pre-allocated serially before dispatch (`:2991-2994`) to eliminate thread data races.

**Request fields (`RentVsBuyBatchRequest`):**

| Field | Wire Type | Description |
| --- | --- | --- |
| `scenarios` | `repeated RentVsBuyRequest` | Array of up to 1000 individual scenarios. |

**Response fields (`RentVsBuyBatchResponse`):**

| Field | Wire Type | Description |
| --- | --- | --- |
| `results` | `repeated RentVsBuyBatchResult` | Positional outcomes. Each entry contains either `result` or `error`. |

**Example call:**

```bash
curl -X POST https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputeRentVsBuyBatch \
  -H "content-type: application/json" \
  -d '{
    "scenarios": [
      {
        "property_price": "500000.00", "down_payment": "100000.00",
        "annual_home_appreciation": "0.03", "current_monthly_rent": "2500.00",
        "annual_rent_increase": "0.03", "annual_investment_return": "0.06",
        "years": 5, "loan_annual_rate": "0.06", "loan_term_years": 30,
        "monthly_taxes_ins_maintenance": "800.00"
      },
      {
        "property_price": "invalid", "down_payment": "100000.00",
        "annual_home_appreciation": "0.03", "current_monthly_rent": "2500.00",
        "annual_rent_increase": "0.03", "annual_investment_return": "0.06",
        "years": 5, "monthly_piti_and_maintenance": "2800.00"
      }
    ]
  }'
```

Response showing per-scenario error isolation:

```json
{
  "results": [
    {
      "result": {
        "totalCostOfBuying": 218400.0,
        "totalCostOfRenting": 161800.0,
        "isBuyingBetter": false,
        "buyingAdvantage": -56600.0,
        "totalCostOfBuyingExact": "218400.000000000000000000",
        "buyingAdvantageExact": "-56600.000000000000000000"
      }
    },
    {
      "error": "property_price is not a decimal number: \"invalid\""
    }
  ]
}
```

---

#### `ComputeHomeNpv`

Computes the Net Present Value (NPV) and Internal Rate of Return (IRR) of homeownership by modeling month-by-month cash flows against displaced rent over a holding horizon (`backend/proto/finance.proto:89, 821-856`, `backend/src/modules/finance_service.cpp:3206-3314`).

Priced in quota via `quota::cost_cash_flow(home_npv_charge_months(request->holding_period_years()))` (`:3219-3220`).

**Request fields (`HomeNpvRequest`):**

| Field | Wire Type | Units / Convention | Restriction / Default | Enforcing Line |
| --- | --- | --- | --- | --- |
| `property_price` | `string` | Dollars | Required decimal | `finance_service.cpp:3229` |
| `down_payment` | `string` | Dollars | Required decimal | `finance_service.cpp:3230` |
| `closing_costs_buy` | `string` | Dollars | Required decimal | `finance_service.cpp:3231` |
| `loan_amount` | `string` | Dollars | Required decimal | `finance_service.cpp:3232` |
| `loan_annual_rate` | `string` | Decimal fraction (e.g. "0.065") | Required decimal; compound growth safe | `finance_service.cpp:3233, 3259` |
| `loan_term_years` | `int32` | Years | Required; $0 < n \le 100$ | `finance_service.cpp:3225-3228` |
| `monthly_taxes_ins_hoa` | `string` | Dollars per month | Required decimal | `finance_service.cpp:3234` |
| `monthly_maintenance` | `string` | Dollars per month | Required decimal | `finance_service.cpp:3235` |
| `annual_appreciation_rate` | `string` | Decimal fraction (e.g. "0.03") | Required decimal | `finance_service.cpp:3236` |
| `selling_closing_cost_percent` | `string` | Decimal fraction (e.g. "0.06") | Required decimal; must be $< 1.0$ | `finance_service.cpp:3238, 3251` |
| `monthly_rent_saved` | `string` | Dollars per month (imputed rent) | Required decimal | `finance_service.cpp:3240` |
| `annual_rent_increase` | `string` | Decimal fraction (e.g. "0.03") | Required decimal | `finance_service.cpp:3241` |
| `annual_discount_rate` | `string` | Nominal discount rate decimal | Required decimal | `finance_service.cpp:3243` |
| `holding_period_years` | `int32` | Years | Required; $0 < n \le 100$ | `finance_service.cpp:3221-3224` |
| `annual_inflation_rate` | `string` | Decimal fraction | Optional; default "0"; must be $> -1.0$ | `finance_service.cpp:3244, 3247` |

**Response fields (`HomeNpvResponse`):**

All fields are **`double`** (`:3297-3313`, `backend/proto/finance.proto:842-845`) because the underlying primitives compute in double via XNPV/XIRR numerical solvers:

| Field | Wire Type | Description |
| --- | --- | --- |
| `net_present_value` | `double` | Nominal net present value of ownership relative to renting. |
| `internal_rate_of_return` | `double` | Nominal annualized internal rate of return. |
| `future_sale_price` | `double` | Projected property sale price at end of holding period. |
| `future_equity` | `double` | Net equity realized at sale after retiring remaining debt and selling costs. |
| `real_internal_rate_of_return` | `double` | Real IRR adjusted for inflation via the Fisher equation. |
| `real_future_sale_price` | `double` | Inflation-deflated future sale price in time-0 dollars. |
| `real_future_equity` | `double` | Inflation-deflated net equity in time-0 dollars. |

*Note on Real NPV:* No `real_net_present_value` field is provided. A nominal NPV discounted at a nominal discount rate is already expressed in time-0 dollars; an inflation-adjusted real cash flow stream discounted at a Fisher real rate yields an identical number term-by-term (`backend/src/modules/finance_service.cpp:3300-3305`).

**Example call:**

```bash
curl -X POST https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputeHomeNpv \
  -H "content-type: application/json" \
  -d '{
    "property_price": "500000.00",
    "down_payment": "100000.00",
    "closing_costs_buy": "12000.00",
    "loan_amount": "400000.00",
    "loan_annual_rate": "0.065",
    "loan_term_years": 30,
    "monthly_taxes_ins_hoa": "750.00",
    "monthly_maintenance": "400.00",
    "annual_appreciation_rate": "0.03",
    "selling_closing_cost_percent": "0.06",
    "monthly_rent_saved": "2600.00",
    "annual_rent_increase": "0.03",
    "annual_discount_rate": "0.07",
    "holding_period_years": 10,
    "annual_inflation_rate": "0.025"
  }'
```

**Refusals and failure modes:**
- `INVALID_ARGUMENT`:
  - `holding_period_years <= 0 || > 100` (`backend/src/modules/finance_service.cpp:3221-3224`).
  - `loan_term_years <= 0 || > 100` (`:3225-3228`).
  - Required decimal missing or malformed (`:3229-3243`).
  - `annual_inflation_rate <= -1.0` (`:3247-3250`).
  - `selling_closing_cost_percent >= 1.0` (`:3251-3254`).
  - `loan_annual_rate` compound growth overflow (`:3259-3264`).
- `FAILED_PRECONDITION`:
  - Solver non-convergence inside `sensen::calculate_home_npv` returns `FAILED_PRECONDITION` via `fail(r)` (`:3294`).

---

#### `ComputeClosingCosts`

Itemizes upfront buyer closing costs across lender charges, third-party title/appraisal services, government recording/transfer taxes, and escrow prepaids, returning net cash required to close (`backend/proto/finance.proto:90, 1047-1120`, `backend/src/modules/finance_service.cpp:3322-3428`).

Priced in quota via `quota::cost_default()` (`:3328`).

##### Base Mismatch and Credit Mechanics

1. **Percentage bases differ:**
   - Origination and discount points are percentages of the **LOAN amount** (`backend/proto/finance.proto:1054-1055`).
   - Title settlement and transfer tax are percentages of the **PURCHASE PRICE** (`backend/proto/finance.proto:1059, 1065`).
2. **Credits reduce the total, not line items:**
   - `seller_lender_credits` is subtracted from `itemised_subtotal` to determine `total_closing_costs`. Credits are not folded into individual fees, preserving line-item auditability (`backend/src/modules/finance_service.cpp:3420-3422`).
3. **Explicit presence for prepaid interest:**
   - `prepaid_interest_days` is an `optional int32` (`backend/proto/finance.proto:1082`, `backend/src/modules/finance_service.cpp:3387-3389`).
   - **Absent / unset:** triggers the standard **15-day** convention (half-month prepaid interest).
   - **Explicit 0:** computes exactly 0 days (representing a closing on the final day of the month where no prepaid interest is owed).

**Request fields (`ClosingCostsRequest`):**

| Field | Wire Type | Units / Convention | Restriction / Default | Enforcing Line |
| --- | --- | --- | --- | --- |
| `home_price` | `string` | Dollars | Required decimal | `finance_service.cpp:3351` |
| `down_payment_percent` | `string` | Decimal fraction (e.g. "0.20") | Required decimal; loan = price $\times$ (1 - down) | `finance_service.cpp:3352` |
| `annual_rate` | `string` | Decimal fraction (e.g. "0.0675") | Required decimal (for per-diem interest) | `finance_service.cpp:3353` |
| `origination_fee_percent` | `string` | Fraction of **LOAN** (e.g. "0.0075") | Optional; default "0" | `finance_service.cpp:3354` |
| `discount_points_percent` | `string` | Fraction of **LOAN** (e.g. "0.01") | Optional; default "0" | `finance_service.cpp:3356` |
| `other_lender_fees` | `string` | Dollars (flat fee) | Optional; default "0" | `finance_service.cpp:3358` |
| `title_settlement_percent` | `string` | Fraction of **PRICE** (e.g. "0.0055") | Optional; default "0" | `finance_service.cpp:3359` |
| `appraisal_fee` | `string` | Dollars (flat fee) | Optional; default "0" | `finance_service.cpp:3361` |
| `inspection_fee` | `string` | Dollars (flat fee) | Optional; default "0" | `finance_service.cpp:3362` |
| `recording_fees` | `string` | Dollars (flat fee) | Optional; default "0" | `finance_service.cpp:3363` |
| `transfer_tax_percent` | `string` | Fraction of **PRICE** (e.g. "0.005") | Optional; default "0" | `finance_service.cpp:3364` |
| `homeowners_insurance_annual`| `string`| Dollars (12 mo collected) | Optional; default "0" | `finance_service.cpp:3366` |
| `property_tax_annual` | `string` | Dollars per year | Optional; default "0" | `finance_service.cpp:3368` |
| `tax_escrow_months` | `int32` | Months | Optional; default 0; $0 \le n \le 24$ | `finance_service.cpp:3333-3336` |
| `seller_lender_credits` | `string` | Dollars | Optional; default "0"; $\le \text{subtotal}$ | `finance_service.cpp:3369` |
| `prepaid_interest_days` | `optional int32`| Days | Optional; unset = 15; explicit 0 = 0; $0 \le n \le 365$ | `finance_service.cpp:3337-3341, 3387` |

**Response fields (`ClosingCostsResponse`):**

| Field | Wire Type | Description |
| --- | --- | --- |
| `origination_fee` | `string` | Calculated lender origination fee. |
| `discount_points` | `string` | Calculated discount points cost. |
| `other_lender_fees` | `string` | Flat lender fees (underwriting/processing). |
| `title_settlement` | `string` | Title examination and settlement fee. |
| `appraisal_fee` | `string` | Property appraisal cost. |
| `inspection_fee` | `string` | Home inspection cost. |
| `recording_fees` | `string` | Municipal deed/mortgage recording fees. |
| `transfer_tax` | `string` | State/county transfer taxes. |
| `homeowners_insurance_prepaid` | `string`| 12 months prepaid homeowner insurance. |
| `property_tax_escrow` | `string` | Initial tax escrow deposit. |
| `prepaid_interest` | `string` | Per-diem interest through end of closing month. |
| `prepaid_interest_days` | `int32` | Echoed number of interest days applied (e.g. 15 if omitted). |
| `itemised_subtotal` | `string` | Sum of all 11 closing cost items above (before credits). |
| `seller_lender_credits` | `string` | Total credits applied. |
| `total_closing_costs` | `string` | Net buyer closing costs (`itemised_subtotal - seller_lender_credits`). |
| `loan_amount` | `string` | Total mortgage principal borrowed. |
| `down_payment` | `string` | Total down payment paid by buyer. |
| `total_cash_to_close` | `string` | Total liquid cash required (`down_payment + total_closing_costs`). |
| `closing_costs_percent_of_price` | `double`| `total_closing_costs / home_price` as a display ratio. |

**Example call:**

```bash
curl -X POST https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputeClosingCosts \
  -H "content-type: application/json" \
  -d '{
    "home_price": "450000.00",
    "down_payment_percent": "0.10",
    "annual_rate": "0.0675",
    "origination_fee_percent": "0.0075",
    "title_settlement_percent": "0.0055",
    "transfer_tax_percent": "0.0050",
    "tax_escrow_months": 3,
    "property_tax_annual": "5400.00",
    "homeowners_insurance_annual": "1200.00",
    "seller_lender_credits": "2500.00"
  }'
```

**Refusals and failure modes:**
- `INVALID_ARGUMENT`:
  - `tax_escrow_months < 0 || > 24` (`backend/src/modules/finance_service.cpp:3333-3336`).
  - `prepaid_interest_days < 0 || > 365` (`:3337-3341`).
  - Decimal parsing or magnitude error on any field (`:3351-3370`).
  - Engine validation errors via `sensen::validate_closing_costs` (`:3401-3404`):
    - Negative home price, negative interest rate, negative down payment percent, down payment percent $> 1.0$, negative fees, or `seller_lender_credits > itemised_subtotal`.
- `FAILED_PRECONDITION`:
  - Engine calculation failure inside `sensen::calculate_closing_costs` returns `FAILED_PRECONDITION` via `fail(r)` (`:3406`).

---

---

## 7. Checking it yourself

```bash
python3 scripts/probe_finance_service.py https://api.optionsandfuturescalculator.com
```

This is the gate, not a demo. Every case is checked against something derived
independently of the engine — put-call parity, price/yield inversion, schedule
closure, the closed-form annuity formula — because a check that compares the
engine to its own previous output only detects a crash, and this service's
failure mode is a wrong number.

---

## 8. Authentication

**Off unless configured**, like quotas. With `FINANCE_API_KEYS` unset the
service behaves exactly as it did before keys existed.

### Two kinds of key

| | Publishable `pk_live_…` | Secret `sk_live_…` |
| --- | --- | --- |
| Where it goes | Your web page — it is *meant* to be visible | Your server, never a browser |
| Protected by | The origins you register + quota + revocation | Secrecy + quota + revocation |
| If it leaks | Only works from your registered origins in a browser | Full access as you, until revoked |

A publishable key is public by design. That is not a weakness to work around —
it is what embedding means. Its security comes from **binding**, not secrecy:
a browser sets `Origin` on cross-origin requests and cannot be made to lie
about it by the page it is on, so a key lifted from your HTML and pasted into
another site stops working there.

A secret key sent **with** an `Origin` header is treated as leaked: refused
outright and logged loudly, because nothing legitimate produces that
combination.

### Sending it

```ts
client.computePayment(req, { 'x-api-key': 'pk_live_…' }, cb);
```

```python
stub.ComputePayment(req, metadata=[("x-api-key", "sk_live_…")])
```

### What refusal looks like

`UNAUTHENTICATED` (status 16) means *we do not know who you are*:

```
no API key supplied (send it in the `x-api-key` header)
malformed API key
unrecognised API key
this API key has been revoked
this API key expired on 2027-01-01
```

`PERMISSION_DENIED` (status 7) means *we know who you are, and no*:

```
this API key is not registered for use from this site
this API key is not entitled to the 'finance' service
a secret key was presented from a browser; treat it as compromised and rotate
it. Use a publishable key for browser traffic
```

The two are kept distinct deliberately. Collapsing them would send a customer
with a scope problem off to check their key.

### Issuing a key

Keys are stored **hashed**. Generate one, hash it, keep the plaintext only long
enough to hand it over:

```bash
KEY="pk_live_$(head -c 32 /dev/urandom | basenc --base64url | tr -d '=')"
echo "give this to the customer: $KEY"
printf '%s' "$KEY" | openssl dgst -sha512 -hex | awk '{print $2}'
```

SHA-512 rather than SHA-256 for margin, and because on 64-bit hardware it is
also the faster of the two. Deliberately **not** Argon2 or bcrypt: those are
slow on purpose to protect low-entropy *passwords*, whereas these keys carry 256
bits of random entropy, so a work factor buys nothing and would put tens of
milliseconds on every request. Comparison is constant-time.

### Configuring

```bash
FINANCE_API_KEYS='{
  "<sha512 hex of the key>": {
    "id": "acme-risk",
    "type": "publishable",
    "tier": "partner",
    "origins": ["https://acme.example", "https://*.acme.example"],
    "scopes": ["finance"],
    "expires": "2027-01-01",
    "enabled": true
  }
}'
```

`origins` applies only to browser traffic — a server-side caller sends no
`Origin`, so a secret key needs none. `expires` is optional. `enabled: false`
revokes without deleting, which keeps the audit trail intelligible.

The tier here is what quota meters against, so a key is configured **once**.
`QUOTA_API_KEYS` is not needed alongside it, and should not be used — it holds
keys in plaintext, which is the exposure hashing exists to remove.

### Rolling it out

`FINANCE_REQUIRE_KEY` stages the change, because a switch that starts refusing
traffic must not be thrown blind:

| Value | Behaviour |
| --- | --- |
| unset / `0` / `observe` | Everything served. Refusals logged as `would-deny`. |
| `1` / `warn` | Everything served. `would-deny` logged at error level. |
| `2` / `enforce` | Refusals are real. |

Observe first, read the logs, and only then enforce. The log line names the key
by its **label**, never the key itself:

```
auth would-deny: key=<none> method=ComputePayment origin=- outcome=no-key
```

The startup log states the posture, so "is auth on?" is answerable without
sending traffic:

```
API key auth ENABLED: 3 keys, mode ENFORCE
Max request size: 1048576 bytes
```

## 9. Quotas

**Off unless configured.** With no policy set the service behaves exactly as it
did before quotas existed. That is the only safe default for a mechanism that
can otherwise start refusing real traffic.

Two limits run per caller, because they answer different questions:

- **rate** — requests per minute. Catches bursts and runaway retry loops.
- **budget** — compute units per hour. Catches a caller doing genuinely
  expensive work at a perfectly reasonable request rate.

The second exists because a request count is the wrong unit here.
`ComputePayment` is a handful of integer operations; `PriceOptionMonteCarlo` at
a million paths and a thousand steps is ~10⁹ RNG draws. Six orders of magnitude
apart — so a caller comfortably inside a requests-per-minute limit can still
saturate the engine. Cost is priced from each request's own arguments.

### Configuring

Two environment variables on the backend service:

```bash
QUOTA_POLICY='{
  "anonymous_tier": "anonymous",
  "tiers": {
    "anonymous": {"requests_per_minute": 60,   "compute_units_per_hour": 600},
    "free":      {"requests_per_minute": 600,  "compute_units_per_hour": 10000},
    "pro":       {"requests_per_minute": 3000, "compute_units_per_hour": 200000},
    "partner":   {"requests_per_minute": 6000, "compute_units_per_hour": 500000}
  }
}'

QUOTA_API_KEYS='{
  "sk_live_abc123": "partner",
  "sk_live_def456": "free"
}'
```

Zero on either axis means unlimited for that axis. Keys live in their own
variable so the policy can be logged and reviewed without exposing them.

> **The policy must name every tier your callers can present, and the numbers
> above are illustrative — they are NOT the live ones.** Do not paste this
> block over a running `QUOTA_POLICY`; read the current value first. This
> example omitted `pro` until 2026-08-12, and copying it as-is would have
> dropped every Pro caller to the anonymous allowance.
>
> A tier the policy does not define is metered against **anonymous** rather
> than being let through unlimited — deliberately, so an entitlement naming a
> renamed tier cannot become unlimited access. `QUOTA_API_KEYS` is checked
> against the policy at boot and a key naming an unknown tier is rejected
> loudly, but that check cannot cover the tier on a *verified identity*
> (Supabase `app_metadata.tier`, or a signed licence), because those are issued
> outside this service. When one of those names an undefined tier the engine
> logs an error once per distinct name, and the refusal reads
> `pro (undefined in QUOTA_POLICY; anonymous limits)` — the marker says the
> number came from anonymous, not from the tier the caller presented.

The startup log states what loaded, so "are quotas on?" is answerable without
sending traffic:

```
Quotas ENABLED: 3 tiers, 2 keys, unkeyed callers get 'anonymous'
Quota enforcement is ON
```

A policy that fails to parse logs an error and leaves quotas **off** — it never
reads as "no limits configured". A key mapped to a tier that does not exist is
named in the log and treated as anonymous, rather than silently getting limits
its issuer did not intend.

### Calling with a key

```ts
client.computePayment(req, { 'x-api-key': 'sk_live_abc123' }, cb);
```

```python
stub.ComputePayment(req, metadata=[("x-api-key", "sk_live_abc123")])
```

### What being over looks like

`RESOURCE_EXHAUSTED` (gRPC status 8) with a real retry-after computed from the
bucket's own refill rate:

```
quota exceeded for tier 'anonymous' on ComputePayment (request rate); retry in 12s
```

`RESOURCE_EXHAUSTED` rather than `UNAVAILABLE` deliberately: `UNAVAILABLE`
invites a gRPC client library to retry immediately, which is precisely wrong.

A single call priced above a whole hour's allowance is refused outright, with no
retry-after, because waiting cannot help:

```
quota exceeded for tier 'partner' on PriceOptionMonteCarlo (compute budget
(this call alone exceeds the tier's hourly allowance)); this request cannot
succeed at this tier regardless of waiting
```

### Two properties worth knowing

**An unrecognised key is not a free pass, and not an error.** It gets the
anonymous tier AND shares the single anonymous bucket. Bucketing on the raw
header would let a caller send a fresh random `x-api-key` per request and mint a
new allowance every time — which is not a limit at all. This is checked by the
deploy gate.

**Quotas are per instance.** The buckets live in the process. One replica today,
so this is exact; behind N replicas a caller would get up to N times the stated
limit. Moving to a shared store is the change to make before scaling out.

### Monitoring clients

Set `SMOKE_API_KEY` for `smoke_client` so the gate's own dozens of calls run on a
generous tier instead of throttling themselves partway through.

## 10. Operational notes

- **Quotas are not authentication**, and the two are separate on purpose.
  Quota answers "how much may this caller use"; §8 answers "who is this, and
  may they call at all". Keeping them apart is what lets an unrecognised key be
  a refusal without quota having to become an authentication system it was not
  designed to be.
- **There is no stored data to protect.** Every RPC is a pure calculation over
  inputs the caller supplies; none reads from Postgres. Authentication here is
  about *access and cost*, not confidentiality — and this note is the thing to
  revisit the moment an RPC starts returning stored data.
- **Requests are capped at 1 MiB**, below gRPC's 4 MiB default. The cap is at
  the transport layer because the quota guard runs *after* deserialization: by
  the time a call can be priced, its payload is already resident.
- **No streaming.** Every RPC is unary.
- **Shared with the calculator.** `calculator.OptionsCalculator` is on the same
  host and port. The two are independent contracts; a client needs only the
  proto for the one it uses.

---

## 11. Test coverage

The financial calculation service, protobuf contracts, input bounds, quota enforcement, and authentication rules are covered by dedicated test suites and validation harnesses:

| Test Suite / Executable | Source Path | Scope and Test Focus | Key Citations |
| --- | --- | --- | --- |
| `FinanceServiceValidationTest` | `backend/tests/test_finance_service_validation.cpp` | In-process gRPC boundary harness hosting `options_calculator::finance::RegisterFinanceService`. Tests all 46 RPCs for input validation, numerical bounds, magnitude overflows, compound-growth limits, unbounded iterations, and NaN bypass guards. | - Section 23: Closing costs bounds, bases, and optionality (`:1955-2145`)<br>- Section 24: Rent vs buy shape dispatch and signal classification (`:2148-2334`)<br>- Section 25: XNPV/XIRR day scaling (`dates_to_seconds`) (`:2335-2518`)<br>- Section 26: TVM cash flow opposite signs (`check_tvm_solvable`) (`:2519-2635`)<br>- Section 27: MACRS depreciation class whitelist (`:2636-2720`) |
| `smoke_client` / `probe_finance_service.py` | `backend/src/smoke_client.cpp`, `scripts/probe_finance_service.py` | Live integration gate testing deployment health against independent closed-form mathematical identities (annuity formulas, put-call parity, schedule closure). | - `check_finance`: Comprehensive RPC verification (`:771-2800`)<br>- `ComputeRefinance` no-op identity (`:1280-1395`)<br>- `ComputePayoffTiming` (`:1458-1502`)<br>- `ComputeMortgageRecast` (`:1504-1550`)<br>- `ComputeHomeFutureValue` (`:1552-1600`)<br>- `ComputeRentVsBuy` (`:1602-1778`)<br>- `ComputeHomeNpv` (`:1780-1900`)<br>- `ComputeClosingCosts` itemization and credits (`:2400-2650`)<br>- `check_quota`: Rate and budget limits (`:3024-3130`)<br>- `check_auth`: Origin and key type enforcement (`:3132-3243`)<br>- `check_key_limit`: Anonymous pool fallback (`:3245-3328`) |
| `MortgageVerificationTest` | `backend/tests/test_mortgage_verification.cpp` | Offline verifier tests for mortgage calculation parameters, ensuring strict input validation, discrimination between valid and corrupted requests, and drift detection. | - Proto label-space drift check against `backend/proto/finance.proto` (`:34-36`)<br>- Slot-kind totality check (`:37-40`)<br>- Discrimination pairs for `ComputeRefinance`, `ComputeRentVsBuy`, and `ComputeHomeFutureValue` (`:22-31`) |
| `VendoredProtoDriftTest` | `backend/tests/test_vendored_proto_drift.cpp` | Compares contract bodies between `backend/proto/finance.proto` and `clients/mortgagefv/proto/finance.proto`. Ensures vendored client copies do not diverge from the canonical service definition. | - Body comparison logic excluding metadata headers (`:57-80`) |
| `ApiKeyEntitlementTest` | `backend/tests/test_api_key_entitlement.cpp` | Evaluates API key authentication outcomes (`api_key::Identity::outcome`) and verifies distinct status codes for unauthenticated, origin mismatch, expired, or malformed requests. | - Direct invocation of `check_assistant_entitlement` and `check_strategy_entitlement` without network overhead (`:19-28`) |
| `QuotaTierLabelTest` | `backend/tests/test_quota_tier_label.cpp` | Verifies that quota exhaustion errors report the tier whose limits were actually enforced (e.g. anonymous fallback for undefined tiers) rather than the requested tier name. | - Fallback verification and discrimination testing (`:6-30`) |

---

## 12. Open questions

None. Every RPC, message field, default value, validation bound, status code, refusal string, and test target described in this document has been verified against the code in:
- `backend/proto/finance.proto`
- `backend/src/modules/finance_service.cppm` and `backend/src/modules/finance_service.cpp`
- `backend/src/modules/quota.cppm` and `backend/src/modules/quota.cpp`
- `backend/src/modules/api_key.cppm` and `backend/src/modules/api_key.cpp`
- `backend/tests/test_finance_service_validation.cpp`
- `backend/src/smoke_client.cpp`
- `backend/tests/test_mortgage_verification.cpp`
- `backend/tests/test_vendored_proto_drift.cpp`
- `backend/tests/test_api_key_entitlement.cpp`
- `backend/tests/test_quota_tier_label.cpp`

