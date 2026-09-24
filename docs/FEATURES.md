# Feature inventory, as built
@author Olumuyiwa Oluwasanmi

## Scope

This document provides the authoritative, as-built reference inventory of all user-facing features, backend gRPC services, natural-language conversion engines, entitlement and quota controls, and regression test suites implemented in this repository.

The repository serves two distinct products from a single unified C++23 backend engine:
1. **optionsandfuturescalculator.com** — The options and futures strategy calculator, risk modeler, and educational guides. Frontend implementation resides in `frontend/`, and backend RPCs are served by `calculator.OptionsCalculator` (`backend/proto/calculator.proto`) and `calculator.assistant.StrategyAssistant` (`backend/proto/assistant.proto`).
2. **mortgagefvcalculator.com** — The loan amortization, time-value-of-money, real estate, and financial planning engine. Served by `sensen.finance.Finance` (`backend/proto/finance.proto`) and `mortgage.assistant.MortgageAssistant` (`backend/proto/mortgage_assistant.proto`). **Its web client lives in a different repository** (with stubs and contracts mirrored in `clients/mortgagefv/`); only the backend engine, model runtime, and verification systems are housed here.

Files covered by this inventory:
- **Frontend Presentation & Routes**:
  - `frontend/src/app/page.tsx`, `frontend/src/app/layout.tsx`, `frontend/src/app/calculator/[strategy]/page.tsx`, `frontend/src/app/guides/page.tsx`, `frontend/src/app/guides/[strategy]/page.tsx`, `frontend/src/app/widget/page.tsx`, `frontend/src/app/privacy/page.tsx`, `frontend/src/app/terms/page.tsx`
  - `frontend/src/components/StrategyWorkspace.tsx`, `frontend/src/components/StrategySelector.tsx`, `frontend/src/components/OptionTicket.tsx`, `frontend/src/components/PositionLegs.tsx`, `frontend/src/components/ExerciseStylePanel.tsx`, `frontend/src/components/BermudanDateBuilder.tsx`, `frontend/src/components/OptionChain.tsx`, `frontend/src/components/TermStructure.tsx`, `frontend/src/components/PayoffLadder.tsx` (unrendered), `frontend/src/components/ProbabilityCurve.tsx`, `frontend/src/components/PnLMatrix.tsx`, `frontend/src/components/PnLSurface.tsx`, `frontend/src/components/StrategyMetrics.tsx`, `frontend/src/components/SavedScenarios.tsx`, `frontend/src/components/AssistantPanel.tsx`, `frontend/src/components/SponsoredBrokers.tsx`, `frontend/src/components/BrokerRouter.tsx` (unrendered), `frontend/src/components/SiteGuide.tsx`, `frontend/src/components/StrategyGuide.tsx`, `frontend/src/components/ThemeToggle.tsx`, `frontend/src/components/TopBar.tsx`, `frontend/src/components/SiteNav.tsx`, `frontend/src/components/AdSlot.tsx`, `frontend/src/components/AuthUI.tsx`, `frontend/src/components/ProPanel.tsx`, `frontend/src/components/UpgradePrompt.tsx`
- **Frontend Configuration & Stores**:
  - `frontend/src/config/strategies.ts`, `frontend/src/config/affiliates.ts`, `frontend/src/config/ad-routes.ts`, `frontend/src/content/strategy-guides.ts`
  - `frontend/src/store/useCalculatorStore.ts`, `frontend/src/store/useAssistantStore.ts`, `frontend/src/store/useSavedScenariosStore.ts`, `frontend/src/store/useTreePricerStore.ts`
  - `frontend/src/lib/chainFreshness.ts`, `frontend/src/lib/licence.ts`, `frontend/src/lib/useProStatus.ts`
  - `frontend/scripts/check-export.mjs`
- **Backend Service Contracts (Protobuf)**:
  - `backend/proto/calculator.proto`, `backend/proto/finance.proto`, `backend/proto/assistant.proto`, `backend/proto/mortgage_assistant.proto`
- **Backend Engine Modules & Implementations**:
  - `backend/src/modules/calculator_service.cppm`, `backend/src/modules/calculator_service.cpp`
  - `backend/src/modules/strategy_catalogue.cppm`
  - `backend/src/modules/market_data.cppm`
  - `backend/src/modules/finance_service.cppm`, `backend/src/modules/finance_service.cpp`
  - `backend/src/modules/assistant_service.cppm`, `backend/src/modules/assistant_service.cpp`, `backend/src/modules/assistant_verification.cppm`
  - `backend/src/modules/mortgage_assistant_service.cppm`, `backend/src/modules/mortgage_assistant_service.cpp`, `backend/src/modules/mortgage_grammar.cppm`, `backend/src/modules/mortgage_verification.cppm`
  - `backend/src/modules/api_key.cppm`, `backend/src/modules/api_key.cpp`
  - `backend/src/modules/quota.cppm`, `backend/src/modules/quota.cpp`
  - `backend/src/modules/strategy_store.cppm`, `backend/src/modules/strategy_store.cpp`
  - `backend/src/modules/state_refresh.cppm`, `backend/src/modules/state_refresh.cpp`
  - `backend/src/modules/inference_admission.cppm`, `backend/src/modules/inference_admission.cpp`, `backend/src/modules/inference_queue.cppm`, `backend/src/modules/inference_queue.cpp`
  - `backend/src/modules/sgee_queue_client.cppm`, `backend/src/modules/sgee_queue_client.cpp`
  - `backend/src/modules/pg.cppm`
  - `backend/src/modules/fips_mode.cppm`
- **Test Suites & Verification Harnesses**:
  - `backend/tests/test_calculator_service.cpp`, `backend/tests/test_api_key_entitlement.cpp`, `backend/tests/test_finance_service_validation.cpp`, `backend/tests/test_assistant_service.cpp`, `backend/tests/test_assistant_verification.cpp`, `backend/tests/test_mortgage_assistant_service.cpp`, `backend/tests/test_mortgage_grammar.cpp`, `backend/tests/test_mortgage_verification.cpp`, `backend/tests/test_option_pricing_service.cpp`, `backend/tests/test_state_assumptions_gate.cpp`, `backend/tests/test_state_refresh.cpp`, `backend/tests/test_strategy_store_pg.cpp`, `backend/tests/test_market_data_resilience.cpp`, `backend/tests/test_inference_admission.cpp`, `backend/tests/test_quota_tier_label.cpp`, `backend/tests/test_vendored_proto_drift.cpp`
  - `backend/src/smoke_client.cpp`
  - `frontend/src/**/*.test.ts`

---

## 1. Calculator features

The calculator frontend is structured around a centralized reactive store (`frontend/src/store/useCalculatorStore.ts`) that dispatches to the C++ engine over gRPC-Web (proxied by Envoy on port 8080 to `:50051`).

### Feature inventory table

