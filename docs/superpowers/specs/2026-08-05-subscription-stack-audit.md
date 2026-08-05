# Subscription Stack Audit — Architecture, Findings, Work Units, Test Plan

**Date:** 2026-08-05.
**Scope:** Pro tier end to end — billing Worker, frontend Pro UI, engine gate, quota,
licences, Supabase entitlement.
**Method:** read-only. Every claim is labelled **VERIFIED** (observed in code, git, or a
live read-only probe on 2026-08-05) or **INFERRED** (reasoned, not observed). No state
was mutated. No credential value appears in this document; keys are referred to by NAME.

Companion: `docs/SUBSCRIPTION_STACK_STATE.md` (state record, same date) — this file is
the architectural audit that document points to.

---

## 0. Headline findings (read these first)

1. **The purchase path is dead in production.** The live bundle on the apex calls
   `https://ofc-billing.optionsandfuturescalculator.workers.dev`, which is **NXDOMAIN**.
   The Worker actually lives at `https://ofc-billing.muyiwamc2.workers.dev` (health 200).
   Every "Start free trial" click fails with a network error; so does the post-checkout
   licence claim. **VERIFIED** (dig @1.1.1.1 → NXDOMAIN authoritative; live chunk
   `3xsnu_mlycfsr.js` on the apex contains the dead hostname; the working hostname
   answers `{"ok":true,"supabaseConfigured":true}` and a CORS preflight from the apex
   origin returns 204).
2. **No checkout round trip has ever completed.** Stripe reports **zero subscriptions,
   ever** (`status=all`) on both OFC price IDs. **VERIFIED** (read-only
   `GET /v1/subscriptions?price=...&status=all` with the restricted key, both prices,
   count 0). The condition CLAUDE.md attaches to `enforce` — "until a live checkout
   round trip is proven" — is **not met**.
3. **`PRO_GATE_MODE=enforce` is live** on Railway service `options-calculator-backend`.
   **VERIFIED** (`railway variables --service options-calculator-backend`). It denies no
   *paying* customer, because there are none (finding 2) — but combined with finding 1
   it means: multi-leg strategies and the assistant are hard-paywalled while nobody can
   buy Pro through the site. Signed licences and JWTs do unlock it (the gate matrix
   passed 13/13 against production on 2026-08-03), so the gate itself is correct; the
   *acquisition* path is what is broken.
4. **The billing Worker processes every subscription event on a shared Stripe
   account.** `handleWebhook` filters by event *type* only — never by price or product.
   The Stripe account hosts at least two other applications' webhook endpoints
   (**VERIFIED**: `GET /v1/webhook_endpoints` lists two `lovable.app`/`lovable.dev`
   endpoints subscribed to `customer.subscription.*` and `checkout.session.completed`).
   If any sibling app sells a subscription on this account, **our Worker will mint that
   customer an OFC Pro licence, email it to them, and write `tier=pro` onto any
   Supabase account matching their email**. INFERRED consequence from VERIFIED code
   (`workers/billing/src/index.ts:294-386` — no price/product check anywhere) and the
   VERIFIED shared-account webhook list. Whether a sibling app currently sells
   subscriptions there was not determined.
5. **Licence-key rotation instantly kills every outstanding licence** — there is no key
   versioning and no dual-key grace. The Supabase `app_metadata.tier` path is fully
   independent and survives rotation. **VERIFIED** in code (details in §2).

---

## 1. Question 1 — PRO_GATE_MODE: what enforce actually changes, and is it safe

### What reads the gate mode

**VERIFIED:** `pro_gate_mode()` in `backend/src/modules/api_key.cpp:389-394` maps
`PRO_GATE_MODE` ∈ {`2`,`enforce`} → Enforce, {`1`,`warn`} → Warn, anything else → Off.
Exactly two consumers exist (grep over `backend/src` confirms no others):

- `check_strategy_entitlement(identity, leg_count)` — `api_key.cpp:405-427`, called from
  `CalculateStrategy` at `backend/src/modules/calculator_service.cpp:1037` **after**
  authentication and **before** quota and any compute.
- `check_assistant_entitlement(identity)` — `api_key.cpp:429-450`, called from
  `ParseStrategy` at `backend/src/modules/assistant_service.cpp:2357` after the CHARGE
  macro (auth+quota) and before inference.

