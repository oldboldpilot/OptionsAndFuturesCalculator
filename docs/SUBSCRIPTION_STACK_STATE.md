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
