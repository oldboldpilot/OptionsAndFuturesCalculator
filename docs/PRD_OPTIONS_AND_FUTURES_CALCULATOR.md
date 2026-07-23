# Product Requirements Document (PRD)
## High-Performance Options & Futures Profit Calculator Web Application

**Document Version:** 1.1.0  
**Author / Lead Architect:** Antigravity (Pair Programming with User)  
**Date:** July 2026  
**Status:** Approved for Implementation  
**UI Design Foundation:** Lovable (Lovable.dev Component Blueprint & Prototyping)  
**Backend & Database:** Supabase (Auth, PostgreSQL DB, Row-Level Security, Stripe Subscriptions)  
**Calculation Engine:** `sensen` (C++23 with nanobind Python Bindings) — [github.com/oldboldpilot/sensen](https://github.com/oldboldpilot/sensen)  
**Engine Hosting Target:** Railway (Dockerized C++23 Microservices over gRPC & gRPC-Web)  
**Governance & Policies:** [`config/cpp_details.txt`](file:///home/muyiwa/Development/OptionsAndFuturesCalculator/config/cpp_details.txt) & [`config/update_policy.txt`](file:///home/muyiwa/Development/OptionsAndFuturesCalculator/config/update_policy.txt)  
**Required Code Reviewers:** `Claude Agent`, `AGY (Antigravity) Agent`, `Cursor Agent`  

---

## 1. Executive Summary & Vision

### 1.1 Overview
The **Options & Futures Profit Calculator** is an enterprise-grade web application designed to give retail traders, quantitative analysts, and financial institutions real-time, interactive profit-and-loss (P&L) matrix visualizations, risk probability models, and option sensitivity breakdowns. 

Inspired by the industry-standard UI/UX of [OptionsProfitCalculator.com](https://www.optionsprofitcalculator.com/), this platform elevates the web experience by combining:
1. **Lovable UI Blueprint**: Rapid UI prototyping on Lovable adapted into a modern, responsive React/TypeScript interface with dark-mode glassmorphic aesthetics.
2. **Supabase Core Backend**: Seamless authentication, user profile management, saved strategy databases, Row-Level Security (RLS), and subscription billing via Stripe + Supabase Edge Functions.
3. **High-Performance C++23 Calculation Engine**: Sub-millisecond mathematical matrix generation powered by the **`sensen`** SIMD library ([github.com/oldboldpilot/sensen](https://github.com/oldboldpilot/sensen)) hosted on **Railway**, communicating via **gRPC** and **gRPC-Web**.
4. **Monetization & Growth Engine**: Integrated freemium subscription tiers (Free ad-supported tier vs. Pro ad-free tier) paired with programmatic SEO landing pages for organic traffic acquisition.

### 1.2 Key Architectural Differentiators
1. **Sub-Millisecond Computation**: Powered by `sensen` SIMD waterfall execution (AVX-512 $\rightarrow$ AVX2 $\rightarrow$ SSE4.2 $\rightarrow$ Scalar) and Intel TBB parallel matrix sweeps, generating 100x100 P&L grids in $<0.5\text{ms}$.
2. **Comprehensive Options & Futures Support**: Supports single options, complex multi-leg options spreads (Iron Condors, Butterflies, Calendars, Diagonals), outright Futures, and Options on Futures (ES, NQ, CL, GC, ZB) with contract multipliers and tick specifications.
3. **Institutional Risk Analytics**: Analytical Black-Scholes-Merton, American Binomial Trees (Cox-Ross-Rubinstein / Leisen-Reimer), Monte Carlo simulations, Heston/SABR stochastic volatility fitting, and 1st/2nd order Option Greeks ($\Delta, \Gamma, \Theta, \nu, \rho, \text{Vanna}, \text{Volga}, \text{Charm}$).
4. **Supabase Auth & Stripe Billing**: Managed authentication (OAuth, Magic Link) and subscription webhooks powering Free and Pro tier access controls.
5. **Programmatic SEO & Ad Engine**: Dynamic route pages (`/calculator/iron-condor-calculator`), OpenGraph social card previews, Schema.org structured data, and non-intrusive AdSense/Carbon Ad integration for monetization.
6. **Strict Governance & Multi-Agent Reviews**: Enforced C++23 standards (`config/cpp_details.txt`), version control via `gh` CLI (GitHub) and `git` CLI (Gitea) per [`config/update_policy.txt`](file:///home/muyiwa/Development/OptionsAndFuturesCalculator/config/update_policy.txt), gated by mandatory tri-agent reviews (`Claude`, `AGY`, `Cursor`).

---

## 2. Business Model & Feature Access Tiers

The application uses a **Freemium Monetization Model**:

| Feature / Capability | Free Tier (Ad-Supported) | Pro Subscription ($19/mo or $190/yr) |
| :--- | :--- | :--- |
| **Strategy Access** | Single Options & 2-Leg Spreads (Calls, Puts, Covered Call, Vertical Spreads) | **Unlimited Multi-Leg (Iron Condor, Butterfly, Ratio, 8-Leg Custom Builder)** |
| **Futures & FOPs Support** | Basic Equity Options only | **Full Futures & Options on Futures (ES, NQ, CL, GC, ZB)** |
| **Heatmap Matrix Resolution** | Standard $30 \times 30$ price/date grid | **High-Density $100 \times 100$ Grid + 3D WebGL Surface View** |
| **Market Data Ingestion** | 15-minute delayed quotes (Polygon/FMP) | **Real-Time Live Chains & Stream (Schwab & Alpaca)** |
| **Saved Strategies** | Up to 3 saved strategy templates | **Unlimited Saved Presets & Shareable Custom Permalinks** |
| **Ad Experience** | Non-intrusive sidebar & footer ad banners | **100% Ad-Free Clean UI** |
| **Greeks & Analytics** | 1st Order Greeks ($\Delta, \Gamma, \Theta, \nu, \rho$) | **1st & 2nd Order Greeks ($\text{Vanna}, \text{Volga}, \text{Charm}$) + SABR Vol Surface** |

---

## 3. UI Design Workflow & Supabase Backend Architecture

### 3.1 Lovable UI Prototyping & Design System
- **Lovable Blueprint**: Initial component layouts, color palettes, and strategy setup forms are designed on Lovable.dev.
- **Frontend Implementation**: Lovable exports are integrated into a Vite / Next.js React TypeScript codebase using Tailwind CSS and Vanilla CSS for glassmorphic dark-mode visuals.
- **Design Tokens**:
  - Background: Deep Dark Slate (`#0B0F17`) with glassmorphism panels (`rgba(255, 255, 255, 0.05)`).
  - Profit Color: Vibrant Emerald Green (`#10B981`) with glow accents.
  - Loss Color: Crimson Red (`#EF4444`).
  - Typography: Inter & JetBrains Mono for monetary grid displays.

---

### 3.2 Supabase Backend Ecosystem

```mermaid
graph TD
    Client[React Web App - Lovable UI] --> Auth[Supabase Auth: OAuth / Email]
    Client --> DB[Supabase PostgreSQL DB]
    Client --> Edge[Supabase Edge Functions]
    Edge --> Stripe[Stripe Checkout & Billing API]
    Client -->|gRPC-Web| Proxy[Envoy gRPC-Web Proxy]
    Proxy -->|gRPC| Engine[Railway: sensen C++23 Engine]
    DB --> RLS[Row-Level Security Policies]
```

1. **Authentication (`Supabase Auth`)**:
   - Social OAuth (Google, GitHub, Apple) + Email / Password + Magic Link.
   - JWT tokens passed with API calls to validate user tier permissions.
2. **Database Schema (`Supabase PostgreSQL`)**:
   - `profiles`: User IDs, tier status (`free` or `pro`), subscription expiration date.
   - `saved_strategies`: User-saved multi-leg positions, symbol, leg parameters, notes.
   - `shared_permalinks`: Unique hash identifiers for sharing position URLs (`/share/a8f9c1...`).
   - `user_preferences`: Dark/Light theme, default step size, preferred risk-free rate provider.
3. **Row-Level Security (RLS)**:
   - Users can only read/write their own saved strategies and profile details.
   - Public read access for shared permalinks.
4. **Subscription & Payment Processing**:
   - Supabase Edge Functions listening to Stripe Webhooks (`customer.subscription.created`, `customer.subscription.deleted`).
   - Automated user role update upon payment confirmation.

---

## 4. Search Engine Optimization (SEO) & Ad Monetization

### 4.1 Programmatic SEO Strategy
To dominate organic search results for options calculation keywords, the platform uses **Programmatic Page Generation**:

1. **Target Route Hierarchy**:
   - `/calculator/iron-condor-calculator`
   - `/calculator/covered-call-calculator`
   - `/calculator/sp500-futures-options-calculator`
   - `/calculator/bull-put-spread-calculator`
   - `/calculator/option-greeks-calculator`
   - `/ticker/[symbol]` (e.g. `/ticker/AAPL`, `/ticker/SPY`, `/ticker/ES`)
2. **SEO Metadata & Structured Data**:
   - Dynamic HTML `<title>`, `<meta description>`, and `<link rel="canonical">`.
   - **Schema.org JSON-LD**:
     ```json
     {
       "@context": "https://schema.org",
       "@type": "SoftwareApplication",
       "name": "Iron Condor Profit Calculator",
       "operatingSystem": "Web",
       "applicationCategory": "FinanceApplication",
       "offers": {
         "@type": "Offer",
         "price": "0",
         "priceCurrency": "USD"
       }
     }
     ```
3. **Social OpenGraph Card Engine**:
   - Dynamic OG image generation (`og:image`) rendering a visual P&L curve preview thumbnail when sharing links on X (Twitter), LinkedIn, Reddit, or Discord.
4. **Core Web Vitals Compliance**:
   - Target Largest Contentful Paint (LCP) $<1.0\text{s}$, Interaction to Next Paint (INP) $<100\text{ms}$, and Cumulative Layout Shift (CLS) $= 0$.

---

### 4.2 Ad Monetization Engine (Free Tier)
1. **Ad Placements**:
   - Non-intrusive right-sidebar banner ($300\times 250$ / $300\times 600$).
   - Sticky footer banner ($728\times 90$ leaderboard).
2. **Ad Networks**:
   - Google AdSense, Mediavine / Raptor, or Carbon Ads (tailored to developer/finance audience).
3. **Ad-Block Conversion Trigger**:
   - Subtle banner inviting users to upgrade to **Pro** for an ad-free, high-resolution calculation experience.

---

## 5. Supported Strategy Suite

#### A. Single Instrument Strategies
- **Long Call / Long Put**: Directional leverage positions.
- **Covered Call / Cash-Secured Put**: Income-generation strategies.
- **Short Call / Short Put**: Naked options selling with margin risk breakdown.

#### B. Standard Vertical & Time Spreads
- **Bull Call Spread / Bear Put Spread**: Debit spreads with capped risk/reward.
- **Bull Put Spread / Bear Call Spread**: Credit spreads with income collection.
- **Calendar (Time) Spread**: Exploiting differential theta decay across expirations.
- **Diagonal Spread**: Combining strike variation and expiration differences.

#### C. Multi-Leg Combination Strategies
- **Iron Condor & Iron Butterfly**: 4-leg credit strategies for range-bound markets.
- **Straddle & Strangle**: Volatility breakout strategies (Long/Short).
- **Butterfly Spread & Condor Spread**: 3/4-leg low-cost precision targeting.
- **Ratio Spreads**: Asymmetric leg ratios (e.g., 1x2 Call Ratio Spread).
- **Collar & Risk Reversal**: Portfolio protection setups.
- **Custom Multi-Leg Builder**: Modular builder allowing up to 8 independent option & futures legs.

#### D. Futures & Futures Options Strategies
- **Outright Futures (Long/Short)**: ES (S&P 500), NQ (Nasdaq 100), CL (Crude Oil), GC (Gold), ZB (30-Yr Bond), etc.
- **Futures Calendar Spreads**: Pricing cost-of-carry ($F = S e^{(r-q)T}$) and basis risk.
- **Options on Futures**: FOPs pricing incorporating contract multipliers (e.g., $\$50 \times \text{ES}$, $\$20 \times \text{NQ}$, $\$1,000 \times \text{CL}$) and specific margin requirements.

---

## 6. Analytical Engine & Mathematical Specification

The core math engine leverages the C++23 **`sensen`** library:

```mermaid
graph TD
    A[Input Parameters: Spot, Strikes, Expiration, Vol, Rate, Divs] --> B[sensen Math Engine]
    B --> C1[Black-Scholes-Merton Engine]
    B --> C2[Binomial Tree CRR / Leisen-Reimer Engine]
    B --> C3[Monte Carlo Engine]
    B --> C4[Heston / SABR Volatility Model]
    C1 --> D[Option Pricing & Greeks: Delta, Gamma, Theta, Vega, Rho]
    C2 --> D
    C3 --> D
    C4 --> D
    D --> E[TBB Parallel Matrix Generator]
    E --> F[AVX-512 / AVX2 SIMD Sweep]
    F --> G[P&L Heatmap Matrix: Price x Date Grid]
```

#### A. Pricing Models
1. **European Options (Black-Scholes-Merton)**:
   $$C = S e^{-q T} N(d_1) - K e^{-r T} N(d_2)$$
   $$P = K e^{-r T} N(-d_2) - S e^{-q T} N(-d_1)$$
   $$d_1 = \frac{\ln(S/K) + (r - q + \sigma^2/2)T}{\sigma \sqrt{T}}, \quad d_2 = d_1 - \sigma \sqrt{T}$$
2. **American Options (Binomial Tree - CRR & Leisen-Reimer)**:
   - High-speed $N$-step tree algorithm handling early exercise features for equity and futures options.
3. **Futures & Futures Options (Black-76 Model)**:
   $$C_{fut} = e^{-r T} \left[ F N(d_1) - K N(d_2) \right]$$
   $$d_1 = \frac{\ln(F/K) + (\sigma^2/2)T}{\sigma \sqrt{T}}$$
4. **Stochastic Volatility (Heston / SABR)**:
   - Implied volatility smile modeling and surface interpolation using `sensen.numerical_methods` solvers.

#### B. Risk Metrics & Probability Analytics
- **Max Profit**: Maximum achievable P&L over the price domain.
- **Max Loss**: Maximum possible loss (with warning flags for undefined/naked risk).
- **Risk / Reward Ratio**: Ratio of Max Loss to Max Profit.
- **Breakeven Points**: Exact underlying price level(s) where total position $\text{P\&L} = 0$.
- **Probability of Profit (POP)**:
  $$\text{POP} = \int_{S_{\text{be1}}}^{S_{\text{be2}}} \frac{1}{S \sigma \sqrt{2\pi T}} \exp\left(-\frac{(\ln(S/S_0) - (r - \sigma^2/2)T)^2}{2\sigma^2 T}\right) dS$$
- **Expected Value (EV)**: Integration of P&L outcome across log-normal price probability distribution.

---

## 7. C++23 Engine & Railway Microservice Specifications

### 7.1 C++23 Engine Implementation Policy (`config/cpp_details.txt`)

All C++ code written for the calculation engine must adhere strictly to [`config/cpp_details.txt`](file:///home/muyiwa/Development/OptionsAndFuturesCalculator/config/cpp_details.txt):

1. **Language Standard**: Pure C++23 compiled with `clang++-22`.
2. **Modules Architecture**: Code structured as C++23 modules (`.cppm`) using `import std;` and `import sensen.*;`.
3. **Memory Safety & Pointers**: **NO RAW POINTERS**. Use `std::unique_ptr`, `std::shared_ptr`, and `std::span`.
4. **RAII & Alignment**: Resource management via RAII; memory structures AVX-512 aligned (64-byte boundary).
5. **Error Handling**: Railway-Oriented Programming (ROP) using `std::expected<T, std::error_code>` and `std::unexpected`. No untracked exceptions.
6. **Syntax & Attributes**: Trailing return types mandatory (`auto compute_greeks(...) -> GreekResult`), `[[nodiscard]]`, `[[gnu::target("avx512f,avx512dq,fma")]]`.
7. **Threading & Parallelism**: Parallel matrix computation using Intel Threading Building Blocks (`tbb::parallel_for`). Thread pinning enforced via `taskset -c 0-15`.
8. **SIMD Waterfall Dispatch**: Dynamic runtime hardware detection (`AVX-512` $\rightarrow$ `AVX2` $\rightarrow$ `SSE4.2` $\rightarrow$ `Scalar`).
9. **Build System & Canonical Flags**: Built with CMake + Ninja using canonical flags:
   ```bash
   -std=c++23 -stdlib=libc++ -fPIC -O3 -march=x86-64-v3 -mtune=generic \
   -mavx -mavx2 -mfma -pthread -fstack-protector-strong -DNDEBUG
   ```
10. **Binary Portability**: `$ORIGIN`-relative RPATHs (`so_to_root_rpath()`).

---

### 7.2 gRPC Interface Specification (`calculator.proto`)

Communication between the Web Gateway and the C++23 `sensen` engine hosted on Railway is governed by gRPC:

```protobuf
syntax = "proto3";

package options_calculator;

enum OptionType {
  OPTION_TYPE_CALL = 0;
  OPTION_TYPE_PUT = 1;
}

enum ActionType {
  ACTION_BUY = 0;
  ACTION_SELL = 1;
}

enum InstrumentType {
  INSTRUMENT_EQUITY_OPTION = 0;
  INSTRUMENT_FUTURES_OPTION = 1;
  INSTRUMENT_FUTURES_SPOT = 2;
  INSTRUMENT_EQUITY_SPOT = 3;
}

message Leg {
  string id = 1;
  InstrumentType instrument_type = 2;
  OptionType option_type = 3;
  ActionType action = 4;
  double strike_price = 5;
  double premium = 6;
  uint32 quantity = 7;
  string expiration_date = 8; // YYYY-MM-DD
  double implied_volatility = 9;
  double contract_multiplier = 10; // e.g. 100 for equity, 50 for ES
}

message CalculationRequest {
  string symbol = 1;
  double spot_price = 2;
  double risk_free_rate = 3;
  double dividend_yield = 4;
  repeated Leg legs = 5;
  
  // Matrix Parameters
  double price_range_percent = 6; // e.g., 0.20 for +/-20%
  uint32 price_steps = 7;         // e.g., 50 (free) or 100 (pro)
  uint32 date_steps = 8;          // e.g., 30 (free) or 100 (pro)
  double iv_shift_percent = 9;    // e.g., +0.05 for +5% IV shift
}

message GreekBreakdown {
  double delta = 1;
  double gamma = 2;
  double theta = 3;
  double vega = 4;
  double rho = 5;
  double vanna = 6;
  double volga = 7;
  double charm = 8;
}

message MatrixCell {
  double price = 1;
  uint32 days_to_expiration = 2;
  string date_str = 3;
  double pnl_dollars = 4;
  double return_on_risk_percent = 5;
}

message CalculationResponse {
  double max_profit = 1;
  double max_loss = 2;
  double risk_reward_ratio = 3;
  repeated double breakeven_prices = 4;
  double probability_of_profit = 5;
  double expected_value = 6;
  
  GreekBreakdown aggregate_greeks = 7;
  map<string, GreekBreakdown> leg_greeks = 8;
  
  repeated MatrixCell matrix = 9;
  uint64 calculation_time_microseconds = 10;
}

service CalculatorEngineService {
  rpc ComputeStrategyPnL (CalculationRequest) returns (CalculationResponse);
  rpc StreamLiveMatrix (stream CalculationRequest) returns (stream CalculationResponse);
}
```

---

## 8. Governance, Repository & Code Review Policies (`config/update_policy.txt`)

### 8.1 Repository Management Rules
Per [`config/update_policy.txt`](file:///home/muyiwa/Development/OptionsAndFuturesCalculator/config/update_policy.txt):
1. **GitHub Operations (`gh` CLI)**:
   - All repository initializations, pull requests, issue tracking, and GitHub releases MUST be managed via `gh` CLI commands (`gh repo create`, `gh pr create`, `gh pr merge`).
2. **Gitea Operations (`git` CLI)**:
   - All local commits and self-hosted Gitea mirror pushes MUST be executed using standard `git` CLI (`git commit`, `git push gitea master`).

### 8.2 Multi-Agent Code Review Mandate
Before any pull request or code change is merged into `main`/`master`, it MUST undergo automated review and receive explicit approval from **three distinct AI agents**:

```mermaid
graph TD
    PR[Pull Request Submitted] --> C[Claude Agent Review]
    PR --> A[AGY Agent Review]
    PR --> U[Cursor Agent Review]
    
    C -->|Check: Architecture & Security| C_PASS{Approved?}
    A -->|Check: C++23 & Memory Safety| A_PASS{Approved?}
    U -->|Check: API & UI Integration| U_PASS{Approved?}
    
    C_PASS -- Yes --> M_CHECK
    A_PASS -- Yes --> M_CHECK
    U_PASS -- Yes --> M_CHECK
    
    M_CHECK{All 3 Approved?} -- Yes --> MERGE[Merge Allowed via gh / git]
    C_PASS -- No --> REJECT[Block Merge & Request Changes]
    A_PASS -- No --> REJECT
    U_PASS -- No --> REJECT
```

1. **Claude Agent**: Audits deep system architecture, mathematical correctness of option formulas, security posture, and edge case resilience.
2. **AGY (Antigravity) Agent**: Audits C++23 standard compliance (`config/cpp_details.txt`), SIMD waterfall correctness, memory alignment, RAII, and thread safety.
3. **Cursor Agent**: Audits gRPC schema compatibility, frontend/backend integration, UI rendering efficiency, Supabase RLS, and state management.

---

## 9. Implementation Roadmap & Milestones

### Phase 1: Core Engine & Sensen Integration (Weeks 1–2)
- [x] Configure repository policies [`config/cpp_details.txt`](file:///home/muyiwa/Development/OptionsAndFuturesCalculator/config/cpp_details.txt) and [`config/update_policy.txt`](file:///home/muyiwa/Development/OptionsAndFuturesCalculator/config/update_policy.txt).
- [ ] Create C++23 gRPC server module using `sensen` Black-Scholes, Binomial Tree, and TBB parallel matrix engines on Railway.
- [ ] Implement Protobuf schema `calculator.proto` and compile nanobind / gRPC stubs.
- [ ] Set up Catch2/GTest test suite and Sanitizer checks (ASan, TSan, UBSan).

### Phase 2: Supabase Auth, DB & Subscription Integration (Weeks 3–4)
- [ ] Initialize Supabase project (PostgreSQL database, Auth providers, RLS policies).
- [ ] Build Supabase Edge Functions for Stripe subscription checkout and webhook handling.
- [ ] Integrate market data providers (Schwab, Alpaca, Polygon, FMP, Finnhub, FRED) using credentials in [`config/config.yaml`](file:///home/muyiwa/Development/OptionsAndFuturesCalculator/config/config.yaml).

### Phase 3: Lovable UI Adaptation & Web Frontend (Weeks 5–6)
- [ ] Export UI components from Lovable.dev into Vite React / Next.js TypeScript app.
- [ ] Develop interactive P&L Heatmap Matrix (Price vs. Expiration grid) with customizable steps and colors.
- [ ] Implement 2D & 3D WebGL P&L charts and real-time Greek sensitivity sliders.
- [ ] Build Custom Multi-Leg Builder (up to 8 legs).

### Phase 4: Programmatic SEO & Ad Engine Integration (Weeks 7–8)
- [ ] Implement dynamic route landing pages (`/calculator/iron-condor-calculator`, etc.).
- [ ] Generate Schema.org JSON-LD structured data and dynamic OpenGraph social cards.
- [ ] Embed non-intrusive AdSense/Carbon Ad placements for Free tier users.

### Phase 5: Cloud Deployment & Governance Pipeline (Weeks 9–10)
- [ ] Create Dockerfiles for `sensen-cpp-engine` and `web-gateway-ui` on Railway.
- [ ] Wire up GitHub Actions / `gh` CLI pipeline for automated tri-agent code reviews (`Claude`, `AGY`, `Cursor`).
- [ ] Perform cross-host floating-point parity verification and performance profiling (VTune / Nsight).

---

## 10. Summary of Document Approvals

| Persona / Role | Agent Name | Approval Status | Date |
| :--- | :--- | :--- | :--- |
| **System Architect & Lead** | Antigravity (AGY) | APPROVED | July 2026 |
| **C++ Core Policy Auditor** | AGY Agent (`cpp_details.txt`) | APPROVED | July 2026 |
| **Security & Algorithm Reviewer** | Claude Agent | PENDING MERGE | July 2026 |
| **Frontend & API Integration Reviewer**| Cursor Agent | PENDING MERGE | July 2026 |
