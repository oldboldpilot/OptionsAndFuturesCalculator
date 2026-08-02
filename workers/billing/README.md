# Billing Worker

Stripe Checkout, webhook handling and Pro licence minting for
Options & Futures Calculator.

| Route | Purpose |
| --- | --- |
| `POST /checkout` | Start a Checkout session. Body `{"plan":"monthly"\|"annual"}` |
| `GET /licence?session_id=cs_…` | Exchange a completed session for a licence |
| `POST /webhook` | Stripe events; mints and emails licences |
| `GET /health` | Liveness |

## Why a Worker and not the engine

The engine is a stateless gRPC compute service. None of this is compute — it is
HTTP routing, webhook signature verification and outbound email. Putting it
there would mean an HTTP router and an SMTP path inside a process whose job is
pricing options.

It is not in the frontend either: that is a static export, so it cannot hold the
Stripe secret key and cannot receive a webhook.

## Licences

A licence is `lk_live_<payload>.<signature>` — base64url JSON carrying the
Stripe customer, tier and expiry, signed HMAC-SHA512 truncated to 256 bits.

`src/licence.ts` is the **twin** of `verify_licence` in
`backend/src/modules/api_key.cpp`. The engine recomputes this exact HMAC over
this exact encoding, so the two must agree byte for byte — a padded base64, a
reordered JSON key or a different truncation makes every licence unverifiable.
**Change one side and you must change the other.** A cross-language test lives
in the session notes; the short version is: mint with this Worker, present the
token to the engine with `PRO_GATE_MODE=enforce`, and a 4-leg strategy must be
admitted.

Signed rather than stored, so the engine needs no database, no lookup and no
cache invalidation, and this Worker can issue a licence without the engine being
redeployed or told anything.

Revocation is by expiry. A cancellation stops the next issuance rather than
killing the current licence — a customer who paid for the month keeps the month.

## Deploying

The ordering matters, because the webhook secret does not exist until the
endpoint does.

```bash
cd workers/billing
npm install

# 1. Secrets. LICENCE_SIGNING_KEY must be the SAME value the engine has.
wrangler secret put STRIPE_SECRET_KEY      # rk_live_… from config/.env
wrangler secret put LICENCE_SIGNING_KEY    # openssl rand -base64 48
wrangler secret put RESEND_API_KEY

# 2. Deploy, to get the URL.
wrangler deploy

# 3. Register the webhook in Stripe against <worker-url>/webhook, subscribing to
#    ALL FOUR of:
#      checkout.session.completed
#      customer.subscription.created
#      customer.subscription.updated
#      customer.subscription.deleted   <- do not omit this one
#
#    Stripe only delivers events you subscribe to. A licence needs no
#    cancellation event because it expires on its own, but the Supabase tier is
#    stored rather than signed and has no expiry -- so without the deleted
#    event a cancelled subscriber keeps Pro indefinitely. The worker has always
#    handled it; it just never arrives if it is not registered.
#
#    Stripe then shows a signing secret.
wrangler secret put STRIPE_WEBHOOK_SECRET  # whsec_…
wrangler deploy                            # again, so it picks the secret up
```

Then on Railway, set the engine's side:

```
LICENCE_SIGNING_KEY=<the same value>
PRO_GATE_MODE=enforce
```

Until `PRO_GATE_MODE` is set the gate is inert and every strategy stays free —
which is the correct default, because turning a feature people already use into
a paid one should not happen as a side effect of a deploy.

And in the frontend, `NEXT_PUBLIC_BILLING_URL` must point at the deployed
Worker.

## Verified behaviour

```
GET  /health            200
GET  /nope              404
GET  /licence bad id    400  missing or malformed session_id
POST /webhook unsigned  400  invalid signature
POST /webhook forged    400  invalid signature
POST /webhook signed    200
POST /webhook replayed  400  invalid signature   (5-minute tolerance)
CORS, unlisted origin        never echoed back
```

The webhook URL is public, so the **signature is the authentication**. Without
verification this endpoint would mint Pro licences for anyone who can POST to
it. The replay window matters for the same reason: without it, one captured
legitimate request stays valid forever.
