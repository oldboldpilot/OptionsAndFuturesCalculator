# mortgagefvcalculator.com → sensen.finance.Finance integration spec

Date: 2026-08-05
Status: proposed (analysis complete; no code, config, or deployment changed)

A second, separate website — `https://mortgagefvcalculator.com/` — will consume the
`sensen.finance.Finance` gRPC service already live at
`https://api.optionsandfuturescalculator.com`. This spec states what the site can build
against today, what is blocked on the proto extension, how to give the site its own
quota allowance instead of draining the shared anonymous bucket, what the open CORS
actually exposes, the exact client tooling, the numeric contract, and a runnable
end-to-end verification.

Every claim is labelled **VERIFIED** (file read or live request run during this
analysis) or **INFERRED**. Live production requests in this document were made
2026-08-05 against `api.optionsandfuturescalculator.com`.

**Note on a referenced document:** the prompt for this work referenced
`docs/superpowers/specs/2026-08-05-finance-proto-extension.md` as the spec covering
exposure of the seven new sensen capabilities. **That file does not exist in the repo
as of this writing** (VERIFIED: `find` across the tree). This spec treats it as
forthcoming and does not duplicate its work; §1b states only *which* of the seven this
site needs.

---

## 1. What the site needs — RPC inventory

### 1a. Usable today (exposed in `backend/proto/finance.proto`, VERIFIED)

All request/response message names below are from
`/home/muyiwa/Development/OptionsAndFuturesCalculator/backend/proto/finance.proto`
(VERIFIED). Compute-unit (CU) costs are from
`backend/src/modules/quota.cpp` (VERIFIED).

**Core mortgage/FV calculators — the site's main surface:**

| RPC | Request → Response | Use on the site | CU cost |
| --- | --- | --- | --- |
| `ComputePayment` | `PaymentRequest` → `DecimalResponse` | Monthly payment (PMT). Also exact recast math — see §1c | 1 |
| `ComputeAmortization` | `AmortizationRequest` → `AmortizationResponse` | Full schedule with `monthly_overpayment`, PMI rate, PMI drop-off vs `original_home_value`; summary includes `actual_term_months` | 1 + term_months/12 (31 for 360 mo) |
| `ComputeDetailedAmortization` | `DetailedAmortizationRequest` → `DetailedAmortizationResponse` | Schedule plus per-row `tax_savings` at a supplied marginal rate | same as above |
| `ComputeAmortizationBatch` | `AmortizationBatchRequest` → `AmortizationBatchResponse` | Scenario comparison grids (rate × term matrices). NOTE: inputs are `double`, not decimal strings — see §5 | 1 + loans·(term/12) |
| `ComputeFutureValueDetailed` | `FutureValueDetailedRequest` → `FutureValueDetailedResponse` | The FV calculator proper: nominal FV, inflation-adjusted FV, total contributions, total interest. `compound_frequency` is required (0 is refused) | 1 |
| `ComputeFutureValue` | `FutureValueRequest` → `DecimalResponse` | Plain FV (Excel semantics) | 1 |
| `ComputePresentValue` | `PresentValueRequest` → `DecimalResponse` | PV | 1 |
| `ComputeInterestPayment` / `ComputePrincipalPayment` | `PeriodPaymentRequest` → `DecimalResponse` | IPMT/PPMT for a single period | 1 each |
| `ComputeRate` | `RateRequest` → `DecimalResponse` | Solve for rate ("what rate makes this payment work") | 1 |
| `ComputePeriods` | `PeriodsRequest` → `DecimalResponse` | Solve for NPER ("how long to pay off") | 1 |
| `ConvertInterestRate` | `RateConversionRequest` → `DoubleResponse` | APR ↔ APY | 1 |
| `ComputeFisherRate` | `FisherRequest` → `DoubleResponse` | Nominal ↔ real (inflation) | 1 |
| `ComputeCumulative` | `CumulativeRequest` → `DoubleResponse` | CUMIPMT/CUMPRINC over a period range | 1 |
| `ComputeHeloc` | `HelocRequest` → `HelocResponse` | HELOC: available equity, draw-period and repayment-period payments | 1 |

**Supporting, if the site covers investment property / cash-flow views:**