| Feature | Route or Component | RPC Called | Entitlement Required | Gate Enforcement Location | Render Status / Verification Note |
| --- | --- | --- | --- | --- | --- |
| Strategy Modeling (presets) | `StrategySelector.tsx` | None (populates ticket / legs store) | Anonymous | N/A (client catalogue) | Rendered in `StrategyWorkspace.tsx:173` and `widget/page.tsx:78`. Lists 48 strategies; `crack_321` is marked `gated: true` (`StrategySelector.tsx:217`). |
| Leg Builder & Order Ticket | `OptionTicket.tsx`, `PositionLegs.tsx` | `calculator.OptionsCalculator/CalculateStrategy` | Single-leg: Anonymous; Multi-leg (>1): Pro | `backend/src/modules/calculator_service.cpp:1214` calling `backend/src/modules/api_key.cpp:457` (`check_strategy_entitlement`) | Rendered in `StrategyWorkspace.tsx:204,207`. Commits draft legs (`useCalculatorStore.ts:823-868`). Refuses null strike, null premium, or empty expiry. |
| Exercise Style & Averaging Panel | `ExerciseStylePanel.tsx`, `BermudanDateBuilder.tsx` | `sensen.finance.Finance/PriceOptionTree` | Anonymous (metered against quota) | `backend/src/modules/finance_service.cpp:964` (`CHARGE`) | Rendered in `StrategyWorkspace.tsx:210`. Prices European, American, Bermudan, and Asian options on a trinomial tree (`useTreePricerStore.ts:377-458`). |
| Option Chain & Freshness Badge | `OptionChain.tsx` | `calculator.OptionsCalculator/GetMarketChain` | Anonymous | None (open read) | Rendered in `StrategyWorkspace.tsx:283` and `widget/page.tsx:87`. Freshness badge evaluated by `chainFreshness.ts:24` against 15s ticker (`OptionChain.tsx:48`). Centers on ATM strike (`OptionChain.tsx:89-95`). |
| Futures Term Structure | `TermStructure.tsx` | `calculator.OptionsCalculator/GetMarketChain` | Anonymous | None (open read) | Rendered in `StrategyWorkspace.tsx:265`. Conditionally rendered **only** when `assetClass === 'FUTURES'` (`TermStructure.tsx:34`). Displays cost-of-carry curve with `MODELLED` badge. |
| Strategy Payoff Curve | `ProbabilityCurve.tsx` | `calculator.OptionsCalculator/CalculateStrategy` | Single-leg: Anonymous; Multi-leg: Pro | `backend/src/modules/calculator_service.cpp:1214` | Rendered in `StrategyWorkspace.tsx:245` and `widget/page.tsx:84`. Plots `StrategyResponse.pnl_matrix` (`expiryCurve`). |
| Payoff Ladder (Tabular) | `PayoffLadder.tsx` | `calculator.OptionsCalculator/CalculateStrategy` | Single-leg: Anonymous; Multi-leg: Pro | `backend/src/modules/calculator_service.cpp:1214` | **Exists in codebase but UNRENDERED**. Imported at `StrategyWorkspace.tsx:6` but omitted from workspace layout. |
| P&L Matrix & User Price Bounds | `PnLMatrix.tsx` | `calculator.OptionsCalculator/CalculateStrategy` | Single-leg: Anonymous; Multi-leg: Pro | `backend/src/modules/calculator_service.cpp:1214` | Rendered in `StrategyWorkspace.tsx:253`. Maps `StrategyResponse.matrix` (price $\times$ date grid). Bounds committed on blur/Enter to `matrix_price_min`/`matrix_price_max` (`useCalculatorStore.ts:508-509`). |
| 3D P&L Surface | `PnLSurface.tsx` | `calculator.OptionsCalculator/CalculateStrategy` | Single-leg: Anonymous; Multi-leg: Pro | `backend/src/modules/calculator_service.cpp:1214` | Rendered in `StrategyWorkspace.tsx:259`. Three.js / Canvas surface. **Disabled by default** (`useState(false)` at `PnLSurface.tsx:73`); requires user click to activate. |
| Probability Distribution | `ProbabilityCurve.tsx` | `calculator.OptionsCalculator/CalculateStrategy` | Single-leg: Anonymous; Multi-leg: Pro | `backend/src/modules/calculator_service.cpp:1214` | Rendered in `StrategyWorkspace.tsx:245`. Overlays lognormal density under GBM, Abramowitz & Stegun erf polynomial (`ProbabilityCurve.tsx:24-36`), and shaded profit area (POP). |
| Strategy Metrics Summary | `StrategyMetrics.tsx` | `calculator.OptionsCalculator/CalculateStrategy` | Single-leg: Anonymous; Multi-leg: Pro | `backend/src/modules/calculator_service.cpp:1214` | Rendered in `StrategyWorkspace.tsx:286` and `widget/page.tsx:79`. Displays max profit, max loss, breakevens, expected value, PoP, aggregate net Greeks, and parametric VaR/CVaR 95% and 99%. |
| Saved Scenarios | `SavedScenarios.tsx` | `calculator.OptionsCalculator/SaveStrategy`, `ListStrategies`, `DeleteStrategy` | Pro (Authenticated) | `backend/src/modules/calculator_service.cpp:1702,1764,1796` calling `backend/src/modules/api_key.cpp:480` (`check_saved_scenarios_entitlement`) | Rendered in `StrategyWorkspace.tsx:222`. Unauthenticated callers receive status 16 (`UNAUTHENTICATED`). Free authenticated callers receive status 7 (`PERMISSION_DENIED`). |
| Natural-Language Strategy Assistant | `AssistantPanel.tsx` | `calculator.assistant.StrategyAssistant/ParseStrategy` | Pro | `backend/src/modules/assistant_service.cpp:2877` calling `backend/src/modules/api_key.cpp:537` (`check_assistant_entitlement`) | Rendered in `StrategyWorkspace.tsx:172`. Conversational text input. Free/anonymous callers receive `PERMISSION_DENIED` with `kStrategySurface.message` (`api_key.cppm:432`). |
| Sponsored Broker Links | `SponsoredBrokers.tsx` | None (outbound HTTP anchors) | Anonymous | N/A | Rendered in `frontend/src/app/layout.tsx:237`. Reads `BROKER_PARTNERS` from `affiliates.ts:50-80` (Interactive Brokers, tastytrade, Tradier, Alpaca). All `trackingId` values currently `null`. |
| Broker Order Router Modal | `BrokerRouter.tsx` | None (simulated mock) | Anonymous | N/A | **Exists in codebase but UNRENDERED**. Contains mock alert and commented-out edge function calls (`BrokerRouter.tsx:20-31`). Not imported anywhere. |
| Strategy Guides & SEO Articles | `SiteGuide.tsx`, `StrategyGuide.tsx`, `/guides`, `/guides/[strategy]` | None (static Next.js SSG) | Anonymous | N/A | Rendered on `/guides` and `/guides/[strategy]`. Contains 26 written guides (`strategy-guides.ts:70-1315`). Host of Google AdSense multiplex units (`ad-routes.ts:35`). |
| Embeddable Calculation Widget | `frontend/src/app/widget/page.tsx` | `CalculateStrategy`, `GetRiskFreeRate`, `GetMarketQuote` | Single-leg: Anonymous; Multi-leg: Pro | Engine gRPC gates (`calculator_service.cpp`) | Route `/widget`. Strips site navigation. Accepts `?theme=light|slate` and `?symbol=XYZ` query parameters (`widget/page.tsx:22-23`). |
| Theme Handling | `ThemeToggle.tsx`, `SiteNav.tsx` | None (client `localStorage`) | Anonymous | N/A | Rendered in `TopBar.tsx:49` and `SiteNav.tsx`. Switches `document.documentElement.dataset.theme` between `'slate'` and `'light'` (`ThemeToggle.tsx:42`). Stored under key `ofc-theme`. |

---

## 2. Finance service features

The general-purpose sensen financial engine is exposed via `sensen.finance.Finance` (`backend/proto/finance.proto`). It comprises **46 RPCs** categorized across 12 distinct functional domains. 

All money values in `finance.proto` are represented as decimal `string`s mapping directly to `sensen::BigDecimal` (`Int256` fixed-point scaled by $10^{38}$ since 2026-09-23; `__int128` at $10^{18}$ before), preventing binary float compounding errors over multi-decade amortizations.

```
Domain Breakdown (46 RPCs):
├── Time value of money (10 RPCs)
├── Mortgages & Amortisation (5 RPCs)
├── HELOC & Refinance (2 RPCs)
├── Real Estate & Rent-vs-Buy (5 RPCs)
├── Cash-Flow Analysis, NPV & IRR (6 RPCs)
├── Depreciation (1 RPC)
├── Fixed Income, Bonds & T-Bills (2 RPCs)
├── Futures Pricing, Margin & Hedging (5 RPCs)
├── Option Pricing, Trees & Greeks (4 RPCs)
├── Portfolio Statistics & Optimization (3 RPCs)
├── Closing Costs (1 RPC)
└── State Assumptions & Census ACS (2 RPCs)
```

### 1. Time value of money (10 RPCs)
- **`ComputePayment`** (`PaymentRequest` $\to$ `DecimalResponse`)
- **`ComputePresentValue`** (`PresentValueRequest` $\to$ `DecimalResponse`)
- **`ComputeFutureValue`** (`FutureValueRequest` $\to$ `DecimalResponse`)
- **`ComputeFutureValueDetailed`** (`FutureValueDetailedRequest` $\to$ `FutureValueDetailedResponse`)
- **`ComputeInterestPayment`** (`PeriodPaymentRequest` $\to$ `DecimalResponse`)
- **`ComputePrincipalPayment`** (`PeriodPaymentRequest` $\to$ `DecimalResponse`)
- **`ComputeRate`** (`RateRequest` $\to$ `DecimalResponse`)
- **`ComputePeriods`** (`PeriodsRequest` $\to$ `DecimalResponse`)
- **`ConvertInterestRate`** (`RateConversionRequest` $\to$ `DoubleResponse`)
- **`ComputeFisherRate`** (`FisherRequest` $\to$ `DoubleResponse`)
*Summary*: Computes discrete and continuous compounding, annuity cash flows, principal/interest period allocations, Newton-Raphson interest rate and term solves, and inflation-adjusted Fisher real rates.

