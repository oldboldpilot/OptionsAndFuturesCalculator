# Options & Futures Profit Calculator - Master Architecture Guide

@author Olumuyiwa Oluwasanmi

This document outlines the system architecture, build commands, and deployment infrastructure for the Options & Futures Profit Calculator.

## System Architecture

```
[ Client Browser ]
      │
      ├──> Cloudflare Workers static assets (Frontend Web UI: Static Next.js Export)
      │      • Domain: https://optionsandfuturescalculator.com
      │      • WWW Alias: https://www.optionsandfuturescalculator.com
      │      • Verification URL: https://optionsandfuturescalculator.muyiwamc2.workers.dev
      │      • NOT Cloudflare Pages -- the Pages project still exists and still
      │        deploys, but NOTHING points at it. See Deployment Details.
      │
      ├──> Railway Container Ingress (gRPC-Web / HTTP Engine Proxy)
      │      • Public Custom Domain: https://api.optionsandfuturescalculator.com
      │      • Railway Service: options-calculator-backend
      │      • Engine: Native C++23 Options & Futures Engine + Envoy Proxy (Port 8080)
      │
      └──> Railway PostgreSQL Database
             • Internal Host: postgres.railway.internal:5432
             • Public TCP Proxy: monorail.proxy.rlwy.net:11453
             • Database: railway (Tables: users, profiles, saved_strategies)
```

## gRPC Surface

