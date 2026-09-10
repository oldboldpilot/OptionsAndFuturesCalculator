# Product Requirements Document (PRD)
@author Olumuyiwa Oluwasanmi
## High-Performance Options & Futures Profit Calculator Web Application

**Document Version:** 2.0.0  
**Author / Lead Architect:** Olumuyiwa Oluwasanmi  
**Date:** September 2026  
**Status:** Deployed and Serving in Production  
**Monetization Framework:** The Hub & Spoke Model (Phase 1: B2C Pro Subscriptions via Stripe & Cloudflare Workers; Phase 2: B2B White-Label SaaS & Calculation API)  
**Pro Tier Highlight:** Advanced Options & Futures Strategy Probability Calculator Engine  
**UI Design Foundation:** Lovable Blueprint & Next.js React/TypeScript Web UI  
**Backend & Database:** Supabase Auth (JWT) & Railway PostgreSQL (Row-Level Security)  
**Calculation Engine:** `sensen` (C++23 with libc++ and nanobind Python Bindings) — [github.com/oldboldpilot/sensen](https://github.com/oldboldpilot/sensen)  
**Engine Hosting Target:** Railway (Dockerized C++23 Microservices over gRPC & gRPC-Web via Envoy; native gRPC via Railway TCP proxy)  
**Governance & Policies:** [`config/cpp_details.txt`](config/cpp_details.txt) & [`config/update_policy.txt`](config/update_policy.txt)  
**Code Review & Verification:** Automated policy verification gate (`scripts/code_review_adversarial.sh`) and manual architectural sign-off  

---

## 1. Executive Summary & Vision

### 1.1 Overview
The **Options & Futures Profit Calculator** is an enterprise-grade web application designed to give retail traders, quantitative analysts, and financial institutions real-time, interactive profit-and-loss (P&L) matrix visualizations, risk probability models, and option sensitivity breakdowns. 

Inspired by the industry-standard UI/UX of [OptionsProfitCalculator.com](https://www.optionsprofitcalculator.com/), this platform elevates the web experience by combining:
1. **Frontend Web UI**: Modern, responsive React/TypeScript interface built on Next.js, exported as a static site (`output: "export"` in `frontend/next.config.ts:6`) and served from the edge via Cloudflare Workers static assets (`frontend/wrangler.toml:1-10`, `CLAUDE.md:12-17`), with dark-mode glassmorphic styling, state management via Zustand (`frontend/src/store/useCalculatorStore.ts:1-20`), and HTML5 Canvas data visualizations.
2. **Backend Services & Database**: Supabase Authentication issuing verified JWTs (`infra/supabase/00_bootstrap.sql:1-20`), Railway-hosted PostgreSQL with strict Row-Level Security (RLS) on saved user strategies (`backend/migrations/04_saved_strategies_rls.sql:1-20`), and Pro subscription billing and licence minting orchestrated via Cloudflare Workers (`workers/billing/README.md:1-15`).
3. **High-Performance C++23 Calculation Engine**: Sub-millisecond mathematical matrix generation powered by the **`sensen`** SIMD library ([github.com/oldboldpilot/sensen](https://github.com/oldboldpilot/sensen)) compiled with `clang++-23` and `libc++` (`config/cpp_details.txt:109`, `backend/Dockerfile:468`), hosted on **Railway** (`backend/Dockerfile:1-10`), communicating via **gRPC-Web** through an Envoy proxy on port 8080 and native **gRPC** over a Railway TCP proxy on port 50443 (`CLAUDE.md:94-110`).
4. **Pro Tier Strategy Probability Calculator**: Institutional-grade probability engine calculating Probability of Profit (POP), Probability of Target Profit (50% Max Profit), Probability of Touch, Expected Value (EV), and Black-Scholes-Merton net and per-leg Greeks (Delta, Gamma, Theta, Vega, Rho, Vanna, Volga, Charm) across 47 catalogued strategies in the backend validator (`backend/src/modules/strategy_catalogue.cppm:67-115`) and 26 static strategy pages in the frontend (`frontend/src/config/strategies.ts:10-37`).
5. **Phased Hub & Spoke Business Architecture**: 
   - **Phase 1 (The Hub - B2C Subscriptions & Lead Generation)**: High-intent consumer calculation tool offering free 1-leg analyses and Pro subscription access ($19/mo or $190/yr) for multi-leg strategies up to 20 legs (`backend/src/modules/calculator_service.cpp:485: kMaxLegs = 20`). Display advertising is strictly restricted to editorial strategy guides (`frontend/src/config/ad-routes.ts:10-25`).
   - **Phase 2 (The Spoke - B2B SaaS & Calculation API)**: White-label embeddable calculation widget (`/widget`) and raw C++ gRPC / gRPC-Web / REST calculation API endpoints (`sensen.finance.Finance` and `calculator.OptionsCalculator`) with token bucket rate and compute unit quotas (`backend/src/modules/quota.cpp:34-70`, `docs/API_SECURITY.md:1-60`).

---

## 2. Business Model & Phased Monetization (The Hub & Spoke Model)

To avoid structural conflicts (such as display ads cannibalizing high-value broker conversions or tracking scripts violating B2B tenant trust), monetization follows a strictly **phased Hub & Spoke architecture**:

```mermaid
graph LR
    subgraph Phase 1: The Hub (B2C Lead Generation)
        A[Consumer Options/Futures Calculator] -->|High-Intent Trade Setup| B[Broker CPA Lead Router<br/>Tastytrade / Schwab / IBKR / Alpaca]
        A -->|Premium Tools| C[Pro Subscription<br/>$19/mo or $190/yr]
    end

    subgraph Phase 2: The Spoke (B2B SaaS & API)
        D[White-Label Widget Embed] -->|FinTech / Media Sites| E[SaaS Licensing]
        F[Raw gRPC / REST Calculation API] -->|Developer Integration| G[Volume API Billing]
    end
```

### 2.1 Phase 1 (The Hub - B2C Lead Generation & High-Intent Referral)
* **High-Intent Broker Routing (CPA Lead Generation)**: 
  When a user builds an option spread or futures position, the platform provides an **"Execute via Partner Broker"** interface (`frontend/src/components/BrokerRouter.tsx:6-10`). This targets partner broker order tickets (e.g., tastytrade, Charles Schwab, Interactive Brokers). In the shipped build, broker deep-linking is implemented as a client-side prototype (`frontend/src/components/BrokerRouter.tsx:19-33`), while automated CPA lead tracking webhooks via Supabase Edge Functions were postponed in favour of direct Pro subscription monetization (`docs/architecture/CODE_MAP_EDGE_AND_CLIENTS.md:194`).
* **Zero Display Ads Policy**: Display ads are **omitted entirely** in Phase 1. Ad networks introduce third-party cookies, bloat page load times, and distract from the primary conversion funnels (Broker CPA + Pro Subscription).

  **As-built correction (August 2026):**  
  The initial zero-display-ads policy was superseded by a structured Google AdSense integration governed by Google's publisher-content policy. AdSense flagged the application on 2026-08-16 for serving ads on screens lacking publisher content, because all 26 calculator pages rendered identical UI controls without editorial text (`CLAUDE.md:2920-2940`). The policy is now strictly enforced by routing architecture:
  - **Editorial Guide Routes (`/guides/[strategy]`)**: The 26 distinct strategy guide articles carry substantive prose (767 to 954 words each, `CLAUDE.md:2986-2989`) and are the **only** pages that load `adsbygoogle.js` and render display ad slots (`frontend/src/app/guides/[strategy]/layout.tsx:1-25`).
  - **Ad-Free Tool & System Screens**: All interactive calculator screens (`/`, `/calculator/*`), embeddable widgets (`/widget`), the guides index (`/guides`), policy pages (`/privacy`, `/terms`), and 404 error pages carry **zero display ads** by design, enforced by an explicit allowlist (`frontend/src/config/ad-routes.ts:10-25: AD_ROUTE_PREFIX = '/guides/'`).
  - **Build Verification**: `frontend/scripts/check-export.mjs:1-50` sweeps every exported `.html` file in `out/` during `npm run build` and asserts that ad tokens appear on guide pages and nowhere else, failing the build if ad code leaks onto calculator screens.

* **Pro Consumer Subscription ($19/mo or $190/yr)**:
  * **Free Users**: Single options / 1 leg (`leg_count <= 1` admitted unconditionally in `backend/src/modules/api_key.cpp:460`), standard 30x30 matrix grid (`kDefaultPriceSteps = 30`, `kDefaultDateSteps = 30` in `backend/src/modules/calculator_service.cpp:430-431`), market data via Alpaca API (`backend/src/modules/market_data.cppm:23-44`).
  * **Pro Users**: Multi-leg strategies up to 20 legs (`kMaxLegs = 20` in `backend/src/modules/calculator_service.cpp:485`, gated by `check_strategy_entitlement` in `backend/src/modules/api_key.cpp:457-478`), 1st & 2nd order Greeks (Delta, Gamma, Theta, Vega, Rho, Vanna, Volga, Charm in `backend/proto/calculator.proto:130-140`), Probability of Profit (POP), Probability of Touch, Probability of Target Profit, Expected Value, Parametric VaR/CVaR (`backend/proto/calculator.proto:158-180`), saved strategy presets with PostgreSQL Row-Level Security (`backend/migrations/04_saved_strategies_rls.sql:1-80`), and natural-language strategy assistant parsing (`backend/proto/assistant.proto:1-30`).

### 2.2 Phase 2 (The Spoke - B2B SaaS & Calculation API)
Once the core C++23 `sensen` engine is battle-tested under production traffic:
* **White-Label Embedded Widget**: A sanitized, brandable iframe/web-component widget deployed at `/widget` (`frontend/src/app/widget/page.tsx`), verified by `frontend/scripts/check-export.mjs:1-50` to carry zero external scripts or ads. Managed via dedicated tenant API keys in the backend engine (`backend/src/modules/api_key.cppm:25-46`).
* **B2B Calculation API**: High-performance gRPC, gRPC-Web, and JSON endpoints (`sensen.finance.Finance` and `calculator.OptionsCalculator`) served at `https://api.optionsandfuturescalculator.com` (`docs/FINANCE_API.md:1-30`) and native gRPC over Railway TCP proxy at `tokaido.proxy.rlwy.net:34513` (`CLAUDE.md:94-110`). Requests are authenticated via publishable `pk_live_...` and secret `sk_live_...` keys with token-bucket rate limiting and hourly compute-unit quotas (`backend/src/modules/quota.cpp:34-70`, `docs/API_SECURITY.md:1-60`).

---

## 3. Pro Subscription Feature: Options & Futures Strategy Probability Calculator

The **Pro Strategy Probability Calculator** is a cornerstone feature of the Pro Subscription. Powered by `sensen`'s high-speed numerical methods, stochastic solvers, and Monte Carlo simulation routines, it displays probability metrics and distribution curves across complex options and futures strategies.

```mermaid
graph TD
    Input[Strategy & Market Parameters] --> Engine[sensen C++23 Probability Engine]
    Engine --> Heston[Heston Stochastic Volatility Model]
    Engine --> PDF[Log-Normal / Monte Carlo PDF Generator]
    Engine --> POP[Probability Metrics Solver]
    Engine --> EV[Expected Value & Risk Integrator]
    
    PDF --> UI[Interactive Distribution Overlay]
    POP --> UI_Table[POP Table: Profit %, Touch %, 50% Max Profit %]
    EV --> UI_Metrics[EV ($), Risk-Adjusted Return, VaR 95%]
```

### 3.1 Supported Strategy Matrix for Pro Probability Calculator

The backend engine validates all incoming strategy identifiers against an authoritative catalogue of **47 strategies** (`backend/src/modules/strategy_catalogue.cppm:67-115: inline constexpr std::array<StrategyInfo, 47> kCatalogue`), while the frontend implements **26 distinct strategy route pages** (`frontend/src/config/strategies.ts:10-37: STRATEGY_SLUGS`).

The table below reconciles the originally planned 30 strategy items with what is actually implemented in the codebase:

| # | Originally Planned Strategy | Backend Catalogue ID (`backend/src/modules/strategy_catalogue.cppm`) | Frontend Route Slug (`frontend/src/config/strategies.ts`) | Implementation Status | Notes & Bounds |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | **Long Call / Short Call** | `long_call` (line 68) | `long-call` (line 11) | **Implemented** | 1 leg; free tier admitted (`api_key.cpp:460`). |
| 2 | **Long Put / Short Put** | `long_put` (line 76) | `long-put` (line 12) | **Implemented** | 1 leg; free tier admitted (`api_key.cpp:460`). |
| 3 | **Covered Call / Protective Put** | `covered_call` (line 102), `protective_put` (line 104) | `covered-call` (line 24), `protective-put` (line 26) | **Implemented** | 2 legs each; requires Pro (`api_key.cpp:460`). |
| 4 | **Cash-Secured Put** | `cash_secured_put` (line 103) | `cash-secured-put` (line 25) | **Implemented** | 1 leg cash-backed put. |
| 5 | **Bull Call Spread / Bear Call Spread** | `bull_call_spread` (line 69), `bear_call_spread` (line 78) | `call-spread` (line 13), `bear-call-spread` (line 16) | **Implemented** | 2 legs debit/credit vertical calls. |
| 6 | **Bull Put Spread / Bear Put Spread** | `bull_put_spread` (line 70), `bear_put_spread` (line 77) | `bull-put-spread` (line 15), `put-spread` (line 14) | **Implemented** | 2 legs debit/credit vertical puts. |
| 7 | **Calendar / Diagonal Spread** | `calendar_spread` (line 99), `diagonal_spread` (line 100) | `calendar-spread` (line 28), `diagonal-spread` (line 29) | **Implemented** | Multi-expiry (`multi_expiry = true`); near leg defines curve horizon (`calculator.proto:193`). |
| 8 | **Double Diagonal Spread** | `double_diagonal` (line 101) | *(None)* | **Backend Only** | 4 legs; catalogued in C++, no dedicated frontend page route. |
| 9 | **Iron Condor (Short / Long)** | `iron_condor` (line 83), `reverse_iron_condor` (line 95) | `iron-condor` (line 19) | **Implemented** | 4 legs; 2 credit spreads. |
| 10 | **Iron Butterfly (Short / Long)** | `iron_butterfly` (line 87) | `iron-butterfly` (line 20) | **Implemented** | 4 legs; center pin risk at $K_0$. |
| 11 | **Standard Butterfly Spread** | `call_butterfly` (line 85), `put_butterfly` (line 86), `broken_wing_butterfly` (line 88) | `butterfly` (line 21) | **Implemented** | 3 legs; symmetric and broken-wing variants. |
| 12 | **Condor Spread (Call / Put)** | `condor` (line 84) | `condor` (line 22) | **Implemented** | 4 legs; four-strike call condor. |
| 13 | **Jade Lizard** | `jade_lizard` (line 91) | `jade-lizard` (line 27) | **Implemented** | 3 legs; short put + short call spread sized for zero upside risk. |
| 14 | **Big Lizard** | *(None)* | *(None)* | **NOT IMPLEMENTED** | Absent from `strategy_catalogue.cppm` and `strategies.ts`. |
| 15 | **Long / Short Straddle** | `long_straddle` (line 93), `short_straddle` (line 89) | `straddle` (line 17) | **Implemented** | 2 legs; volatility breakout / premium collection. |
| 16 | **Long / Short Strangle** | `long_strangle` (line 94), `short_strangle` (line 90) | `strangle` (line 18) | **Implemented** | 2 legs; OTM breakout / range capture. |
| 17 | **Call Ratio Spread (1x2 / 1x3)** | `call_ratio_spread` (line 74) | *(None)* | **Backend Only** | 2 legs; long near call vs 2 short calls; naked risk above wing. |
| 18 | **Put Ratio Spread (1x2 / 1x3)** | `put_ratio_spread` (line 81) | *(None)* | **Backend Only** | 2 legs; long near put vs 2 short puts; naked risk below wing. |
| 19 | **Ratio Backspread (Call / Put)** | `call_backspread` (line 71), `put_backspread` (line 79) | *(None)* | **Backend Only** | 2 legs each; long convexity on breakout. |
| 20 | **Collar & Risk Reversal** | `collar` (line 105), `risk_reversal` (line 72) | `collar` (line 23), `risk-reversal` (line 30) | **Implemented** | 3 legs (`collar`) and 2 legs (`risk_reversal`). |
| 21 | **Synthetic Long / Short Stock** | `synthetic_long` (line 73), `synthetic_short` (line 80) | *(None)* | **Backend Only** | 2 legs each; synthetic equity replication. |
| 22 | **Box Spread** | `box_spread` (line 92) | *(None)* | **Backend Only** | 4 legs; synthetic long + synthetic short arbitrage/financing. |
| 23 | **Outright Long / Short Futures** | `futures_long` (line 107), `futures_short` (line 108) | `futures-outright` (line 32) | **Implemented** | 1 linear futures leg; front-month contract. |
| 24 | **Futures Calendar Spread** | `futures_calendar` (line 109) | `futures-calendar-spread` (line 33) | **Implemented** | 2 futures legs along term structure; basis pricing. |
| 25 | **Futures Inter-Commodity Spread** | `spark_spread` (line 110), `crush_spread` (line 111) | `futures-intercommodity-spread` (line 34) | **Implemented** | 2 legs (`spark_spread`) and 3 legs (`crush_spread`). Note: `crack_321` in UI is refused by backend (`strategy_catalogue.cppm:30-40`). |
| 26 | **Covered Futures Call / Put** | `covered_futures_call` (line 113) | `covered-futures-call` (line 35) | **Partially Implemented** | Covered Futures Call is modelled (2 legs). Covered Futures Put is **NOT IMPLEMENTED**. |
| 27 | **Futures Option Straddle / Strangle** | *(None)* | *(None)* | **NOT IMPLEMENTED** | Absent as dedicated FOP structures in catalogue. |
| 28 | **Futures Option Iron Condor** | *(None)* | *(None)* | **NOT IMPLEMENTED** | Absent as dedicated FOP structure in catalogue. |
| 29 | **Futures Hedged Synthetic** | *(None)* | *(None)* | **NOT IMPLEMENTED** | Absent from catalogue and frontend routes. |
| 30 | **Custom Multi-Leg Pro Builder** | Arbitrary legs via `StrategyRequest` | Custom builder in UI | **Implemented** | Prices arbitrary combinations up to `kMaxLegs = 20` (`calculator_service.cpp:485`). |

**Additional strategies catalogued in the C++ backend (`backend/src/modules/strategy_catalogue.cppm`) not in the original PRD list:**
- `bull_call_ladder` (3 legs, line 75)
- `covered_put` (2 legs, line 82)
- `long_guts` (2 legs, line 96)
- `strip` (2 legs, line 97)
- `strap` (2 legs, line 98)
- `pmcc` (Poor Man's Covered Call, 2 legs, multi_expiry=true, line 106)
- `cash_and_carry` (2 legs, line 112)
- `min_variance_hedge` (2 legs, line 114)
- Frontend also includes routes for `futures-spread` (`strategies.ts:31`) and `futures-basis-arbitrage` (`strategies.ts:36`).

### 3.2 Pro Probability Analytical Metrics & Mathematical Models

The engine utilizes the **Black-Scholes-Merton (BSM)** framework for rapid baseline calculations and **Heston Stochastic Volatility / Monte Carlo paths** for deep out-of-the-money (OTM) tail-risk metrics.

1. **Probability of Profit (POP)**:
   $$\text{POP} = \mathbb{P}(\text{P\&L}(S_T) > 0) = \int_{\text{Profit Zones}} f(S_T) dS_T$$
   Implemented in `backend/src/modules/calculator_service.cpp:780-815` by numerical integration over the log-normal terminal price distribution.
2. **Probability of Target Profit (50% Max Profit)**:
   Calculated using first-passage time approximations for standard models (`backend/proto/calculator.proto:180: probability_of_target_profit`).
3. **Probability of Touch ($P_{\text{touch}}$)**:
   Probability that the underlying spot price touches a breakeven strike at any point prior to expiration (`backend/proto/calculator.proto:179: probability_of_touch`).
4. **Expected Value ($\text{EV}$)**:
   $$\text{EV} = \mathbb{E}[\text{P\&L}] = \int_{-\infty}^{\infty} \text{P\&L}(S_T) \cdot f(S_T) dS_T$$
   Reported in `backend/proto/calculator.proto:169`.
5. **Value at Risk (VaR 95% & 99%) & Conditional VaR (Expected Shortfall)**: Tail risk assessment computed from the CDF of simulated P&L distributions (`backend/proto/calculator.proto:158-163`).
6. **Interactive Price Distribution Visualizer**: HTML5 Canvas displaying log-normal PDF/CDF curves overlaid directly on the P&L payoff graph (`frontend/src/components/ProbabilityCurve.tsx`, drawn over the payoff in `frontend/src/components/PayoffLadder.tsx`).

---

## 4. UI Design Workflow & Supabase Backend Architecture

### 4.1 UI Implementation & State Management
- **Design Foundation**: Components prototyped on Lovable.dev and adapted into a high-performance React 19 / Next.js application under `frontend/`.
- **Frontend Architecture**: Next.js static export (`output: "export"` in `frontend/next.config.ts:6`), generating pre-rendered static assets into `frontend/out/` deployed to Cloudflare Workers (`frontend/wrangler.toml:1-10`, `CLAUDE.md:12-17`), rather than dynamic SSR.
- **State Management**: Zustand store (`frontend/src/store/useCalculatorStore.ts:1-20`) for atomic local state mutation (slider drag events for volatility/price matrices) without React re-render cascades.
- **Design Tokens (Tailwind + Vanilla CSS)**:
  - Background: Deep Dark Slate (`#0B0F17`) with glassmorphism panels (`rgba(255, 255, 255, 0.05)`).
  - Profit Color: Vibrant Emerald Green (`#10B981`) with glow accents.
  - Loss Color: Crimson Red (`#EF4444`).
  - Typography: Inter (UI) & JetBrains Mono (Financial Data Grids).

### 4.2 Supabase Backend Schema & Services

1. **Database Schema (PostgreSQL on Railway)**:
   - `users`: Managed by Supabase Auth (OAuth/Email) issuing signed JWT access tokens (`infra/supabase/00_bootstrap.sql:1-20`).
   - `profiles`: `id` (uuid, references auth.users), `tier` (text, e.g. 'free'/'pro'), `stripe_customer_id`, `updated_at` (`backend/migrations/06_user_tables_rls.sql:1-40`).
   - `saved_strategies`: `id` (uuid), `user_id` (uuid, references auth.users), `name`, `symbol`, `strategy_type`, `legs` (jsonb), `created_at`, `updated_at` (`backend/migrations/04_saved_strategies_rls.sql:1-25`, `backend/migrations/05_saved_strategies_auth_fk.sql:1-40`).
   - `state_assumptions`: `slug`, `name`, `abbr`, `median_price`, `property_tax_rate`, `median_rent`, `data_source`, `data_year` (`backend/migrations/07_state_assumptions.sql:1-50`).
   - `shared_permalinks` and `broker_lead_events`: Retained in historical migrations but superseded in production (`docs/architecture/CODE_MAP_EDGE_AND_CLIENTS.md:194`).
2. **Row-Level Security (RLS)**:
   - Enforced by PostgreSQL policies (`backend/migrations/04_saved_strategies_rls.sql:58-65`, `05_saved_strategies_auth_fk.sql:103-107`):
     ```sql
     CREATE POLICY saved_strategies_own_rows ON public.saved_strategies
         FOR ALL TO ofc_app
         USING (user_id = NULLIF(current_setting('app.current_user_id', true), '')::uuid)
         WITH CHECK (user_id = NULLIF(current_setting('app.current_user_id', true), '')::uuid);
     ```
   - The engine authenticates callers via the signature-verified `sub` claim of a Supabase access token (`backend/src/modules/api_key.cpp:67-93`), failing closed with `UNAUTHENTICATED` if no verified subject is present (`backend/src/modules/api_key.cpp:487-493`).
3. **Billing Infrastructure**:
   - Implemented as a dedicated Cloudflare Worker (`workers/billing/README.md:1-15`) handling Stripe Checkout sessions (`POST /checkout`), Stripe webhooks (`POST /webhook`), and cryptographic HMAC-SHA512 Pro licence minting (`lk_live_...`), verified directly in-engine by `backend/src/modules/api_key.cpp:28-40`.

---

## 5. C++23 Engine, Railway Microservice, & Performance

### 5.1 C++23 Engine Implementation Policy (`config/cpp_details.txt`)
All C++ code for the calculation engine adheres strictly to [`config/cpp_details.txt`](config/cpp_details.txt):
1. **Language Standard**: Pure C++23 compiled with `clang++-23` using `libc++` (`config/cpp_details.txt:109`, `backend/Dockerfile:468`). *(Note: Migrated from `clang++-22` on 2026-08-29; forward-compatible with C++26 per rule 60 of `config/cpp_details.txt:127`).*
2. **Modules Architecture**: Code structured as C++23 modules (`.cppm`) using `import std;` and `import sensen.*;` (`config/cpp_details.txt:68-69`).
3. **Memory Safety & Pointers**: **NO RAW POINTERS**. Uses smart pointers (`std::unique_ptr`, `std::shared_ptr`) and non-owning views (`std::span`, `std::string_view`) (`config/cpp_details.txt:18-20, 40-42`).
4. **Error Handling**: Railway-Oriented Programming (ROP) using `std::expected<T, std::error_code>` (`config/cpp_details.txt:55-56`).
5. **Threading & Parallelism**: Parallel matrix computation using Intel oneTBB (`tbb::parallel_for`, `config/cpp_details.txt:34-35, 106`).
6. **SIMD Waterfall Dispatch**: Dynamic runtime hardware detection (`AVX-512` -> `AVX2` -> `SSE4.2` -> `Scalar`) via per-function attributes (`config/cpp_details.txt:47-52, 109`).

### 5.2 Microservice Architecture & Performance SLA
- **Hosting**: Railway.app Dockerized containers (`backend/Dockerfile:1-10`, `railway.json:1-10`).
- **Proxy & Transports**:
  - Envoy Proxy translates browser gRPC-Web into HTTP/2 gRPC for the C++ backend on port 8080 (`backend/envoy.yaml:1-50`).
  - Native gRPC is exposed over TLS on port 50443 via Railway TCP proxy (`tokaido.proxy.rlwy.net:34513`, `CLAUDE.md:94-110`).
- **Market Data Integration**: Real-time quotes, implied volatility, and option chains provided via the Alpaca Market Data API (`backend/src/modules/market_data.cppm:23-44`), querying `/v2/stocks/{sym}/snapshot`, `/v1beta1/options/snapshots/{sym}?feed=opra`, and `/v2/options/contracts`.
- **Performance Targets & Constraints (SLA)**:
  - Standard Matrix Calculation (30x30): **< 5ms** p99 latency at the engine level.
  - Pro Monte Carlo Probabilities (10,000 paths): **< 45ms** p99 latency at the engine level.
  - Load-bearing bounds: maximum 20 legs per request (`backend/src/modules/calculator_service.cpp:485: kMaxLegs = 20`), maximum 20,000 grid cells (`backend/src/modules/calculator_service.cpp:446: kMaxGridCells = 20000`), and price steps capped between 3 and 200 (`backend/src/modules/calculator_service.cpp:439-440`).

### 5.3 gRPC Interface Specification (`calculator.proto`)
The canonical service definition from [`backend/proto/calculator.proto`](backend/proto/calculator.proto):

```protobuf
syntax = "proto3";

package calculator;

service OptionsCalculator {
  rpc CalculateStrategy (StrategyRequest) returns (StrategyResponse);
  rpc GetMarketQuote (QuoteRequest) returns (QuoteResponse);
  rpc GetMarketChain (ChainRequest) returns (ChainResponse);
  rpc GetRiskFreeRate (RiskFreeRateRequest) returns (RiskFreeRateResponse);

  // Saved scenarios (Pro-only, keyed to verified Supabase JWT subject claim)
  rpc SaveStrategy (SaveStrategyRequest) returns (SaveStrategyResponse);
  rpc ListStrategies (ListStrategiesRequest) returns (ListStrategiesResponse);
  rpc DeleteStrategy (DeleteStrategyRequest) returns (DeleteStrategyResponse);
}

message Leg {
  enum Action { BUY = 0; SELL = 1; }
  enum Type { CALL = 0; PUT = 1; FUTURE = 2; STOCK = 3; }

  Action action = 1;
  Type type = 2;
  double strike = 3;
  double expiration_days = 4;
  int32 quantity = 5;
  double premium = 6;
  double implied_volatility = 7;
  double contract_multiplier = 8;

  enum AsianType { NOT_ASIAN = 0; AVERAGE_PRICE = 1; AVERAGE_STRIKE = 2; }
  AsianType asian_type = 9;
  int32 averaging_states = 10;
}

message StrategyRequest {
  string underlying_symbol = 1;
  double current_price = 2;
  double implied_volatility = 3;
  double risk_free_rate = 4;
  repeated Leg legs = 5;

  double price_range_percent = 6;  // e.g. 0.20 for ±20%
  uint32 price_steps = 7;
  uint32 date_steps = 8;
  double dividend_yield = 9;
  double matrix_price_min = 10;
  double matrix_price_max = 11;
}

message Greeks {
  double delta = 1;
  double gamma = 2;
  double theta = 3;
  double vega = 4;
  double rho = 5;
  double vanna = 6;
  double volga = 7;
  double charm = 8;
}

message PnLPoint {
  double underlying_price = 1;
  double pnl = 2;
}

message MatrixCell {
  double price = 1;
  uint32 days_to_expiration = 2;
  string date_str = 3;          // YYYY-MM-DD
  double pnl_dollars = 4;
  double return_on_risk_percent = 5;
}

message RiskMetrics {
  double var_parametric_95 = 1;
  double var_parametric_99 = 2;
  double cvar_parametric_95 = 3;
  double cvar_parametric_99 = 4;
}

message StrategyResponse {
  double max_profit = 1;
  double max_loss = 2;
  double break_even = 3;
  double expected_value = 4;
  double pop = 5;
  Greeks net_greeks = 6;
  RiskMetrics risk_metrics = 7;
  repeated PnLPoint pnl_matrix = 8;

  double risk_reward_ratio = 9;
  repeated double breakeven_prices = 10;
  repeated MatrixCell matrix = 11;
  uint64 calculation_time_microseconds = 12;
  double probability_of_touch = 13;
  double probability_of_target_profit = 14;
  double curve_days_to_expiration = 15;
  repeated LegRisk leg_risk = 16;
}

message LegRisk {
  int32 leg_index = 1;
  Greeks greeks = 2;
  double model_price = 3;
  double open_pnl = 4;
}
```

---

## 6. Programmatic SEO & Traffic Growth Strategy

To dominate organic search results without relying on paid acquisition:
1. **Programmatic Landing Pages**: Static Site Generation creates distinct route pages based on search volume intent (`/calculator/<slug>` and `/guides/<slug>`).
2. **OpenGraph Metadata**: Pre-rendered social cards and metadata embedded per strategy for Twitter and Reddit sharing (`frontend/src/app/layout.tsx`).
3. **Structured Data JSON-LD**: `SoftwareApplication` and `FinancialProduct` schema markup to capture Google rich snippets.
4. **Performance Targets**: 100/100 Lighthouse performance across Core Web Vitals via static HTML edge serving from Cloudflare Workers.

### 6.1 As built (2026-08-16), and one constraint the plan above does not state

The route hierarchy is **two pages per strategy, 26 strategies**, not thousands
of generated permutations, and `/ticker/<symbol>/…` does not exist:

| route | intent | content |
| --- | --- | --- |
| `/calculator/<slug>` | "iron condor **calculator**" | the tool |
| `/guides/<slug>` | "**what is** an iron condor" | the article |

They are cross-linked in both directions and both are in `sitemap.xml`.

**The constraint, which is not negotiable and is enforced at build time:**
AdSense flagged this site on 2026-08-16 for Google-served ads on screens without
publisher content, because all 26 calculator pages were identical to the word.
Ads and publisher content must live on the same screens — so the guide pages
carry the advertising and the calculator, widget and policy pages carry none.
Adding a generated route means deciding which of those two it is.
`frontend/scripts/check-export.mjs` fails `npm run build` otherwise. See
CLAUDE.md, "Advertising and the publisher-content policy".

---

## 7. Governance, Repository & Code Review Policies

### 7.1 Automated Adversarial Code Review Gate
Per [`config/update_policy.txt`](config/update_policy.txt) and `scripts/code_review_adversarial.sh`, all commits pushed to remote repositories must pass an automated, strict multi-phase review gate:

1. **Phase 1: Deterministic Static Policy Verification**:
   - Scans diffs for forbidden constructs: zero raw `new`/`delete`, zero `-ffast-math` (preventing floating-point divergence, `config/cpp_details.txt:104, 122`), zero unmasked credentials or secrets (`config/update_policy.txt:80-93`).
   - Verifies C++23 standards conformance, 64-byte AVX memory alignment, RAII memory management, and oneTBB parallelism (`config/cpp_details.txt:15-35`).
2. **Phase 2: Multi-Engine Independent Review Vote**:
   - The diff and binding policy specifications (`config/cpp_details.txt`, `config/update_policy.txt`) are evaluated by automated independent reviewers.
   - Requires an explicit affirmative approval threshold (at least 2 approvals out of responding reviewers) before proceed tokens are granted.
3. **Phase 3: Deep AST & Language Server Blast Radius Audit**:
   - Real compiler and language-server diagnostics (`clangd`, `tsserver`) evaluate symbol referencing across the repository (`find_referencing_symbols`), checking declaration/implementation consistency and catching regressions across boundaries.
4. **Authorship & Dual-Remote Synchronization**:
   - All commits and codebase contributions are strictly attributed to Olumuyiwa Oluwasanmi (`config/update_policy.txt:94-112`).
   - Commits are synchronized across both GitHub and self-hosted Gitea remotes (`config/update_policy.txt:4-6, 139-143`).

---

## 8. Implementation Roadmap & Milestones

### Phase 1: Core Engine & Sensen Integration (Weeks 1–2)
- [x] Configure repository policies (`cpp_details.txt` and `update_policy.txt`).
- [x] Expand PRD to version 2.0.0 with as-built architecture and monetization models.
- [x] Implement C++23 gRPC server module using `sensen` SIMD matrix engines on Railway (`backend/src/modules/calculator_service.cpp`).
- [x] Finalize Protobuf schema `calculator.proto` and generate stubs (`backend/proto/calculator.proto`, `scripts/gen_proto.sh`).

### Phase 2: Supabase Auth, DB & Broker Lead Router (Weeks 3–4)
- [x] Initialize Supabase Auth and Railway PostgreSQL database with strict Row-Level Security (`backend/migrations/04_saved_strategies_rls.sql`, `06_user_tables_rls.sql`).
- [x] Build subscription billing and lead routing infrastructure (Shipped differently: Stripe checkout, webhooks, and HMAC-SHA512 Pro licence minting built in a Cloudflare Worker at `workers/billing/README.md`; broker CPA lead webhooks abandoned in favour of direct Pro subscriptions).
- [x] Integrate Market Data APIs (Shipped differently: integrated Alpaca Market Data API at `backend/src/modules/market_data.cppm` for spot, quotes, OPRA option chains, and Greeks; Yahoo Finance was superseded).

### Phase 3: Lovable UI & Consumer Hub Launch (Weeks 5–6)
- [x] Export Lovable UI components to Next.js React application (Shipped: Next.js React 19 / TypeScript application under `frontend/`).
- [x] Implement Zustand state management and gRPC-Web client connectivity (`frontend/src/store/useCalculatorStore.ts`, `frontend/src/grpc/CalculatorServiceClientPb.ts`).
- [x] Render P&L Heatmaps & Strategy Dashboard (Shipped differently: 2D Canvas matrix heatmap and SVG payoff diagrams deployed for 26 strategy routes and 47 backend catalogued strategies; 3D WebGL heatmap abandoned in favour of high-performance 2D Canvas rendering).

### Phase 4: Programmatic SEO & B2B Spoke Packaging (Weeks 7–8)
- [x] Deploy Next.js static route hierarchy and OpenGraph metadata (Shipped differently: 26 `/calculator/<slug>` and 26 `/guides/<slug>` static export routes deployed with sitemap and metadata; dynamic edge OG image generator abandoned).
- [x] Sanitize and package the White-Label Widget iframe for B2B API access (`frontend/src/app/widget/page.tsx`, verified by `frontend/scripts/check-export.mjs`).

### Phase 5: Cloud Deployment & Adversarial CI/CD (Weeks 9–10)
- [x] Deploy Dockerized `sensen` engine to Railway (Shipped: Dockerized backend on Railway with Envoy proxy for gRPC-Web and TCP proxy for native gRPC at `tokaido.proxy.rlwy.net:34513`).
- [x] Lock down review pipeline (Shipped: Enforced pre-commit and pre-push verification via `scripts/code_review_adversarial.sh` and `config/update_policy.txt` across GitHub and Gitea remotes).

---

## 9. Subsequent Architecture Additions (Post-Launch Expansion)

### 9.1 General-Purpose Finance Service & Mortgage Assistant Surface
Beyond options calculations, the backend serves `sensen.finance.Finance` (`backend/proto/finance.proto:1-35`, `docs/FINANCE_API.md:1-60`), providing roughly fifty financial functions spanning time value of money, amortization schedules, bond analytics, cash-flow metrics, and portfolio optimization. Alongside it, the natural-language mortgage assistant surface (`backend/proto/mortgage_assistant.proto:1-30`, `docs/MORTGAGEFV_INTEGRATION.md:1-60`) serves natural-language parsing for loan and mortgage parameters using an in-process fine-tuned language model (`backend/Dockerfile:22-32`). A dedicated client package and integration guide support external consumer platforms such as `mortgagefvcalculator.com` (`docs/MORTGAGEFV_INTEGRATION.md`, `docs/FINANCE_API.md`).

### 9.2 Saved Scenarios with Row-Level Security
The platform supports persisting, listing, and deleting custom strategy setups via dedicated gRPC endpoints (`backend/proto/calculator.proto:32-34`, `backend/src/modules/calculator_service.cpp:1710-1760`). Rows are stored in the Railway PostgreSQL database (`public.saved_strategies`) under strict Row-Level Security policies (`backend/migrations/04_saved_strategies_rls.sql:58-65`, `backend/migrations/05_saved_strategies_auth_fk.sql:103-107`) keyed to the caller's verified `sub` claim from Supabase Auth tokens (`backend/src/modules/api_key.cpp:480-494`). The database enforces tenant isolation directly and fails closed on unauthenticated access. Full details in `docs/architecture/CODE_MAP_BACKEND_SERVICES.md` and `backend/migrations/04_saved_strategies_rls.sql`.

### 9.3 Automated State-Assumptions Refresh
A background data synchronization worker in the finance backend maintains up-to-date demographic, tax, and housing statistics across all fifty U.S. states in `public.state_assumptions` (`backend/migrations/07_state_assumptions.sql:1-50`, `docs/STATE_ASSUMPTIONS_HANDOFF.md:1-60`). The system periodically ingests US Census Bureau American Community Survey (ACS 5-year) aggregates, providing public read access through the `GetStateAssumptions` RPC on `sensen.finance.Finance` while restricting manual refresh triggers (`RefreshStateAssumptions`) to authorized administrative callers. Full details in `docs/STATE_ASSUMPTIONS_HANDOFF.md`.

### 9.4 API Key Authentication & Quota Tiers
To support third-party integrations and white-label embeds without compromising security, the system implements a dual-key architecture separating public publishable keys (`pk_live_...`) restricted to browser use via origin allowlists from private secret keys (`sk_live_...`) for backend servers (`docs/API_SECURITY.md:22-34`, `backend/src/modules/api_key.cppm:25-46`). Keys are stored at rest as SHA-512 hashes and evaluated in constant time, coupled with an in-memory token-bucket quota enforcer (`backend/src/modules/quota.cpp:34-70`) that meters both request frequency (requests per minute) and computational workload (compute units per hour) per configured tier (`docs/API_SECURITY.md`, `docs/API_USAGE.md`).

---

## 10. Summary of Document Approvals

| Role | Name | Approval Status | Date |
| :--- | :--- | :--- | :--- |
| **Author, System Architect & Lead** | Olumuyiwa Oluwasanmi | APPROVED | September 2026 |