### 2. Amortisation and mortgage (5 RPCs)
- **`ComputeAmortization`** (`AmortizationRequest` $\to$ `AmortizationResponse`)
- **`ComputeDetailedAmortization`** (`DetailedAmortizationRequest` $\to$ `DetailedAmortizationResponse`)
- **`ComputeAmortizationBatch`** (`AmortizationBatchRequest` $\to$ `AmortizationBatchResponse`)
- **`ComputePayoffTiming`** (`PayoffTimingRequest` $\to$ `PayoffTimingResponse`)
- **`ComputeMortgageRecast`** (`MortgageRecastRequest` $\to$ `MortgageRecastResponse`)
*Summary*: Computes full periodic loan amortization schedules with PMI elimination, tax deduction schedules, parallel multi-offer comparison batches, early extra-payment payoff timelines, and post-lump-sum recast re-amortizations.

### 3. HELOC and refinance (2 RPCs)
- **`ComputeHeloc`** (`HelocRequest` $\to$ `HelocResponse`)
- **`ComputeRefinance`** (`RefinanceRequest` $\to$ `RefinanceResponse`)
*Summary*: Computes two-phase home equity lines of credit (draw period with interest-only options followed by repayment amortisation) and loan refinance breakeven economics accounting for closing fees.

### 4. Rent-vs-buy and real estate valuation (5 RPCs)
- **`ComputeRentVsBuy`** (`RentVsBuyRequest` $\to$ `RentVsBuyResponse`)
- **`ComputeRentVsBuyBatch`** (`RentVsBuyBatchRequest` $\to$ `RentVsBuyBatchResponse`)
- **`ComputeRentalRoi`** (`RentalRoiRequest` $\to$ `RentalRoiResponse`)
- **`ComputeHomeFutureValue`** (`HomeFutureValueRequest` $\to$ `HomeFutureValueResponse`)
- **`ComputeHomeNpv`** (`HomeNpvRequest` $\to$ `HomeNpvResponse`)
*Summary*: Computes multi-decade net wealth differences between homeownership and renting with capital reinvestment, property capitalization rates and cash-on-cash ROI, home appreciation trajectories, and housing net present values.

### 5. NPV/IRR and dated cash flows (6 RPCs)
- **`ComputeNpv`** (`NpvRequest` $\to$ `DoubleResponse`)
- **`ComputeIrr`** (`IrrRequest` $\to$ `DoubleResponse`)
- **`ComputeXnpv`** (`DatedCashFlowRequest` $\to$ `DoubleResponse`)
- **`ComputeXirr`** (`DatedCashFlowRequest` $\to$ `DoubleResponse`)
- **`ComputePaybackPeriod`** (`PaybackRequest` $\to$ `DoubleResponse`)
- **`ComputeCumulative`** (`CumulativeRequest` $\to$ `DoubleResponse`)
*Summary*: Computes discounted net present value and internal rate of return for both fixed-period arrays and exact calendar-dated cash-flow schedules (XNPV/XIRR), simple/discounted payback horizons, and cumulative interest/principal sums.

### 6. Depreciation (1 RPC)
- **`ComputeDepreciation`** (`DepreciationRequest` $\to$ `DoubleResponse`)
*Summary*: Computes asset depreciation deductions across Straight-Line, Sum-of-Years' Digits, Declining Balance (with salvage floors), and Modified Accelerated Cost Recovery System (MACRS half-year tables for 3, 5, 7, 10, 15, and 20-year property classes).

### 7. Bonds and bills (2 RPCs)
- **`AnalyzeBond`** (`BondRequest` $\to$ `BondResponse`)
- **`AnalyzeTreasuryBill`** (`TreasuryBillRequest` $\to$ `TreasuryBillResponse`)
*Summary*: Computes fixed-coupon bond pricing, yield-to-maturity, Macaulay duration, modified duration, convexity, and short-term Treasury bill money-market and bond-equivalent discount yields.

### 8. Futures and hedging (5 RPCs)
- **`PriceFutures`** (`FuturesPricingRequest` $\to$ `DoubleResponse`)
- **`ValueFutures`** (`FuturesValuationRequest` $\to$ `DoubleResponse`)
- **`SimulateMarginAccount`** (`MarginSimulationRequest` $\to$ `MarginSimulationResponse`)
- **`ComputeHedge`** (`HedgeRequest` $\to$ `HedgeResponse`)
- **`ComputeCommoditySpread`** (`CommoditySpreadRequest` $\to$ `DoubleResponse`)
*Summary*: Computes theoretical futures forward pricing under cost-of-carry with storage/dividend yields, mark-to-market contract values, multi-day margin maintenance simulation with margin-call detection, minimum-variance beta hedge ratios, and crack/spark/crush energy and agriculture spread differentials.

### 9. Options pricing and Greeks (4 RPCs)
- **`PriceOptionTree`** (`OptionTreeRequest` $\to$ `OptionPricingResponse`)
- **`PriceBlackScholes`** (`BlackScholesRequest` $\to$ `BlackScholesResponse`)
- **`PriceOptionMonteCarlo`** (`MonteCarloRequest` $\to$ `DoubleResponse`)
- **`ComputeProbabilityTree`** (`ProbabilityTreeRequest` $\to$ `ProbabilityTreeResponse`)
*Summary*: Computes option fair values, early-exercise boundary premiums, and Greeks ($\Delta, \Gamma, \Theta, \mathcal{V}, \rho$) using trinomial trees (supporting European, American, Bermudan exercise dates, and Asian average-price/average-strike averaging), closed-form Black-Scholes-Merton, pseudo-random Monte Carlo paths, and discrete risk-neutral probability trees.

### 10. Portfolio statistics and optimization (3 RPCs)
- **`ComputePortfolioStats`** (`PortfolioStatsRequest` $\to$ `PortfolioStatsResponse`)
- **`OptimizePortfolio`** (`PortfolioOptimizeRequest` $\to$ `PortfolioOptimizeResponse`)
- **`ComputeRiskContributions`** (`RiskContributionRequest` $\to$ `RiskContributionResponse`)
*Summary*: Computes annualized mean returns, covariance matrices, portfolio variance, Sharpe and Sortino ratios, Markowitz mean-variance efficient portfolio weights, and percentage risk contribution decompositions per asset.

### 11. Closing costs (1 RPC)
- **`ComputeClosingCosts`** (`ClosingCostsRequest` $\to$ `ClosingCostsResponse`)
- *Summary*: Computes itemised residential purchase closing expenses including lender origination fees, discount points, third-party title/appraisal/recording services, property tax and insurance escrow reserves, prepaid interest, and net cash-to-close after seller credits.

### 12. State assumptions (2 RPCs)
- **`RefreshStateAssumptions`** (`RefreshStateAssumptionsRequest` $\to$ `RefreshStateAssumptionsResponse`)
- **`GetStateAssumptions`** (`GetStateAssumptionsRequest` $\to$ `GetStateAssumptionsResponse`)
- *Summary*: Fetches, bounds-checks, and writes US Census Bureau ACS 5-year housing estimates across all 50 states into PostgreSQL (restricted to `partner` credentials), and serves public per-state baseline property tax rates, median home prices, and insurance costs.

---

## 3. The two assistants

The engine hosts **two separate, fine-tuned in-process language models** (0.6B parameters, Q8_0 quantized on CPU via `sensen`):
1. **Strategy Assistant** (`calculator.assistant.StrategyAssistant/ParseStrategy` defined in `backend/proto/assistant.proto`)
2. **Mortgage Assistant** (`mortgage.assistant.MortgageAssistant/ParseOperation` defined in `backend/proto/mortgage_assistant.proto`)

### Core invariants common to both assistants
- **Neither assistant computes anything**. They are natural-language semantic decoders, not math calculators.
  - The Strategy Assistant converts words into parameters for `calculator.OptionsCalculator/CalculateStrategy`.
  - The Mortgage Assistant converts words into an operation name and request JSON for `sensen.finance.Finance`.
- **Both return gRPC status `OK` on three distinct outcomes**:
  1. `params` — Successful conversion into validated parameters.
  2. `clarification` — The model detected an ambiguous request and asks a clarifying question rather than hallucinating defaults.
  3. `refusal` — The model declined the request (unsupported structure, unknown symbol, ungrounded number, or out of scope).
  Encoding clarification or refusal as a gRPC transport error is strictly prohibited: returning an error code would corrupt service latency and error metrics when the model performed its function correctly.