| RPC | Request → Response | Use | CU cost |
| --- | --- | --- | --- |
| `ComputeNpv` / `ComputeIrr` | `NpvRequest` / `IrrRequest` → `DoubleResponse` | Client-composed home NPV/IRR until the dedicated RPC lands | 1 + entries/10 |
| `ComputeXnpv` / `ComputeXirr` | `DatedCashFlowRequest` → `DoubleResponse` | Dated cash flows (closing date, sale date) | 1 + entries/10 |
| `ComputePaybackPeriod` | `PaybackRequest` → `DoubleResponse` | Break-even framing | 1 + entries/10 |
| `ComputeRentalRoi` | `RentalRoiRequest` → `RentalRoiResponse` | NOI, cash flow, cash-on-cash, cap rate, GRM | 1 |
| `ComputeDepreciation` | `DepreciationRequest` → `DoubleResponse` | Rental-property MACRS/SLN | 1 |

Not needed by this site: bonds, T-bills, futures/margin/hedging/spreads, options,
portfolio (the remaining ~15 RPCs).

Input validation the frontend must design for (VERIFIED, `docs/FINANCE_API.md` §5):
malformed decimals are refused (`INVALID_ARGUMENT`), `compound_frequency=0` is refused,
ragged batches are refused, terms over 1200 months are refused. `ComputePayment`
returns a **negative** value (Excel cash-flow convention) — flip the sign for display.

### 1b. Blocked until the finance-proto extension lands

sensen `299cc4fc` (`backend/sensen`, VERIFIED via `git log`) added the
mortgagefvcalculator.com parity features in `src/financial.cppm`. None are in
`finance.proto` yet. Of the seven named capabilities, the site needs **six as RPCs**;
the seventh is not a function:

| sensen capability | Site feature | Needed? |
| --- | --- | --- |
| `calculate_refinance_metrics` (`RefinanceInput` → `RefinanceSummary`: new payment, monthly savings, PMI drop-off months for both loans, payoff-date shift, three break-evens — simple, cash-flow, equity-adjusted — total lifetime savings) | Refinance calculator | **Yes — the flagship gap.** Break-evens and the PMI comparison are not composable from existing RPCs |
| `calculate_rent_vs_buy` (→ `RentVsBuySummary`) | Rent vs buy | **Yes.** Composable only with heavy client-side cash-flow construction |
| `calculate_home_npv` (`HomeNPVInput` → `HomeNPVSummary`: NPV, IRR, future sale price, future equity) | Buy-decision NPV/IRR | **Yes** (interim composition via `ComputeXnpv`/`ComputeXirr` possible, see §1c) |
| `calculate_home_future_value` (→ `FutureValueSummary`) | Home equity projection — the site's namesake | **Yes** (interim composition possible, clumsy) |
| `calculate_payoff_timing` (→ `PayoffTimingSummary`) | Extra-payment payoff | Useful, but **replaceable today** — see §1c |
| `calculate_mortgage_recast` (→ `RecastSummary`) | Recast calculator | Useful, but **exactly replaceable today** — see §1c |
| `get_pmi_drop` | — | **Not an RPC candidate.** VERIFIED: it is a lambda *inside* `calculate_refinance_metrics` (`financial.cppm:1906`), not an exported function. Its outputs surface as `RefinanceSummary.current_loan_pmi_drop_off_months` / `new_loan_pmi_drop_off_months` — it ships as fields of the refinance RPC |

So the minimum the proto extension must expose for this site: **RefinanceMetrics,
RentVsBuy, HomeNpv, HomeFutureValue**, with PayoffTiming and MortgageRecast as
nice-to-haves that remove client-side composition.

### 1c. Interim compositions (lets the site ship before the extension)