### Warn vs Enforce, precisely

**VERIFIED** from the two functions above:

| | Warn | Enforce |
| --- | --- | --- |
| Multi-leg (`legs > 1`) request from non-pro identity | logs `pro-gate would-deny: key=... legs=... tier=...` at error level, **returns OK** — request is served | logs `pro-gate deny: ...` at info level, returns **gRPC status 7 PERMISSION_DENIED**: "Multi-leg strategies are a Pro feature. Single-leg calls and puts remain free. This position has N legs." |
| `ParseStrategy` from non-pro identity | logs would-deny, serves | status 7: "The natural-language strategy assistant is a Pro feature. ... upgrade for assisted parsing." |
| Single-leg strategy, any caller | OK | OK (`leg_count <= 1` short-circuits) |
| Pro/partner identity (`is_pro`: tier == `pro` or `partner`) | OK | OK |

Nothing else changes. `PRO_GATE_MODE` does **not** affect quota, does not affect API-key
auth (`FINANCE_REQUIRE_KEY` is a separate variable, unset in Railway → Observe mode),
and does not affect JWT handling: an invalid/expired `Authorization: Bearer` header
hard-fails with UNAUTHENTICATED only when the *KeyRegistry* mode is Enforce
(`api_key.cpp:799-803`), which it is not — so today an expired session silently degrades
to anonymous, and the multi-leg request is then denied as anonymous. **VERIFIED.**

What a denied browser user sees: grpc-web surfaces status 7's message; the store's catch
at `frontend/src/store/useCalculatorStore.ts:792-794` puts `err.message` into `error`,
which the workspace renders. The UI also pre-labels: `StrategySelector.tsx:475-477`
shows a "Multi-leg strategies need Pro" marker on multi-leg strategies when `!pro`.
**VERIFIED** in code; the copy is present in the live bundle.

### The limits_for_tier() fallback

**VERIFIED — still true.** `backend/src/modules/quota.cpp`:

- `limits_for_tier()` (lines 95-100): unknown tier → returns the **anonymous** tier's
  limits (or zero limits if even anonymous is undefined).
- `charge()` (lines 256-263): the Decision is labelled `d.tier = tier` — the **requested**
  tier name — even when the limits actually applied were the anonymous fallback. The
  refusal message (`to_status`, line 382) then reads "quota exceeded for tier 'pro'..."
  while the numbers were anonymous's. The fallback is deliberate (comment at 253-255: a
  renamed tier must not become unlimited access), but it is silent — no log marks the
  substitution.

**However, the live QUOTA_POLICY does not trip it.** **VERIFIED** (read from Railway;
the policy is non-secret): it defines exactly `anonymous` (6000 rpm / 120,000 cu/hr,
shared), `free` (120 / 3,600), `pro` (600 / 240,000), `partner` (2400 / 1,200,000).
Every tier the system can currently assign — `free` (JWT default, `api_key.cpp:314`),
`pro` (licences and webhook writes), `partner` (accepted by `is_pro`) — is defined. The
CLAUDE.md warning stands: the doc example in `docs/FINANCE_API.md` lacks `pro`, and
copying it over the live policy would silently put every Pro subscriber on the *shared
site-wide* anonymous bucket while refusals still said "tier 'pro'".

### Is enforce safe right now?

- **Correctness of the gate: yes.** The deny/allow matrix was verified against the
  deployed engine over gRPC-Web on 2026-08-03: 13/13 with a liveness control — Pro
  allowed via both JWT and licence; free tier, `user_metadata` spoof, post-signature
  edits, expiry, `alg:none`, wrong-key, garbage all denied (**VERIFIED** via
  `docs/session_logs/session_2026-08-03_...md` §2; the probe passing with denies also
  proves enforce was already live on 2026-08-03, since Warn returns OK and would have
  failed every deny row).
- **Denying paying users: no paying users exist to deny** (finding 2). No free user with
  a legitimate entitlement can be denied either: entitlement only exists via licence or
  `app_metadata.tier`, both verified working.
- **Business safety: no.** Enforce turned on the paywall while the buy button is dead
  (finding 1). Anyone who wants Pro hits a broken checkout.

### When did it change to enforce, and was a round trip proven?