- **Both are gated by the Pro entitlement**. Unentitled callers receive status 7 (`PERMISSION_DENIED`).
  - Strategy Assistant gate: `backend/src/modules/assistant_service.cpp:2877` calling `check_assistant_entitlement` with `kStrategySurface` (`backend/src/modules/api_key.cppm:430`). Refusal message: `"The natural-language strategy assistant is a Pro feature. The calculator itself remains free -- build your strategy manually with the symbol and strategy selectors, or upgrade for assisted parsing."`
  - Mortgage Assistant gate: `backend/src/modules/mortgage_assistant_service.cpp:3139` calling `check_assistant_entitlement` with `kMortgageSurface` (`backend/src/modules/api_key.cppm:454`). Refusal message: `"The natural-language mortgage assistant is a Pro feature. The calculations themselves remain free: call the sensen.finance.Finance operation you want directly, or upgrade to have it chosen and filled in for you."`
- **Neither gives financial, legal, or investment advice**. Advice requests are refused by design (`agent/dataset/build_mortgage_dataset.py`, `backend/proto/mortgage_assistant.proto:32-38`).

### Mortgage Assistant operation label space

The Mortgage Assistant's label space is derived mechanically from `backend/proto/finance.proto` and validated by `backend/src/modules/mortgage_verification.cppm` and `backend/src/modules/mortgage_grammar.cppm`.

- **Label Space Size**: Exactly **27 operations** (`backend/src/modules/mortgage_assistant_service.cpp:1844` and `backend/src/modules/mortgage_verification.cppm:384`), spanning **184 distinct fields** (`mortgage_verification.cppm:197`).
- **Complete Inventory of 27 Operation Identifiers**:
  1. `ComputeAmortization`
  2. `ComputeAmortizationBatch`
  3. `ComputeClosingCosts`
  4. `ComputeCumulative`
  5. `ComputeDepreciation`
  6. `ComputeDetailedAmortization`
  7. `ComputeFutureValue`
  8. `ComputeFutureValueDetailed`
  9. `ComputeHeloc`
  10. `ComputeHomeFutureValue`
  11. `ComputeHomeNpv`
  12. `ComputeInterestPayment`
  13. `ComputeIrr`
  14. `ComputeMortgageRecast`
  15. `ComputeNpv`
  16. `ComputePaybackPeriod`
  17. `ComputePayment`
  18. `ComputePayoffTiming`
  19. `ComputePeriods`
  20. `ComputePresentValue`
  21. `ComputePrincipalPayment`
  22. `ComputeRate`
  23. `ComputeRefinance`
  24. `ComputeRentVsBuy`
  25. `ComputeRentalRoi`
  26. `ComputeXirr`
  27. `ComputeXnpv`

- **Enforced Verification Gates (`mortgage_verification.cppm:48-68`)**:
  - `G1` (Operation vocabulary): Must match one of the 27 operations in `kOperationIds` (`mortgage_verification.cppm:384`).
  - `G2` (Field schema): Keys must match declared proto fields; enum values must match `kEnumConstants` (`mortgage_verification.cppm:407`).
  - `G3` (Value grounding): Numerical values emitted by the model must appear in the user utterance or match approved mathematical convention values (`mortgage_verification.cppm:181-193`).
  - `G4` (Structural presence): Emitted JSON must contain a well-formed `<params>` block.
  - `G5` (Plausibility bounds): Numerical parameters must satisfy physiological financial bounds (e.g. loan amount $\le \$100,000,000$, rate $\le 30\%$, term $\le 50$ years).

---

## 4. Entitlement and quota

Access control and usage limits are enforced in-process by `backend/src/modules/api_key.cpp` and `backend/src/modules/quota.cpp`.

### Tier entitlement and quota allowances

Allowances in production are configured via the `QUOTA_POLICY` environment variable. The table below lists the live production quotas (configured in production deployment via `QUOTA_POLICY`) alongside documented reference baselines (`docs/FINANCE_API.md:427-430`).

| Tier | Unlocks | Requests / Minute (Prod) | Compute Units / Hour (Prod) | Documented Baseline (Reference) | Bucket Scope & State Invariant |
| --- | --- | --- | --- | --- | --- |
| `anonymous` | Single-leg strategy calculation (`legs <= 1`), live market quotes, option chain & forward curves, Treasury risk-free rate, public Finance calculations, `GetStateAssumptions`, static guides, widget. | 6,000 | 120,000 | 60 req/min, 600 CU/hr | **Shared site-wide** in a single `~anonymous` bucket, **per replica**. |
| `free` | Authenticated user account. Same calculation capabilities as anonymous, but isolated from shared anonymous burst traffic. Multi-leg and assistants remain locked. | 120 | 3,600 | 600 req/min, 10,000 CU/hr | Dedicated bucket per authenticated caller ID, **per replica**. |
| `pro` | Multi-leg strategy calculations (`legs > 1`), Strategy Assistant (`ParseStrategy`), Mortgage Assistant (`ParseOperation`), Saved Scenarios (`SaveStrategy`, `ListStrategies`, `DeleteStrategy`). | 600 | 240,000 | 3,000 req/min, 200,000 CU/hr | Dedicated bucket per authenticated caller ID, **per replica**. |
| `partner` | Full access to all Pro features plus administrative access to Census ACS data writing (`RefreshStateAssumptions`), custom per-key SLA overrides. | 2,400 | 1,200,000 | 6,000 req/min, 500,000 CU/hr | Dedicated bucket per API key ID, **per replica**. |

> [!IMPORTANT]
> **Allowances are strictly PER REPLICA.**
> Quota token buckets (`Bucket rate`, `Bucket budget` in `backend/src/modules/quota.cpp:64-69`) are maintained in an in-memory `std::unordered_map<std::string, CallerState>` protected by a local process mutex (`quota.cpp:82,87`). There is no distributed Redis or cross-replica coordination.
> When the engine runs $N$ container replicas (e.g. `numReplicas: 3` in production `railway.json`), the effective site-wide capacity is multiplied by $N$ (e.g., $3 \times 6,000 = 18,000$ anonymous requests/minute). Refusals occur when an individual replica's local token bucket is exhausted.

---

## 5. Feature gates and regression tests

Every major feature and security boundary is enforced by automated test targets and build-time gates:

| Feature / Subsystem | Gate Mechanism | Test Target / File | Failure Mode If Regressed |
| --- | --- | --- | --- |
| Strategy Payoff & SGEE Graph | In-process gRPC test against loopback server with full action graph | `test_calculator_service` (`backend/tests/test_calculator_service.cpp`) | Fails if 580/600 bull call spread does not yield maxProfit=1275, breakEven=587.25; fails if action registration is incomplete. |
| Multi-Leg Entitlement Gate | Unit verification of `check_strategy_entitlement` | `test_api_key_entitlement` (`backend/tests/test_api_key_entitlement.cpp:105-225`) | Fails if multi-leg calculation is permitted anonymously or if refusal message does not specify leg count. |
| Assistant Entitlement Gates | Unit verification of `check_assistant_entitlement` for both surfaces | `test_api_key_entitlement` (`backend/tests/test_api_key_entitlement.cpp:226-331`) | Fails if unentitled callers receive OK; fails if mortgage surface returns strategy selector copy. |
| Saved Scenarios Pro Gate | Integration test of `check_saved_scenarios_entitlement` | `test_calculator_service` (`backend/tests/test_calculator_service.cpp:722-850`) | Fails if anonymous caller is not rejected with `UNAUTHENTICATED`, or free caller is not rejected with `PERMISSION_DENIED`. |
| Asian Leg Rejection | Precondition check in calculator engine | `test_calculator_service` (`backend/tests/test_calculator_service.cpp:641-710`) | Fails if Asian leg returns `OK` or is evaluated against terminal spot rather than returning `FAILED_PRECONDITION`. |
| Matrix Price Bounds | Request parameter validation in SGEE grid builder | `test_calculator_service` (`backend/tests/test_calculator_service.cpp:932-1050`) | Fails if user min/max bounds truncate expiry curve instead of scoping to the matrix grid. |
| Finance Service Input Validation | Direct gRPC validation suite across 27 failure sections | `test_finance_service_validation` (`backend/tests/test_finance_service_validation.cpp`) | Fails on NaN/Infinity inputs, overflow in amortization, TVM same-signed loan solve errors, or MACRS zero-class regression. |
| Option Pricing & Trees | Closed-form put-call parity and tree convergence | `test_option_pricing_service` (`backend/tests/test_option_pricing_service.cpp`) | Fails if tree pricing deviates from Black-Scholes limits or Bermudan dead band is violated. |
| Strategy Assistant Verification | Rule-based reasoning domain policy checks | `test_assistant_verification` (`backend/tests/test_assistant_verification.cpp`) | Fails if uncatalogued strategies, hallucinated symbols, or unsupported assets pass verification. |
| Mortgage Grammar Constrained Decoding | State machine token mask automaton checks | `test_mortgage_grammar` (`backend/tests/test_mortgage_grammar.cpp`) | Fails if grammar generates malformed JSON, invalid field names, or uncatalogued operations. |
| Mortgage Value Grounding | Utterance lexical grounding and plausibility bounds | `test_mortgage_verification` (`backend/tests/test_mortgage_verification.cpp`) | Fails if ungrounded numbers pass verification or proto-drift occurs between `finance.proto` and `kLabelSpace`. |
| State Assumptions Partner Gate | Role-based gate on Census write RPC | `test_state_assumptions_gate` (`backend/tests/test_state_assumptions_gate.cpp`) | Fails if non-partner tier executes `RefreshStateAssumptions`. |
| State Refresh Transaction & Bounds | Advisory locks and plausibility thresholds | `test_state_refresh` (`backend/tests/test_state_refresh.cpp`) | Fails if fewer than 45 states update, or if editorial columns (`insurance_annual`, `note`) are overwritten. |
| Market Data Resilience & Circuit Breaker | Provider failure and backoff simulator | `test_market_data_resilience` (`backend/tests/test_market_data_resilience.cpp`) | Fails if HTTP error loops indefinitely without tripping `CircuitOpen`. |
| Quota Metering & Tier Labels | Token bucket rate and compute unit tests | `test_quota_tier_label` (`backend/tests/test_quota_tier_label.cpp`) | Fails if undefined tiers fail open rather than falling back to anonymous limits. |
| Protobuf Contract Synchronization | Build-time diff against vendored proto headers | `test_vendored_proto_drift` (`backend/tests/test_vendored_proto_drift.cpp`) | Fails if `backend/proto/*.proto` drift from generated C++ / TypeScript stubs. |
| AdSense Route Policy Isolation | Build-time export check script | `frontend/scripts/check-export.mjs`, `frontend/src/config/ad-routes.test.ts` | Fails `npm run build` if any page outside `/guides/[strategy]` contains Google AdSense tags or `<ins>` units. |

---

## 6. Not built / deliberately excluded

The following features were specified in initial architecture proposals (`docs/PRD_OPTIONS_AND_FUTURES_CALCULATOR.md`) or roadmap discussions, but **do not exist in the code**. They are documented here to prevent readers from confusing roadmap intentions with running software.

1. **Zero Display Ads Policy**
   - *Specified in*: `PRD_OPTIONS_AND_FUTURES_CALCULATOR.md` §2.1 ("Display ads are omitted entirely in Phase 1").
   - *Actual Implementation*: Display ads **are live** on the site. Google AdSense multiplex units are embedded on all 26 strategy guide articles (`frontend/src/content/strategy-guides.ts`, `frontend/src/app/guides/[strategy]/page.tsx`). Advertising is strictly forbidden on calculator, widget, terms, and privacy screens (`frontend/src/config/ad-routes.ts:35-56`).
2. **Automated Broker CPA Lead Attribution via Supabase Edge Functions**
   - *Specified in*: `PRD_OPTIONS_AND_FUTURES_CALCULATOR.md` §2.1, §4.2 (`log_broker_lead`, `lead-attribution` Deno Edge Functions).
   - *Actual Implementation*: **Wired to nothing.** The Deno function itself DOES exist
     (`supabase/functions/lead-attribution/index.ts`, 1,538 bytes) and inserts UTM
     parameters into a `leads` table — so "there are no Supabase Edge Functions in this
     repository" would be wrong, and an earlier draft of this document said exactly that.
     What is missing is everything on both sides of it: **nothing calls it** (no reference to
     `lead-attribution` or `functions/v1` anywhere under `frontend/src`, `workers/` or
     `clients/`), and **no migration in this repository creates the `leads` table** it writes
     to — `supabase/migrations/20260723000000_initial_schema.sql:150` only mentions leads in a
     comment. Broker links in `SponsoredBrokers.tsx` are plain static outbound links
     (`frontend/src/config/affiliates.ts:47-63`) with `trackingId: null`, which that file's own
     header explains is the deliberate record of "no affiliate agreement signed yet". No CPA
     webhook and no click-attribution token is generated anywhere.
     **Deployed source with no caller and no table reads exactly like a shipped feature at a
     glance; that is why it is listed here rather than under section 1.**
3. **Interactive Broker Order Routing Modal (`BrokerRouter.tsx`)**
   - *Specified in*: `PRD_OPTIONS_AND_FUTURES_CALCULATOR.md` §2.1 ("Execute via Partner Broker single-click deep link").
   - *Actual Implementation*: `frontend/src/components/BrokerRouter.tsx` exists in the filesystem as an unrendered mock containing browser `alert()` popups. **It is not imported or rendered anywhere in the application**.
4. **WebSocket Live Streaming Matrix (`StreamLiveMatrix`)**
   - *Specified in*: `PRD_OPTIONS_AND_FUTURES_CALCULATOR.md` §5.3 (`rpc StreamLiveMatrix (stream CalculationRequest) returns (stream CalculationResponse)`).
   - *Actual Implementation*: **Not built**. The C++ engine serves strictly unary gRPC over HTTP/2 and gRPC-Web (`backend/proto/calculator.proto:15-35`). There is no streaming RPC, no duplex socket, and no WebSocket listener on port 50051 or 8080.
5. **Dynamic OpenGraph Image Generation Engine**
   - *Specified in*: `PRD_OPTIONS_AND_FUTURES_CALCULATOR.md` §6.2 ("Edge functions generate on-the-fly og:image PNGs showcasing a 3D P&L curve").
   - *Actual Implementation*: **Not built**. No dynamic image generator or `opengraph-image.tsx` exists in `frontend/src/app`.
6. **Programmatic Per-Ticker Landing Pages (`/ticker/<symbol>/...`)**
   - *Specified in*: `PRD_OPTIONS_AND_FUTURES_CALCULATOR.md` §6 ("Thousands of distinct route pages... /ticker/AAPL/options-calculator").
   - *Actual Implementation*: **Deliberately excluded**. As recorded in PRD §6.1, the site generates exactly **26 strategy pairs** (calculator and guide), not thousands of ticker permutations.
7. **Heston Stochastic Volatility Model for Strategy Payoffs**
   - *Specified in*: `PRD_OPTIONS_AND_FUTURES_CALCULATOR.md` §3.2.
   - *Actual Implementation*: `calculator_service.cpp` and `ProbabilityCurve.tsx` utilize standard lognormal Geometric Brownian Motion (GBM) and Black-Scholes-Merton formulations. Heston characteristic-function numerical integration is not wired into the calculator strategy endpoint.
8. **3-2-1 Crack Spread (`crack_321`) in Natural-Language Assistant**
   - *Specified in*: `frontend/src/components/StrategySelector.tsx:217`.
   - *Actual Implementation*: The UI lists `crack_321` with `gated: true`. The backend runtime catalogue (`backend/src/modules/strategy_catalogue.cppm:67-115`) contains **47 strategies** and deliberately excludes `crack_321`. If the Strategy Assistant emits `crack_321`, it is refused.
9. **Tabular Payoff Ladder Component (`PayoffLadder.tsx`)**
   - *Specified in*: `frontend/src/components/PayoffLadder.tsx`.
   - *Actual Implementation*: Imported in `StrategyWorkspace.tsx:6`, but **never rendered** in the JSX.
10. **Direct Execution of Asian Options in Strategy Calculator**
    - *Specified in*: `frontend/src/components/ExerciseStylePanel.tsx`.
    - *Actual Implementation*: `calculator.OptionsCalculator/CalculateStrategy` explicitly **refuses** positions containing Asian options (`backend/src/modules/calculator_service.cpp:658-665`), returning status `FAILED_PRECONDITION`. Asian contracts must be evaluated independently via `sensen.finance.Finance/PriceOptionTree`.

---

## Frontend architecture & component inventory

### Frontend Workspace & Core Components

