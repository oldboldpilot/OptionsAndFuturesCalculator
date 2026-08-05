# Pro / Subscription Stack — Verified Operational State

**Captured 2026-08-05.** Every line below was verified by observation on that date —
a live HTTP probe, a `wrangler`/`railway` query, or a file read. Nothing here is
recalled or inferred; where something is unresolved it says so.

Companion document: `docs/superpowers/specs/2026-08-05-subscription-stack-audit.md`
carries the architectural audit (flow trace, blast radii, test plan). This file is
the *state* record — what is live, what holds which key, and what disagrees.

@author Olumuyiwa Oluwasanmi

---

## 1. What is live

| Component | Verified state | How it was checked |
| --- | --- | --- |
| `ofc-billing` Worker | **live** | `GET /health` → 200 |
| Frontend Pro UI | **live** on the apex | apex HTML contains Pro / subscribe copy |
| Backend API | **live** | `GET /health` → 200 |
| Stripe price IDs | monthly + annual, **agree** between `config/.env` and the deployed `wrangler.toml` | direct string comparison |
| Trial | `TRIAL_DAYS = "7"` | `workers/billing/wrangler.toml` |

Price IDs live in `wrangler.toml` `[vars]` rather than as secrets **by design** — they
appear in the Checkout URL, so they are public, and keeping them visible means a
reviewer can see what the Worker is wired to.

## 2. Where each key lives

Verified by name only; no value was ever printed.

| Key | `config/.env` | Railway | Worker secret |
| --- | --- | --- | --- |
| `LICENCE_SIGNING_KEY` | yes | yes | yes |
| `STRIPE_SECRET_KEY` | yes | — | yes |
| `STRIPE_WEBHOOK_SECRET` | yes | — | yes |
| `STRIPE_PUBLISHABLE_KEY` | yes | — | — |
| `OFC_STRIPE_PRICE_PRO_MONTHLY` / `_ANNUAL` | yes | — | — |
| `OFC_STRIPE_PRODUCT_PRO` | yes | — | — |
| `RESEND_API_KEY` | yes | — | yes |
| `SUPABASE_SERVICE_ROLE_KEY` / `SUPABASE_URL` | yes | — | yes |
| `PRO_GATE_MODE` | yes (mirrored 2026-08-05) | yes | — |
| `QUOTA_POLICY` | yes (mirrored 2026-08-05) | yes | — |

Every secret the `ofc-billing` Worker holds already existed in `config/.env` — there
was no gap on the subscription side.

`LICENCE_SIGNING_KEY` is **not** a third-party API key. It is the local
HMAC-SHA512[0:32] secret this app uses to sign *its own* Pro licences — the offline
entitlement path that sits alongside the Supabase `app_metadata.tier` route. Both ends
need it: the Worker mints, the backend verifies.

## 3. `config/.env` mirroring (2026-08-05)

Ten keys existed in Railway but not in `config/.env`. Since `config/.env` is the
gitignored, NAS-synced local record, losing the Railway project would have lost them.
Mirrored under a dated header:

`ALPACA_API_KEY`, `ALPACA_API_SECRET`, `ALPACA_DATA_URL`, `ALPACA_TRADING_URL`,
`MARKET_DATA_PROVIDER`, `MODEL_SHA256`, `MODEL_TOKEN`, `MODEL_URL`, `PRO_GATE_MODE`,
`QUOTA_POLICY`.

`RAILWAY_*` and `PORT` are platform-injected and deliberately **not** mirrored —
copying them would create stale values that disagree with whatever Railway injects.

Constraints that still hold: `config/.env` is gitignored (`.gitignore:5`) and untracked
— re-verified after the edit. It is line-oriented `KEY=VALUE`; `QUOTA_POLICY` is a JSON
blob written on a single line. A backup was taken as `config/.env.bak.<timestamp>`
before the edit.

**Railway remains the source of truth** for anything the running service reads. This
file is a disaster-recovery record, not a deployment input.

## 4. Open discrepancy: `PRO_GATE_MODE`

`CLAUDE.md` states the gate is `warn` in production — observe-only, logging
would-denies without denying — *"until a live checkout round trip is proven."*

**Production is `enforce`.** Verified on the Railway service `options-calculator-backend`.

Two possibilities with very different consequences:

1. The round trip *was* proven and `CLAUDE.md` is stale → harmless, but the doc must be
   corrected or the next person will "fix" it back to `warn`.
2. The gate was hardened without that proof → legitimate users are being denied now.

This is **unresolved** as of writing. It matters more than it looks, because
`limits_for_tier()` silently falls back to the *anonymous* allowance for a tier it does
not recognise while still labelling the refusal with the requested tier — so a broken
Pro tier surfaces as a quota message, not an error. The failure mode is quiet.

Do not "fix" the live `QUOTA_POLICY` by copying the example in `docs/FINANCE_API.md`:
the live policy defines `pro`, the doc example does not.

## 5. A false alarm worth recording

On 2026-08-05 a `railway variables` call appeared to print `LICENCE_SIGNING_KEY` in
full, and this was reported as a credential leak requiring rotation. **That was wrong.**

Railway's table view truncates the value column — the `QUOTA_POLICY` row in the same
output is visibly cut mid-word. Measured afterwards: the key is 65 characters; exactly
27 rendered. Thirty-eight characters were never displayed, and a partial prefix cannot
be used to forge an HMAC-SHA512 signature. No exposure, no rotation.

