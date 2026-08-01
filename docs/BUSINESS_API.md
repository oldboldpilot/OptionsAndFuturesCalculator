# Handing the API to a customer

How to issue a key, set that customer's limit, and tell them how to call.

Two audiences, one engine: a **website** embedding the calculator in a browser,
and a **business** calling from their own servers. They differ only in which
transport they use and which kind of key they hold.

## 1. Issue the key

```bash
calculator_engine issue-key --id acme-risk --rpm 600 --cu 50000
```

```
API key issued for 'acme-risk'.

  key       sk_live_gCvkqUP5yEOA_PQ9ALHGxPlCzY8_xmtdp7B1bVXam9A
  kind      secret (server-side only)
  tier      business
  limit     600 requests/minute, 50000 compute units/hour  (on the key, overrides the tier)
  scopes    finance
  expires   never

Add to FINANCE_API_KEYS on the engine service:

  "26de79...e41c": {"id": "acme-risk", "tier": "business", "type": "secret",
                    "requests_per_minute": 600, "compute_units_per_hour": 50000,
                    "scopes": ["finance"]}
```

`--merge` prints the complete `FINANCE_API_KEYS` value with the new entry
already spliced into the existing one, which is what you want when pasting into
Railway:

```bash
FINANCE_API_KEYS="$(railway variables get FINANCE_API_KEYS)" \
  calculator_engine issue-key --id acme-risk --rpm 600 --merge
```

The key is printed once and never stored. Only its SHA-512 digest is
configured, so a stolen configuration file yields nothing usable — and a lost
key cannot be recovered, only replaced.

Minting lives in the server binary on purpose. It uses the same `generate_key`
and `sha512_hex` the verification path uses, so the two cannot drift; a separate
script that appended a newline or emitted upper-case hex would produce keys
indistinguishable from forgeries at the door.

### Publishable vs secret

| | `sk_live_` secret | `pk_live_` publishable |
| --- | --- | --- |
| Where it lives | the customer's server | the customer's HTML |
| Who can read it | only them | every visitor to their site |
| Bound by | secrecy | **origin allowlist** |
| Sent with an `Origin` header | treated as **leaked**, refused | normal |

A publishable key is public by design, so `issue-key` refuses to mint one
without at least one `--origin`. Origin is the only thing binding it:

```bash
calculator_engine issue-key --id acme-widget --publishable \
  --origin 'https://acme.example' --origin 'https://*.acme.example' --rpm 120
```

A secret key arriving with an `Origin` header is refused rather than served,
because the only way it got into a browser is by being pasted into client-side
code.

## 2. Setting the limit

Two axes, because a request count alone is the wrong unit for this service.
`ComputePayment` is a handful of `__int128` operations; `PriceOptionMonteCarlo`
at a million paths is ~10⁹ RNG draws. They differ by six orders of magnitude, so
a customer well inside a per-minute limit can still saturate the engine.

| Flag | Unit | Catches |
| --- | --- | --- |
| `--rpm` | requests per minute | bursts, runaway retry loops |
| `--cu` | compute units per hour | expensive work at a reasonable request rate |

One compute unit ≈ one closed-form call. Either flag may be given alone. **Zero
means unlimited**, so omit a flag rather than passing `0` unless you mean it.

Limits written on the key override its tier, and are enforced whether or not
`QUOTA_POLICY` is configured — a limit you typed must never depend on a second,
unrelated variable to take effect. Omit both flags and the key follows its
tier's limits instead, which is the right choice when many customers share a
shape.

A refusal names where the number came from:

```
quota exceeded for tier 'business (per-key)' on ComputePayment (request rate); retry in 6s
```

`(per-key)` means the limit is on the key; without it, the limit came from the
tier in `QUOTA_POLICY`. `retry in 6s` is computed from the bucket's own refill
rate, so a client that honours it arrives exactly when it can be served.

## 3. How the customer calls

### JSON — the default for a business

No proto files, no codegen, no gRPC library.

```bash
curl https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputePayment \
  -H 'x-api-key: sk_live_...' \
  -H 'Content-Type: application/json' \
  -d '{"rate":"0.005","periods":360,"present_value":"300000"}'
```

```json
{"value": "-1798.651575458257198999"}
```

Errors are JSON, with the HTTP status you would expect:

| | |
| --- | --- |
| `401` | `{"code":16,"message":"no API key supplied (send it in the ` `x-api-key` ` header)"}` |
| `429` | `{"code":8,"message":"quota exceeded for tier 'business (per-key)' ... retry in 6s"}` |