- **Files**: `frontend/src/components/StrategyWorkspace.tsx`, `StrategySelector.tsx`, `OptionTicket.tsx`, `PositionLegs.tsx`, `ExerciseStylePanel.tsx`, `BermudanDateBuilder.tsx`, `OptionChain.tsx`, `TermStructure.tsx`, `ProbabilityCurve.tsx`, `PnLMatrix.tsx`, `PnLSurface.tsx`, `StrategyMetrics.tsx`, `SavedScenarios.tsx`, `AssistantPanel.tsx`, `ThemeToggle.tsx`, `SponsoredBrokers.tsx`.
- **Purpose**: Implements the responsive desktop and mobile trading terminal layout, user input ticket, real-time analytics graphs, and reference chain tables.

| Component / Symbol | Export / Type | Purpose & Behavior |
| --- | --- | --- |
| `StrategyWorkspace` | Function Component | Four-column responsive grid layout (`StrategyWorkspace.tsx:159-164`) hosting the picker, ticket, result plots, and reference chain. Enforces `overflowX: 'auto'` and `overflowY: 'hidden'` (`StrategyWorkspace.tsx:99-100`). |
| `StrategySelector` | Function Component | Displays searchable strategy library across 6 categories (Bullish, Bearish, Neutral, Volatility, Income & Hedge, Futures). Contains 48 definitions (`StrategySelector.tsx:67-237`). |
| `OptionTicket` | Function Component | Form for drafting options legs before committing to position. Provides inputs for Action, Type, Expiration, Strike, Premium, Quantity, IV, and Asian style. |
| `PositionLegs` | Function Component | List of active position legs with inline editing for strike, premium, quantity, IV, and delete controls. |
| `ExerciseStylePanel` | Function Component | Segmented selector for European, American, Bermudan exercise styles and Asian averaging. Drives `useTreePricerStore`. |
| `BermudanDateBuilder` | Function Component | Sub-panel for configuring discrete Bermudan exercise dates by preset (Monthly, Quarterly, Semi-Annual) or custom day additions (`BermudanDateBuilder.tsx:1-85`). |
| `OptionChain` | Function Component | High-density option chain table displaying Calls and Puts across strikes. Implements automated centering on the at-the-money row (`OptionChain.tsx:89-95`). |
| `TermStructure` | Function Component | Table displaying forward futures contracts, basis, and annualised carry (`TermStructure.tsx:76-105`). Active only when `assetClass === 'FUTURES'` (`TermStructure.tsx:34`). |
| `ProbabilityCurve` | Function Component | SVG visualizer combining lognormal probability density of terminal underlying price with strategy payoff curve. Shaded teal in profit zone (`ProbabilityCurve.tsx:1-250`). |
| `PnLMatrix` | Function Component | Two-dimensional price $\times$ date P&L table. Allows switching between dollar P&L and return-on-risk percentage. Hosts custom min/max price boundary inputs (`PnLMatrix.tsx:1-392`). |
| `PnLSurface` | Function Component | WebGL 3D height-field surface rendered using Three.js (`@react-three/fiber`). Disabled by default (`PnLSurface.tsx:73`). |
| `StrategyMetrics` | Function Component | Tabular readouts for Max Profit, Max Loss, Breakevens, Expected Value, POP, Net Greeks ($\Delta, \Gamma, \Theta, \mathcal{V}, \rho$), and VaR/CVaR 95%/99%. |
| `SavedScenarios` | Function Component | Management panel for naming, saving, listing, and deleting user positions (`SavedScenarios.tsx:24-185`). |
| `AssistantPanel` | Function Component | Conversational natural-language interface for `StrategyAssistant`. Parses user sentences into strategy parameters (`AssistantPanel.tsx:1-200`). |
| `ThemeToggle` | Function Component | Two-way segmented control toggling `slate` and `light` themes on `document.documentElement.dataset.theme` (`ThemeToggle.tsx:38-75`). |
| `SponsoredBrokers` | Function Component | Informational cards linking to approved partner brokers (`SponsoredBrokers.tsx:25-138`). |

#### Invariants, refusals, and failure modes
- `OptionTicket` refuses commit with `notReady: "Pick a strike before adding the leg."` if strike is missing or non-positive (`useCalculatorStore.ts:825-828`).
- `OptionTicket` refuses commit with `notReady: "This contract has no quoted price. Enter the price you would pay or receive."` if premium is missing or non-positive (`useCalculatorStore.ts:829-832`).
- `OptionTicket` refuses commit with `notReady: "Pick an expiry before adding the leg."` if expiry date does not match listed expirations (`useCalculatorStore.ts:843-848`).
- `StrategyWorkspace` recalculates only when `legs.length > 0` (`StrategyWorkspace.tsx:51`). Clearing legs wipes calculation results and gate denials (`useCalculatorStore.ts:697-705`).

#### Gotchas
- `PayoffLadder.tsx` is imported in `StrategyWorkspace.tsx:6` but never placed in the JSX; readers looking for the tabular payoff ladder in the UI will find only `ProbabilityCurve.tsx` and `PnLMatrix.tsx`.
- `BrokerRouter.tsx` is not rendered anywhere; partner broker links are rendered exclusively by `SponsoredBrokers.tsx` in `frontend/src/app/layout.tsx:237`.

---

### Frontend Stores & State Management

- **Files**: `frontend/src/store/useCalculatorStore.ts`, `useAssistantStore.ts`, `useSavedScenariosStore.ts`, `useTreePricerStore.ts`, `frontend/src/lib/chainFreshness.ts`, `licence.ts`, `useProStatus.ts`.
- **Purpose**: Manages gRPC client lifecycles, race-condition sequencing tokens, auth metadata headers, and cache freshness states.

| Store / Module | Exported State & Actions | Functionality |
| --- | --- | --- |
| `useCalculatorStore` | `symbol`, `legs`, `spotPrice`, `riskFreeRate`, `result`, `calculateStrategy`, `setSymbol`, `loadChain`, `setMatrixBounds` | Primary reactive calculation store. Dispatches to `OptionsCalculatorClient`. Manages `calculationSeq` token to discard stale responses (`useCalculatorStore.ts:437,964,1099`). |
| `useAssistantStore` | `prompt`, `status`, `params`, `clarification`, `refusal`, `sendPrompt`, `applyParams` | Conversational strategy assistant store. Discriminates responses by outcome oneof (`useAssistantStore.ts:61-65`). Routes status 7 to `gateDenied` and status 9 to `modelLimit`. |
| `useSavedScenariosStore` | `scenarios`, `status`, `failure`, `save`, `remove`, `apply`, `refresh` | Scenario persistence store. Discriminates gRPC status 16 (`UNAUTHENTICATED`) and 7 (`PERMISSION_DENIED`) (`useSavedScenariosStore.ts:70-92`). |
| `useTreePricerStore` | `exerciseType`, `asianType`, `bermudanDates`, `results`, `priceTree` | Trinomial tree pricing store for single contracts. Dispatches to `sensen.finance.Finance/PriceOptionTree`. |
| `chainFreshness` | `chainFreshness(chainFetchedAt, nowMs)` | Computes chain age in seconds and determines live status ($<60\text{s}$) with clock skew tolerance (`chainFreshness.ts:24-55`). |
| `licence` | `authMetadata()`, `startCheckout()` | Assembles `x-api-key` and `Authorization: Bearer <token>` gRPC headers (`licence.ts:1-120`). Initiates Stripe billing sessions. |
| `useProStatus` | `useProStatus()` | Hook resolving Pro status from Supabase session or custom API key (`useProStatus.ts:1-50`). |

#### Invariants, refusals, and failure modes
- `calculateStrategy` returns early with `notReady: "No spot price for {symbol} — cannot price the position."` when `spotPrice <= 0` (`useCalculatorStore.ts:994-1002`).
- `calculateStrategy` returns early with `notReady` message when contract IV is missing (`useCalculatorStore.ts:1003-1022`).
- `calculateStrategy` returns early with `notReady: "Every leg expires today..."` when all leg expiration days are zero (`useCalculatorStore.ts:1023-1040`).
- Gated multi-leg calls catch gRPC status 7 (`PERMISSION_DENIED`) and set `gateDenied: message`, rendering `UpgradePrompt` without overwriting the user's input (`useCalculatorStore.ts:1211-1222`).

---

## Backend architecture & service reference

### Options Calculator Engine