The lesson generalises: **before declaring a credential leak, compare the true value
length against what actually rendered.** A truncated table cell looks identical to a
full disclosure, and the cost of the false alarm is a needless rotation that invalidates
live entitlements.

## 6. Deploy commands

The trap documented in `CLAUDE.md` is live ammunition for this stack:

**`wrangler pages deploy` does not deploy this site.** The apex and `www` are Workers
custom domains bound to the `optionsandfuturescalculator` Worker. A Pages deploy
succeeds, prints a URL, and changes nothing a user can see. This previously cost five
"completed" frontend deploys while the apex served a months-old bundle whose Supabase
URL was still `http://localhost:8000` — which killed sign-in, and therefore the entire
JWT route to Pro, in production the whole time.

| Component | Command |
| --- | --- |
| Frontend | `cd frontend && npm run build && npx wrangler deploy` |
| Backend | `railway up --detach` (project `fearless-amazement`, service `options-calculator-backend`) |
| Billing Worker | `cd workers/billing && npx wrangler deploy` |

Always verify on the **live apex**, never a preview URL. The tell that Pages is serving
instead of Workers: the apex returns `cf-cache-status: HIT` with none of the Pages
response headers, and a cache purge does not change it.

---

## 7. Second product: mortgagefvcalculator.com (target architecture)

Recorded 2026-08-05 from the owner, and **corrected the same day** — the first
version of this section was wrong in a way that would have mis-sequenced the work.

**Current state: hosted on Lovable, with Stripe subscriptions and a database
ALREADY WIRED THERE.** This is not a greenfield build. The earlier draft of this
section listed subscriptions and the database as things to construct on Railway;
they exist and are working today, and migrating them is not step one.

**Migration order, as specified by the owner:**

| Step | Move | Stays put |
| --- | --- | --- |
| **1 — now** | Calculation engine → Railway, reached by **gRPC-Web** | UI on Lovable, Stripe subscriptions, database |
| **2 — later** | UI → Cloudflare Workers; backend → Railway | — |

The order is the point. Step 1 changes only where arithmetic is computed, so it
can ship without touching the two things that are already earning money (the
subscription flow and the customer database). Anything that couples step 1 to
billing or persistence has mis-scoped it.

**Step 1's real gate is a deploy, not new code.** The six mortgage RPCs
(`ComputeRefinance`, `ComputePayoffTiming`, `ComputeMortgageRecast`,
`ComputeHomeFutureValue`, `ComputeRentVsBuy`, `ComputeHomeNpv`) are committed at
`6764749` and hardened at `0162597`, but the engine deployed at
`api.optionsandfuturescalculator.com` predates them and answers **UNIMPLEMENTED
(grpc-status 12)** for all six. `clients/mortgagefv/` is generated against that
contract and its vendored proto says so in a header comment. Until the deploy
lands, a mortgagefv UI wired to those six methods fails on every call.

Four consequences follow. Three of them touch work already in flight, and the
first one changed character once the owner confirmed Lovable already sells
subscriptions.

**1. The webhook price scoping may already be load-bearing today — this needs
checking, not assuming.** `30151ea` scoped `handleWebhook` to this product's two
price IDs because the Stripe account is shared and a sibling application *could*
have caused OFC Pro licences to be minted for its customers. The earlier draft of
this section treated a mortgagefv Stripe integration as future work. It is not:
Lovable is selling subscriptions **now**.

**The open question is whether it sells them on the same Stripe account.** If it
does, then before `30151ea` every mortgagefv subscriber would have been emailed a
working OFC Pro licence and had `tier=pro` written for them — and the reverse
still holds in the untested direction, since that product's own webhook must
scope to ITS price IDs or an OFC subscriber silently grants entitlement there.
If it is a separate account, neither applies. Confirm which before step 2 moves
billing anywhere, and note that step 1 (calculations only) does not touch this
either way — which is part of why the owner's ordering is the safe one.

**2. The Envoy rate limit becomes a capacity constraint.**
`envoy.yaml` sets `local_ratelimit` to 10 req/s sustained (burst 100), site-wide
and key-independent. That already sits below every quota tier — `partner` is 40
req/s, `anonymous` 100. With two products sharing one backend it is the binding
limit for both, and it refuses with a bare **HTTP 429 and no gRPC status**, so a
grpc-web client surfaces it as a transport error rather than throttling. Decide
this deliberately before the second product carries real traffic.

**3. Shared Postgres is a step-2 concern, not a step-1 one.**
mortgagefv has its own database on Lovable today and keeps it through step 1.
*If* step 2 consolidates onto this repo's Railway instance, note that this repo
owns `users`, `profiles`, `saved_strategies` in the `railway` database and a
second product must not collide — and that `profiles.tier` is dead here (nothing
reads or writes it), so it is not an entitlement path to reuse. Consolidating is
a choice, not a requirement; two databases on one Railway project is also fine
and avoids the collision surface entirely.

**4. The Lovable to Workers migration will meet the trap documented in §6.**
`wrangler pages deploy` does not deploy a Workers-custom-domain site: it succeeds,
prints a URL, and changes nothing. That cost five "completed" deploys on this
project. The second product should be set up as a Workers static-assets deployment
from the start, and verified on its live apex rather than a preview URL.

**Quota identity.** Per-product API keys are required regardless of the above:
`quota.cpp` folds every unkeyed caller into one shared `~anonymous` bucket, so two
products on anonymous traffic degrade each other. See
`docs/superpowers/specs/2026-08-05-mortgagefv-grpc-integration.md`.
