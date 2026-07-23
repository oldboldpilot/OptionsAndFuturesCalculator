# Product Requirements Document (PRD)
## High-Performance Options & Futures Profit Calculator Web Application

**Document Version:** 1.4.0  
**Author / Lead Architect:** Antigravity (Pair Programming with User)  
**Date:** July 2026  
**Status:** Approved for Implementation  
**Monetization Framework:** The Hub & Spoke Model (Phase 1: B2C Lead Gen & CPA + Pro Subscriptions; Phase 2: B2B White-Label SaaS & Calculation API)  
**Pro Tier Highlight:** Advanced Options & Futures Strategy Probability Calculator Engine  
**UI Design Foundation:** Lovable (Lovable.dev Component Blueprint & Prototyping)  
**Backend & Database:** Supabase (Auth, PostgreSQL DB, Row-Level Security, Stripe Subscriptions, Lead Attribution)  
**Calculation Engine:** `sensen` (C++23 with nanobind Python Bindings) — [github.com/oldboldpilot/sensen](https://github.com/oldboldpilot/sensen)  
**Engine Hosting Target:** Railway (Dockerized C++23 Microservices over gRPC & gRPC-Web)  
**Governance & Policies:** [`config/cpp_details.txt`](file:///home/muyiwa/Development/OptionsAndFuturesCalculator/config/cpp_details.txt) & [`config/update_policy.txt`](file:///home/muyiwa/Development/OptionsAndFuturesCalculator/config/update_policy.txt)  
**Required Code Reviewers:** `Claude Agent`, `AGY (Antigravity) Agent`, `Cursor Agent`  

---

## 1. Executive Summary & Vision

### 1.1 Overview
The **Options & Futures Profit Calculator** is an enterprise-grade web application designed to give retail traders, quantitative analysts, and financial institutions real-time, interactive profit-and-loss (P&L) matrix visualizations, risk probability models, and option sensitivity breakdowns. 

Inspired by the industry-standard UI/UX of [OptionsProfitCalculator.com](https://www.optionsprofitcalculator.com/), this platform elevates the web experience by combining:
1. **Lovable UI Blueprint**: Rapid UI prototyping on Lovable adapted into a modern, responsive React/TypeScript (Next.js) interface with dark-mode glassmorphic aesthetics, complex state management via Zustand, and highly optimized WebGL/Canvas data visualizations.
2. **Supabase Core Backend**: Seamless authentication, user profile management, saved strategy databases, strict Row-Level Security (RLS), and subscription billing via Stripe combined with Supabase Edge Functions.
3. **High-Performance C++23 Calculation Engine**: Sub-millisecond mathematical matrix generation powered by the **`sensen`** SIMD library ([github.com/oldboldpilot/sensen](https://github.com/oldboldpilot/sensen)) hosted on **Railway**, communicating via **gRPC** and **gRPC-Web** via an Envoy proxy.
4. **Pro Tier Strategy Probability Calculator**: Institutional-grade probability engine calculating Probability of Profit (POP), Probability of 50% Max Profit, Probability of Touch, Expected Value (EV), and Monte Carlo PDF/CDF distribution overlays across 30+ complex options and futures strategies.
5. **Phased Hub & Spoke Business Architecture**: 
   - **Phase 1 (The Hub - B2C Lead Gen & Subscriptions)**: Highest-converting consumer tool with high-intent broker lead routing (CPA) and Pro subscriptions. Display ads are explicitly omitted to maximize conversion rates and maintain tenant trust.
   - **Phase 2 (The Spoke - B2B SaaS & Calculation API)**: White-label embeddable calculation widgets and raw C++ gRPC/REST calculation API endpoints for FinTech developers, brokerages, and trading platforms.

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
  When a user builds an option spread or futures position, the platform provides a single-click **"Execute via Partner Broker"** action. This deep-links the exact strategy parameters directly into partner broker order tickets (e.g., Tastytrade, Charles Schwab, Interactive Brokers, Alpaca, TradeStation). Revenue is captured per action/signup (CPA).
  - *Tracking Mechanism*: Click events generate a unique `lead_id` in Supabase. UTM parameters and strategy hashes are appended to the broker redirect URL. Postbacks via Webhooks from brokers (where supported) confirm funded accounts.
* **Zero Display Ads Policy**: Display ads are **omitted entirely** in Phase 1. Ad networks introduce third-party cookies, bloat page load times, and distract from the primary conversion funnels (Broker CPA + Pro Subscription).
* **Pro Consumer Subscription ($19/mo or $190/yr)**:
  * **Free Users**: Single options, 2-leg vertical spreads, standard 30x30 matrix grid, 15-min delayed quotes (IEX Cloud/Polygon basic).
  * **Pro Users**: **Full Pro Options & Futures Probability Calculator Suite**, Unlimited 8-leg Custom Builder, Iron Condors/Butterflies, Futures & FOPs (ES, NQ, CL, GC, ZB), high-density 100x100 grid + 3D WebGL P&L surface, live streaming market data via WebSocket, unlimited saved strategy presets, 1st & 2nd order Greeks (Vanna, Volga, Charm).

### 2.2 Phase 2 (The Spoke - B2B SaaS & Calculation API)
Once the core C++23 `sensen` engine is battle-tested under production traffic:
* **White-Label Embedded Widget**: A sanitized, brandable iframe/web-component widget offered as a SaaS subscription to financial media outlets, trading blogs, and RIA advisory portals. Managed via dedicated tenant API keys in Supabase.
* **B2B Calculation API**: Offering raw high-performance gRPC and REST calculation endpoints (`sensen` powered) to FinTech developers and quantitative trading firms on a usage-based API tier (e.g., Stripe metered billing per 1M compute requests).

---

## 3. Pro Subscription Feature: Options & Futures Strategy Probability Calculator

The **Pro Strategy Probability Calculator** is a cornerstone feature of the Pro Subscription. Powered by `sensen`'s high-speed numerical methods, stochastic solvers, and Monte Carlo simulation routines, it displays probability metrics and distribution curves across 30+ strategies.

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

#### Category A: Single-Leg Strategies
1. **Long Call / Short Call**: Probability of breaching strike $K$ and probability of covering premium paid/collected.
2. **Long Put / Short Put**: Downside probability distribution and probability of assignment.
3. **Covered Call / Protective Put**: Combined equity + option probability density, upside cap probability, and downside break-even probability.
4. **Cash-Secured Put**: Probability of acquisition vs. probability of full premium retention.

#### Category B: Vertical & Time Spreads
5. **Bull Call Spread / Bear Call Spread**: Probability of reaching max profit ($S \ge K_2$), breakeven, and max loss ($S \le K_1$).
6. **Bull Put Spread / Bear Put Spread**: Credit spread win rate ($POP$) and probability of 50% max profit before expiration.
7. **Calendar (Time) Spread / Diagonal Spread**: Multi-expiration probability surface accounting for differential theta decay and implied volatility term structure.
8. **Double Diagonal Spread**: Multi-strike, multi-expiration range probability bounds.

#### Category C: Multi-Leg Neutral & Range-Bound Strategies
9. **Iron Condor (Short / Long)**: Probability of underlying remaining inside the short strike range $[K_{s1}, K_{s2}]$, probability of touching short strikes ($P_{\text{touch}}$), and probability of early profit target (50% max profit).
10. **Iron Butterfly (Short / Long)**: Pin-risk probability at exact center strike $K_0$, probability of profitability window.
11. **Standard Butterfly Spread (Call / Put / Iron)**: Probability of landing in the high-profit peak zone.
12. **Condor Spread (Call / Put)**: Wide range profit zone probability evaluation.
13. **Jade Lizard**: Zero-upside-risk probability analysis.
14. **Big Lizard**: Unlimited downside / capped upside risk-reward distribution.

#### Category D: Volatility & Asymmetric Strategies
15. **Long / Short Straddle**: Volatility breakout probability (probability that $|S_T - S_0| > \text{Total Premium}$).
16. **Long / Short Strangle**: Out-of-the-money volatility expansion probability.
17. **Call Ratio Spread (1x2 / 1x3)**: Asymmetric payoff probability showing exact risk zone beyond the long strike ratio.
18. **Put Ratio Spread (1x2 / 1x3)**: Downside ratio risk probability analysis.
19. **Ratio Backspread (Call / Put)**: Volatility explosive move probability.
20. **Collar & Risk Reversal**: Portfolio hedging probability bounds.
21. **Synthetic Long / Synthetic Short Stock**: Equivalent equity probability distribution.
22. **Box Spread**: Arbitrage yield and financing risk-free probability verification.

#### Category E: Futures & Options on Futures (FOPs) Strategies
23. **Outright Long / Short Futures**: ES, NQ, CL, GC, ZB. Probability of price target hitting before contract expiration.
24. **Futures Calendar Spread**: Probability of basis expansion/contraction ($F_2 - F_1$) based on cost-of-carry $F = S e^{(r-q)T}$.
25. **Futures Inter-Commodity Spread**: Crack spread (CL vs. RB/HO), Spark spread.
26. **Covered Futures Call / Covered Futures Put**: Futures position with options overlay probability matrix.
27. **Futures Option Straddle / Strangle**: High-volatility commodity breakout probability.
28. **Futures Option Iron Condor**: Range-bound probability calculator incorporating contract multipliers (e.g., $\$50 \times \text{ES}$).
29. **Futures Hedged Synthetic**: Futures Long + Protective Put + Covered Call.
30. **Custom Multi-Leg Pro Builder (Up to 8 Legs)**: Arbitrary combination of up to 8 equity options, futures, and FOPs legs evaluated simultaneously via advanced Monte Carlo simulations.

### 3.2 Pro Probability Analytical Metrics & Mathematical Models

The engine utilizes the **Black-Scholes-Merton (BSM)** framework for rapid baseline calculations and **Heston Stochastic Volatility / Monte Carlo paths** for deep out-of-the-money (OTM) tail-risk metrics.

1. **Probability of Profit (POP)**:
   $$\text{POP} = \mathbb{P}(\text{P\&L}(S_T) > 0) = \int_{\text{Profit Zones}} f(S_T) dS_T$$
2. **Probability of Target Profit (25%, 50%, 75% Max Profit)**:
   Calculated using first-passage time approximations for standard models or Monte Carlo path boundary conditions for complex strategies.
3. **Probability of Touch ($P_{\text{touch}}$)**:
   Probability that the underlying spot price touches a breakeven or short strike at any point during the trade lifecycle.
4. **Expected Value ($\text{EV}$)**:
   $$\text{EV} = \mathbb{E}[\text{P\&L}] = \int_{-\infty}^{\infty} \text{P\&L}(S_T) \cdot f(S_T) dS_T$$
5. **Value at Risk (VaR 95% & 99%) & Conditional VaR (Expected Shortfall)**: Tail risk assessment computed from the CDF of simulated P&L distributions.
6. **Interactive Price Distribution Visualizer**: WebGL Canvas displaying log-normal PDF/CDF curves overlaid directly on the P&L payoff graph.

---

## 4. UI Design Workflow & Supabase Backend Architecture

### 4.1 UI Prototyping & State Management
- **Lovable Blueprint**: Initial component layouts and setup forms are prototyped on Lovable.dev to lock in UX.
- **Frontend Framework**: Next.js (React/TypeScript) utilizing Server-Side Rendering (SSR) for fast initial loads and SEO.
- **State Management**: Zustand for rapid local state mutation (slider drag events for volatility/price matrices) to prevent unnecessary React re-renders. WebWorkers handle local payload parsing.
- **Design Tokens (Tailwind + Vanilla CSS)**:
  - Background: Deep Dark Slate (`#0B0F17`) with glassmorphism panels (`rgba(255, 255, 255, 0.05)`).
  - Profit Color: Vibrant Emerald Green (`#10B981`) with glow accents.
  - Loss Color: Crimson Red (`#EF4444`).
  - Typography: Inter (UI) & JetBrains Mono (Financial Data Grids).

### 4.2 Supabase Backend Schema & Services

1. **Database Schema (PostgreSQL)**:
   - `users`: Managed by Supabase Auth (OAuth/Email).
   - `profiles`: `id` (uuid, references auth.users), `tier` (enum: free/pro), `stripe_customer_id`, `subscription_status`.
   - `saved_strategies`: `id`, `user_id`, `symbol`, `strategy_type`, `legs_jsonb`, `created_at`.
   - `shared_permalinks`: `hash_id`, `strategy_payload_jsonb`, `views`, `created_at`.
   - `broker_lead_events`: `id`, `user_id` (nullable), `broker_name`, `strategy_type`, `utm_source`, `clicked_at`.
2. **Row-Level Security (RLS)**:
   - Example Policy: `CREATE POLICY "Users can view own strategies" ON saved_strategies FOR SELECT USING (auth.uid() = user_id);`
3. **Edge Functions (Deno)**:
   - `stripe-webhook`: Handles subscription upgrades/downgrades and updates the `profiles` table.
   - `lead-attribution`: Handles secure postbacks from broker API webhooks to track CPA conversions.

---

## 5. C++23 Engine, Railway Microservice, & Performance

### 5.1 C++23 Engine Implementation Policy (`config/cpp_details.txt`)
All C++ code for the calculation engine must adhere strictly to [`config/cpp_details.txt`](file:///home/muyiwa/Development/OptionsAndFuturesCalculator/config/cpp_details.txt):
1. **Language Standard**: Pure C++23 compiled with `clang++-22`.
2. **Modules Architecture**: Code structured as C++23 modules (`.cppm`) using `import std;` and `import sensen.*;`.
3. **Memory Safety & Pointers**: **NO RAW POINTERS**. Use `std::unique_ptr`, `std::shared_ptr`, and `std::span`. Avoid `malloc`; use custom Arena Allocators for Monte Carlo path generation to prevent memory fragmentation and GC pauses.
4. **Error Handling**: Railway-Oriented Programming (ROP) using `std::expected<T, std::error_code>`. No untracked exceptions.
5. **Threading & Parallelism**: Parallel matrix computation using Intel Threading Building Blocks (`tbb::parallel_for`).
6. **SIMD Waterfall Dispatch**: Dynamic runtime hardware detection (`AVX-512` -> `AVX2` -> `SSE4.2` -> `Scalar`).

### 5.2 Microservice Architecture & Performance SLA
- **Hosting**: Railway.app Dockerized containers.
- **Proxy**: Envoy Proxy translates gRPC-Web (from the browser) into native HTTP/2 gRPC for the C++ backend.
- **Performance Targets (SLA)**:
  - Standard Matrix Calculation (30x30): **< 5ms** p99 latency at the engine level.
  - Pro Monte Carlo Probabilities (10,000 paths, 8 legs): **< 45ms** p99 latency at the engine level.
  - The engine must scale horizontally; Railway Pod Autoscaling triggers at 70% CPU utilization. Compute-exhaustion attacks are mitigated via strictly enforced gRPC rate limits per IP/User token.

### 5.3 gRPC Interface Specification (`calculator.proto`)
```protobuf
syntax = "proto3";
package options_calculator;

enum InstrumentType { EQUITY_OPTION = 0; FUTURES_OPTION = 1; FUTURES_SPOT = 2; EQUITY_SPOT = 3; }
enum OptionType { CALL = 0; PUT = 1; }
enum ActionType { BUY = 0; SELL = 1; }

message Leg {
  string id = 1;
  InstrumentType instrument_type = 2;
  OptionType option_type = 3;
  ActionType action = 4;
  double strike_price = 5;
  double premium = 6;
  uint32 quantity = 7;
  string expiration_date = 8;
  double implied_volatility = 9;
  double contract_multiplier = 10;
}

message CalculationRequest {
  string symbol = 1;
  double spot_price = 2;
  double risk_free_rate = 3;
  repeated Leg legs = 4;
  double price_range_percent = 5;
  uint32 price_steps = 6;
  uint32 date_steps = 7;
  bool compute_pro_probabilities = 8;
  // Auth token passed in metadata headers for pro tier validation
}

message MatrixCell {
  double price = 1;
  uint32 days_to_expiration = 2;
  double pnl_dollars = 3;
  double probability_density = 4;
}

message CalculationResponse {
  double max_profit = 1;
  double max_loss = 2;
  repeated MatrixCell matrix = 3;
  // Sub-messages for Greeks and Probabilities omitted for brevity
}

service CalculatorEngineService {
  rpc ComputeStrategyPnL (CalculationRequest) returns (CalculationResponse);
  rpc StreamLiveMatrix (stream CalculationRequest) returns (stream CalculationResponse);
}
```

---

## 6. Programmatic SEO & Traffic Growth Strategy

To dominate organic search results without relying on paid acquisition:
1. **Programmatic Landing Pages**: Next.js Static Site Generation (SSG) creates thousands of distinct route pages based on search volume intent:
   - `/calculator/iron-condor-calculator`
   - `/calculator/sp500-futures-options-calculator`
   - `/ticker/AAPL/options-calculator`
2. **Dynamic OpenGraph Engine**: Edge functions generate on-the-fly `og:image` PNGs showcasing a 3D P&L curve specific to the queried strategy/ticker for highly engaging Twitter/Reddit social sharing.
3. **Structured Data JSON-LD**: Extensive `SoftwareApplication` and `FinancialProduct` schema markup to win Google Rich Snippets.
4. **Performance Targets**: 100/100 Lighthouse score across Core Web Vitals to ensure top Google ranking.

---

## 7. Governance, Repository & Code Review Policies

### 7.1 Multi-Agent Adversarial Code Review Gate
Per [`config/update_policy.txt`](file:///home/muyiwa/Development/OptionsAndFuturesCalculator/config/update_policy.txt) and `scripts/code_review_adversarial.sh`, all commits pushed to GitHub and Gitea MUST pass an automated, strict **Tri-Agent Review Gate**:

1. **Claude Agent**: Audits deep system architecture, mathematical correctness of option & probability formulas (Black-Scholes, Monte Carlo stability), security posture (Rate limiting, RLS), and edge case resilience.
2. **AGY (Antigravity) Agent**: Audits strictly against `config/cpp_details.txt`. Verifies C++23 standard compliance, SIMD waterfall correctness, memory alignment (64-byte AVX), Arena Allocators, RAII, and thread safety via Intel TBB.
3. **Cursor Agent**: Audits gRPC schema compatibility, frontend/backend integration (Next.js to Envoy), UI rendering efficiency (React re-renders, WebGL performance), Supabase RLS policies, and state management.

**Voting Mechanism**: A pull request or direct commit sync script requires explicit `APPROVED` tokens from at least 2 out of 3 agents to proceed.

---

## 8. Implementation Roadmap & Milestones

### Phase 1: Core Engine & Sensen Integration (Weeks 1–2)
- [x] Configure repository policies (`cpp_details.txt` and `update_policy.txt`).
- [x] Expand PRD to version 1.4.0 with detailed architecture and monetization models.
- [ ] Implement C++23 gRPC server module using `sensen` SIMD matrix engines on Railway.
- [ ] Finalize Protobuf schema `calculator.proto` and generate stubs.

### Phase 2: Supabase Auth, DB & Broker Lead Router (Weeks 3–4)
- [ ] Initialize Supabase project (PostgreSQL DB, Auth, strict RLS).
- [ ] Build Supabase Edge Functions for Stripe subscriptions and CPA Lead Webhooks.
- [ ] Integrate Tier-1 Market Data APIs (Polygon.io / IEX) mapped to `config.yaml`.

### Phase 3: Lovable UI & Consumer Hub Launch (Weeks 5–6)
- [ ] Export Lovable UI components to Next.js Vite/React application.
- [ ] Implement Zustand state management and gRPC-Web client connectivity.
- [ ] Render 2D/3D WebGL P&L Heatmaps & the 30+ Strategy Pro Dashboard.

### Phase 4: Programmatic SEO & B2B Spoke Packaging (Weeks 7–8)
- [ ] Deploy Next.js SSG SEO route hierarchy and OpenGraph image generator.
- [ ] Sanitize and package the White-Label Widget iframe for B2B API access.

### Phase 5: Cloud Deployment & Tri-Agent CI/CD (Weeks 9–10)
- [ ] Deploy Dockerized `sensen` engine to Railway with Horizontal Pod Autoscaling.
- [ ] Lock down CI/CD pipeline using GitHub Actions to enforce the `code_review_adversarial.sh` 2/3 agent approval gate for all production deployments.

---

## 9. Summary of Document Approvals

| Persona / Role | Agent Name | Approval Status | Date |
| :--- | :--- | :--- | :--- |
| **System Architect & Lead** | Antigravity (AGY) | APPROVED | July 2026 |
| **C++ Core Policy Auditor** | AGY Agent (`cpp_details.txt`) | APPROVED | July 2026 |
| **Security & Algorithm Reviewer** | Claude Agent | PENDING | July 2026 |
| **Frontend & API Integration Reviewer**| Cursor Agent | PENDING | July 2026 |