- **Files**: `backend/proto/calculator.proto`, `backend/src/modules/calculator_service.cppm`, `backend/src/modules/calculator_service.cpp`, `backend/src/modules/strategy_catalogue.cppm`, `backend/src/modules/market_data.cppm`.
- **Purpose**: Houses the core options and futures pricing engine, SGEE execution graph pipeline, strategy catalogue validation, and live Alpaca / US Treasury market data feeds.

| Symbol | Signature / Scope | Purpose & Enforcing Logic |
| --- | --- | --- |
| `RegisterCalculatorService` | `void(ServerBuilder&, IStrategyStore*)` | Binds `calculator.OptionsCalculator` service to gRPC server builder (`calculator_service.cpp:1890-1930`). |
| `CalculateStrategy` | RPC Method | Computes at-expiry payoff curve, price $\times$ date matrix, net Greeks, and parametric VaR via SGEE graph pipeline (`calculator_service.cpp:1200-1420`). |
| `GetMarketQuote` | RPC Method | Returns live spot quote and timestamp for equity/crypto/futures tickers via `market_data` (`calculator_service.cpp:1000-1060`). |
| `GetMarketChain` | RPC Method | Returns listed option chain strikes or forward futures term structure (`calculator_service.cpp:1070-1170`). |
| `GetRiskFreeRate` | RPC Method | Returns current US Treasury yield curve rate and continuous compounding conversion (`calculator_service.cpp:1175-1198`). |
| `SaveStrategy` | RPC Method | Persists user position payload to PostgreSQL via `IStrategyStore` (`calculator_service.cpp:1694-1755`). |
| `ListStrategies` | RPC Method | Lists saved positions for authenticated user (`calculator_service.cpp:1758-1790`). |
| `DeleteStrategy` | RPC Method | Deletes saved position by ID (`calculator_service.cpp:1792-1825`). |
| `strategy::is_known` | `bool(string_view id)` | Validates strategy ID against the 47-entry catalogue (`strategy_catalogue.cppm:140-142`). |
| `strategy::kCatalogue` | `array<StrategyInfo, 47>` | Authoritative table of catalogued strategies (`strategy_catalogue.cppm:67-115`). |

#### Invariants, refusals, and failure modes
- Multi-leg strategy execution (`legs.size() > 1`) without Pro tier returns `PERMISSION_DENIED` (`calculator_service.cpp:1214` $\to$ `api_key.cpp:457-478`).
- Presence of an Asian option leg in `CalculateStrategy` returns `FAILED_PRECONDITION` (`calculator_service.cpp:658-664`).
- Asian `averaging_states` $< 2$ or $> 200$ returns `INVALID_ARGUMENT`: `"Leg {i}: averaging_states must be 0 (engine default) or between 2 and 200."` (`calculator_service.cpp:650-656`).
- Saving a strategy without a name returns `INVALID_ARGUMENT: "A scenario needs a name."` (`calculator_service.cpp:1712-1714`).
- Saving a strategy name exceeding 120 bytes returns `INVALID_ARGUMENT: "A scenario name may be at most 120 characters."` (`calculator_service.cpp:1715-1719`).
- Saving a strategy with zero legs returns `INVALID_ARGUMENT: "A scenario needs at least one leg."` (`calculator_service.cpp:1723-1726`).

#### Gotchas
- `strategy_catalogue.cppm` contains **47 strategies**; `StrategySelector.tsx` in the frontend lists **48 strategies** (including `crack_321`). The backend deliberately refuses `crack_321` if emitted by the assistant (`strategy_catalogue.cppm:30-40`).

---

### Sensen Finance Service

- **Files**: `backend/proto/finance.proto`, `backend/src/modules/finance_service.cppm`, `backend/src/modules/finance_service.cpp`.
- **Purpose**: Serves the 46 financial mathematics, mortgage, bond, real estate, and portfolio RPCs wrapping `sensen` library algorithms.

| Symbol / Category | Exported Handlers | Behavior & Numerical Discipline |
| --- | --- | --- |
| `RegisterFinanceService` | `void(ServerBuilder&)` | Registers `sensen.finance.Finance` with gRPC server (`finance_service.cpp:4550-4575`). |
| TVM Handlers | `ComputePayment`, `ComputePresentValue`, `ComputeFutureValue`, etc. | Exact arithmetic in `BigDecimal` ($10^{18}$ fixed point). Pre-checks opposite signs on rate/period solves (`finance_service.cpp:2480-2520`). |
| Mortgage Handlers | `ComputeAmortization`, `ComputeDetailedAmortization`, `ComputeAmortizationBatch` | Generates period schedules with interest/principal/tax allocations. Enforces $10^{11}$ maximum principal ceiling (`finance_service.cpp:1000-1120`). |
| Cash-Flow Handlers | `ComputeNpv`, `ComputeIrr`, `ComputeXnpv`, `ComputeXirr` | Solves polynomial / discounted sums. Hardened against NaN/Infinity cash flows (`finance_service.cpp:1500-1650`). |
| Options Handlers | `PriceOptionTree`, `PriceBlackScholes`, `PriceOptionMonteCarlo` | Evaluates trinomial trees, Black-Scholes formulas, and Monte Carlo paths. Validates positive spot/strike/volatility (`finance_service.cpp:2100-2400`). |
| Real Estate Handlers | `ComputeClosingCosts`, `ComputeRentVsBuy`, `RefreshStateAssumptions` | Multi-variable closing cost breakdowns, rent-vs-buy wealth projections, and Census ACS writes (`finance_service.cpp:3080-3400`). |

#### Invariants, refusals, and failure modes
- Every RPC executes the `CHARGE(method_name, cost)` admission macro first (`finance_service.cpp:964-981`), enforcing authentication and quota token bucket deduction.
- Same-signed PV and Payment with zero FV in `ComputeRate` or `ComputePeriods` returns `INVALID_ARGUMENT` naming the opposite-sign TVM cash-flow convention (`finance_service.cpp:2520-2560`).
- Non-positive period count result in `ComputePeriods` returns `INVALID_ARGUMENT: "The payment is too small to cover interest; loan will never amortize."` (`finance_service.cpp:2580-2600`).
- `RefreshStateAssumptions` called by any identity with `_id.tier != "partner"` returns `PERMISSION_DENIED: "Refreshing state assumptions requires a partner credential. Reading them (GetStateAssumptions) does not."` (`finance_service.cpp:3102-3109`).
- `ComputeClosingCosts` returns `INVALID_ARGUMENT` if `tax_escrow_months` is not in $[0, 24]$ (`finance_service.cpp:3333-3336`) or `prepaid_interest_days` is not in $[0, 365]$ (`finance_service.cpp:3337-3341`). Seller credits exceeding total closing fees return `FAILED_PRECONDITION` (`test_finance_service_validation.cpp:2126-2130`).

---

### Natural-Language Assistants

- **Files**: `backend/proto/assistant.proto`, `backend/src/modules/assistant_service.cpp`, `assistant_verification.cppm`, `backend/proto/mortgage_assistant.proto`, `backend/src/modules/mortgage_assistant_service.cpp`, `mortgage_grammar.cppm`, `mortgage_verification.cppm`.
- **Purpose**: Runs in-process Qwen3-0.6B language models under `sensen` on CPU, with grammar-constrained decoding and post-generation lexical verification.

| Component | Exported Functions / Types | Role & Mechanism |
| --- | --- | --- |
| `StrategyAssistant` | `ParseStrategy(ParseRequest, ParseResponse)` | Natural-language strategy parser. Generates `<params>` JSON. Runs verification via `assistant_verification::verify_strategy_output` (`assistant_service.cpp:2870-2920`). |
| `assistant_verification` | `verify_strategy_output(params, utterance)` | Five-gate fail-closed verifier: structural checks, strategy catalogue lookup, asset class classification, symbol validation, and lexical grounding (`assistant_verification.cppm:1-500`). |
| `MortgageAssistant` | `ParseOperation(ParseRequest, ParseResponse)` | Natural-language mortgage parser. Generates target Finance RPC name and arguments JSON (`mortgage_assistant_service.cpp:3130-3200`). |
| `mortgage_grammar` | `Schema::build()`, `GrammarMatcher` | Automaton for constrained decoding. Restricts token generation to valid JSON paths across the 27 operations (`mortgage_grammar.cppm:1-300`). |
| `mortgage_verification` | `verify_mortgage_output(output, utterance)`, `operation_ids()` | Verifies emitted operation ID, field schema, and utterance numerical grounding (`mortgage_verification.cppm:48-68,384-395`). |