- **Payoff timing:** call `ComputeAmortization` twice, with and without
  `monthly_overpayment`. `summary.actual_term_months` difference = months saved;
  `summary.total_interest_paid` difference = interest saved. Functionally equivalent
  to `calculate_payoff_timing` (INFERRED from both implementations' inputs/outputs).
- **Recast:** new payment = `ComputePayment(rate_per_period, remaining_months,
  balance − lump_sum)`. This is the same annuity formula `calculate_mortgage_recast`
  applies (VERIFIED reading `financial.cppm:2094`).
- **Home NPV/IRR:** build the dated cash-flow vector client-side (down payment +
  closing costs out, monthly net costs, terminal sale proceeds in) and call
  `ComputeXnpv`/`ComputeXirr`. Loses the packaged `future_equity`/`future_sale_price`
  outputs.
- **Home FV:** property FV via `ComputeFutureValue` (appreciation), remaining balance
  via the `ComputeAmortization` schedule row at the target month; equity = difference,
  computed client-side **with a decimal library** (§5).

---

## 2. Quota — the load-bearing issue

### 2a. What happens today with no key (VERIFIED from code)

`backend/src/modules/finance_service.cpp` runs a `CHARGE` macro at the top of every
RPC: `KeyRegistry::authenticate(...)` resolves an identity, then
`QuotaEnforcer::admit_identity(_id.id, _id.tier, ...)` meters it
(finance_service.cpp:161–178, VERIFIED). In `quota.cpp::charge`, **an empty
`caller_id` collapses to the single shared `"~anonymous"` bucket** (quota.cpp:266,
VERIFIED), and an unkeyed or unrecognised caller has an empty id.

Consequences, all VERIFIED from `quota.cpp`:

- **Yes, the two sites degrade each other.** All unkeyed traffic — every anonymous
  visitor of optionsandfuturescalculator.com (the calculator service meters through
  the same singleton `QuotaEnforcer` and the same `"~anonymous"` id) plus every
  visitor of mortgagefvcalculator.com — draws down one bucket pair: the live
  anonymous tier's 6,000 req/min and 120,000 CU/hr (tier numbers per CLAUDE.md;
  the audit spec `2026-08-05-subscription-stack-audit.md` confirms the live policy
  was read from Railway). When the bucket empties, *whoever calls next* is refused
  with `RESOURCE_EXHAUSTED`, regardless of which site they came from. A mortgagefv
  traffic spike throttles the options calculator and vice versa.
- Sending a random/unregistered `x-api-key` does **not** escape this: unrecognised
  keys deliberately share the same `"~anonymous"` bucket (quota.cpp:209–221,
  VERIFIED) so allowance cannot be minted by varying the header.
- At ~31 CU per 360-month amortization, the shared 120,000 CU/hr supports roughly
  3,800 amortizations/hr **site-wide across both properties combined** (arithmetic,
  INFERRED sizing).

**A second, lower shared ceiling sits in front of quota** (VERIFIED config,
`backend/envoy.yaml:105–127`): Envoy's `local_ratelimit` filter on `:8080` has
`max_tokens: 100, tokens_per_fill: 10, fill_interval: 1s` — a burst of 100 and a
sustained **10 req/s (~600 req/min) for the entire listener**, all callers, all three
services, keys or no keys. Refusals at this layer are HTTP 429 with
`x-local-rate-limit: true`, never a gRPC status. (That the bucket is shared across all
callers/workers rather than per-connection is INFERRED from Envoy's local-rate-limit
defaults; the numbers are VERIFIED.) Note the tension: the quota policy's anonymous
6,000 req/min and partner 2,400 req/min are both *above* what this filter will ever
admit sustained. For the site's expected traffic (a handful of calls per user
interaction) 10 req/s is probably adequate, but it is the true binding rate ceiling
and an API key does nothing to lift it. See work unit W5.

### 2b. The correct mechanism: a registered publishable key (VERIFIED from code)

Tier resolution for the finance service is `FINANCE_API_KEYS` → `KeyRegistry`
(`backend/src/modules/api_key.cpp`), **not** `QUOTA_API_KEYS` (that older plaintext
path feeds only the raw-header `admit()` used by the calculator service;
`docs/FINANCE_API.md` §8 explicitly says not to use it for new keys). How it works:

1. Browser sends `x-api-key: pk_live_…` (Envoy already allows the header —
   `envoy.yaml:101`, VERIFIED; it forwards to the engine unchanged).
2. `KeyRegistry::check` hashes the presented key (SHA-512, constant-time scan),
   finds the record, validates enabled/expiry, **binds to `Origin`** (browser sets it
   and cannot forge it), checks the `finance` scope (api_key.cpp:605–724, VERIFIED).
3. The resulting identity — `id` = the key's label, `tier` = the key's tier, plus any
   per-key limits — is what quota meters. A recognised key therefore gets **its own
   bucket keyed on the key's `id`**, fully separate from `~anonymous`.

**What must be provisioned** (nothing in the repo changes; this is Railway env only —
see work units W1–W2):

- Mint a **publishable** key. Preferred: `calculator_engine issue-key --id
  mortgagefv-web --rpm <n> --cu <n>` (`backend/src/issue_key.cpp`, VERIFIED — it links
  the same `generate_key`/`sha512_hex` the server verifies with and prints the
  ready-to-paste JSON). Manual alternative in `docs/FINANCE_API.md` §8.
