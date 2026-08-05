# Calling this API — transports, auth, quota, numerics, errors

**The contract every consumer needs, in one place.** `docs/FINANCE_API.md` documents
*what* the finance functions compute. This document covers *how to reach them at
all* — which transport works, what to send, what the errors mean, and the two rules
that will silently corrupt your figures if you get them wrong.

Applies to `sensen.finance.Finance` (the general-purpose financial library, built
for reuse by other applications) and `calculator.OptionsCalculator` (this
application's own API). Both are served by one engine on one port.

Everything below was verified against the live service on 2026-08-05. Where a
claim is inference rather than observation, it says so.

@author Olumuyiwa Oluwasanmi

---

## 1. Transport — read this first

| Transport | Through `api.optionsandfuturescalculator.com` | Locally / Railway TCP proxy |
| --- | --- | --- |
| **gRPC-Web** | ✅ **the only thing that works** | ✅ |
| Native gRPC (HTTP/2) | ❌ **does not survive the ingress** | ✅ |

**Native gRPC does not reach the container through the public domain.** A native
client against `api.optionsandfuturescalculator.com:443` fails with `Stream
removed`, and *no corresponding request appears in the Railway logs* — the
connection dies at the ingress, not in the engine. Only gRPC-Web gets through.

This is the single most common integration mistake, because the failure looks like
a server bug rather than a transport one: your client connects, TLS completes, and
the stream is then dropped with no server-side trace.

- **Browsers and any external service** → gRPC-Web against the public domain.
- **Server-side callers who want plain HTTP** → a **JSON transcoder** is also
  available on the same URLs: send `Content-Type: application/json` with a JSON
  body and you get JSON back. Convenient, but read §3.1 before using it — it has a
  failure mode gRPC-Web does not.
- **Local development and in-cluster callers** → native gRPC works normally
  (`localhost:50051`, or the Railway TCP proxy).

Envoy terminates gRPC-Web and forwards to the engine over native gRPC. Its route
is a deliberate catch-all (`prefix: "/"`), so **every** service and method on the
engine is reachable without a per-method routing change — adding an RPC needs no
proxy edit.

## 2. Making a gRPC-Web call

```
POST https://api.optionsandfuturescalculator.com/<package>.<Service>/<Method>
```

Required headers:

| Header | Value |
| --- | --- |
| `content-type` | `application/grpc-web+proto` (or `application/grpc-web-text`) |
| `x-grpc-web` | `1` |
| `x-api-key` | your key, if you have one — see §4 |
| `authorization` | `Bearer <jwt>`, for user-scoped calls |

Both `x-api-key` and `authorization` are already in Envoy's `allow_headers`, so no
proxy change is needed to start sending them.

**Generating a client.** `backend/proto/finance.proto` is self-contained — it has
no imports, so you can vendor that one file. Use `protoc-gen-grpc-web` with the
`grpc-web` runtime; `scripts/gen_proto.sh` holds the exact flags and versions this
project already uses, and matching them avoids a runtime/stub version mismatch.

**CORS is currently open.** `envoy.yaml` sets `allow_origin_string_match` to
`safe_regex: ".*"`, verified live to echo back any `Origin` — including origins
that have nothing to do with this project. No allow-listing is required to call
from a new site today. This is a deliberate, reviewed position rather than an
oversight: no credentials or cookies cross that boundary (`allow_credentials` is
not set), so the exposure is quota consumption and market-data vendor cost, not
data access. Those are addressed by per-key attribution (§4), which an origin
allow-list would not solve anyway, since it only constrains browsers.

## 3. Errors — what each failure actually means

| Signal | Meaning | Typical cause |
| --- | --- | --- |
| `grpc-status: 0` | success | — |
| `grpc-status: 3` (`INVALID_ARGUMENT`) | your input was rejected | malformed decimal string, missing required field |
| `grpc-status: 7` (`PERMISSION_DENIED`) | entitlement refusal | a Pro-gated feature without entitlement |
| `grpc-status: 9` (`FAILED_PRECONDITION`) | the maths has no answer | payment below periodic interest; a solver that cannot converge |
| `grpc-status: 12` (`UNIMPLEMENTED`) | that method is not on the deployed build | your proto is newer than the server |
| `grpc-status: 13` (`INTERNAL`) | malformed request framing | a body that is not a valid gRPC-Web frame |
| **HTTP 429, no `grpc-status`** | **proxy rate limit** — see §5 | too many requests per second |

Two of these mislead if you are not expecting them:

- **`12` usually means version skew, not a missing feature.** If you generated
  stubs from a proto newer than what is deployed, the method genuinely does not
  exist on the server yet. Check the deployed build before assuming a defect.
- **HTTP 429 carries no gRPC status at all**, so grpc-web surfaces it as a
  *transport error* rather than a rate limit. If your client reports a network
  failure under load, check for 429 before looking anywhere else.

**Refusals are deliberate.** This API declines rather than guessing: a malformed
decimal, an absent compounding frequency, an underspecified bond, a ragged batch,
or a loan whose payment cannot cover its interest all produce an error, never a
plausible-looking number. Treat a refusal as correct behaviour and surface it;
do not fall back to a default.

### 3.1 The JSON path silently drops unknown fields

**A mistyped or omitted field on the JSON path does not error. It defaults, and the
service computes on the default.** Measured against production on 2026-08-05:

| Request | Response | HTTP |
| --- | --- | --- |
| `{"present_value":"300000","rate":"0.005","periods":360}` | `-1798.651575458257198999` | 200 |
| `{"presentvalue":"300000", ...}` (one typo) | `0.000000000000000000` | 200 |
| `{}` | `0.000000000000000000` | 200 |
| `{"present_value":"300000","periods":360}` (no rate) | **`-833.333333333333333333`** | 200 |

The last row is the one to worry about. A zero looks broken and gets investigated;
omitting the *rate* makes the service assume 0% and return principal ÷ periods — a
plausible, confidently wrong payment.

The cause is structural: Envoy's gRPC-JSON transcoder ignores unknown JSON fields,
and proto3 scalars have no presence, so an absent field is indistinguishable from
one explicitly set to its default.

**Consequences for you:**

- **Prefer generated gRPC-Web stubs over hand-written JSON.** Stub field names are
  checked at compile time, so this class of mistake cannot reach the wire. This is
  the main practical reason to generate a client rather than hand-roll HTTP.
- **If you use the JSON path, assert on the answer, not on HTTP 200.** A 200 with a
  zero — or a suspiciously round figure — means check your field names first.
- Note the empty-string distinction: because money fields are *strings*, `""` means
  absent while `"0"` means an explicit zero. That difference is one reason they are
  strings rather than numbers.

**Status: fixed in the code, not yet deployed.** Every required decimal field is now
refused with `INVALID_ARGUMENT` naming the field, rather than parsed as zero — the
last row above becomes `rate is required and was not supplied`. Genuinely optional
fields (`future_value`, PMI amounts, overpayments, `cash_out_amount`, `drawn_amount`,
extra/lump payments) still default to zero, because omitting them is a real question
and not a mistake.

The table above describes **the currently deployed build**, so treat it as live until
the deploy lands. Two things do not change either way: the structural cause is
Envoy's transcoder plus proto3's lack of field presence, so the same trap returns for
any future field that is added without an explicit required-check; and the advice to
prefer generated stubs stands, because a compile-time field-name check catches this
class before the wire, whereas a server-side check only catches the fields someone
remembered to mark.

## 4. Quota and identity — the part that surprises people

| Tier | req/min | compute-units/hr | scope |
| --- | --- | --- | --- |
| anonymous | 6000 | 120,000 | **shared site-wide** |
| free | 120 | 3,600 | per caller |
| pro | 600 | 240,000 | per caller |
| partner | 2400 | 1,200,000 | per caller |

**Every unkeyed caller shares ONE `~anonymous` bucket.** It is not per-user and
not per-site: it is a single site-wide allowance that all anonymous traffic draws
from, across every service on the engine. Two products calling anonymously
therefore degrade each other, and a third party driving traffic from their own
site spends *your* budget.

If you are a separate application or website, get your own key. That gives you a
bucket keyed to your identity rather than the shared one, and makes your usage
attributable.

Two traps worth knowing:

- **An unrecognised tier name silently falls back to the anonymous allowance**
  while still labelling refusals with the tier you asked for. A typo in a
  provisioned tier therefore looks like it worked and quietly delivers shared
  limits. Use a tier the live `QUOTA_POLICY` actually defines.
- **Do not copy the example policy from `docs/FINANCE_API.md` into the live
  config.** The live policy defines `pro`; that example does not.

## 5. The rate limit that binds before quota does

Envoy enforces a **local rate limit of 10 requests/second sustained** (burst 100),
**site-wide and independent of any API key**, ahead of the engine.

That is below every quota tier — `partner` is 40 req/s, `anonymous` is 100 — so
for anything above 10 req/s **the proxy limit is the real constraint, and a bigger
quota tier will not raise it.** It refuses with a bare HTTP 429 and no gRPC status
(§3).

Design your client for it: batch where the API offers a batch RPC, cache
aggressively, and back off on 429 rather than retrying immediately.

## 6. Numerics — the rule that silently corrupts money

**A money field is a decimal `string`. Numbers that are genuinely `double` are
`double`. The distinction is deliberate and per-field.**

A field is a `string` where the engine computes in `BigDecimal` — an exact
`__int128` fixed-point decimal with eighteen places — and a `double` where the
engine genuinely computes in `double`. Every field carries a comment in the proto
saying which it is and why.

Money is a string for two reasons. Rounding it to `double` compounds over a
360-period amortization. And this service is reachable from browsers, where
JavaScript's `number` **is** a float64 — so a `double` money field is already
lossy before the consumer writes a line of code.

**In JavaScript, therefore:**

```js
// WRONG — silently loses precision, and the loss compounds
const total = parseFloat(res.getPayment()) * 360;

// RIGHT — keep it exact, format only at the display edge
import Decimal from 'decimal.js';
const total = new Decimal(res.getPayment()).times(360);
element.textContent = total.toFixed(2);
```

Never `parseFloat` a money string and then do arithmetic on it. Parse it into a
decimal type, compute there, and convert to a display string only at the very
edge.

**Sign convention:** payment RPCs return a **negative** value — it is a cash
outflow. `ComputePayment(300000, 6%, 30y)` returns `-1798.651575458257198999`.
That is the full-precision decimal string, and it is exactly why the field is not
a `double`.

## 7. A live call, end to end

Verified against production on 2026-08-05:

```
POST https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputePayment
content-type: application/grpc-web+proto
x-grpc-web: 1

→ grpc-status: 0
  {"value":"-1798.651575458257198999"}
```

A CORS preflight from an unrelated origin returns `204` with that origin echoed in
`access-control-allow-origin` (§2).

## 8. Verifying an integration

1. `GET https://api.optionsandfuturescalculator.com/health` → `200`.
2. Call `ComputePayment` with a known loan; assert the value matches the
   closed-form annuity payment, not merely that a response arrived.
3. Send a deliberately malformed decimal; assert you get `grpc-status: 3` and that
   your client surfaces it rather than substituting a default.
4. Confirm your money handling: assert a returned decimal string survives your
   client round-trip **without precision loss** — the failure here is silent, so
   it must be asserted rather than eyeballed.

Do not load-test production to find the rate limit. It is 10 req/s (§5); take that
as given.