#### Invariants, refusals, and failure modes
- Both assistants require Pro entitlement; unentitled callers receive `PERMISSION_DENIED` with surface-specific messages (`assistant_service.cpp:2877`, `mortgage_assistant_service.cpp:3139`).
- Both assistants force `config.repetition_penalty = 1.0F` and `n_gpu_layers = 0` (`assistant_service.cpp:660`, `mortgage_assistant_service.cpp:586`).
- Emitted numerical parameters failing grounding against the utterance return `Outcome::Refusal` with status `OK` (`mortgage_verification.cppm:54-58`).

---

### Authentication, Quota, Persistence & Infrastructure

- **Files**: `backend/src/modules/api_key.cppm`, `api_key.cpp`, `quota.cppm`, `quota.cpp`, `strategy_store.cppm`, `strategy_store.cpp`, `pg.cppm`, `state_refresh.cppm`, `state_refresh.cpp`, `inference_admission.cppm`, `fips_mode.cppm`.
- **Purpose**: Implements API key hash verification, rate and compute-unit quota metering, PostgreSQL connection pooling, Census ACS data refreshes, and FIPS provider gating.

| Module | Core Symbols | Responsibility |
| --- | --- | --- |
| `api_key` | `KeyRegistry`, `check_strategy_entitlement`, `check_assistant_entitlement`, `check_saved_scenarios_entitlement` | Parses `FINANCE_API_KEYS` (SHA-512 hashes) and validates Supabase JWTs. Enforces Pro gates (`api_key.cpp:457,480,537,558-650`). |
| `quota` | `QuotaEnforcer`, `TierLimits`, `Decision` | Enforces two-axis token bucket: `requests_per_minute` and `compute_units_per_hour` per tier/key (`quota.cpp:73-238`). |
| `strategy_store` | `IStrategyStore`, `PostgresStrategyStore` | Abstract database seam. Persists scenario rows without leaking `pg` types into pricing translation units (`strategy_store.cppm:1-60`). |
| `state_refresh` | `refresh_state_assumptions()`, `Outcome`, `Refusal` | Pulls Census ACS 5-year data (`CENSUS_API_KEY`), validates plausibility bounds, and updates `state_assumptions` under Postgres advisory lock (`state_refresh.cpp:1-400`). |
| `inference_admission` | `Device`, `QueuedBackend`, `PostgresAdmission` | Admission queue and backpressure layer for language model inference. Dispatches between CPU and CUDA devices (`inference_admission.cppm:53-150`). |
| `fips_mode` | `init_fips_mode()`, `Status` | Manages OpenSSL 3.x FIPS provider initialization via `FIPS_MODE=required|preferred|off` (`fips_mode.cppm:1-120`). |

#### Invariants, refusals, and failure modes
- `QuotaEnforcer` returns status `RESOURCE_EXHAUSTED` (8) with dynamic `retry-after-seconds` header when rate or compute budget is exceeded (`quota.cpp:350-380`).
- Unrecognized or missing API keys are assigned to the shared `~anonymous` bucket (`quota.cpp:250-262`).
- Saved scenario queries without verified `identity.subject` return `UNAUTHENTICATED: "Saved scenarios belong to an account. Sign in to save, list or delete them."` (`api_key.cpp:487-493`).
- State refresh aborts with `Refusal{Abort::TooFewUsableRows}` if fewer than 45 states satisfy plausibility bounds (`state_refresh.cppm:69`).
- State refresh aborts with `Refusal{Abort::AlreadyRunning}` if another replica holds advisory lock `kStateRefreshAdvisoryLockId` (`state_refresh.cpp:180-195`).

---

## Test coverage

The test targets below provide regression gating across the entire system. All C++ test targets use the hand-rolled `check()/section()` harness compliant with repository policy (`config/cpp_details.txt` rule 39).

| Test Target / Executable | Test Source File | Subsystems & Invariants Exercised |
| --- | --- | --- |
| `test_calculator_service` | `backend/tests/test_calculator_service.cpp` | SGEE pipeline execution; 580/600 bull call spread closed-form identity; Iron Condor breakevens; missing action silent-halt detection; Asian leg refusal (`FAILED_PRECONDITION`); matrix price bounds; saved scenario name validation. |
| `test_api_key_entitlement` | `backend/tests/test_api_key_entitlement.cpp` | Refusal and admit paths for `check_strategy_entitlement`, `check_assistant_entitlement` (strategy vs mortgage surface message isolation), and `GateMode::Off` behavior. |
| `test_finance_service_validation` | `backend/tests/test_finance_service_validation.cpp` | 27 validation sections covering magnitude overflows, TVM same-signed loan solve errors, period payment iteration bounds, cumulative NaN handling, NPV/IRR bounds, Black-Scholes/Monte Carlo parameter validation, closing cost bases, and MACRS class completeness. |
| `test_option_pricing_service` | `backend/tests/test_option_pricing_service.cpp` | Trinomial tree convergence against Black-Scholes closed forms; European vs American early-exercise premiums; Bermudan discrete date schedules and dead band validation. |
| `test_assistant_service` | `backend/tests/test_assistant_service.cpp` | Strategy Assistant prompt construction, turn-role formatting, repetition penalty pinning ($1.0$), and gRPC response outcome mapping. |
| `test_assistant_verification` | `backend/tests/test_assistant_verification.cpp` | Five-gate verification for strategy assistant: closed-vocabulary strategy IDs, asset class filtering, ticker validation, and lexical grounding against prompt. |
| `test_mortgage_assistant_service` | `backend/tests/test_mortgage_assistant_service.cpp` | Mortgage Assistant service lifecycle, Q8_0 CPU execution, multi-turn clarification handling, and Pro gate enforcement. |
| `test_mortgage_grammar` | `backend/tests/test_mortgage_grammar.cpp` | Constrained decoding automaton across 27 operations; token masking; valid JSON punctuation enforcement. |
| `test_mortgage_verification` | `backend/tests/test_mortgage_verification.cpp` | Label space checking against `finance.proto`; value grounding rules; convention value exemptions; plausibility bounds ($G1$–$G5$). |
| `test_state_assumptions_gate` | `backend/tests/test_state_assumptions_gate.cpp` | Access gate on `RefreshStateAssumptions` ensuring non-partner tiers are rejected with `PERMISSION_DENIED`. |
| `test_state_refresh` | `backend/tests/test_state_refresh.cpp` | Census ACS data parsing; 45-state minimum floor; advisory locking; preservation of editorial columns in PostgreSQL. |
| `test_strategy_store_pg` | `backend/tests/test_strategy_store_pg.cpp` | PostgreSQL persistence for saved scenarios using `pg::Pool`; duplicate replacement semantics; JSON serialization round-tripping. |
| `test_market_data_resilience` | `backend/tests/test_market_data_resilience.cpp` | Alpaca provider timeout handling, circuit-breaker tripping on repeated failures, and fallback behavior. |
| `test_option_chain_cache` | `backend/tests/test_option_chain_cache.cpp` | In-memory 15-minute TTL caching of option chains and forward term structures. |
| `test_inference_admission` | `backend/tests/test_inference_admission.cpp` | Local in-process queue admission, timeout backpressure, and CPU vs CUDA device selection logic. |
| `test_quota_tier_label` | `backend/tests/test_quota_tier_label.cpp` | Quota policy JSON parsing, token bucket refill mathematics, and fallback of undefined tiers to anonymous limits. |
| `test_vendored_proto_drift` | `backend/tests/test_vendored_proto_drift.cpp` | Verifies zero schema drift between `backend/proto/*.proto` and generated wire code. |
| `smoke_client` | `backend/src/smoke_client.cpp` | End-to-end integration binary testing live deployment over TLS/TCP proxy: put-call parity, bond price-yield inversion, amortization schedule closure, and closed-form annuity checks. |
| Frontend Vitest Suite | `frontend/src/**/*.test.ts` | Unit tests for store state machines: `calculate-guards.test.ts`, `calculator-race.test.ts`, `chainFreshness.test.ts`, `asian-leg.test.ts`, `assistant-outcomes.test.ts`, `saved-scenarios.test.ts`, `matrix-bounds.test.ts`, `strategy-guides.test.ts`, `ad-routes.test.ts`. |
| Export Guard | `frontend/scripts/check-export.mjs` | Build-time script validating that no static page outside `/guides/[strategy]` contains Google AdSense tags or ad slot components. |

---

## Open questions

None. Every symbol, bound, constant, refusal message, and test target cited in this document has been verified directly against the running source tree.