- **Bounded, not pinpointed.** `PRO_GATE_MODE` is a Railway environment variable — the
  change is in no git history. Evidence: the 2026-08-01 session log records setting it
  to `warn` ("stays at warn until the checkout round trip is proven", Still-open list);
  the 2026-08-03 gate probe behaviour proves enforce was live by 2026-08-03 ~14:00Z.
  **Flip happened between those two timestamps; no session log records the decision.**
  I could not determine the exact time or rationale without Railway's audit log
  (dashboard → project activity feed would settle it).
- **A checkout round trip was never proven.** Zero subscriptions on either price,
  `status=all` (**VERIFIED**). The 2026-08-03 gate test minted licences directly with
  the signing key (`scripts/mint_pro_gate_creds.mjs`) — it validated verification, not
  purchase. CLAUDE.md ("warn ... until a live checkout round trip is proven") is stale
  on the mode and correct on the condition.

---

## 2. Question 2 — LICENCE_SIGNING_KEY rotation blast radius

### Who touches the key

**VERIFIED:**

| System | Role | Where |
| --- | --- | --- |
| `ofc-billing` Worker | **mints** (`mintLicence`, HMAC-SHA512 truncated to 32 bytes, base64url, over payload `{"s":cus,"t":tier,"e":period_end+3d,"v":1}`) | `workers/billing/src/licence.ts:47-78`; secret set (name confirmed via `wrangler secret list`) |
| Railway engine | **verifies** — the only verifier anywhere | `backend/src/modules/api_key.cpp:329-368` (`verify_licence`), reached through the `x-api-key` header's `lk_live_` branch in `KeyRegistry::check` (line 620) |
| Frontend | decodes claims **unverified**, UI labels only | `frontend/src/lib/licence.ts:86-101` — grants nothing |
| `config/.env` (gitignored, **VERIFIED** via `git check-ignore`) | offline record, line 197 | not read by any running system |

The two implementations must agree byte-for-byte (unpadded base64url, fixed JSON key
order, 32-byte truncation) — documented in both files as twins.

### What rotation does

**VERIFIED from code:**

- `verify_licence` reads exactly **one** secret (`env_or("LICENCE_SIGNING_KEY","")`),
  recomputes the HMAC, and rejects on mismatch. The `"v":1` field is a payload-format
  version, not a key id — nothing selects among keys. **There is no key versioning and
  no dual-key grace mechanism.**
- Therefore the moment the engine's key changes, **every outstanding licence fails
  verification instantly**. The 3-day `GRACE_DAYS` in minting extends expiry, not key
  validity — it does not help here.
- The failure is confusing client-side: the UI reads claims without verifying, so the
  Pro badge stays lit (`isPro` checks tier+expiry only) while the engine returns
  status 7 on every multi-leg call. INFERRED from verified code paths.
- Re-issue paths after rotation: (a) the `GET /licence?session_id=cs_...` exchange
  re-mints — but the user no longer has their session id (the frontend deliberately
  strips it from the URL, `useProStatus.ts:133`); (b) the webhook re-mints and re-emails
  **only when Stripe next sends a subscription event** — i.e. at renewal, up to a month
  away. So a licence-only subscriber loses Pro until their next billing event unless
  licences are manually re-minted and re-sent.
- **The Supabase path is independent and survives rotation.** `app_metadata.tier` rides
  the GoTrue access token, verified against `SUPABASE_JWT_SECRET` (a different key,
  `api_key.cpp:253-327`). A signed-in subscriber whose tier was written keeps Pro
  throughout. **VERIFIED** (separate secrets, separate verify functions).
- Engine env note: Railway variable changes do not reach the running process until the
  service is redeployed/restarted — `pro_gate_mode()`/`env_or` read the process
  environment, fixed at start. INFERRED (standard Railway behaviour; the repo's own
  practice after variable changes is to redeploy).

### Safe rotation procedure (in order)

1. **Precondition — minimise the licence-only population:** list active subscribers
   (Stripe, read-only) and check each has a Supabase account with `app_metadata.tier =
   "pro"` (GoTrue admin GET, read-only). Anyone covered by the tier path feels nothing.
   (Today this set is empty — rotation right now breaks nobody. **VERIFIED**, zero
   subscriptions.)
2. Generate the new 256-bit key. Do not print it; write it to `config/.env:197` as the
   new record.