The method path is `/<package>.<Service>/<Method>` — the same path gRPC uses.
Field names come straight from the `.proto`, so the JSON body and the gRPC
message are the same shape.

**Money is a string, not a number.** `sensen` computes in `BigDecimal` (exact
`__int128` fixed-point, eighteen places). JavaScript's `number` is a float64, so
a JSON number field would be lossy in the browser before anyone wrote a line of
code, and rounding compounds over a 360-period amortization. Fields that are
genuinely `double` in the engine are emitted as numbers; money is not.

### Native gRPC — for throughput

Strongly typed, multiplexed, and materially faster under load. Requires the
TCP-proxy endpoint, **not** `api.optionsandfuturescalculator.com` (see §4).

```go
conn, _ := grpc.Dial("grpc.optionsandfuturescalculator.com:PORT",
    grpc.WithTransportCredentials(credentials.NewTLS(&tls.Config{})))
```

The key goes in metadata as `x-api-key`, and is authenticated and metered
identically to the JSON path.

### gRPC-Web — for a browser

What the frontend uses. Works through `api.optionsandfuturescalculator.com`
today. Pair it with a **publishable** key and an origin allowlist.

## 4. Why native gRPC needs its own endpoint

Railway's HTTP edge terminates HTTP/2 and speaks HTTP/1.1 to the container, so
**HTTP/2 trailers are dropped**. gRPC carries its status entirely in trailers,
including on success — so a native gRPC call to `api.optionsandfuturescalculator.com`
returns a correct, complete response body and then hangs waiting for a
`grpc-status` that no longer exists. The client reports `Stream removed`, which
reads like the backend crashed.

Confirmed by differential test: byte-identical request, same Envoy config, same
engine. `grpc-status: 0` present locally, absent through Railway, message frame
identical in both. Railway documents this and points at their TCP proxy, which
forwards bytes without interpreting HTTP, so trailers survive.

gRPC-Web and JSON are unaffected — gRPC-Web encodes trailers into the response
body, and JSON has none. That is the entire reason gRPC-Web exists.

### Enabling it

1. **Railway → service → Settings → Networking → TCP Proxy**, targeting the
   port you will set as `GRPC_NATIVE_PORT` (e.g. `50052`). Railway returns a
   host and port.
2. Point `grpc.optionsandfuturescalculator.com` at that host as an **un-proxied**
   CNAME — the same three constraints as `api.` in `CLAUDE.md`, each of which
   broke that endpoint once.
3. Obtain a certificate for that name and set it on the service:

   ```
   GRPC_NATIVE_PORT=50052
   GRPC_TLS_CERT=<PEM chain>
   GRPC_TLS_KEY=<PEM private key>
   ```

TLS is **required**. Setting `GRPC_NATIVE_PORT` without certificate material
aborts startup rather than serving plaintext: a TCP proxy is reachable from the
public internet, and this endpoint authenticates with a long-lived key sent as a
header, so a single capture stays valid until somebody notices and rotates it.
`GRPC_ALLOW_PLAINTEXT=1` is the explicit escape hatch for a genuinely private
port, and says so loudly in the logs.

## 5. Rolling out

`FINANCE_REQUIRE_KEY` is staged, because a change that starts refusing traffic
must not be switched on blind:

| Value | Behaviour |
| --- | --- |
| unset / `observe` | serve everything, log what would have happened |
| `warn` | serve everything, log loudly |
| `enforce` | refuse |

Run in `observe` long enough to see whether anything legitimate is about to
break, then `enforce`. The logs answer that with data instead of a guess.

## 6. Revoking

Set `"enabled": false` on the entry, or remove it, and restart. For a key issued
against a subscription, prefer `"expires": "YYYY-MM-DD"` — it stops working on
its own, and a lapsed customer needs no intervention.

## 7. Checking it works

```bash
SMOKE_TLS=1 \
SMOKE_API_KEY=sk_live_... \
SMOKE_KEY_LIMIT_KEY=sk_live_customer... SMOKE_KEY_LIMIT_RPM=600 \
  ./smoke_client api.optionsandfuturescalculator.com:443 SPY finance
```

`check_key_limit` asserts the ceiling is real **and** that it is the right one —
that calls stop at roughly the figure on the key, not at some tier default the
key was silently falling back to. A limit enforced at the wrong number is as
much a billing defect as one that never refuses.