The engine serves **two** services on one port (`:50051` native, and through
Envoy's catch-all route as gRPC-Web). They are separate contracts, not separate
deployments.

| Service | Proto | Purpose |
| --- | --- | --- |
| `calculator.OptionsCalculator` | `backend/proto/calculator.proto` | This application's own API — strategies, legs, payoff curves, market data |
| `sensen.finance.Finance` | `backend/proto/finance.proto` | The general-purpose sensen financial library, exposed for reuse by other applications |
| `calculator.assistant.StrategyAssistant` | `backend/proto/assistant.proto` | Natural-language strategy parsing, served by a fine-tuned Qwen3-0.6B running in-process |

`sensen.finance.Finance` covers roughly fifty functions across sensen's
`financial.cppm`, `options.cppm` and `portfolio.cppm`: time value of money,
mortgage amortization (with tax deductions), HELOC, rental ROI, NPV/IRR/XIRR,
depreciation, bonds, T-bills, futures pricing and margin simulation, hedging,
commodity spreads, option pricing (trees, Black-Scholes with full Greeks, Monte
Carlo) and portfolio statistics/optimization.

**Numeric types are not uniform, and the difference is deliberate.** A field is
`string` where sensen computes in `BigDecimal` (an exact `__int128` fixed-point
decimal, eighteen places) and `double` where sensen genuinely computes in
`double`. Money is a decimal string because rounding it to `double` compounds
over a 360-period amortization, and because this service is reachable from
browsers where JavaScript's `number` *is* a float64 — a `double` money field is
lossy on the client before anyone writes a line of code.

Gate: `backend/src/smoke_client.cpp` (`check_finance`) verifies the answers
against independent identities — put-call parity, price/yield inversion,
schedule closure, and the closed-form annuity formula — not against figures the
engine produced earlier.

**Native gRPC does not survive the Railway ingress.** `smoke_client` against
`api.optionsandfuturescalculator.com:443` fails with `Stream removed`, and no
corresponding request appears in `railway logs` — only gRPC-Web reaches the
container. Verify production through the browser path or the logs; the native
smoke client works locally and against the Railway TCP proxy, not the custom
domain.

## Strategy assistant

A fine-tuned Qwen3-0.6B (QLoRA, rank 16, 95.0% params exact-match) converts a
plain-English request into calculator parameters. It runs **in-process**, Q8_0
on CPU, fetched at image build time from a private HF repo with the checksum
pinned — it cannot travel through `railway up`, which enforces an upload
deadline that 62 MB already failed.

The full training-to-serving chain — dataset generation, the QLoRA-vs-full
comparison that decided the recipe, merge/export/quantize, the Dockerfile's
build-time fetch (and its secret-mount trap on Railway's builder), and every
serving constraint below in more detail — is documented end to end in
`docs/STRATEGY_ASSISTANT_PIPELINE.md`. This section is the short version.

Three things about it are load-bearing and easy to get wrong:

1. **The model requires its training system prompt.** Without it, it reverts to
   stock Qwen3 and emits no `<params>` block at all.
2. **Qwen3 emits a `<think>` block on every response, including correct ones.**
   The block being *empty* is the signal the system prompt took. Treating its
   presence as failure rejects every valid answer.
3. **`n_gpu_layers` must be 0 on a CPU build.** `GenerationConfig` defaults
   `compute_backend` to `AUTO`, which sensen counted as a GPU request; that
   enables `on_device_sampling`, a contract only the CUDA decode block honours,
   and the CPU path then casts a raw float logit to a token id. Fixed upstream
   in sensen and independently here, so it does not depend on the pinned commit.

Concurrency comes from sensen's iteration-level scheduler, not threads —
`generate()` cannot be called concurrently, because `FeedForwardNetwork` holds
`mutable` scratch per instance rather than `thread_local`, so parallel calls
corrupt hidden state silently. One owner thread, one fused `forwardBatch` per
step, ~20 MiB marginal per user.

Known limit: the training set restricts futures roots to `ES` and `NQ`, so
commodity queries are out of distribution — the model answers `CND` for a crude
crack spread, which is not an instrument. Symbol validation refuses that, and
`crack_321` is gated out of the UI (the calculator prices it correctly; the
assistant was never taught it).

## Pro tier and quota

Entitlement flows through Supabase `auth.users.app_metadata.tier` (never
`user_metadata`, which is browser-writable) or a signed licence
(`HMAC-SHA512[0:32]`, base64url). `PRO_GATE_MODE` is `warn` in production —
observe-only, logging would-denies without denying — until a live checkout
round trip is proven. `profiles.tier` is dead; nothing reads or writes it.

`quota.cpp` collapses **every unkeyed caller into one shared `~anonymous`
bucket**. A per-user-looking anonymous limit is therefore a site-wide limit; it
is sized as what it is:

| tier | req/min | compute-units/hr | scope |
| --- | --- | --- | --- |
| anonymous | 6000 | 120,000 | shared site-wide |
| free | 120 | 3,600 | per caller |
| pro | 600 | 240,000 | per caller |
| partner | 2400 | 1,200,000 | per caller |

`limits_for_tier()` silently falls back to the *anonymous* allowance for a tier
it does not recognise while still labelling refusals with the requested tier.
The live `QUOTA_POLICY` defines `pro`; the example in `docs/FINANCE_API.md` does
not. Do not "fix" the live policy by copying the doc.

## Features & Capabilities

1. **Futures & Options Strategy Modeler:**
   - Options Strategies: Bull Call Spread, Bear Put Spread, Straddle, Strangle, Iron Condor, Call Butterfly, Covered Call.
   - Futures Strategies: Futures Outright Long/Short, Futures Calendar Spread, Inter-Commodity / Crack Spread, Covered Futures Call (FOP), Cash & Carry / Basis Trade.

2. **Interactive Symbol Selector:**
   - Live ticker search across Equities (`SPY`, `QQQ`, `NVDA`, `AAPL`, `TSLA`), Futures (`ES`, `NQ`, `CL`, `GC`, `ZB`), and Crypto (`BTC`, `ETH`).
   - Automated asset class classification (`EQUITY`, `FUTURES`, `CRYPTO`).
   - Fast-select preset pills and custom spot price simulation.

3. **Full Complement Market Chains:**
   - **Options Chain:** Dynamic ITM/ATM/OTM strikes (15+ strikes), Bids, Asks, Deltas, Volumes, Open Interests, IVs, and Weekly/Monthly/Quarterly expiration date filters.
   - **Futures Term Structure Chain:** Contract codes (`ESU26`, `ESZ26`, `ESH27`), delivery months, days to expiry, forward prices, basis vs spot, cost of carry yield (% p.a.), volume, and open interest.

## Deployment Details

- **Frontend Deployment:** `cd frontend && npm run build && npx wrangler deploy` — a
  **Workers static-assets** deployment (`frontend/wrangler.toml`, `[assets] directory = "./out"`).

  **`wrangler pages deploy` does not deploy this site.** The Pages project
  `optionsandfuturescalculator` still exists and still accepts deployments, so the
  command succeeds and prints a URL — but the live domains are **Workers custom
  domains** bound to the `optionsandfuturescalculator` Worker, so a Pages deploy
  changes nothing a user can see. This cost five "completed" frontend deploys: each
  updated Pages while the apex kept serving a months-old bundle whose Supabase URL
  was still `http://localhost:8000` with a placeholder anon key, so sign-in — and
  therefore the JWT route to Pro — was dead in production the whole time.

  The tell, if it happens again: the apex returns `cf-cache-status: HIT` with **none**
  of the Pages response headers (`access-control-allow-origin`, `referrer-policy`,
  `x-content-type-options`) that `pages.dev` returns, and a cache purge does not change
  it. The apex/`www` DNS records are `AAAA 100::` (the discard prefix Cloudflare writes
  for a Workers custom domain), not a CNAME. Confirm with
  `GET /accounts/{acct}/workers/domains` — if the hostname is listed there, Pages is
  not serving it and never was.

  Do not "fix" this by attaching the domains to the Pages project: while the Workers
  custom domain owns the hostname, the Pages attachment sits at `status=pending`
  forever and does nothing.
- **Backend Deployment:** Railway CLI (`railway up --detach` linked to project `fearless-amazement` service `options-calculator-backend`).
- **Database Schema:** Applied `backend/migrations/01_init.sql` to Railway Postgres via `psql`.

### DNS (Cloudflare zone `optionsandfuturescalculator.com`)

Recorded here because none of it is otherwise represented in the repository.

| Record | Target | Proxied |
| --- | --- | --- |
| `optionsandfuturescalculator.com` AAAA | `100::` (Workers custom domain) | yes |
| `www` AAAA | `100::` (Workers custom domain) | yes |
| `api` CNAME | `3nw3v5qd.up.railway.app` | **no** |
| `_railway-verify.api` TXT | `railway-verify=8f82c9d1...` | n/a |

Three constraints on the `api` records, each of which broke the endpoint once:

1. The CNAME must target the **per-domain** endpoint Railway mints when the
   custom domain is attached (`3nw3v5qd.up.railway.app`), *not* the service
   domain `options-calculator-backend-production.up.railway.app`. Railway's
   router rejects the hostname otherwise and answers with
   `x-railway-fallback: true` and a 404.
2. It must stay **un-proxied**. Behind Cloudflare's proxy Railway resolves the
   record to anycast addresses, sees no CNAME, and can neither verify ownership
   nor complete the ACME challenge.
3. The `_railway-verify.api` TXT record must persist. It is what moves the
   certificate out of `VALIDATING_OWNERSHIP`, and removing it breaks renewal.

Attaching a custom domain needs an account-scoped Railway credential. If
`railway domain <name>` returns `Unauthorized` while read commands succeed,
`~/.railway/config.json` holds only a read-scoped `accessToken`; run
`railway login` or use the dashboard.

## Build Commands
- **Frontend Production Build:** `cd frontend && npm run build`
- **Frontend Dev Server:** `cd frontend && npm run dev`
- **Backend Docker Build:** `docker build -t options-backend backend/`
