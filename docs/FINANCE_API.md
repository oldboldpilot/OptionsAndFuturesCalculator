# Using `sensen.finance.Finance` from another site or service

The sensen financial library is served at:

```
https://api.optionsandfuturescalculator.com
```

Both call styles work against that one URL, verified live:

| Caller | Protocol | `content-type` |
| --- | --- | --- |
| Browser / any JS frontend | gRPC-Web | `application/grpc-web-text` or `application/grpc-web+proto` |
| Backend service, any language | native gRPC over HTTP/2 | `application/grpc` |

There is no separate host, port or auth for the two. Envoy fronts the engine and
routes by path prefix, so the same `/sensen.finance.Finance/<Method>` path serves
both.

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

## 3. Backend service (native gRPC)

Any gRPC language works. Use TLS on port 443 and the public hostname.

**Python**

```bash
pip install grpcio grpcio-tools
python -m grpc_tools.protoc -I. --python_out=. --grpc_python_out=. finance.proto
```

```python
import grpc
import finance_pb2 as pb
import finance_pb2_grpc as rpc

channel = grpc.secure_channel(
    "api.optionsandfuturescalculator.com:443", grpc.ssl_channel_credentials()
)
stub = rpc.FinanceStub(channel)

res = stub.ComputeAmortization(pb.AmortizationRequest(
    loan_amount="300000", annual_rate="0.06", term_months=360,
    monthly_overpayment="500",
))
print(res.summary.actual_term_months)      # 212 -- retired early
print(res.summary.total_interest_paid)     # exact decimal string
```

**Go**

```go
conn, _ := grpc.NewClient("api.optionsandfuturescalculator.com:443",
    grpc.WithTransportCredentials(credentials.NewTLS(&tls.Config{})))
client := pb.NewFinanceClient(conn)
res, _ := client.PriceBlackScholes(ctx, &pb.BlackScholesRequest{
    Spot: 100, Strike: 100, Rate: 0.05, Volatility: 0.2, YearsToExpiry: 1,
})
fmt.Println(res.Value, res.Delta, res.Vega)
```

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
| Options | `PriceOptionTree` (American/Bermudan/Asian) `PriceBlackScholes` (11 Greeks) `PriceOptionMonteCarlo` `ComputeProbabilityTree` |
| Portfolio | `ComputePortfolioStats` `OptimizePortfolio` `ComputeRiskContributions` |

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

## 8. Operational notes

- **No authentication.** Anyone with the URL can call it. Envoy applies a local
  rate limit; there is no per-caller quota or key. Do not put anything behind
  this that assumes the caller is trusted.
- **No streaming.** Every RPC is unary.
- **Shared with the calculator.** `calculator.OptionsCalculator` is on the same
  host and port. The two are independent contracts; a client needs only the
  proto for the one it uses.
