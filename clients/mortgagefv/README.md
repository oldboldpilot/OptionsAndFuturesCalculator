# `sensen.finance.Finance` client for mortgagefvcalculator.com

A self-contained gRPC-Web client for the `sensen.finance.Finance` service that
already runs in production at `https://api.optionsandfuturescalculator.com`.
This directory is meant to be copied wholesale into the mortgagefvcalculator.com
codebase (or added as a git subtree) -- it has no dependency on anything else
in the OptionsAndFuturesCalculator repo at runtime.

```
clients/mortgagefv/
├── proto/finance.proto           vendored copy of the contract (source commit noted inline)
├── scripts/gen_proto.sh          regenerates src/grpc/ from proto/finance.proto
├── src/grpc/
│   ├── finance_pb.js             generated message classes (CommonJS, runtime)
│   ├── finance_pb.d.ts           generated message class type declarations
│   └── FinanceServiceClientPb.ts generated TypeScript service client (`FinanceClient`)
├── example/compute-payment.js    runnable end-to-end example, plain Node
└── package.json                  grpc-web + google-protobuf as runtime deps
```

## Endpoint

```
https://api.optionsandfuturescalculator.com
```

One URL for everything. The gRPC-Web client appends the path itself
(`/sensen.finance.Finance/<Method>`); you never construct it by hand in normal
use.

There is no separate host or port for this service versus the calculator
application that also lives on this domain -- Envoy routes by path prefix, and
`sensen.finance.Finance` is a completely separate contract from
`calculator.OptionsCalculator`. You only need `finance.proto`.

## Headers

The `grpc-web` runtime sets the framing headers itself -- **do not set these by
hand**:

- `content-type: application/grpc-web-text` (the client is constructed with
  `{ format: 'text' }`, matching `mode=grpcwebtext` used at generation time)
- `x-grpc-web: 1`

Two headers you *do* set, as call metadata (the second argument to every
generated method, or the third argument to `client.rpcCall(...)`):