3. Update **both stores back-to-back, then restart the engine** — the update is not
   atomic and both orders have the same mismatch window, so make it short:
   `wrangler secret put LICENCE_SIGNING_KEY` (redeploys the Worker automatically), then
   `railway variables --set` equivalent via dashboard/CLI **followed by a service
   redeploy** so the engine process picks it up.
4. Re-mint licences for every active subscriber with the new key and re-email them
   (a small script against `mintLicence` + the Stripe subscription list + Resend),
   **or** consciously accept that licence-only users are broken until their next
   renewal webhook.
5. Verify: run `scripts/probe_pro_gate_web.py` against production with the **new** key
   in the environment — the licence rows must pass; then run it with the old key to
   confirm old-key licences now deny.

**Unavoidable breakage:** any licence minted before the rotation dies the moment the
engine restarts with the new key, and cannot be saved by ordering — only by the tier
path (unaffected) or re-issuance (step 4). Eliminating this class requires code:
teach `verify_licence` an optional `LICENCE_SIGNING_KEY_PREVIOUS` accepted for a
bounded window (Work Unit 6).

---

## 3. Question 3 — what is actually undeployed

Short answer: **nothing is missing from deployment; what is deployed is faithfully
running broken committed source.** "Deploy the UI and backend for subscriptions" is not
the fix — Work Unit 1 (fix the billing URL, then redeploy the frontend) is.

| Component | Deployed? | Current with committed source? | Evidence |
| --- | --- | --- | --- |
| `ofc-billing` Worker | **Yes** — live at `ofc-billing.muyiwamc2.workers.dev`, health 200, `supabaseConfigured:true`; all 6 secrets present by name | **Yes** — last deploy 2026-08-03T14:08Z; last commit touching `workers/billing/src` is 63374e8 (2026-08-03 07:08 PT); none since | **VERIFIED** (`wrangler deployments list`, `wrangler secret list`, git log) |
| Frontend (Workers static assets) | **Yes** — apex serves build chunk `3xsnu_mlycfsr`, identical to local `frontend/out`; Pro panel copy present in the live bundle | **Yes** — last deploy 2026-08-03T17:00Z; no commits under `frontend/` since. **But the committed source itself carries the dead billing hostname** (`frontend/src/lib/licence.ts:158` default; no `NEXT_PUBLIC_BILLING_URL` in `frontend/.env.production.local`), so a rebuild today reproduces the bug | **VERIFIED** (live chunk fetch + grep; git log) |
| Backend engine | **Yes** — last Railway deploy SUCCESS 2026-08-04T09:51Z | **Effectively yes for subscriptions** — last commits to `api_key.cpp` / `quota.cpp` / `calculator_service.cpp` are 2026-08-02, before the deploy. One later commit (9643b21, sensen bump, 2026-08-04 13:22 PT) post-dates the deploy but touches nothing subscription-related | **VERIFIED** (`railway deployment list`, git log per file) |
| Stripe webhook endpoint | **Yes** — enabled, pointing at `ofc-billing.muyiwamc2.workers.dev/webhook`, subscribed to exactly the four events the Worker handles | n/a | **VERIFIED** (read-only endpoint list) |
| GoTrue (Supabase auth) | **Yes** — `supabase-auth-production-c656.up.railway.app/health` → 200; this is the URL baked into the live bundle | n/a. Note: `auth.optionsandfuturescalculator.com` is **NXDOMAIN** — the custom domain in `infra/supabase/README.md` and the wrangler.toml comments was never attached. Docs are aspirational; the system works via the Railway URL | **VERIFIED** (curl, dig, bundle grep) |

**Correct deploy commands** (the CLAUDE.md trap is live: `wrangler pages deploy`
succeeds and deploys nothing users see — the domains are Workers custom domains):

- Frontend: `cd frontend && npm run build && npx wrangler deploy` — then verify on the
  **apex**, never a preview URL.
- Billing Worker: `cd workers/billing && npx wrangler deploy`.
- Backend: `scripts/railway_deploy.sh` (preferred — `railway up`'s 30 s upload deadline
  makes it a coin flip; the script exists precisely for this) or `railway up --detach`
  against project `fearless-amazement`, service `options-calculator-backend`.

---

## 4. Question 4 — end-to-end flow, hop by hop

