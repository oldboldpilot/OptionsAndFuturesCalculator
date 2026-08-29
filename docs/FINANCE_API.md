# Using `sensen.finance.Finance` from another site or service

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

- **A malformed decimal.** `"12x3"` is rejected. (`BigDecimal`'s own parser
  skips non-digits and would read it as `123`; the service validates first.)
- **An unstated compounding frequency** on `ComputeFutureValueDetailed`. It
  changes the answer materially, so no default is invented.
- **A bond with neither `yield` nor `price`.** Supply exactly one; the other is
  derived. Supplying both would let them disagree.
- **A ragged batch.** Every repeated field in `ComputeAmortizationBatch` must be
  the same length; truncating to the shortest would return a short list that
  looks complete.
- **A term over 1200 months.** One row is allocated per period, so an
  unbounded term is a denial of service dressed as a mortgage.

Where a figure is genuinely not computable it is marked, not zeroed:
`ComputeHedge` returns `contracts_computed = false` when no position size was
given, and `ComputePortfolioStats` returns `benchmark_supplied = false` so you
can tell an absent beta from a measured beta of zero.

---

## 6. What is available

Roughly fifty functions. See `backend/proto/finance.proto` for the full list.

| Area | RPCs |
| --- | --- |
| Time value of money | `ComputePayment` `ComputePresentValue` `ComputeFutureValue` `ComputeFutureValueDetailed` `ComputeInterestPayment` `ComputePrincipalPayment` `ComputeRate` `ComputePeriods` `ConvertInterestRate` `ComputeFisherRate` |
| Mortgages, HELOC | `ComputeAmortization` `ComputeDetailedAmortization` (tax deductions) `ComputeAmortizationBatch` `ComputeHeloc` |
| Cash flow | `ComputeNpv` `ComputeIrr` `ComputeXnpv` `ComputeXirr` `ComputePaybackPeriod` `ComputeCumulative` |
| Depreciation | `ComputeDepreciation` (SLN, SYD, DDB, MACRS) |
| Fixed income | `AnalyzeBond` (price, yield, duration, convexity) `AnalyzeTreasuryBill` (price + BEY/MMY/BDY) |
| Futures | `PriceFutures` `ValueFutures` `SimulateMarginAccount` `ComputeHedge` `ComputeCommoditySpread` |
| Real estate | `ComputeRentalRoi` |
| State assumptions | `GetStateAssumptions` (open) `RefreshStateAssumptions` (**partner only — the one write on this service**) |
| Options | `PriceOptionTree` (American/Bermudan/Asian) `PriceBlackScholes` (11 Greeks) `PriceOptionMonteCarlo` `ComputeProbabilityTree` |
| Portfolio | `ComputePortfolioStats` `OptimizePortfolio` `ComputeRiskContributions` |

### The one write, and why it is gated differently

Everything else on this service computes a number from its arguments and
returns it. `RefreshStateAssumptions` overwrites fifty rows that fifty live
pages render, from a third-party feed, so it requires a **partner** credential
and `GetStateAssumptions` requires none.

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
error. A caller must be able to tell "the site is serving last week's numbers"
apart from "the RPC did not happen", and a status code collapses those. See
[State assumptions handoff](STATE_ASSUMPTIONS_HANDOFF.md).

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