- `x-api-key` -- a publishable key (`pk_live_…`) once one is provisioned for
  this site (tracked separately; not yet issued as of this writing). It is
  *meant* to be visible in page source -- its security is that the server
  binds it to the registered `Origin` (`https://mortgagefvcalculator.com`),
  not that it's secret. Until a key exists, calls are simply unauthenticated
  and metered into the shared anonymous quota bucket (see "Known
  constraints" below).
- `authorization` -- not needed for this service today; included here only
  because Envoy already allows it through (`allow_headers`), in case a future
  key scheme uses it instead of `x-api-key`.

Both headers are already present in Envoy's CORS `allow_headers` on the
production ingress, confirmed live (see "Verified live" below) -- no server
change is needed to start sending either one.

## Minimal working example

```js
global.XMLHttpRequest = require('xhr2'); // Node only -- browsers have this natively

const grpcWeb = require('grpc-web');
const finance_pb = require('./src/grpc/finance_pb.js');

const client = new grpcWeb.GrpcWebClientBase({ format: 'text' });

const computePaymentDescriptor = new grpcWeb.MethodDescriptor(
  '/sensen.finance.Finance/ComputePayment',
  grpcWeb.MethodType.UNARY,
  finance_pb.PaymentRequest,
  finance_pb.DecimalResponse,
  (request) => request.serializeBinary(),
  finance_pb.DecimalResponse.deserializeBinary
);

const req = new finance_pb.PaymentRequest();
req.setRate('0.005');          // PER PERIOD, not per year (0.06 / 12)
req.setPeriods(360);
req.setPresentValue('300000');

client.rpcCall(
  'https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputePayment',
  req,
  {}, // metadata: { 'x-api-key': '...' } once a key exists
  computePaymentDescriptor,
  (err, res) => {
    if (err) { console.error(err.code, err.message); return; }
    console.log(res.getValue()); // "-1798.651575458257198999"
  }
);
```

Run it for real:

```bash
cd clients/mortgagefv
npm install
node example/compute-payment.js
# ComputePayment -> -1798.651575458257198999
# (negative = cash outflow; this is correct, not a bug)
```

That call hits the live production endpoint. It was also run during this
work unit and returned exactly that value (see "Verified live" below).

**TypeScript / normal app code** should use the generated client class
directly instead of hand-building a `MethodDescriptor` (the example above
does that only so it runs as plain Node JS with no build step):

```ts
import { FinanceClient } from './src/grpc/FinanceServiceClientPb';
import { AmortizationRequest } from './src/grpc/finance_pb';

const client = new FinanceClient('https://api.optionsandfuturescalculator.com');

const req = new AmortizationRequest();
req.setLoanAmount('300000');
req.setAnnualRate('0.06');
req.setTermMonths(360);
req.setMonthlyOverpayment('500');

client.computeAmortization(req, { 'x-api-key': 'pk_live_…' }, (err, res) => {
  if (err) { console.error(err.code, err.message); return; }
  console.log(res.getSummary()!.getActualTermMonths());
  console.log(res.getSummary()!.getTotalInterestPaid());
});
```

## Regenerating the client

If `proto/finance.proto` is updated (a newer commit vendored in), regenerate
both `finance_pb.js` and `FinanceServiceClientPb.ts` from **one** invocation --
never hand-edit generated files, and never regenerate only one of the two
files. Mismatched versions is the exact bug `scripts/gen_proto.sh` exists to
prevent (see the comment at the top of that script): a client that references
a message the runtime doesn't have throws `Cannot read properties of
undefined (reading 'deserializeBinary')` at call time, not at build time.

```bash
npm install --no-save grpc-tools@1.13.1   # or have protoc on PATH
bash scripts/gen_proto.sh
```

Pinned toolchain (matches the server repo that owns the canonical proto,
verified against `scripts/gen_proto.sh` and `frontend/package.json` there):
`protoc` via `grpc-tools@1.13.1` (`libprotoc 3.19.1`), plugin
`protoc-gen-grpc-web` **v1.5.0** (https://github.com/grpc/grpc-web/releases),
runtime `grpc-web@^2.0.2`. The exact invocation `gen_proto.sh` runs:

```bash
protoc \
  --proto_path=proto \
  --plugin=protoc-gen-grpc-web="$(command -v protoc-gen-grpc-web)" \
  --js_out=import_style=commonjs,binary:src/grpc \
  --grpc-web_out=import_style=typescript,mode=grpcwebtext:src/grpc \
  proto/finance.proto
```

## Vendoring policy

There is no published package for this contract (no npm registry entry, no
Buf Schema Registry) -- it travels as a copied file. `proto/finance.proto`
carries a header comment stating the source repo and the exact commit it was
copied at. When updating: pull the file at a newer commit, update that
comment, regenerate (above), and re-run the verification in "Verified live"
against the new methods before shipping.

## The numeric contract

Read this before writing any code that does arithmetic on a response field.
The proto's own header (`finance.proto` top-of-file comment) states the rule
and the reason:

> `string` -- sensen computed this in `BigDecimal`: an `__int128` scaled by
> 1e18, exact to eighteen decimal places. `double` -- sensen computed this
> value in `double` natively. Widening a double to a string would imply a
> precision the engine never had; narrowing a BigDecimal string to a double
> loses precision the engine actually has.

Money is a decimal `string` for two concrete reasons, not convention:
rounding an 18-decimal-place amortization to `double` compounds over a
360-period schedule until it stops closing (`start − principal − end` would
stop being exactly zero). And this API is called from browsers, where
JavaScript's `number` **is** an IEEE‑754 float64 -- a `double` money field
would already be lossy on the client before any of the site's own code runs.

### Which fields are which (derived from `finance.proto`, not guessed)

**String (exact `BigDecimal`) -- every field on every RPC this site is
expected to use:**

| Message | String fields |
| --- | --- |
| `PaymentRequest` / `PresentValueRequest` / `FutureValueRequest` | `rate`, `payment`/`present_value`/`future_value` (whichever apply) |
| `DecimalResponse` | `value` -- the return type of `ComputePayment`, `ComputePresentValue`, `ComputeFutureValue`, `ComputeInterestPayment`, `ComputePrincipalPayment`, `ComputeRate`, `ComputePeriods` |
| `FutureValueDetailedRequest` / `Response` | `annual_rate`, `annual_contribution`, `current_principal`, `annual_inflation_rate` / `nominal_fv`, `inflation_adjusted_fv`, `total_contributions`, `total_interest_earned` |
| `AmortizationRequest` / `AmortizationRow` / `MortgageSummary` | all money fields (`loan_amount`, `monthly_overpayment`, every row's `start_balance`…`end_balance`, every summary total) |
| `DetailedAmortizationRequest` / `Row` / `Summary` | same, plus `annual_tax_rate` and `tax_savings` |
| `HelocRequest` / `HelocResponse` | `home_value`, `max_ltv_rate`, `drawn_amount`, `annual_rate`, `available_equity`, `draw_period_payment`, `repayment_period_payment` |
| `RentalRoiRequest` / `RentalRoiResponse` | all fields |
| `RefinanceRequest`/`Response`, `PayoffTimingRequest`/`Response`, `MortgageRecastRequest`/`Response`, `HomeFutureValueRequest` | all money/rate fields **except** two noted below -- these six RPCs are not live yet, see "Known constraints" |

**Double -- genuinely computed in `double` by sensen, safe to use as a JS
number directly:**

| Field | Where | Why (from the proto comment at that field) |
| --- | --- | --- |
| `DoubleResponse.value` | `ConvertInterestRate`, `ComputeFisherRate`, `ComputeNpv`/`Irr`/`Xnpv`/`Xirr`, `ComputePaybackPeriod`, `ComputeCumulative`, `ComputeDepreciation` | these are all `double`-domain calculators (rate conversions, IRR solvers, depreciation tables) |
| `AmortizationBatchRequest.*` (`repeated double`) | `ComputeAmortizationBatch` | **the one that surprises**: every input to the batch RPC is `double`, not string, on purpose -- it returns summaries-only comparison grids, not cent-exact figures. Do not use this RPC where a displayed number must be exact; use the singular `ComputeAmortization` for that. |
| `RefinanceResponse.total_savings_over_life` | `ComputeRefinance` (not yet live) | proto comment: accumulated over the whole horizon in a `double` loop, so a decimal string would claim digits the computation never had |
| `HomeFutureValueResponse.future_property_value` | `ComputeHomeFutureValue` (not yet live) | compound appreciation is computed in double; `future_loan_balance` and `future_equity` on the same message are still exact strings |
| `RentVsBuyResponse.*`, `HomeNpvResponse.*` (not yet live) | `ComputeRentVsBuy`, `ComputeHomeNpv` | entire response messages are double -- see proto comments on those messages |

### How to handle string fields safely

**Never `parseFloat()` or `Number()` a money string and then do arithmetic on
it.** `parseFloat("300000.123456789012345678")` silently truncates to
float64 immediately; summing amortization rows after that will not close the
schedule the engine closed exactly. Use a decimal library end to end and only
convert to a display string at the very last step:

```ts
import Decimal from 'decimal.js';

const res = await client.computePayment(req, {});
const payment = new Decimal(res.getValue());   // "-1798.651575458257198999", exact
const annual  = payment.times(12);              // still exact -- no float touched it
const display = payment.negated().toFixed(2);   // "1798.65" -- round ONLY here, for display
```

What **not** to do:

```ts
// WRONG -- silently corrupts the figure before you've used it once.
const payment = parseFloat(res.getValue());
const annual = payment * 12;
```

Rules of thumb:

1. Parse every `string` field into `Decimal` immediately on receipt; don't let
   the raw string sit around and get `+`'d by accident later.
2. Do all arithmetic (sums, differences, rate × principal, etc.) in `Decimal`.
3. Format with `.toFixed(2)` (or feed the already-rounded string to
   `Intl.NumberFormat`) only at the point you render it -- never mid-computation.
4. Requests carry the same fields back as strings: `req.setRate('0.005')`,
   not `req.setRate(0.005)`. In the JSON/transcoder surface this means quoted
   strings in the payload -- `"rate": "0.005"`, not `"rate": 0.005`. A
   malformed one (`"12x3"`) is refused with `INVALID_ARGUMENT` (verified live
   below) rather than silently parsed as `123`.
5. `double` fields are fine as plain JS numbers as-is -- wrapping them in
   `Decimal` would imply precision sensen never computed.
6. Amortization curves plotted on a chart can take floats for the *pixels* --
   that's fine, a chart axis isn't a ledger -- but every number actually
   printed on the page must come from the `Decimal` path.

### Sign convention

**`ComputePayment` returns a NEGATIVE value.** `-1798.651575458257198999` for
a $300,000 loan at 6%/30yr is correct, not a bug -- it follows the Excel
cash-flow convention sensen implements throughout (payment is money leaving
the borrower). Flip the sign (`.negated()` in `decimal.js`, or a leading `-`
strip) before displaying it as "your monthly payment is $1,798.65".

## Failure modes

| Symptom | Meaning | What to do |
| --- | --- | --- |
| `err.code === 3` (`INVALID_ARGUMENT`) | A field failed validation -- malformed decimal string, `compound_frequency=0`, ragged batch arrays, term over 1200 months | Fix the input; this is not retryable |
| `err.code === 12` (`UNIMPLEMENTED`) | The RPC exists in `finance.proto` but the *deployed* server doesn't have it yet | See "Known constraints" -- currently true for all six new home-finance RPCs |
| `err.code === 8` (`RESOURCE_EXHAUSTED`) | Quota exceeded (per-caller, or the shared anonymous bucket) | Message includes a `retry in <N>s`; honor it, don't tight-retry |
| `err.code === 16` / `7` | Only once the server enforces API keys (not the case today): 16 = no/bad/expired key, 7 = known key but wrong origin/scope | n/a today |
| Plain HTTP 429, `x-local-rate-limit: true`, **no gRPC status at all** | Envoy's site-wide local rate limit, not the quota system -- see "Known constraints" | Back off; this is not a `grpcWeb.RpcError`, it's a raw transport-level rejection your error handler needs to check for separately |

## Known constraints

- **Six RPCs are declared in the proto but not deployed yet.**
  `ComputeRefinance`, `ComputePayoffTiming`, `ComputeMortgageRecast`,
  `ComputeHomeFutureValue`, `ComputeRentVsBuy`, `ComputeHomeNpv` were added to
  `finance.proto` in commit `6764749` but the production server predates that
  commit. Calling any of them today returns `UNIMPLEMENTED` (gRPC status 12)
  over gRPC-Web -- **verified live**, see below. Over the JSON transcoder
  path the failure looks different and worse: the transcoder itself doesn't
  recognize the method and returns gRPC status **2** with
  `grpc-message: Missing :te header` -- read that as "method not deployed
  yet", not as a transport bug, if you ever debug through curl/JSON instead
  of the gRPC-Web client. Until these ship, compose the equivalent client-side
  from RPCs that do exist (double `ComputeAmortization` calls for payoff
  timing, `ComputePayment` on the reduced balance for a recast, etc.) -- ask
  in the parent repo if you need the exact recipe.
- **Site-wide rate limit, shared with every other caller of this API.**
  Envoy enforces a sustained 10 requests/second (burst 100) limit across
  *all* callers of *all* three services on this host -- not per-key, not
  per-origin. Exceeding it gets a bare HTTP 429 with no gRPC status (see
  "Failure modes" above), which will not look like a normal RPC error in
  most gRPC-Web client code. Batch UI interactions so a single user action
  doesn't fire more than a handful of calls at once, and don't poll this
  endpoint in a tight loop.
- **No API key is provisioned for this site yet.** Calls today are
  unauthenticated and share the site-wide anonymous quota bucket with every
  other unkeyed caller of this host, including anonymous visitors of
  optionsandfuturescalculator.com itself. That bucket is generous for normal
  interactive use, but a traffic spike on either site can throttle the other.
  Provisioning a dedicated `x-api-key` (tracked as separate work) gives this
  site its own quota, isolated from that shared bucket.

## Verified live

Run 2026-08-05 against production, low volume (a handful of calls, well
under the rate limit above):

**`ComputePayment`, exact response** (JSON transcoder path, showing headers):

```
$ curl -sS -X POST \
    https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputePayment \
    -H 'content-type: application/json' \
    -H 'Origin: https://mortgagefvcalculator.com' \
    -d '{"rate":"0.005","periods":360,"presentValue":"300000"}' -i

HTTP/2 200
access-control-allow-origin: https://mortgagefvcalculator.com
grpc-status: 0
content-type: application/json

{"value":"-1798.651575458257198999"}
```

**Same call, over the actual gRPC-Web binary path this client package uses**
(`node example/compute-payment.js`, no JSON involved):

```
ComputePayment -> -1798.651575458257198999
(negative = cash outflow; this is correct, not a bug)
```

**CORS preflight from this site's origin:**

```
$ curl -i -X OPTIONS \
    https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputePayment \
    -H "Origin: https://mortgagefvcalculator.com" \
    -H "Access-Control-Request-Method: POST" \
    -H "Access-Control-Request-Headers: content-type,x-grpc-web,x-api-key,authorization"

HTTP/2 200
access-control-allow-headers: ...,content-type,...,x-grpc-web,...,x-api-key,authorization
access-control-allow-origin: https://mortgagefvcalculator.com
access-control-max-age: 1728000
```

**`grpc-status: 12` (UNIMPLEMENTED) -- calling `ComputeRefinance`, one of the
six RPCs not yet deployed, over gRPC-Web** (this is the shape your error
handling should expect until that RPC ships):

```
grpc-web ComputeRefinance -> code=12 message=(empty)
```

Over the JSON path the same call instead surfaces as gRPC status 2 with
`grpc-message: Missing :te header` -- both are shown so this is recognized
as "not deployed yet" rather than misdiagnosed as a client bug in whichever
transport you're debugging with.

**Malformed decimal, refused rather than silently coerced:**

```
$ curl -i -X POST \
    https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputePayment \
    -H 'content-type: application/json' \
    -d '{"rate":"12x3","periods":360,"presentValue":"300000"}'

HTTP/2 400
{"code":3,"message":"rate is not a decimal number: \"12x3\"","details":[]}
```