Numbered hops; every file:function named; break-risk flagged. Status legend:
**OK-verified** (observed working), **BROKEN-now**, **UNVERIFIED** (code exists, never
exercised live).

1. **Upgrade click** — `frontend/src/components/ProPanel.tsx` `go(plan)` →
   `startCheckout(plan)` in `frontend/src/lib/licence.ts:161-171`:
   `POST {BILLING_URL}/checkout`. **BROKEN-now**: BILLING_URL default is the NXDOMAIN
   hostname and no env override is set in `.env.production.local`. User sees "Could not
   start checkout." Fix: Work Unit 1.
2. **Checkout session** — Worker `handleCheckout` (`workers/billing/src/index.ts:114-145`):
   picks `PRICE_MONTHLY`/`PRICE_ANNUAL` (real live price IDs in `wrangler.toml` [vars],
   **VERIFIED** they exist in Stripe by the subscription query answering for them),
   trial 7 days, success URL carries `{CHECKOUT_SESSION_ID}`. **OK-verified** at the
   API level (2026-08-01 session log: session creation succeeded with the restricted
   key); never driven from the UI.
3. **Stripe Checkout → payment** — Stripe-hosted. **UNVERIFIED** end-to-end (zero
   subscriptions ever).
4. **Return to site** — `useProStore.init` (`frontend/src/lib/useProStatus.ts:110-134`)
   reads `?checkout=success&session_id=`, calls `claimLicence(sessionId)`
   (`licence.ts:180-185`) → `GET {BILLING_URL}/licence`. **BROKEN-now** (same dead
   hostname). Failure is *masked politely*: UI says "Check your email — it is on its
   way", which is only true if the webhook+email leg works.
5. **Licence exchange** — Worker `handleLicence` (`index.ts:161-191`): re-fetches the
   session from Stripe, requires `complete` + (`paid` or `no_payment_required` — the
   trial case), mints via `mintLicence`. **UNVERIFIED** live (no session has ever
   existed); logic verified by reading.
6. **Webhook** — Stripe → `POST /webhook` → `handleWebhook` (`index.ts:294-386`):
   signature verified (`verifyStripeSignature`, `licence.ts:91-126`; replay window
   300 s; constant-time compare); on active subscription: `setSupabaseTier(email,"pro")`
   (`index.ts:213-292`, direct-GoTrue `/admin/users` paths — the Kong-prefix 404 trap is
   fixed and documented in-code) and `sendLicenceEmail` via Resend (`index.ts:393-431`,
   response now checked and logged). Downgrade on `deleted`/`canceled`/`unpaid`/
   `incomplete_expired` → `tier=free`; `past_due` → grace, no re-mint. **UNVERIFIED**
   live — the endpoint is registered and enabled (**VERIFIED**) but has never received
   a real event. Three silent-break risks, all mitigated to *logged*-break in current
   code: missing Supabase config (loud CONFIG ERROR), GoTrue 404s (logged), Resend
   failure (logged). One risk is not code: whether the Worker's `SUPABASE_URL` secret
   holds the **working** GoTrue URL (the Railway one) rather than the never-attached
   `auth.` custom domain — the value cannot be read; `/health` proves presence only.
   **I could not determine this**; a test checkout with `wrangler tail` open settles it.
   Plus the shared-account contamination defect — finding 0.4, Work Unit 2.
7. **Entitlement storage** — two parallel paths, by design:
   (a) `app_metadata.tier` on the GoTrue account (service-role write; browser-writable
   `user_metadata` is deliberately never read — enforced engine-side at
   `api_key.cpp:311-320`); bites on next token refresh, ≤ 1 h.
   (b) the signed licence, emailed and/or claimed, stored in
   `localStorage["ofc.licence"]` (`licence.ts:103-124`).
8. **Requests carry identity** — `authMetadata()` (`licence.ts:148-155`) attaches
   `x-api-key: lk_live_...` and/or `authorization: Bearer <supabase JWT>` on all four
   gRPC call sites in `useCalculatorStore.ts` (393, 455, 556, 717). **OK-verified**
   (code + live 13/13 matrix).
9. **Engine gate** — `KeyRegistry::authenticate` resolves Identity (JWT first, then
   licence/API-key channel; `api_key.cpp:758-861`), then `check_strategy_entitlement` /
   `check_assistant_entitlement`, then quota `admit_identity`. **OK-verified** live
   (2026-08-03, 13/13 incl. negatives).
