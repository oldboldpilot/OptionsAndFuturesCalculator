# Handoff — switching mortgagefvcalculator.com's AI assistant to `mortgage.assistant.MortgageAssistant`

@author Olumuyiwa Oluwasanmi

Companion to `MORTGAGEFV_INTEGRATION.md`, which covers the `sensen.finance.Finance`
compute RPCs. That document deliberately said nothing about the assistant. This
one covers only the natural-language layer, and assumes you have already wired
Finance.

Everything below was measured against production on 2026-08-08, not inferred
from the proto.

---

## 1. The one thing to understand first

**The assistant computes nothing.** It reads a sentence and answers with the
NAME of a `sensen.finance.Finance` operation plus that operation's parameters.
You then call that Finance RPC yourself to get numbers.

```
user sentence
   │
   ▼
ParseOperation ──► { operation: "ComputeAmortization", params: {...} }
   │                          │
   │                          ▼
   │                 POST /sensen.finance.Finance/ComputeAmortization
   │                          │
   ▼                          ▼
refusal / clarification    the actual numbers
```

If you are expecting one call that returns a payment figure, this is not that.
It is a router with a safety gate in front of it.

---

## 2. Endpoint

```
POST https://api.optionsandfuturescalculator.com/mortgage.assistant.MortgageAssistant/ParseOperation
Content-Type: application/json
x-api-key: <the same publishable key you already send to Finance>
```

Same host, same transport (Envoy's gRPC-JSON transcoder), same auth header as
the Finance RPCs. Nothing new to provision.

**Native gRPC does not work** against this hostname — it fails with
`Stream removed` and never reaches the container. Use the JSON/gRPC-Web path
above, which is what your browser code already does.

### CORS

Already configured for you. `https://mortgagefvcalculator.com` and its `www`
form are allowed, and `x-api-key` / `authorization` are in
`access-control-allow-headers`. A preflight from your origin returns 200.

---

## 3. Authentication — you are already entitled

Your key (`id: mortgagefvcalculator`) is `tier: partner`, which satisfies the
Pro entitlement the assistant requires. **Send the key and it works.** Omit it
and you get:

```json
{"code":7,"message":"The natural-language mortgage assistant is a Pro feature. ..."}
```

That is the only auth change from Finance: Finance tolerates an anonymous
caller, the assistant does not.

Your key's `scopes` were updated on 2026-08-08 from the default `["finance"]` to
`["finance","assistant"]`. It worked without that (server-side key enforcement
is in observe mode today), but would have started returning
`PERMISSION_DENIED: this API key is not entitled to the 'assistant' service` the
day enforcement is switched on. Nothing for you to do; recorded so the change is
not a mystery later.

---

## 4. Request

```json
{
  "utterance": "Amortization schedule for a 15-year loan of $250,000 at 5 percent.",
  "prior_clarification": ""
}
```

`utterance` is the user's own words, verbatim. Do not pre-parse, normalise or
"clean" it — normalisation is the model's job, and doing it twice lets the two
disagree about what the user meant.

`prior_clarification` is set ONLY when this request is the user's answer to a
`clarification` this same RPC returned a moment ago (see §6). Otherwise omit it.
It carries just that one question, not a conversation transcript.

---

## 5. Three outcomes, all of them normal

The response is a oneof. Exactly one field is present. **All three are success
responses at the HTTP level** — a refusal is not an error.

### 5.1 `params` — it understood

```json
{"params":{
  "operation":"ComputeAmortization",
  "params":{
    "loan_amount":"250000.00",
    "annual_rate":"0.0500",
    "term_months":"180",
    "monthly_overpayment":"0.00",
    "pmi_annual_rate":"0.0000",
    "original_home_value":"250000.00"
  }}}
```

`operation` is the Finance method name. `params` is a `map<string,string>` whose
keys are **exactly** that method's request field names — verified: all six above
match `AmortizationRequest` one-for-one.

So the follow-up call is a pass-through:

```
POST /sensen.finance.Finance/{operation}
body = the params map, unchanged
```

Note `term_months` is `int32` in the proto but arrives as the string `"180"`,
because the map is string-valued. proto3 JSON accepts a quoted integer for
int32/int64 fields, so passing the map straight through works. Do not convert
the money fields to numbers — see §8.

### 5.2 `clarification` — it needs one more fact

```json
{"clarification":{"question":"What rate should I use?"}}
```

Show the question. Send the user's reply back as a new `ParseOperation` call
with `utterance` = their reply and `prior_clarification` = the question you just
showed.

### 5.3 `refusal` — it will not guess

```json
{"refusal":{
  "reason":"OUT_OF_SCOPE",
  "message":"I don't give financial, tax or legal advice -- describe a specific calculation ... and I will work it out."}}
```

`reason` is one of `UNSUPPORTED_OPERATION`, `INVALID_PARAMETERS`,
`OUT_OF_SCOPE`, `MODEL_UNAVAILABLE`, `REASON_UNSPECIFIED`.

`message` is written to be shown to a user as-is. It is not a stack trace.

`MODEL_UNAVAILABLE` is the one to treat as infrastructure rather than as the
model's judgement — it means the service has no weights loaded. Do not surface
it as "I didn't understand you."

---

## 6. Design for refusal — it is the common case, not the edge

**The model is at 50% params exact-match on held-out data.** Measured through
this exact RPC, on the Q8_0 model that actually serves, not on a training-time
checkpoint.

Half the time it will refuse or ask rather than answer. That is the system
working, not failing. A UI that treats `params` as the happy path and everything
else as an error state will feel broken half the time.

Refusals are not the model being unsure — they are a verifier rejecting output
the model was confident about. Every number the model emits must trace back to a
figure in the user's own sentence (or a documented conversion of one: percent to
decimal, years to months, and six others). Anything that does not trace is
refused. Concretely, this is refused:

```
utterance : "... $2,158/month ..."
model said: current_monthly_payment = 2157.98
refusal   : "current_monthly_payment" = 2157.98 does not correspond to anything
            in the request (the nearest figure you gave is 2158)
```

That is a value the model quietly corrupted. It names a real operation, sits in
a real field, parses, satisfies every bound, and prices a **different loan**.
Nothing downstream could catch it, because nothing downstream knows what the
user said. This gate is the reason the assistant is safe to put in front of
someone's mortgage, and it is why the exact-match number is 50% rather than 95%.

Do not add a client-side retry loop that re-asks until it gets `params`. You
would be looping until the gate happens to miss.

---

## 7. Latency, measured

| outcome | observed |
| --- | --- |
| `params` | ~1.4 s |
| `clarification` | ~0.4 s |
| `refusal` (out of scope) | ~0.1 s |

It is a 0.6B model running on CPU, one request at a time per worker. Budget a
few seconds, show a pending state, and do not fire it on every keystroke.

---

## 8. Traps

1. **Money is a decimal string, everywhere.** `"250000.00"`, not `250000.00`.
   JavaScript's `number` IS float64, so parsing it loses precision that
   compounds over a 360-period amortisation. Keep it a string end to end; hand
   it to Finance as a string.
2. **proto-JSON emits `lowerCamelCase` on the wire.** It accepts `snake_case` on
   input, but Finance responses come back camelCased even though the proto is
   snake_case. The assistant's `params` MAP keys are exempt — map keys are data,
   not field names, so they stay `snake_case` exactly as shown.
3. **A gRPC-Web failure is an HTTP 200.** Never treat the status code as success.
   Check the body: a failure carries `code` and `message` at the top level, a
   real answer carries `params` / `clarification` / `refusal`.
4. **`RefinanceResponse` PMI drop-off months use `-1` as "never reached."**
   Rendered raw, your page will say "PMI drops off in -1 months." That is a
   Finance response field, not an assistant one, but it is the most common
   rendering bug in this API.

---

## 9. A complete call, verified live

```bash
curl -sS -X POST \
  https://api.optionsandfuturescalculator.com/mortgage.assistant.MortgageAssistant/ParseOperation \
  -H 'content-type: application/json' \
  -H "x-api-key: $YOUR_KEY" \
  -d '{"utterance":"Amortization schedule for a 15-year loan of $250,000 at 5 percent."}'
```

```json
{"params":{"operation":"ComputeAmortization","params":{
  "loan_amount":"250000.00","annual_rate":"0.0500","term_months":"180",
  "monthly_overpayment":"0.00","pmi_annual_rate":"0.0000",
  "original_home_value":"250000.00"}}}
```

Then, unchanged:

```bash
curl -sS -X POST \
  https://api.optionsandfuturescalculator.com/sensen.finance.Finance/ComputeAmortization \
  -H 'content-type: application/json' -H "x-api-key: $YOUR_KEY" \
  -d '{"loan_amount":"250000.00","annual_rate":"0.0500","term_months":"180",
       "monthly_overpayment":"0.00","pmi_annual_rate":"0.0000",
       "original_home_value":"250000.00"}'
```

---

## 10. How to verify your integration

1. Send a fully-specified request; assert you get `params`, and that
   `operation` is a method that exists on `Finance`.
2. Send `"What will my mortgage payment be?"`; assert you get `clarification`,
   NOT `params`. Params here would mean invented numbers.
3. Send `"Should I refinance? Is it a good deal?"`; assert `refusal` with
   `OUT_OF_SCOPE`. This service does not give advice.
4. Send a request whose figures you control, then assert every number in the
   returned `params` appears in your sentence or is a documented conversion of
   one. If you ever see a figure that does not, stop and report it — that is the
   one failure mode worth paging someone about.
5. Assert your money handling round-trips a decimal string without precision
   loss. This failure is silent and must be asserted, not eyeballed.