- Add to `FINANCE_API_KEYS` on the Railway service `options-calculator-backend`
  (VERIFIED: this variable is read at startup, api_key.cpp:480; it appears in neither
  the `SUBSCRIPTION_STACK_STATE.md` env table nor the audit spec, so it is INFERRED
  currently unset and key auth is currently disabled):

```json
{
  "<sha512 hex of the key>": {
    "id": "mortgagefv-web",
    "type": "publishable",
    "tier": "partner",
    "origins": ["https://mortgagefvcalculator.com", "https://www.mortgagefvcalculator.com"],
    "scopes": ["finance"],
    "enabled": true
  }
}
```

- Leave `FINANCE_REQUIRE_KEY` unset (Observe mode — VERIFIED default,
  api_key.cpp:471–478; the audit spec confirms it is unset in Railway). Setting
  `FINANCE_API_KEYS` alone refuses **nobody**: unkeyed callers continue to be served
  and metered as anonymous; the new key simply starts metering separately. This is
  the safe rollout.

**Tier choice and the `limits_for_tier()` trap.** `limits_for_tier()` silently falls
back to the **anonymous** allowance for a tier name the live `QUOTA_POLICY` does not
define, while refusals still name the requested tier (quota.cpp:95–100 and 252–263,
VERIFIED). `KeyRegistry` does **not** validate a key's tier against `QUOTA_POLICY` at
load (api_key.cpp:517–519 accepts any string — VERIFIED; only the legacy
`QUOTA_API_KEYS` loader validates). So yes — **a newly invented tier name (e.g.
`"mortgagefv"`) on the key would hit the trap exactly**: the site would silently run
on the anonymous *limits* (in its own bucket, so no shared-fate — but sized 6,000/
120,000 rather than whatever was intended) and any refusal would be labelled
`tier 'mortgagefv'`, pointing the operator at a tier that exists nowhere. Two safe
options:

- **Option A (recommended): `"tier": "partner"`.** `partner` is defined in the live
  `QUOTA_POLICY` (2,400 req/min, 1,200,000 CU/hr — CLAUDE.md, corroborated by the
  audit spec's read of Railway). No policy edit needed, no trap possible.
- **Option B: per-key limits** (`"requests_per_minute"` / `"compute_units_per_hour"`
  in the key record). These flow through as `explicit_limits` and **bypass
  `limits_for_tier()` entirely** (finance_service.cpp:169–174 → quota.cpp:258,
  VERIFIED), so the trap cannot fire; they are honoured even if `QUOTA_POLICY` were
  ever unset (quota.cpp:239–247, VERIFIED). Use this if partner's numbers are wrong
  for the site. Do NOT set both axes to 0 — that is explicit-unlimited and logged as
  such (api_key.cpp:539–548, VERIFIED).

Two operational caveats (VERIFIED from code):

- Bucket capacity is fixed at **first sight** of a caller id (`try_emplace`,
  quota.cpp:271–283); a later tier/limit change takes effect only after the caller
  idles >1 hour and is swept. Bounce the service after changing a live key's limits
  if immediacy matters.
- Quotas are **per replica** (in-process buckets — `docs/FINANCE_API.md` §9). One
  replica today; N replicas would multiply every stated limit by N.

---

## 3. Security assessment of the open CORS

`envoy.yaml:96–99` (VERIFIED) allows any origin via `safe_regex: ".*"`, and the parent
verification confirmed the live endpoint echoes arbitrary origins.

**What open CORS does and does not enable here:**

- It does **not** leak credentials. The CORS policy sets no `allow_credentials`
  (VERIFIED absent from envoy.yaml), the API uses no cookies and no ambient
  authority — `x-api-key`/`authorization` are attached explicitly by the calling
  page's own JS. A malicious site cannot cause a visitor's browser to present
  someone else's key or session.
- It does **not** expose stored data. Every finance RPC is a pure computation over
  caller-supplied inputs; none reads Postgres (`docs/FINANCE_API.md` §10, VERIFIED
  statement; consistent with finance_service.cpp, which touches no DB).
- It **does** let any website drive the API from its visitors' browsers, distributing
  the traffic across visitor IPs. The exposure is therefore: **(a) quota exhaustion**
  of the shared anonymous bucket (a hostile or merely popular third-party embed
  starves both first-party sites — the §2a shared-fate problem, made reachable by any
  page on the internet), and **(b) cost**, concentrated in the *calculator* service's
  metered market-data RPCs (`GetMarketQuote`/`GetMarketChain` spend the shared Alpaca
  vendor budget — quota.cpp:512–604, VERIFIED; the finance service itself has no
  upstream cost, only CPU). Not (c) data access.
- Honest limit of an allow-list: CORS constrains **browsers only**. curl, scripts,
  and servers ignore it entirely, and they are the cheaper way to abuse an API. An
  allow-list narrows drive-by browser abuse; it is not a security boundary.

**Recommendation: keep `.*`.** Open browser access is not an accident here — it is
documented product intent (`docs/FINANCE_API.md` advertises "a third-party site can
call this directly from the browser with no proxy of its own", and the whole
publishable-key design in api_key.cpp exists to secure exactly this posture via
origin *binding on the key*, not origin *filtering at the proxy*). The defense stack
that matters is: Envoy local rate limit (site-wide backstop) → per-key origin-bound
quota (partners) → anonymous bucket (everyone else). Closing CORS would break the
advertised third-party surface while leaving non-browser abuse untouched.

If that posture is ever reversed (e.g. anonymous abuse materialises), the exact
change in `backend/envoy.yaml` is to replace lines 96–99 with:

```yaml
                    cors:
                      allow_origin_string_match:
                        - exact: "https://optionsandfuturescalculator.com"
                        - exact: "https://www.optionsandfuturescalculator.com"
                        - exact: "https://mortgagefvcalculator.com"
                        - exact: "https://www.mortgagefvcalculator.com"
                        - exact: "http://localhost:3000"
```

What breaks under the allow-list: every other third-party browser consumer
`FINANCE_API.md` currently invites (their preflights fail); local dev on any port not
listed; and each future partner becomes an envoy.yaml edit + backend redeploy instead
of an env-var key registration. curl/server callers and the smoke probes are
unaffected (no `Origin`). Browser calls from unlisted origins fail at preflight with
no `access-control-allow-origin` — which presents as a generic network error, not a
gRPC status, so support burden goes up.

---

## 4. Client integration

### 4a. Getting the contract

Copy `backend/proto/finance.proto` into the mortgagefv project. It is deliberately
self-contained — **no imports** (VERIFIED), so one file travels. It is not published
anywhere today (no npm package, no public URL — VERIFIED absence in repo; work unit
W4 proposes the lightweight fix: commit a copy under the consuming site's repo and
record the source commit hash in a comment).

### 4b. Tooling that matches the existing setup (VERIFIED from `scripts/gen_proto.sh` and `frontend/package.json`)

- `protoc` from `grpc-tools` (dev dependency), plugin `protoc-gen-grpc-web` **v1.5.0**
- runtime: `grpc-web` npm **^2.0.2**
- generation flags exactly as the existing frontend uses:

```bash
protoc \
  --proto_path=. \
  --plugin=protoc-gen-grpc-web="$(command -v protoc-gen-grpc-web)" \
  --js_out=import_style=commonjs,binary:./src/grpc \
  --grpc-web_out=import_style=typescript,mode=grpcwebtext:./src/grpc \
  finance.proto
```

This emits `finance_pb.js`, `finance_pb.d.ts`, and `FinanceServiceClientPb.ts`
(client class `FinanceClient`). Commit the generated output (the existing repo does,
because its static hosting has no protoc at build time — same reasoning applies to a
static mortgagefv site).

### 4c. Endpoint and headers

- Endpoint: `https://api.optionsandfuturescalculator.com` — the grpc-web client
  appends `/sensen.finance.Finance/<Method>`.
- The grpc-web runtime sets `content-type: application/grpc-web-text` (in
  `grpcwebtext` mode) and `x-grpc-web: 1` itself; do not set them manually.
- `x-api-key` and `authorization` are both already in Envoy's `allow_headers`
  (envoy.yaml:101, VERIFIED) — pass them as call metadata:

```ts
import { FinanceClient } from './grpc/FinanceServiceClientPb';
import { AmortizationRequest } from './grpc/finance_pb';

const client = new FinanceClient('https://api.optionsandfuturescalculator.com');

const req = new AmortizationRequest();
req.setLoanAmount('300000');
req.setAnnualRate('0.06');
req.setTermMonths(360);
req.setMonthlyOverpayment('500');

client.computeAmortization(req, { 'x-api-key': 'pk_live_…' }, (err, res) => {
  if (err) { console.error(err.code, err.message); return; }
  console.log(res.getSummary()!.getActualTermMonths());   // 212 — retired early
  console.log(res.getSummary()!.getTotalInterestPaid());  // exact decimal string
});
```

The publishable key is *meant* to be visible in the page; its security is the origin
binding registered in §2b, not secrecy (api_key.cpp origin matching, VERIFIED).

### 4d. Server-side callers (if the site ever adds SSR/API routes)

**Do not use native gRPC against the custom domain** — it does not survive the
Railway ingress (HTTP/2 trailers dropped; "Stream removed"). CLAUDE.md documents this
and the parent verification confirmed it. Note: `docs/FINANCE_API.md` §3 still
advertises native gRPC against `api.…:443` — that section is contradicted by
CLAUDE.md's later finding and should be corrected (work unit W6). Server-side, use
the **JSON surface** instead: Envoy's `grpc_json_transcoder` maps
`POST /sensen.finance.Finance/<Method>` with `content-type: application/json`
(auto-mapping over the same descriptor — envoy.yaml:154–168, VERIFIED; live-tested in
§6). Field names are proto-JSON lowerCamelCase (`presentValue`), decimal fields are
JSON *strings*, and `x-api-key` passes through the transcoder untouched.

---

## 5. Numeric contract — how not to corrupt the figures

The rule (from finance.proto's header, VERIFIED): **`string` = sensen computed it in
BigDecimal (exact, 18 decimal places) — never let it touch a float. `double` = sensen
computed it in double — safe to use as a JS number.**

**String (exact decimal) fields the site will handle** — every one of these, request
and response: all of `PaymentRequest`, `PresentValueRequest`, `FutureValueRequest`,
`FutureValueDetailedRequest/Response`, `PeriodPaymentRequest`, `RateRequest`,
`PeriodsRequest`, `DecimalResponse.value`, `AmortizationRequest`, `AmortizationRow`
(all money columns), `MortgageSummary`, `DetailedAmortizationRequest/Row/Summary`,
`HelocRequest/Response`, `RentalRoiRequest/Response`. (VERIFIED against
finance.proto.)

**Double fields the site will handle**: `DoubleResponse.value` (so the outputs of
`ConvertInterestRate`, `ComputeFisherRate`, `ComputeNpv/Irr/Xnpv/Xirr`,
`ComputePaybackPeriod`, `ComputeCumulative`, `ComputeDepreciation`), and — the one
that surprises — **all inputs of `ComputeAmortizationBatch`** (`repeated double`,
deliberate: summaries-only scenario grids). Do not use the batch RPC where
cent-exactness of a displayed figure matters; use it for comparison matrices.

Rules for the site's engineers:

1. **Never `parseFloat()`/`Number()` a money string and then do arithmetic.**
   `parseFloat("300000.123456789012345678")` silently truncates to float64;
   summing schedule rows in floats will fail to close the schedule that the engine
   closed exactly. Use `decimal.js` (or `big.js`):

   ```ts
   import Decimal from 'decimal.js';
   const payment = new Decimal(res.getValue());       // exact
   const annual  = payment.times(12);                 // still exact
   const display = payment.negated().toFixed(2);      // format at the edge only
   ```

2. **Format only at the display edge.** Round with `Decimal.toFixed(2)` (or
   `Intl.NumberFormat` fed the already-rounded string), never mid-computation.
3. **Send strings, exactly.** Requests carry decimal fields as strings
   (`req.setRate('0.005')` — note per-*period* rate). In JSON, `"rate": "0.005"`
   with quotes; the service validates and refuses malformed decimals
   (`"12x3"` → `INVALID_ARGUMENT`, live-verified in §6).
4. **`double` fields are fine as numbers** — widening them to Decimal would imply
   precision the engine never had.
5. Chart libraries may take floats for *plotting* amortization curves — acceptable,
   pixels aren't ledgers — but every printed figure comes from the Decimal path.
6. Remember sign conventions: `ComputePayment` is negative (outflow).

---

## 6. Verification — proving it end to end

All of the below were **run live on 2026-08-05** against production (VERIFIED),
except where marked.

**Check 1 — the calculation surface works from this origin (curl, JSON path):**

```bash
curl -sS -X POST \
  https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputePayment \
  -H 'content-type: application/json' \
  -H 'Origin: https://mortgagefvcalculator.com' \
  -d '{"rate":"0.005","periods":360,"presentValue":"300000"}' -i
```

Correct response (observed):

```
HTTP/2 200
access-control-allow-origin: https://mortgagefvcalculator.com
grpc-status: 0
content-type: application/json

{"value":"-1798.651575458257198999"}
```

The three things to check: `access-control-allow-origin` echoes the site's origin,
`grpc-status: 0`, and the value is the exact 18-place decimal string (independently:
300000·0.005/(1−1.005⁻³⁶⁰) = 1798.6516 — the closed-form annuity check the finance
gate uses).

**Check 2 — from the site itself (browser console on mortgagefvcalculator.com):**

```js
fetch('https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputeAmortization', {
  method: 'POST',
  headers: { 'content-type': 'application/json' /*, 'x-api-key': 'pk_live_…' */ },
  body: JSON.stringify({ loanAmount: '300000', annualRate: '0.06',
                         termMonths: 360, monthlyOverpayment: '500' }),
}).then(r => r.json()).then(d => console.log(d.summary));
// expect: { …, actualTermMonths: 212, totalInterestPaid: "<exact decimal string>" }
```

Running in the page's own console proves CORS, TLS, and the transcoder for the real
origin in one shot. (Snippet shape verified via curl with the same Origin header; the
in-browser run is the site team's acceptance step.) The gRPC-Web binary path was also
verified directly: a hand-framed `application/grpc-web-text` POST to `ComputePayment`
returned the same value with trailer `grpc-status:0`.

**Check 3 — failure modes, so they are recognised and not misdiagnosed:**

| Symptom | Meaning | Observed shape (live, VERIFIED) |
| --- | --- | --- |
| `grpc-status: 12` (UNIMPLEMENTED) | RPC not exposed — e.g. calling a refinance RPC before the proto extension lands | gRPC-Web path: HTTP 200, `grpc-status: 12`, empty body. JSON path is *worse*: an unknown method bypasses the transcoder and dies with `grpc-status: 2`, `grpc-message: Missing :te header` — read that as "method unknown", not as a transport bug |
| HTTP 400, body `{"code":3,"message":"rate is not a decimal number: \"12x3\"",…}` | Bad payload, JSON path (`INVALID_ARGUMENT`; the transcoder converts status to a JSON object) | verified live |
| `grpc-status: 3` / `13` on gRPC-Web | 3 = input the service refused; 13 = malformed frame/internal | 13 not provoked live (INFERRED from gRPC semantics) |
| `grpc-status: 8` (RESOURCE_EXHAUSTED), message `quota exceeded for tier '<tier>' on <Method> (request rate); retry in <N>s` | Quota refusal; honor the retry-after, do not tight-retry | shape VERIFIED from quota.cpp:381–390; not provoked live |
| HTTP 429 + `x-local-rate-limit: true` header, no gRPC status | Envoy's site-wide 10 req/s local rate limit — not the quota system | shape VERIFIED from envoy.yaml:123–127; not provoked live |
| `grpc-status: 16` / `7` | Key problems, only once `FINANCE_REQUIRE_KEY=enforce`: 16 = who are you (no/bad/expired key), 7 = known but refused (wrong origin, wrong scope, secret key in a browser) | messages enumerated in docs/FINANCE_API.md §8, VERIFIED source api_key.cpp |
| Preflight fails, no `access-control-allow-origin` | Only possible after a CORS allow-list change (§3) — origin missing from the list | n/a today (`.*`) |

**Check 4 — the key is metered separately (after W2):** send one request with the
key and confirm the Railway log's startup line reads
`API key auth ENABLED: 1 keys, mode OBSERVE…`, and that a deliberate burst past the
key's limit refuses with the key's tier named in the message while an unkeyed curl
still succeeds.

---

## 7. Work units

**W1 — Mint the site's publishable key.** Files: none (operational).
Run `calculator_engine issue-key --id mortgagefv-web` (add `--rpm/--cu` only if
choosing Option B of §2b) on a trusted machine; capture the printed `pk_live_…` once
and the JSON stanza it emits. Risk: plaintext key mishandled at issuance — hand it
straight to the site's deploy config, store only the hash. Acceptance: the emitted
JSON contains a 128-char lowercase hex hash and no plaintext.

**W2 — Register the key in Railway.** Change: add `FINANCE_API_KEYS` (stanza from
§2b, `"tier": "partner"`, both `mortgagefvcalculator.com` origins) to service
`options-calculator-backend`; redeploy. Do **not** set `FINANCE_REQUIRE_KEY`. Do
**not** touch `QUOTA_POLICY` (partner already defined; and per CLAUDE.md, never
"fix" it from the FINANCE_API.md example). Risks: malformed JSON leaves auth
disabled — but loudly (`FINANCE_API_KEYS is not valid JSON` in logs, VERIFIED
api_key.cpp:490) and fails open, so worst case is status quo; inventing a new tier
name triggers the `limits_for_tier()` anonymous-fallback trap (§2b). Acceptance:
startup log shows `API key auth ENABLED: 1 keys, mode OBSERVE`; §6 Check 4 passes;
unkeyed traffic still serves.

**W3 — Generate and vendor the client in the mortgagefv repo.** Files (that repo):
`proto/finance.proto` (copied, with source commit noted), `src/grpc/finance_pb.{js,d.ts}`,
`src/grpc/FinanceServiceClientPb.ts`, plus a `gen_proto.sh` cloned from this repo's
script with the drift-check loop retained. Tooling pinned to §4b. Risk: stub/runtime
drift — the exact bug this repo's script exists to prevent; keep its verification
loop. Acceptance: §6 Check 2 passes from the deployed site; a money field round-trips
through `decimal.js` with all 18 places intact.

**W4 — Publish the contract for consumers.** Files:
`docs/FINANCE_API.md` — add a "Get the contract" note stating the file is
self-contained, that consumers should vendor it recording the source commit, and
where the canonical copy lives. (A registry package is not warranted for one
consumer.) Risk: none. Acceptance: doc names the canonical path and the vendoring
rule.

**W5 — Decide the Envoy local rate limit posture.** Files: `backend/envoy.yaml`
(lines 105–127) — *decision only in this spec; change deferred*. Today's 10 req/s
sustained is shared by both sites and all three services and sits far below every
quota tier's req/min. Before mortgagefv launch, either (a) accept it as the abuse
backstop and size launch expectations to it, or (b) raise `tokens_per_fill` (e.g. 50)
and let the per-key quota do the fine-grained work. Risk of raising: the backstop
against non-browser floods weakens; risk of not raising: cross-site 429s under
combined load that no quota message explains. Acceptance: an explicit number chosen
and recorded in CLAUDE.md alongside the quota table.

**W6 — Correct the native-gRPC claim in FINANCE_API.md.** Files:
`docs/FINANCE_API.md` §intro + §3. It currently states native gRPC works against
`api.optionsandfuturescalculator.com:443` "verified live"; CLAUDE.md and the parent's
verification establish it does not survive the Railway ingress (gRPC-Web and JSON
only). Rewrite §3 to point server-side callers at the JSON surface (§4d). Risk:
none — doc-only. Acceptance: no doc in the repo advertises native gRPC through the
custom domain.

**W7 — (Blocked, tracked elsewhere) Finance proto extension.** The six RPCs of §1b —
refinance metrics (carrying the PMI drop-off fields), rent-vs-buy, home NPV, home FV,
payoff timing, recast — belong to the forthcoming
`2026-08-05-finance-proto-extension.md`. This spec's only requirement on it: request
and response money fields must be decimal **strings** per the §5 contract (sensen
computes them in BigDecimal — `RefinanceSummary` et al. are BigDecimal structs,
VERIFIED `financial.cppm`), and each new RPC needs a `CHARGE` line with a cost
function (amortization-shaped: `1 + term/12`-like, since refinance/payoff/recast all
iterate schedules — INFERRED sizing).

---

## 8. What I could not determine

- The **current live value of `FINANCE_API_KEYS`** (whether truly unset). INFERRED
  unset from its absence in `SUBSCRIPTION_STACK_STATE.md`'s env inventory and the
  audit spec; confirm with `railway variables` before W2.
- Whether Envoy's local-rate-limit bucket is strictly process-global vs per-worker
  (§2a). Config numbers VERIFIED; sharing semantics INFERRED from Envoy defaults.
  Either way it is far below the quota tiers and shared across both sites.
- `grpc-status: 13` was not provoked against production (I did not send a corrupt
  binary frame); its row in §6 is INFERRED from gRPC semantics.
- Real traffic expectations for mortgagefvcalculator.com — the W5 sizing decision
  needs a number from the site owner.