10. **UI unlock** — `useProStatus()` returns `pro = isPro(licence) || accountTier ===
    'pro'`; `StrategySelector` drops the Pro markers; multi-leg requests now succeed.
    **One cosmetic defect (VERIFIED in code):** `ProPanel` renders the "active"
    panel only for `pro && licence` — a subscriber who is Pro via account tier alone
    still sees the subscribe pitch and trial button above the signed-in AuthUI block.

**Stub/TODO scan:** no TODO/stub markers in the billing Worker, licence code, or gate
code. The chain has no stubbed hop; it has one dead hostname (hop 1/4), one unexercised
leg (hops 3-6), and one unverifiable secret value (hop 6).

---

## 5. Work units

### WU1 — Fix the billing URL and redeploy the frontend (CRITICAL, first)

- **Files:** `frontend/.env.production.local` (add
  `NEXT_PUBLIC_BILLING_URL=https://ofc-billing.muyiwamc2.workers.dev`), and fix the
  default in `frontend/src/lib/licence.ts:158` to the same working hostname so the
  fallback is never a dead one. Optional, better long-term: attach a custom domain
  (e.g. `billing.optionsandfuturescalculator.com`) to the `ofc-billing` Worker in the
  Cloudflare dashboard and use that — it survives account-subdomain changes; if so,
  also add the origin nothing: `ALLOWED_ORIGINS` needs no change (it lists calling
  origins, not the Worker's own).
- **Commands:** `cd frontend && npm run build && npx wrangler deploy` (NOT
  `wrangler pages deploy` — see §3).
- **Risks:** editing `.env.production.local` is untracked local state — keep the
  `config/.env`-style discipline (it is gitignored via `.env.*`). Do not touch the
  Supabase URL lines: the comment block there is stale (§3) but the value is correct.
- **Acceptance gate:** on the live apex, the served bundle contains a billing hostname
  that resolves (`dig` NOERROR) and
  `curl -X POST -H 'Origin: https://optionsandfuturescalculator.com' <url>/checkout`
  returns 200 with a `checkout.stripe.com` URL. Clicking "Start free trial" on the apex
  lands on Stripe Checkout.

### WU2 — Scope the webhook to this app's prices (HIGH — shared Stripe account)

- **Files:** `workers/billing/src/index.ts` (`handleCheckout`: set
  `metadata[app]=ofc` and/or `subscription_data[metadata][app]=ofc` on the session;
  `handleWebhook`: before minting/writing tier, require the event's price to be
  `PRICE_MONTHLY`/`PRICE_ANNUAL` — subscription events carry `obj.items.data[].price.id`;
  `checkout.session.completed` needs either the metadata check or a line-items fetch).
  Return 200 "ignored" for non-OFC events (a non-2xx would make Stripe retry and
  eventually disable the endpoint — preserved behaviour).
- **Commands:** `cd workers/billing && npx wrangler deploy`.
- **Risks:** none to existing users (there are none); mis-filtering would drop real OFC
  events — cover with the test-mode plan (WU5) before live.
- **Acceptance gate:** a test-mode sibling-shaped event (subscription with a foreign
  price) is answered 200 with no licence minted and no tier write in `wrangler tail`;
  an OFC-price event still mints.

### WU3 — Decide the gate mode, and make docs agree with production

- Recommendation: keep `enforce` **only after** WU1 lands and WU5/WU7 prove a checkout;
  until then either (a) revert `PRO_GATE_MODE` to `warn` on Railway (honest per
  CLAUDE.md; requires service redeploy to take effect) or (b) keep `enforce` and accept
  a paywall nobody can pay through for the interim — given zero existing customers,
  (b) costs conversions, not correctness.
- **Files:** `CLAUDE.md` (the "PRO_GATE_MODE is warn" sentence) — update to the final
  state once WU7 completes; note the flip window (2026-08-01 → 2026-08-03) is otherwise
  unrecorded. If the exact flip time matters, the Railway dashboard activity feed is
  the only remaining source — **I could not determine it read-only from the CLI.**
- **Acceptance gate:** CLAUDE.md, `docs/SUBSCRIPTION_STACK_STATE.md` §4 and the live
  Railway value all say the same thing.

### WU4 — Make the quota fallback loud (LOW, code hygiene)

- **Files:** `backend/src/modules/quota.cpp` — in `charge()`, when `limits_for_tier`
  fell back (requested tier ∉ `tiers_`), log one error naming both tiers, and/or label
  the Decision `"<requested> (anonymous limits)"`. Keeps the deliberate fail-closed
  behaviour; removes the silence.
- **Acceptance gate:** unit test: charge with tier `"gold"` → refusal text/log names the
  fallback; existing tests unaffected. Deploy with the next backend release; no
  urgency (live policy defines all reachable tiers).

### WU5 — Stripe test-mode rig (prerequisite for WU2/WU7 negative coverage)

- **Files:** none in-repo necessarily; a staging Worker `ofc-billing-test` (same source,
  `wrangler deploy --name ofc-billing-test` or a second wrangler environment) with
  test-mode `STRIPE_SECRET_KEY`/`STRIPE_WEBHOOK_SECRET`, test-mode clones of the two
  prices, a test-mode webhook endpoint pointing at the staging Worker, and the **same**
  `LICENCE_SIGNING_KEY` as production — the signing key is local to this app, so
  test-minted licences verify against the production engine, which is exactly what lets
  the full chain be tested without money.
- **Risks:** the shared account means test-mode webhooks also fan out to siblings'
  test endpoints — harmless, but expect their logs to see events.
- **Acceptance gate:** `4242 4242 4242 4242` checkout on staging → `wrangler tail`
  shows signature-verified event → GoTrue admin GET shows `app_metadata.tier: "pro"` →
  emailed/returned licence passes `scripts/probe_pro_gate_web.py`'s allow row against
  production.

### WU6 — Dual-key rotation support (MEDIUM, before the first real rotation)

- **Files:** `backend/src/modules/api_key.cpp` (`verify_licence`: on primary-key
  mismatch, try `LICENCE_SIGNING_KEY_PREVIOUS` if set), `workers/billing/wrangler.toml`
  comment block, rotation runbook in `docs/SUBSCRIPTION_STACK_STATE.md`.
- **Acceptance gate:** unit tests: licence signed with old key verifies while
  `_PREVIOUS` is set, fails once unset; new-key licences verify throughout. Rotation
  procedure in §2 then loses its "unavoidable breakage" clause.

### WU7 — The live checkout round trip (the condition CLAUDE.md names)

See test plan §6, part C. Only after WU1 (and ideally WU2/WU5).

---

## 6. Question 5 — test plan

Evidence discipline for every step: capture the command, the timestamp, and the raw
response (headers included for HTTP) into `docs/session_logs/` or the PR description.
Redact nothing *but* credential values — session ids and event ids are fine and useful.

### A. Safe against production, today (read-only or free-tier traffic)

| # | Test | Command / action | Pass criteria | Evidence |
| --- | --- | --- | --- | --- |
| A1 | Gate matrix (incl. all negatives) | `LICENCE_SIGNING_KEY=... SUPABASE_JWT_SECRET=... scripts/probe_pro_gate_web.py https://api.optionsandfuturescalculator.com` (keys from `config/.env`; the script mints its own valid/tampered/expired/wrong-key credential matrix via the Worker's real `mintLicence`) | exit 0, `13/13`, liveness control ALLOWED; denies are grpc-status 7 only | stdout |
| A2 | Anonymous denied appropriately | covered by A1's free/anonymous multi-leg rows; single-leg anonymous row must ALLOW | row-level results | stdout |
| A3 | Tampered / expired / wrong-key licence rejected | covered by A1 (post-signature payload edit, expiry, wrong key, garbage, `alg:none`, `user_metadata` spoof) | all DENY with status 7 | stdout |
| A4 | Billing Worker up + CORS | `curl https://ofc-billing.muyiwamc2.workers.dev/health`; OPTIONS preflight with apex Origin | 200 `{"ok":true,"supabaseConfigured":true}`; 204 with ACAO echoing the apex | curl -i output |
| A5 | Live bundle points at a resolvable billing host (WU1 gate) | fetch apex chunks, grep `ofc-billing`; `dig` the hostname | NOERROR + A/AAAA records | grep + dig output |
| A6 | GoTrue up | `curl .../health` on the URL found in the live bundle | 200, GoTrue banner | curl output |
| A7 | Quota labels sane (optional) | burst free-tier calls past 120/min with a signed free JWT | RESOURCE_EXHAUSTED naming tier `free`, retry-after present | grpc-status + message |

Note on A1: it sends real (cheap, single/multi-leg) CalculateStrategy traffic and is the
designed release gate; it does not mutate server state beyond quota-bucket consumption.

### B. Stripe **test mode** required (WU5 rig)

| # | Test | Pass criteria |
| --- | --- | --- |
| B1 | Checkout session creation via staging Worker | 200, URL on `checkout.stripe.com`, session lists the test price, trial 7 days |
| B2 | Full test checkout (card 4242…) | redirect to `/?checkout=success&session_id=cs_test_...` |
| B3 | Licence claim | staging `GET /licence?session_id=` → 200 with `lk_live_`-prefixed token; token's decoded claims: `t:"pro"`, `e` ≈ period_end + 3 d |
| B4 | Claimed licence unlocks production engine | multi-leg CalculateStrategy with `x-api-key: <licence>` → OK (works because staging signs with the production `LICENCE_SIGNING_KEY`) |
| B5 | Webhook → tier write | `wrangler tail` on staging shows signature pass; GoTrue admin GET (service role, read) shows `app_metadata.tier:"pro"` for the account email |
| B6 | Email leg | Resend dashboard/API shows the licence email delivered (or the tail shows the exact failure — the silent-fail class is already converted to logged) |
| B7 | Downgrade | cancel the test subscription → `customer.subscription.deleted` → tier flips to `free`; a fresh JWT (after refresh) is denied multi-leg; the outstanding licence keeps working until `e` (expected, documented behaviour) |
| B8 | Cross-app filter (WU2) | replay/construct a test event with a foreign price id → 200 "ignored", no mint, no tier write |
| B9 | Bad signature | POST the webhook body with a wrong/absent `Stripe-Signature` → 400 |

### C. Live mode — the real checkout round trip (run once, after WU1; this is what
lets CLAUDE.md's condition be marked met)

1. From the live apex, signed in as a real (owner-controlled) account, click **Start
   free trial**, complete Checkout with a real card. The 7-day trial means the charge
   at signup is $0 (`payment_status: no_payment_required` — explicitly honoured by
   `handleLicence`).
2. Assert, in order, capturing evidence at each hop:
   - browser lands back on the apex; the panel flips to "Activating…" then PRO active
     (hop 4-5 working);
   - `wrangler tail ofc-billing` recorded `checkout.session.completed` with signature
     pass and no CONFIG/lookup errors (settles the `SUPABASE_URL` secret-value unknown
     from §4 hop 6);
   - Stripe dashboard: one subscription, status `trialing`, on the OFC price;
   - GoTrue admin GET: `app_metadata.tier:"pro"` on the account;
   - licence email arrives;
   - after a token refresh, a multi-leg strategy computes on the live site with **no**
     licence pasted (JWT path), and separately with only the pasted licence in a clean
     browser profile (licence path);
   - `railway logs` shows no `pro-gate deny` for this identity.
3. Teardown: cancel the subscription in Stripe before the trial ends (no charge ever
   lands). Assert the `deleted` webhook downgrades tier to `free` and the site returns
   to the gated state for that account.
4. Then: update CLAUDE.md and `docs/SUBSCRIPTION_STACK_STATE.md` §4 — the round trip is
   proven, `enforce` is justified, the discrepancy closes (WU3).

### What I could not determine read-only (named, with what settles each)

- The **value** correctness of the Worker secrets `SUPABASE_URL` /
  `SUPABASE_SERVICE_ROLE_KEY` / `STRIPE_SECRET_KEY` (presence verified by name only).
  Settled by: C.2's `wrangler tail` during the round trip (or B5 on staging).
- The exact **time/actor of the `PRO_GATE_MODE` flip** to enforce. Settled by: Railway
  dashboard activity feed.
- Whether the **Resend sending domain** is currently verified. Settled by: B6, or a
  read of the Resend dashboard.
- Whether any **sibling app actively sells subscriptions** on the shared Stripe account
  (governs WU2 urgency). Settled by: listing subscriptions without a price filter in
  the dashboard — deliberately not done here to avoid trawling another app's customer
  data.
