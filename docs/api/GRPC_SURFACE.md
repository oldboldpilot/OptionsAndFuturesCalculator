# gRPC surface reference
@author Olumuyiwa Oluwasanmi

> **How to read a citation in this document.** Every `path:line` here was checked
> mechanically: the file exists and the line is inside it. The line is a pointer to
> a NEIGHBOURHOOD, not a guarantee — a sample of 1,167 citations found the named
> symbol within eight lines of the cited line in about four cases in five, and the
> residue is mostly prose adjacency rather than error. Trust the path and the symbol
> name; re-grep the symbol if the line looks wrong. An attempt to auto-correct the
> residue by moving line numbers made it worse and was reverted, which is why this
> note exists instead of a tighter number.

## Scope

This document provides reference documentation for the wire contract of the four canonical protobuf schemas defining the gRPC surface of the application:
- `backend/proto/calculator.proto`
- `backend/proto/finance.proto`
- `backend/proto/assistant.proto`
- `backend/proto/mortgage_assistant.proto`

It documents every service, RPC, and message, including wire types, units, presence rules, invariants, validation bounds, failure codes, HTTP JSON-transcoder routes, and corresponding test targets.

---

## Core wire conventions

### String-vs-double numeric convention
Numeric fields across this surface use two distinct wire types based on the underlying representation in the C++ engine (`backend/proto/finance.proto:20-38`):

| Wire type | Native engine type | Scope and rationale | Enforcing citation |
| --- | --- | --- | --- |
| `string` | `sensen::BigDecimal` (`__int128` scaled by $10^{18}$, 18 decimal places exact) | Used for currency amounts, mortgage balances, interest rates, property values, contributions, and exact TVM/amortization arithmetic. Preserves full 18-decimal precision across network boundaries and prevents precision loss in browser JavaScript clients where native `number` is an IEEE 754 `float64` (`backend/proto/finance.proto:33-37`). Also used as the value type in `FinanceParams.params` (`backend/proto/mortgage_assistant.proto:198-219`) to carry numeric literals without floating-point conversion. | `backend/proto/finance.proto:25-28`, `backend/proto/mortgage_assistant.proto:200-209` |
| `double` | IEEE 754 binary64 `double` | Used where computation is natively floating-point: option pricing models (Black-Scholes, trinomial trees, Monte Carlo), Greeks, implied volatilities, probabilities, futures cost-of-carry, bond yield/duration/convexity solvers, portfolio optimization, and empirical return statistics (`backend/proto/finance.proto:29-32`). Widening these to string would claim precision the engine never computed. | `backend/proto/finance.proto:29-32`, `backend/proto/calculator.proto:43-56` |

Notable exceptions where monetary or financial fields are deliberately `double`:
1. `RefinanceResponse.total_savings_over_life` (`backend/proto/finance.proto:382`): Carried as `double` because the engine accumulates savings in a month-by-month double loop; emitting an 18-decimal string would fabricate digits (`backend/proto/finance.proto:379-382`).
2. `HomeFutureValueResponse.future_property_value` (`backend/proto/finance.proto:659`): Carried as `double` because compound property appreciation is computed in floating point (`backend/proto/finance.proto:658`).
3. `RentVsBuyResponse` legacy fields `total_cost_of_buying`, `total_cost_of_renting`, `buying_advantage` (`backend/proto/finance.proto:736-739`): Kept as `double` at field tags 1, 2, and 4 to avoid breaking older wire clients. Exact 18-decimal string companions (`total_cost_of_buying_exact`, etc.) are provided at tags 5, 6, and 7 for callers using the amortizing model (`backend/proto/finance.proto:726-750`).
4. `HomeNpvResponse` fields (`backend/proto/finance.proto:849-855`): All fields are `double` because the underlying XNPV/XIRR discounting primitives compute in double (`backend/proto/finance.proto:842-845`).
5. `ClosingCostsResponse.closing_costs_percent_of_price` (`backend/proto/finance.proto:1119`): Carried as `double` because it is a terminal display ratio rather than a monetary input to downstream calculations.
6. All fields in `backend/proto/calculator.proto` (strikes, spot prices, premiums, Greeks, P&L curves, VaR): Carried as `double` because derivative pricing and statistical distributions are floating-point operations.

### Dated cash-flow day-offset convention
In `DatedCashFlowRequest` (`backend/proto/finance.proto:443-448`) used by `ComputeXnpv` and `ComputeXirr`:
- Wire unit: `dates` are floating-point **DAY offsets** from an arbitrary common epoch (`backend/proto/finance.proto:431-432`).
- Engine internal unit: The underlying engine (`sensen::xnpv`, `sensen::xirr`) computes in **Unix timestamp SECONDS** (`backend/proto/finance.proto:432-433`).
- Translation and bounds: Handled exclusively in `finance_service.cpp::dates_to_seconds` (`backend/src/modules/finance_service.cpp:4446-4477`), which scales days to seconds via `kSecondsPerDay = 86'400.0` (`backend/src/modules/finance_service.cpp:4417`).
- Failure modes:
  - Multi-flow span $< 1.0$ day: Refused with `INVALID_ARGUMENT` (`backend/src/modules/finance_service.cpp:4451-4457`) because every discount factor would be $1.0$ and collapse the calculation into an undiscounted sum.
  - Multi-flow span $> 36,525.0$ days (100 years, `kMaxCashFlowDaySpan`): Refused with `INVALID_ARGUMENT` (`backend/src/modules/finance_service.cpp:4459-4467`) with a diagnostic noting that the caller likely transmitted timestamp seconds rather than day offsets.
  - Lone cash flow: Exempt from span validation as it sits at $t = 0$ (`backend/src/modules/finance_service.cpp:4439-4441`).

### The single explicit-presence (`optional`) field
- Field: `ClosingCostsRequest.prepaid_interest_days` (`backend/proto/finance.proto:1082`), declared as `optional int32 prepaid_interest_days = 16`.
- Wire semantics:
  - Absent (unset): Directs the engine to use the default convention of 15 days (half-month prepaid interest) (`backend/proto/finance.proto:1075-1077`, `backend/src/modules/finance_service.cpp:4254-4258`).
  - Explicit `0`: Means zero days of prepaid interest, representing a closing on the final day of the month where no prepaid interest is owed (`backend/proto/finance.proto:1077-1078`, `backend/src/modules/finance_service.cpp:4256-4258`).
- Text-level parser defect: Standard proto3 fields collapse unset and zero. Because proto3 explicit presence was introduced in protoc 3.15, text-level proto regex parsers that stripped `repeated` but did not capture `optional` (e.g. `agent/dataset/build_mortgage_dataset.py:107-114`, `backend/tests/test_mortgage_grammar.cpp:127-133`) misclassified `optional` as part of the field type or treated it as required. This prevented omitting the field to invoke the 15-day convention, causing schema validators to reject valid convention calls.

### Mortgage assistant label-space exclusions
The mortgage assistant service (`backend/proto/mortgage_assistant.proto`) maps natural language utterances to `sensen.finance.Finance` RPCs. Out of 46 RPCs defined in `backend/proto/finance.proto:40-104`, the assistant's label space covers 26 operations in the deployed v2 production model (`backend/proto/mortgage_assistant.proto:183-186`, `docs/CLOSING_COSTS_CLIENT_NOTICE.md:156-158`) and 27 in the verification grammar (`backend/src/modules/mortgage_verification.cppm:384`, `backend/tests/test_mortgage_grammar.cpp:553`).

The excluded RPCs and rationale are:
1. Out-of-scope functional sections (`agent/dataset/build_mortgage_dataset.py:196-202`):
   - Fixed income: `AnalyzeBond`, `AnalyzeTreasuryBill`
   - Futures and hedging: `PriceFutures`, `ValueFutures`, `SimulateMarginAccount`, `ComputeHedge`, `CommoditySpread`
   - Options: `PriceOptionTree`, `PriceBlackScholes`, `PriceOptionMonteCarlo`, `ComputeProbabilityTree`
   - Portfolio: `ComputePortfolioStats`, `OptimizePortfolio`, `ComputeRiskContributions`
2. Rate-theory utilities (`agent/dataset/build_mortgage_dataset.py:206-207`, `backend/tests/test_mortgage_grammar.cpp:248-251`):
   - `ConvertInterestRate`, `ComputeFisherRate`: Theoretical rate conversion utilities not phrased as consumer mortgage or cash flow requests.
3. Batch APIs (`agent/dataset/build_mortgage_dataset.py:209-217`, `backend/tests/test_mortgage_grammar.cpp:248-251`):
   - `ComputeAmortizationBatch`, `ComputeRentVsBuyBatch`: High-throughput bulk APIs accepting repeated arrays (up to 1,000 scenarios) for offline batching; no single conversational utterance can ground parameters for a batch.
4. Administrative and reference operations (`agent/dataset/build_mortgage_dataset.py:221-225`, `backend/tests/test_mortgage_grammar.cpp:248-251`):
   - `RefreshStateAssumptions`: Operational maintenance mutation triggering external Census ACS fetches and database writes.
   - `GetStateAssumptions`: Reference data lookup for state housing parameters.
5. Deployed model status of `ComputeClosingCosts`:
   - Included in the 27-operation grammar and C++ verification rules (`backend/src/modules/mortgage_verification.cppm:384`), but excluded from the active deployed v2 model label space (26 operations, 160 fields). The v3 candidate model achieved only 16.7% accuracy on closing cost utterances and regressed existing operations, so v2 remains deployed without natural language closing cost parsing (`docs/CLOSING_COSTS_CLIENT_NOTICE.md:152-160`). Direct calls to `Finance/ComputeClosingCosts` via gRPC or JSON transcoder are fully operational (`docs/CLOSING_COSTS_CLIENT_NOTICE.md:117-145`).

---

## Service: `calculator.OptionsCalculator` (`backend/proto/calculator.proto`)

### Purpose
Provides multi-leg option and futures strategy payoff modeling, risk metrics, live market quotes, option chains, Treasury risk-free rates, and user-scoped scenario persistence.

### RPC inventory

| RPC name | Request message | Response message | Description |
| --- | --- | --- | --- |
| `CalculateStrategy` | `StrategyRequest` | `StrategyResponse` | Evaluates multi-leg strategy payoffs, at-expiry P&L curves, price $\times$ date matrix, Greeks, and risk metrics (`backend/proto/calculator.proto:16`). |
| `GetMarketQuote` | `QuoteRequest` | `QuoteResponse` | Fetches live market quotes for equity, futures, or crypto instruments (`backend/proto/calculator.proto:17`). |
| `GetMarketChain` | `ChainRequest` | `ChainResponse` | Fetches live option chains and modeled futures term structures (`backend/proto/calculator.proto:18`). |
| `GetRiskFreeRate` | `RiskFreeRateRequest` | `RiskFreeRateResponse` | Returns the measured continuous Treasury risk-free rate and yield curve (`backend/proto/calculator.proto:23`). |
| `SaveStrategy` | `SaveStrategyRequest` | `SaveStrategyResponse` | Saves or updates a named strategy scenario scoped to a Supabase user token (`backend/proto/calculator.proto:32`). |
| `ListStrategies` | `ListStrategiesRequest` | `ListStrategiesResponse` | Lists saved strategy scenarios for the authenticated caller, ordered newest first (`backend/proto/calculator.proto:33`). |
| `DeleteStrategy` | `DeleteStrategyRequest` | `DeleteStrategyResponse` | Deletes a saved scenario by ID scoped to the authenticated user (`backend/proto/calculator.proto:34`). |

### Message and field inventory

#### `Leg` (`backend/proto/calculator.proto:37-82`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `action` | `Action` (enum: `BUY=0`, `SELL=1`) | Trade action | Default `BUY=0` (`backend/proto/calculator.proto:41`). |
| `type` | `Type` (enum: `CALL=0`, `PUT=1`, `FUTURE=2`, `STOCK=3`) | Instrument type | Default `CALL=0` (`backend/proto/calculator.proto:42`). |
| `strike` | `double` | Dollars | Required for options; ignored for stock/future (`backend/proto/calculator.proto:43`). |
| `expiration_days` | `double` | Calendar days | Required for derivatives; $> 0$ enforced in `backend/src/modules/calculator_service.cpp:601`. |
| `quantity` | `int32` | Number of contracts or shares | Positive integer (`backend/proto/calculator.proto:45`). |
| `premium` | `double` | Entry price paid or received in dollars | Required for P&L computation (`backend/proto/calculator.proto:50`). |
| `implied_volatility` | `double` | Annual volatility as decimal (e.g. 0.20 for 20%) | Per-leg IV. Zero means unquoted; falls back to request IV (`backend/src/modules/calculator_service.cpp:297-305`). |
| `contract_multiplier` | `double` | Multiplier (100 for equity options, 50 for ES) | Default 100 for standard equity options (`backend/proto/calculator.proto:55`). |
| `asian_type` | `AsianType` (enum: `NOT_ASIAN=0`, `AVERAGE_PRICE=1`, `AVERAGE_STRIKE=2`) | Asian option averaging style | Non-zero triggers `FAILED_PRECONDITION` in `CalculateStrategy` (`backend/src/modules/calculator_service.cpp:658-664`). |
| `averaging_states` | `int32` | Discretization states for Hull-White tree | 0 uses engine default (50). Wire bounds: $[2, 200]$ (`backend/src/modules/calculator_service.cpp:650-656`). |

#### `StrategyRequest` (`backend/proto/calculator.proto:84-121`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `underlying_symbol` | `string` | Ticker symbol (e.g. "NVDA") | Informational label (`backend/proto/calculator.proto:85`). |
| `current_price` | `double` | Spot price in dollars | Required; must be $> 0.0$ (`backend/src/modules/calculator_service.cpp:600`). |
| `implied_volatility` | `double` | Annualized volatility decimal | Baseline IV used if leg IV is zero (`backend/src/modules/calculator_service.cpp:304`). |
| `risk_free_rate` | `double` | Continuous annual rate decimal | Required for option discounting (`backend/proto/calculator.proto:88`). |
| `legs` | `repeated Leg` | Position legs | Required; length in $[1, 20]$ (`backend/src/modules/calculator_service.cpp:485, 1188`). |
| `price_range_percent` | `double` | Decimal fraction (e.g. 0.20 for $\pm 20\%$) | 0 defaults to 0.25 ($\pm 25\%$) (`backend/src/modules/calculator_service.cpp:672`). |
| `price_steps` | `uint32` | Grid step count | 0 defaults to 81; clamped to max 500 (`backend/src/modules/calculator_service.cpp:445, 510-513`). |
| `date_steps` | `uint32` | Date step count | 0 defaults to 12; clamped to max 180; total grid cells clamped to 20,000 (`backend/src/modules/calculator_service.cpp:446-447, 518-520`). |
| `dividend_yield` | `double` | Continuous annual yield decimal | Merton continuous dividend yield assumption (`backend/proto/calculator.proto:95-106`). |
| `matrix_price_min` | `double` | Price lower bound in dollars | For matrix only. 0 leaves unset (`backend/src/modules/calculator_service.cpp:699-707`). |
| `matrix_price_max` | `double` | Price upper bound in dollars | For matrix only. 0 leaves unset; must be $> \text{min}$ (`backend/src/modules/calculator_service.cpp:708`). |

#### `Greeks` (`backend/proto/calculator.proto:130-140`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `delta` | `double` | Share-equivalents of underlying | Position-level (`backend/proto/calculator.proto:131`). |
| `gamma` | `double` | Delta change per $1 underlying move | Position-level (`backend/proto/calculator.proto:132`). |
| `theta` | `double` | Dollars per calendar day | Position-level (`backend/proto/calculator.proto:133`). |
| `vega` | `double` | Dollars per 1 IV point (1%) | Position-level (`backend/proto/calculator.proto:134`). |
| `rho` | `double` | Dollars per 1 interest rate point (1%) | Position-level (`backend/proto/calculator.proto:135`). |
| `vanna` | `double` | Delta change per 1 IV point | Position-level (`backend/proto/calculator.proto:137`). |
| `volga` | `double` | Vega change per 1 IV point | Position-level (`backend/proto/calculator.proto:138`). |
| `charm` | `double` | Delta change per calendar day | Position-level (`backend/proto/calculator.proto:139`). |

#### `PnLPoint` (`backend/proto/calculator.proto:143-146`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `underlying_price` | `double` | Dollars | Payoff curve price node (`backend/proto/calculator.proto:144`). |
| `pnl` | `double` | Net profit/loss in dollars | Evaluated at `curve_days_to_expiration` (`backend/proto/calculator.proto:145`). |

#### `MatrixCell` (`backend/proto/calculator.proto:150-156`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `price` | `double` | Dollars | Underlying price coordinate (`backend/proto/calculator.proto:151`). |
| `days_to_expiration` | `uint32` | Calendar days | Date coordinate (`backend/proto/calculator.proto:152`). |
| `date_str` | `string` | "YYYY-MM-DD" | Date string representation (`backend/proto/calculator.proto:153`). |
| `pnl_dollars` | `double` | Dollars | Strategy P&L repriced at that horizon (`backend/proto/calculator.proto:154`). |
| `return_on_risk_percent`| `double` | Percent | P&L divided by max risk (`backend/proto/calculator.proto:155`). |

#### `RiskMetrics` (`backend/proto/calculator.proto:158-163`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `var_parametric_95` | `double` | Dollars | 95% 1-day parametric Value at Risk (`backend/proto/calculator.proto:159`). |
| `var_parametric_99` | `double` | Dollars | 99% 1-day parametric Value at Risk (`backend/proto/calculator.proto:160`). |
| `cvar_parametric_95`| `double` | Dollars | 95% 1-day parametric Conditional VaR (`backend/proto/calculator.proto:161`). |
| `cvar_parametric_99`| `double` | Dollars | 99% 1-day parametric Conditional VaR (`backend/proto/calculator.proto:162`). |

#### `StrategyResponse` (`backend/proto/calculator.proto:165-202`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `max_profit` | `double` | Dollars | Max profit across evaluated curve (`backend/proto/calculator.proto:166`). |
| `max_loss` | `double` | Dollars | Max loss across evaluated curve (`backend/proto/calculator.proto:167`). |
| `break_even` | `double` | Dollars | First breakeven underlying price (`backend/proto/calculator.proto:168`). |
| `expected_value` | `double` | Dollars | Probability-weighted expected P&L (`backend/proto/calculator.proto:169`). |
| `pop` | `double` | Decimal probability in $[0.0, 1.0]$ | Probability of profit (`backend/proto/calculator.proto:170`). |
| `net_greeks` | `Greeks` | Greek sensitivities | Position aggregate (`backend/proto/calculator.proto:171`). |
| `risk_metrics` | `RiskMetrics` | Risk statistics | VaR and CVaR measures (`backend/proto/calculator.proto:172`). |
| `pnl_matrix` | `repeated PnLPoint` | Price-P&L pairs | Payoff curve evaluated at nearest expiry (`backend/proto/calculator.proto:173`). |
| `risk_reward_ratio` | `double` | Ratio | Max profit / abs(max loss) (`backend/proto/calculator.proto:175`). |
| `breakeven_prices` | `repeated double` | Dollars | All breakeven prices where curve crosses zero (`backend/proto/calculator.proto:176`). |
| `matrix` | `repeated MatrixCell` | Grid cells | Price $\times$ date matrix (`backend/proto/calculator.proto:177`). |
| `calculation_time_microseconds` | `uint64` | Microseconds | Execution elapsed time (`backend/proto/calculator.proto:178`). |
| `probability_of_touch` | `double` | Decimal probability in $[0.0, 1.0]$ | Probability underlying touches breakeven (`backend/proto/calculator.proto:179`). |
| `probability_of_target_profit` | `double` | Decimal probability in $[0.0, 1.0]$ | Probability of reaching 50% max profit (`backend/proto/calculator.proto:180`). |
| `curve_days_to_expiration` | `double` | Calendar days | Horizon of earliest leg expiry (`backend/proto/calculator.proto:193`). |
| `leg_risk` | `repeated LegRisk` | Per-leg breakdowns | Ordered matching request legs (`backend/proto/calculator.proto:201`). |

#### `LegRisk` (`backend/proto/calculator.proto:206-215`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `leg_index` | `int32` | 0-based index | Corresponds to `StrategyRequest.legs` (`backend/proto/calculator.proto:207`). |
| `greeks` | `Greeks` | Greek sensitivities | Scaled to quantity and multiplier (`backend/proto/calculator.proto:208`). |
| `model_price` | `double` | Dollars per contract/share | Model price before multipliers; 0 for linear legs (`backend/proto/calculator.proto:211`). |
| `open_pnl` | `double` | Dollars | Position-scaled mark-to-market open P&L (`backend/proto/calculator.proto:214`). |

#### `QuoteRequest` (`backend/proto/calculator.proto:221-231`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `symbol` | `string` | Ticker symbol | Required (`backend/proto/calculator.proto:222`). |
| `asset_class` | `string` | "EQUITY", "FUTURES", "CRYPTO" | Empty defaults to "EQUITY" (`backend/proto/calculator.proto:227-230`). |

#### `QuoteResponse` (`backend/proto/calculator.proto:233-244`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `symbol` | `string` | Ticker symbol | Requested symbol (`backend/proto/calculator.proto:234`). |
| `price` | `double` | Dollars | Current spot price (`backend/proto/calculator.proto:235`). |
| `previous_close` | `double` | Dollars | Prior close price (`backend/proto/calculator.proto:236`). |
| `forward_pe` | `double` | Ratio | Forward P/E (0 if unquoted) (`backend/proto/calculator.proto:237`). |
| `implied_volatility` | `double` | Decimal fraction | Baseline IV (0 if unquoted) (`backend/proto/calculator.proto:238`). |
| `asset_class` | `string` | Asset class | Echoed or resolved asset class (`backend/proto/calculator.proto:239`). |
| `provider` | `string` | Provider label | E.g. "alpaca" or "alpaca:SPYx10 (index proxy)" (`backend/src/modules/calculator_service.cpp:1390-1392`). |
| `quote_timestamp` | `string` | RFC3339 | Upstream feed observation timestamp (`backend/proto/calculator.proto:243`). |

#### `OptionStrike` (`backend/proto/calculator.proto:246-268`)
Contains call/put bid, ask, delta, volume, open interest, IV, gamma, theta, vega, ATM flag, and expiration date string for a single strike node.

#### `FuturesContract` (`backend/proto/calculator.proto:270-282`)
Contains contract code, delivery month (`YYYY-MM`), days to expiry, modeled forward price, basis, annualized yield, and state (`"MODELLED"`). Order-book fields (bid, ask, volume, open interest) remain 0 (`backend/src/modules/calculator_service.cpp:1478-1482`).

#### `ExpirationDate` (`backend/proto/calculator.proto:284-288`)
Contains `date_str` (`YYYY-MM-DD`), `days_to_expiry`, and display `label` (`"Aug 21, 2026 (30d)"`).

#### `ChainRequest` (`backend/proto/calculator.proto:290-295`)
Contains `symbol`, optional `expiration_days`, optional `asset_class`, and optional explicit `expiration_date` (`"YYYY-MM-DD"`).

#### `ChainResponse` (`backend/proto/calculator.proto:297-306`)
Contains `symbol`, `spot_price`, `repeated OptionStrike option_strikes`, `repeated FuturesContract futures_contracts`, `selected_expiration_date`, `repeated ExpirationDate available_expirations`, `provider`, and `fetched_at` (RFC3339).

#### `RiskFreeRateRequest` (`backend/proto/calculator.proto:317`)
Empty message reserved for future tenor filters.

#### `RatePoint` (`backend/proto/calculator.proto:319-324`)
Contains `tenor` (e.g. "1M", "3M", "1Y"), nominal `days`, bond-equivalent yield `rate_bey`, and continuously compounded rate `rate_continuous` ($2 \ln(1 + \text{bey}/2)$).

#### `RiskFreeRateResponse` (`backend/proto/calculator.proto:326-339`)
Contains continuously compounded `rate`, `rate_published` (BEY), selected `tenor`, observation `as_of_date` (`YYYY-MM-DD`), `source` (`"us_treasury_par_yield"`), short-end `repeated RatePoint curve`, and `fetched_at` timestamp.

#### `SavedStrategy` (`backend/proto/calculator.proto:352-366`)
Contains server-assigned `id` (UUID), user-assigned `name`, full `StrategyRequest request`, and RFC3339 UTC timestamps `created_at` and `updated_at`.

#### `SaveStrategyRequest` (`backend/proto/calculator.proto:368-374`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `name` | `string` | User label | Required; 1..120 chars after trimming (`backend/src/modules/calculator_service.cpp:1712-1718`). |
| `request` | `StrategyRequest` | Strategy payload | Required; must have $\ge 1$ leg (`backend/src/modules/calculator_service.cpp:1723-1726`). |

#### `SaveStrategyResponse` (`backend/proto/calculator.proto:376-384`)
Contains stored `SavedStrategy strategy` and `bool replaced_existing` (true if updating an existing record under the same user and name).

#### `ListStrategiesRequest` (`backend/proto/calculator.proto:390`)
Empty message; caller identity is extracted from verified Supabase token metadata (`backend/src/modules/calculator_service.cpp:1762-1763`).

#### `ListStrategiesResponse` (`backend/proto/calculator.proto:392-395`)
Contains `repeated SavedStrategy strategies` ordered newest first (`backend/src/modules/calculator_service.cpp:235-238`).

#### `DeleteStrategyRequest` (`backend/proto/calculator.proto:397-399`)
Contains string `id` (UUID).

#### `DeleteStrategyResponse` (`backend/proto/calculator.proto:401-408`)
Contains `bool deleted` (false if ID did not exist or belonged to another user; returns OK to preserve delete idempotency and prevent ID enumeration, `backend/proto/calculator.proto:404-407`).

### Invariants, refusals, and failure modes
- `CalculateStrategy` leg limit: $> 20$ legs refused with `INVALID_ARGUMENT` ("Too many legs...") (`backend/src/modules/calculator_service.cpp:1189-1192`).
- `CalculateStrategy` required inputs: Spot $\le 0$, no legs, or all expirations $\le 0$ refused with `INVALID_ARGUMENT` ("Cannot price this position...") (`backend/src/modules/calculator_service.cpp:599-606`).
- `CalculateStrategy` Asian legs: Asian legs refused with `FAILED_PRECONDITION` ("This position contains an Asian option...") because terminal spot modeling cannot evaluate path-dependent average payoffs (`backend/src/modules/calculator_service.cpp:658-664`).
- `CalculateStrategy` averaging states: If Asian leg has `averaging_states != 0` and outside $[2, 200]$, refused with `INVALID_ARGUMENT` ("averaging_states must be 0 ... or between 2 and 200") (`backend/src/modules/calculator_service.cpp:651-655`).
- `CalculateStrategy` matrix price bounds: Non-finite bounds or negative bounds refused with `INVALID_ARGUMENT` (`backend/src/modules/calculator_service.cpp:695-703`); `matrix_price_max <= matrix_price_min` refused with `INVALID_ARGUMENT` (`backend/src/modules/calculator_service.cpp:708-713`).
- `CalculateStrategy` entitlement: Multi-leg strategies checked against Pro tier when `PRO_GATE_MODE` is active; unentitled calls return `PERMISSION_DENIED` (`backend/src/modules/calculator_service.cpp:1214-1218`).
- Market data failures: Upstream feed unavailability returns `UNAVAILABLE` (`backend/src/modules/calculator_service.cpp:1378-1380, 1437-1439, 1504-1506`).
- Saved scenarios authentication & capacity: Unauthenticated calls or missing bearer token return `UNAUTHENTICATED` (`backend/src/modules/calculator_service.cpp:1701, 1845-1850`). Exceeding per-user scenario cap (50) returns `RESOURCE_EXHAUSTED` ("You have reached the limit of 50 saved scenarios...") (`backend/src/modules/calculator_service.cpp:1837-1840`). Missing database connection returns `FAILED_PRECONDITION` ("Saved scenarios are not available on this deployment.") (`backend/src/modules/calculator_service.cpp:1827-1829`).

### Gotchas
- Multi-expiry curve horizon: For calendar and diagonal spreads, `StrategyResponse.curve_days_to_expiration` represents the EARLIEST leg expiry, not the latest. At far expiry, near legs are expired and structures collapse to debit lines (`backend/proto/calculator.proto:182-194`).
- Grid bounds separation: Setting `matrix_price_min` / `matrix_price_max` only zooms the price $\times$ date matrix. It does not alter the evaluation grid for `pnl_matrix`, `max_profit`, `max_loss`, or POP, preventing window-zooming from corrupting headline metrics (`backend/proto/calculator.proto:108-119`).

---

## Service: `sensen.finance.Finance` (`backend/proto/finance.proto`)

### Purpose
Exposes general-purpose financial mathematics: time value of money (TVM), mortgages and HELOCs, cash flow analysis (NPV, IRR, XNPV, XIRR), depreciation, fixed income analytics, futures and margin simulation, real estate returns, option trees, Black-Scholes, Monte Carlo pricing, and portfolio optimization.

### RPC inventory

| RPC name | Request message | Response message | Description |
| --- | --- | --- | --- |
| `ComputePayment` | `PaymentRequest` | `DecimalResponse` | Periodic annuity payment (PMT) (`backend/proto/finance.proto:42`). |
| `ComputePresentValue` | `PresentValueRequest` | `DecimalResponse` | Present value of an annuity (PV) (`backend/proto/finance.proto:43`). |
| `ComputeFutureValue` | `FutureValueRequest` | `DecimalResponse` | Future value of an annuity (FV) (`backend/proto/finance.proto:44`). |
| `ComputeFutureValueDetailed` | `FutureValueDetailedRequest` | `FutureValueDetailedResponse` | Investment future value with contributions and inflation adjustments (`backend/proto/finance.proto:45`). |
| `ComputeInterestPayment` | `PeriodPaymentRequest` | `DecimalResponse` | Interest portion of a specific period's payment (IPMT) (`backend/proto/finance.proto:46`). |
| `ComputePrincipalPayment` | `PeriodPaymentRequest` | `DecimalResponse` | Principal portion of a specific period's payment (PPMT) (`backend/proto/finance.proto:47`). |
| `ComputeRate` | `RateRequest` | `DecimalResponse` | Solves for per-period interest rate (RATE) (`backend/proto/finance.proto:48`). |
| `ComputePeriods` | `PeriodsRequest` | `DecimalResponse` | Solves for number of payment periods (NPER) (`backend/proto/finance.proto:49`). |
| `ConvertInterestRate` | `RateConversionRequest` | `DoubleResponse` | Nominal to effective or effective to nominal rate conversion (`backend/proto/finance.proto:50`). |
| `ComputeFisherRate` | `FisherRequest` | `DoubleResponse` | Exact Fisher equation rate conversion (`backend/proto/finance.proto:51`). |
| `ComputeAmortization` | `AmortizationRequest` | `AmortizationResponse` | Full mortgage amortization schedule and summary (`backend/proto/finance.proto:54`). |
| `ComputeDetailedAmortization` | `DetailedAmortizationRequest` | `DetailedAmortizationResponse` | Amortization schedule including tax savings breakdown (`backend/proto/finance.proto:55`). |
| `ComputeAmortizationBatch` | `AmortizationBatchRequest` | `AmortizationBatchResponse` | High-throughput batch amortization summaries (`backend/proto/finance.proto:56`). |
| `ComputeHeloc` | `HelocRequest` | `HelocResponse` | HELOC available borrowing equity and payments (`backend/proto/finance.proto:57`). |
| `ComputeRefinance` | `RefinanceRequest` | `RefinanceResponse` | Mortgage refinance comparison and breakeven horizons (`backend/proto/finance.proto:58`). |
| `ComputePayoffTiming` | `PayoffTimingRequest` | `PayoffTimingResponse` | Accelerated mortgage payoff timeline with extra payments (`backend/proto/finance.proto:59`). |
| `ComputeMortgageRecast` | `MortgageRecastRequest` | `MortgageRecastResponse` | Loan recast payment reduction following lump-sum principal reduction (`backend/proto/finance.proto:60`). |
| `ComputeNpv` | `NpvRequest` | `DoubleResponse` | Net present value for periodic cash flows (`backend/proto/finance.proto:63`). |
| `ComputeIrr` | `IrrRequest` | `DoubleResponse` | Internal rate of return for periodic cash flows (`backend/proto/finance.proto:64`). |
| `ComputeXnpv` | `DatedCashFlowRequest` | `DoubleResponse` | Net present value for irregularly dated cash flows (`backend/proto/finance.proto:65`). |
| `ComputeXirr` | `DatedCashFlowRequest` | `DoubleResponse` | Internal rate of return for irregularly dated cash flows (`backend/proto/finance.proto:66`). |
| `ComputePaybackPeriod` | `PaybackRequest` | `DoubleResponse` | Plain or discounted payback period (`backend/proto/finance.proto:67`). |
| `ComputeCumulative` | `CumulativeRequest` | `DoubleResponse` | Cumulative principal or interest paid between periods (`backend/proto/finance.proto:68`). |
| `ComputeDepreciation` | `DepreciationRequest` | `DoubleResponse` | Depreciation charges (SLN, SYD, DDB, MACRS) (`backend/proto/finance.proto:71`). |
| `AnalyzeBond` | `BondRequest` | `BondResponse` | Bond pricing, yield to maturity, duration, and convexity (`backend/proto/finance.proto:74`). |
| `AnalyzeTreasuryBill` | `TreasuryBillRequest` | `TreasuryBillResponse` | T-bill pricing and yields (BEY, MMY, BDY) (`backend/proto/finance.proto:75`). |
| `PriceFutures` | `FuturesPricingRequest` | `DoubleResponse` | Cost-of-carry futures price (`backend/proto/finance.proto:78`). |
| `ValueFutures` | `FuturesValuationRequest` | `DoubleResponse` | Mark-to-market futures contract valuation (`backend/proto/finance.proto:79`). |
| `SimulateMarginAccount` | `MarginSimulationRequest` | `MarginSimulationResponse` | Simulates daily margin path and margin call triggers (`backend/proto/finance.proto:80`). |
| `ComputeHedge` | `HedgeRequest` | `HedgeResponse` | Minimum-variance hedge ratio and contract sizing (`backend/proto/finance.proto:81`). |
| `ComputeCommoditySpread` | `CommoditySpreadRequest` | `DoubleResponse` | Crack, spark, or crush commodity spread processing margin (`backend/proto/finance.proto:82`). |
| `ComputeRentalRoi` | `RentalRoiRequest` | `RentalRoiResponse` | Real estate NOI, cash flow, cap rate, and CoC return (`backend/proto/finance.proto:85`). |
| `ComputeHomeFutureValue` | `HomeFutureValueRequest` | `HomeFutureValueResponse` | Projected property value, remaining balance, and equity (`backend/proto/finance.proto:86`). |
| `ComputeRentVsBuy` | `RentVsBuyRequest` | `RentVsBuyResponse` | Rent vs buy wealth and cost comparison (`backend/proto/finance.proto:87`). |
| `ComputeRentVsBuyBatch` | `RentVsBuyBatchRequest` | `RentVsBuyBatchResponse` | Batch rent vs buy scenarios for corpus generation (`backend/proto/finance.proto:88`). |
| `ComputeHomeNpv` | `HomeNpvRequest` | `HomeNpvResponse` | Lifetime homeownership NPV and IRR (`backend/proto/finance.proto:89`). |
| `ComputeClosingCosts` | `ClosingCostsRequest` | `ClosingCostsResponse` | Itemized buyer closing costs, prepaids, escrow, and cash to close (`backend/proto/finance.proto:90`). |
| `RefreshStateAssumptions` | `RefreshStateAssumptionsRequest` | `RefreshStateAssumptionsResponse` | Triggers automated US Census ACS state housing data refresh (`backend/proto/finance.proto:91`). |
| `GetStateAssumptions` | `GetStateAssumptionsRequest` | `GetStateAssumptionsResponse` | Reads state housing and tax reference assumptions (`backend/proto/finance.proto:92`). |
| `PriceOptionTree` | `OptionTreeRequest` | `OptionPricingResponse` | Trinomial tree pricer for American, Bermudan, and Asian options (`backend/proto/finance.proto:95`). |
| `PriceBlackScholes` | `BlackScholesRequest` | `BlackScholesResponse` | Closed-form European option value and complete 1st-3rd order Greeks (`backend/proto/finance.proto:96`). |
| `PriceOptionMonteCarlo` | `MonteCarloRequest` | `DoubleResponse` | Monte Carlo pricing for European and Asian options (`backend/proto/finance.proto:97`). |
| `ComputeProbabilityTree` | `ProbabilityTreeRequest` | `ProbabilityTreeResponse` | Trinomial probability distribution tree (`backend/proto/finance.proto:98`). |
| `ComputePortfolioStats` | `PortfolioStatsRequest` | `PortfolioStatsResponse` | Sharpe, Sortino, Treynor, alpha, beta, and historical/parametric VaR/CVaR (`backend/proto/finance.proto:101`). |
| `OptimizePortfolio` | `PortfolioOptimizeRequest` | `PortfolioOptimizeResponse` | Mean-variance or maximum Sharpe portfolio optimization (`backend/proto/finance.proto:102`). |
| `ComputeRiskContributions` | `RiskContributionRequest` | `RiskContributionResponse` | Marginal component risk contributions decomposing portfolio risk (`backend/proto/finance.proto:103`). |

### Message and field inventory

#### TVM Messages (`backend/proto/finance.proto:111-224`)
- `DecimalResponse`: `value` (`string`, 18-decimal fixed-point).
- `DoubleResponse`: `value` (`double`).
- `PaymentRequest`: `rate` (`string`, decimal per-period rate), `periods` (`int32`, $> 0$), `present_value` (`string`), `future_value` (`string`, omit for 0), `timing` (`AnnuityTiming`).
- `PresentValueRequest`: `rate` (`string`), `periods` (`int32`), `payment` (`string`), `future_value` (`string`), `timing` (`AnnuityTiming`).
- `FutureValueRequest`: `rate` (`string`), `periods` (`int32`), `payment` (`string`), `present_value` (`string`), `timing` (`AnnuityTiming`).
- `FutureValueDetailedRequest`: `annual_rate` (`string`), `years` (`int32`), `annual_contribution` (`string`), `current_principal` (`string`), `annual_inflation_rate` (`string`), `compound_frequency` (`int32`, 12=monthly, 4=quarterly, 1=annual; 0 rejected, `backend/proto/finance.proto:160-163`).
- `FutureValueDetailedResponse`: `nominal_fv` (`string`), `inflation_adjusted_fv` (`string`), `total_contributions` (`string`), `total_interest_earned` (`string`).
- `PeriodPaymentRequest`: `rate` (`string`), `period` (`int32`, 1-based; outside $[1, \text{periods}]$ returns 0), `periods` (`int32`), `present_value` (`string`), `future_value` (`string`), `timing` (`AnnuityTiming`).
- `RateRequest`: `periods` (`int32`), `payment` (`string`), `present_value` (`string`), `future_value` (`string`), `timing` (`AnnuityTiming`), `guess` (`string`, optional solver seed).
- `PeriodsRequest`: `rate` (`string`), `payment` (`string`), `present_value` (`string`), `future_value` (`string`), `timing` (`AnnuityTiming`).
- `RateConversionRequest`: `direction` (`Direction`: `NOMINAL_TO_EFFECTIVE=0`, `EFFECTIVE_TO_NOMINAL=1`), `rate` (`double`), `periods_per_year` (`double`).
- `FisherRequest`: `direction` (`Direction`: `NOMINAL_TO_REAL=0`, `REAL_TO_NOMINAL=1`), `rate` (`double`), `inflation_rate` (`double`).

#### Mortgages & HELOC Messages (`backend/proto/finance.proto:229-413`)
- `AmortizationRequest`: `loan_amount` (`string`), `annual_rate` (`string`), `term_months` (`int32`), `monthly_overpayment` (`string`), `pmi_annual_rate` (`string`), `original_home_value` (`string`).
- `AmortizationRow`: `period` (`int32`), `start_balance` (`string`), `scheduled_payment` (`string`), `extra_payment` (`string`), `interest_paid` (`string`), `principal_paid` (`string`), `pmi_paid` (`string`), `end_balance` (`string`).
- `MortgageSummary`: `total_principal_paid` (`string`), `total_interest_paid` (`string`), `total_pmi_paid` (`string`), `total_payments_paid` (`string`), `actual_term_months` (`int32`).
- `AmortizationResponse`: `schedule` (`repeated AmortizationRow`), `summary` (`MortgageSummary`).
- `DetailedAmortizationRequest`: Same as `AmortizationRequest` plus `annual_tax_rate` (`string`, marginal tax deduction rate applied to interest, `backend/proto/finance.proto:274`).
- `DetailedAmortizationRow`: Same as `AmortizationRow` plus `tax_savings` (`string`).
- `DetailedMortgageSummary`: Same as `MortgageSummary` plus `total_tax_savings` (`string`).
- `DetailedAmortizationResponse`: `schedule` (`repeated DetailedAmortizationRow`), `summary` (`DetailedMortgageSummary`).
- `AmortizationBatchRequest`: Parallel arrays: `loan_amounts` (`repeated double`), `annual_rates` (`repeated double`), `term_months` (`repeated int32`), `extra_payments` (`repeated double`), `pmi_rates` (`repeated double`), `home_values` (`repeated double`). Arrays must have identical length; ragged arrays are rejected (`backend/proto/finance.proto:303-304`).
- `AmortizationBatchResponse`: `summaries` (`repeated MortgageSummary`).
- `HelocRequest`: `home_value` (`string`), `current_mortgage_balance` (`string`), `max_ltv_rate` (`string`), `drawn_amount` (`string`), `annual_rate` (`string`), `repayment_term_years` (`int32`), `payments_per_year` (`int32`, 12 or 26).
- `HelocResponse`: `available_equity` (`string`), `draw_period_payment` (`string`), `repayment_period_payment` (`string`).
- `RefinanceRequest`: `current_loan_balance` (`string`), `current_monthly_payment` (`string`), `current_annual_rate` (`string`), `current_remaining_months` (`int32`), `property_value` (`string`), `new_annual_rate` (`string`), `new_term_years` (`int32`), `closing_costs` (`string`), `closing_cost_type` (`ClosingCostType`: `PAID_IN_CASH=0`, `ROLLED_INTO_LOAN=1`), `cash_out_amount` (`string`), `current_pmi_monthly` (`string`), `new_pmi_monthly` (`string`), `pmi_drop_off_ltv` (`string`), `payments_per_year` (`int32`, 0 rejected).
- `RefinanceResponse`: `new_loan_amount` (`string`), `new_monthly_payment` (`string`), `monthly_savings_initial` (`string`), `current_loan_pmi_drop_off_months` (`int32`, -1 if never reached), `new_loan_pmi_drop_off_months` (`int32`), `payoff_date_shift_months` (`int32`), `simple_break_even_months` (`int32`), `cash_flow_break_even_months` (`int32`), `equity_adjusted_break_even_months` (`int32`), `total_savings_over_life` (`double`).
- `PayoffTimingRequest`: `current_loan_balance` (`string`), `annual_rate` (`string`), `current_monthly_payment` (`string`), `extra_monthly_payment` (`string`), `payments_per_year` (`int32`, 0 rejected).
- `PayoffTimingResponse`: `original_months_remaining` (`int32`), `new_months_remaining` (`int32`), `months_saved` (`int32`), `total_interest_saved` (`string`).
- `MortgageRecastRequest`: `current_loan_balance` (`string`), `current_monthly_payment` (`string`), `lump_sum_payment` (`string`), `annual_rate` (`string`), `remaining_months` (`int32`), `payments_per_year` (`int32`, 0 rejected).
- `MortgageRecastResponse`: `new_monthly_payment` (`string`), `monthly_savings` (`string`).

#### Cash Flow & Depreciation Messages (`backend/proto/finance.proto:418-492`)
- `NpvRequest`: `rate` (`double`), `values` (`repeated double`).
- `IrrRequest`: `values` (`repeated double`), `guess` (`double`, optional seed).
- `DatedCashFlowRequest`: `rate` (`double`, XNPV only; ignored by XIRR), `values` (`repeated double`), `dates` (`repeated double`, day offsets), `guess` (`double`, XIRR only).
- `PaybackRequest`: `values` (`repeated double`), `discounted` (`bool`, true for discounted payback), `rate` (`double`, required if discounted=true).
- `CumulativeRequest`: `component` (`Component`: `INTEREST=0`, `PRINCIPAL=1`), `rate` (`double`), `periods` (`int32`), `present_value` (`double`), `start_period` (`int32`), `end_period` (`int32`), `timing` (`AnnuityTiming`).
- `DepreciationRequest`: `method` (`Method`: `STRAIGHT_LINE=0`, `SUM_OF_YEARS_DIGITS=1`, `DECLINING_BALANCE=2`, `MACRS=3`), `cost` (`double`), `salvage` (`double`, unused by MACRS), `life` (`double`, years; unused by MACRS), `period` (`double`, which period's charge; unused by SLN), `factor` (`double`, DDB only; 2.0 = double-declining), `recovery_period` (`int32`, MACRS only), `year` (`int32`, MACRS only).

#### Fixed Income Messages (`backend/proto/finance.proto:501-542`)
- `BondRequest`: `par` (`double`), `coupon_rate` (`double`), `frequency` (`int32`), `years_to_maturity` (`double`), `redemption` (`double`), `oneof known` (`yield = 6` or `price = 7`, `double`), `yield_guess` (`double`).
- `BondResponse`: `price` (`double`), `yield` (`double`), `macaulay_duration` (`double`), `convexity` (`double`).
- `TreasuryBillRequest`: `face_value` (`double`), `days_to_maturity` (`int32`), `oneof known` (`discount_rate = 3` or `price = 4`, `double`).
- `TreasuryBillResponse`: `price` (`double`), `bond_equivalent_yield` (`double`), `money_market_yield` (`double`), `bank_discount_yield` (`double`).

#### Futures & Margin Messages (`backend/proto/finance.proto:548-621`)
- `FuturesPricingRequest`: `spot` (`double`), `rate` (`double`), `cost_of_carry` (`double`), `years_to_maturity` (`double`), `continuous` (`bool`).
- `FuturesValuationRequest`: `current_spot` (`double`), `delivery_price` (`double`), `rate` (`double`), `years_to_maturity` (`double`), `is_long` (`bool`).
- `MarginSimulationRequest`: `initial_deposit` (`double`), `initial_margin_requirement` (`double`), `maintenance_margin_requirement` (`double`), `contract_size` (`int32`), `entry_price` (`double`), `daily_prices` (`repeated double`), `is_long` (`bool`).
- `MarginSimulationResponse`: `balance` (`double`), `initial_margin` (`double`), `maintenance_margin` (`double`), `contract_size` (`int32`), `margin_call` (`bool`, true if balance breached maintenance at any point).
- `HedgeRequest`: `asset_volatility` (`double`), `futures_volatility` (`double`), `correlation` (`double`), `spot_value` (`double`, optional), `contract_multiplier` (`double`, optional), `futures_price` (`double`, optional).
- `HedgeResponse`: `hedge_ratio` (`double`), `contracts` (`double`, 0 if sizing unstated), `contracts_computed` (`bool`).
- `CommoditySpreadRequest`: `spread` (`Spread`: `CRACK_321=0`, `SPARK=1`, `CRUSH=2`), `a` (`double`), `b` (`double`), `c` (`double`). For Crack: a=crude, b=gasoline, c=heating oil; for Spark: a=power, b=gas, c=heat rate; for Crush: a=soybean, b=oil, c=meal (`backend/proto/finance.proto:615-620`).

#### Real Estate Messages (`backend/proto/finance.proto:627-856`)
- `RentalRoiRequest`: `property_value` (`string`), `total_cash_invested` (`string`), `periodic_gross_rent` (`string`), `periodic_operating_expenses` (`string`, non-debt expenses only), `periodic_mortgage_payment` (`string`), `periods_per_year` (`int32`).
- `RentalRoiResponse`: `net_operating_income` (`string`), `annual_cash_flow` (`string`), `cash_on_cash_return` (`string`), `cap_rate` (`string`), `gross_rent_multiplier` (`string`).
- `HomeFutureValueRequest`: `current_property_value` (`string`), `annual_appreciation_rate` (`string`), `current_loan_balance` (`string`), `annual_mortgage_rate` (`string`), `current_monthly_payment` (`string`), `target_years` (`int32`), `payments_per_year` (`int32`, 0 rejected).
- `HomeFutureValueResponse`: `future_property_value` (`double`), `future_loan_balance` (`string`), `future_equity` (`string`).
- `RentVsBuyRequest`: `property_price` (`string`), `down_payment` (`string`), `monthly_piti_and_maintenance` (`string`, legacy aggregate; triggers legacy model if used alone, `backend/proto/finance.proto:670-679`), `annual_home_appreciation` (`string`), `current_monthly_rent` (`string`), `annual_rent_increase` (`string`), `annual_investment_return` (`string`), `years` (`int32`), `loan_annual_rate` (`string`, defaults to 0), `loan_term_years` (`int32`, omitted defaults to 30, `backend/proto/finance.proto:693-696`), `loan_amount` (`string`), `monthly_taxes_ins_maintenance` (`string`), `closing_costs_buy` (`string`), `selling_cost_percent` (`string`, omitted defaults to 0, `backend/proto/finance.proto:711-718`), `annual_inflation_rate` (`string`).
- `RentVsBuyResponse`: `total_cost_of_buying` (`double`), `total_cost_of_renting` (`double`), `is_buying_better` (`bool`), `buying_advantage` (`double`), exact string companions `total_cost_of_buying_exact`, `total_cost_of_renting_exact`, `buying_advantage_exact`, `owner_terminal_wealth`, `renter_terminal_wealth`, `final_loan_balance`, `home_sale_price`, `selling_costs`, `total_principal_paid`, `total_interest_paid`, `total_rent_paid`, and real (inflation-deflated) terminal metrics: `real_buying_advantage`, `real_owner_terminal_wealth`, `real_renter_terminal_wealth`.
- `RentVsBuyBatchRequest`: `scenarios` (`repeated RentVsBuyRequest`, max 1,000, `backend/proto/finance.proto:794-797`).
- `RentVsBuyBatchResult`: `oneof outcome`: `result` (`RentVsBuyResponse = 1`) or `error` (`string = 2`).
- `RentVsBuyBatchResponse`: `results` (`repeated RentVsBuyBatchResult`, preserves exact index positional correspondence).
- `HomeNpvRequest`: `property_price` (`string`), `down_payment` (`string`), `closing_costs_buy` (`string`), `loan_amount` (`string`), `loan_annual_rate` (`string`), `loan_term_years` (`int32`), `monthly_taxes_ins_hoa` (`string`), `monthly_maintenance` (`string`), `annual_appreciation_rate` (`string`), `selling_closing_cost_percent` (`string`), `monthly_rent_saved` (`string`), `annual_rent_increase` (`string`), `annual_discount_rate` (`string`), `holding_period_years` (`int32`), `annual_inflation_rate` (`string`).
- `HomeNpvResponse`: `net_present_value` (`double`), `internal_rate_of_return` (`double`), `future_sale_price` (`double`), `future_equity` (`double`), `real_internal_rate_of_return` (`double`), `real_future_sale_price` (`double`), `real_future_equity` (`double`).
- `ClosingCostsRequest`: `home_price` (`string`), `down_payment_percent` (`string`), `annual_rate` (`string`), `origination_fee_percent` (`string`, share of LOAN), `discount_points_percent` (`string`, share of LOAN), `other_lender_fees` (`string`), `title_settlement_percent` (`string`, share of PRICE), `appraisal_fee` (`string`), `inspection_fee` (`string`), `recording_fees` (`string`), `transfer_tax_percent` (`string`, share of PRICE), `homeowners_insurance_annual` (`string`), `property_tax_annual` (`string`), `tax_escrow_months` (`int32`), `seller_lender_credits` (`string`), `optional int32 prepaid_interest_days = 16` (absent uses 15; explicit 0 uses 0).
- `ClosingCostsResponse`: Itemized fee lines (`origination_fee`, `discount_points`, `other_lender_fees`, `title_settlement`, `appraisal_fee`, `inspection_fee`, `recording_fees`, `transfer_tax`, `homeowners_insurance_prepaid`, `property_tax_escrow`, `prepaid_interest`), `prepaid_interest_days` (`int32`), `itemised_subtotal` (`string`), `seller_lender_credits` (`string`), `total_closing_costs` (`string`), `loan_amount` (`string`), `down_payment` (`string`), `total_cash_to_close` (`string`), and `closing_costs_percent_of_price` (`double`).
- `RefreshStateAssumptionsRequest`: `dry_run` (`bool`), `data_year` (`int32`, 0 selects newest eligible vintage).
- `RefreshStateAssumptionsResponse`: `ok` (`bool`), `data_year` (`int32`), `states_updated` (`int32`), `states_rejected` (`int32`), `data_source` (`string`), `error` (`string`).
- `GetStateAssumptionsRequest`: `slug` (`string`, empty returns all 50 states).
- `StateAssumption`: `slug`, `name`, `abbr`, `median_price`, `property_tax_rate`, `insurance_annual`, `state_income_tax`, `median_rent`, `note`, `data_source`, `data_year`, `refreshed_at` (RFC3339).
- `GetStateAssumptionsResponse`: `states` (`repeated StateAssumption`).

#### Options & Portfolio Messages (`backend/proto/finance.proto:882-1028`)
- `OptionTreeRequest`: `spot` (`double`), `strike` (`double`), `rate` (`double`), `volatility` (`double`), `years_to_expiry` (`double`), `steps` (`int32`), `option_type` (`OptionType`), `exercise_type` (`ExerciseType`), `bermudan_dates` (`repeated double`, required when exercise is BERMUDAN), `asian_type` (`AsianType`), `averaging_states` (`int32`), `lambda` (`double`, 0 uses $\sqrt{1.5}$).
- `OptionPricingResponse`: `value` (`double`), `delta` (`double`), `gamma` (`double`), `theta` (`double`).
- `BlackScholesRequest`: `spot` (`double`), `strike` (`double`), `rate` (`double`), `volatility` (`double`), `years_to_expiry` (`double`), `option_type` (`OptionType`).
- `BlackScholesResponse`: `value` (`double`), `delta` (`double`), `gamma` (`double`), `theta` (`double`), `vega` (`double`), `rho` (`double`), `vanna` (`double`), `volga` (`double`), `charm` (`double`), `color` (`double`), `speed` (`double`).
- `MonteCarloRequest`: `spot` (`double`), `strike` (`double`), `rate` (`double`), `volatility` (`double`), `years_to_expiry` (`double`), `paths` (`int32`), `steps` (`int32`), `option_type` (`OptionType`), `asian_type` (`AsianType`), `num_threads` (`int32`, -1 for auto).
- `ProbabilityTreeRequest`: `rate` (`double`), `volatility` (`double`), `years_to_expiry` (`double`), `steps` (`int32`), `lambda` (`double`).
- `ProbabilityTreeResponse`: `stock_prices` (`repeated double`), `state_probabilities` (`repeated double`), `steps` (`int32`).
- `PortfolioStatsRequest`: `portfolio_returns` (`repeated double`), `market_returns` (`repeated double`, benchmark), `risk_free_rate` (`double`).
- `PortfolioStatsResponse`: `sharpe_ratio`, `sortino_ratio`, `treynor_ratio`, `beta`, `alpha`, `max_drawdown`, historical and parametric 95%/99% VaR/CVaR (`var_historical_95`, etc.), `omega_ratio`, `calmar_ratio`, `information_ratio`, `tracking_error`, `benchmark_supplied` (`bool`).
- `PortfolioOptimizeRequest`: `expected_returns` (`repeated double`), `covariance` (`repeated double`, flat square size $\times$ size row-major), `size` (`int32`), `risk_free_rate` (`double`), `max_sharpe` (`bool`).
- `PortfolioOptimizeResponse`: `weights` (`repeated double`), `expected_return` (`double`), `volatility` (`double`), `sharpe_ratio` (`double`).
- `RiskContributionRequest`: `weights` (`repeated double`), `covariance` (`repeated double`), `size` (`int32`).
- `RiskContributionResponse`: `contributions` (`repeated double`).

### Invariants, refusals, and failure modes
- NaN / Infinity validation: All raw wire double fields in pricer and cash-flow calls are guarded against NaN / Infinity; non-finite inputs return `INVALID_ARGUMENT` (`backend/tests/test_finance_service_validation.cpp:55-86`).
- Compound growth magnitude: Excessive compounding parameters that would wrap or overflow `__int128` (e.g. rate $10^6$ over 1,200 periods) are refused with `INVALID_ARGUMENT` (`backend/tests/test_finance_service_validation.cpp:36-44`).
- Period iteration bounds: Loop counts in `ComputeInterestPayment`, `ComputePrincipalPayment`, and `ComputeCumulative` are capped to prevent CPU exhaustion DoS (`backend/tests/test_finance_service_validation.cpp:49-65`).
- `RentVsBuyBatchRequest` size: Capped at 1,000 scenarios; requests with $> 1,000$ scenarios are refused with `INVALID_ARGUMENT` (`backend/proto/finance.proto:794-797`).
- `ClosingCostsRequest` credit limit: `seller_lender_credits` exceeding `itemised_subtotal` returns `FAILED_PRECONDITION` (`docs/CLOSING_COSTS_CLIENT_NOTICE.md:140`).
- `RefreshStateAssumptions` auth gate: Requires `partner` tier credential; anonymous or Pro callers receive `PERMISSION_DENIED` (`backend/src/modules/finance_service.cpp:3102-3109`, `backend/tests/test_state_assumptions_gate.cpp:13-18`).
- Census data plausibility: State refresh rows outside bounds ($50k-$3M price, $300-$8k rent, 0.05%-4% tax) are incremented into `states_rejected` (`backend/proto/finance.proto:1145-1149`, `backend/tests/test_state_refresh.cpp:18-21`).

### Gotchas
- Base mismatch in closing costs: `origination_fee_percent` and `discount_points_percent` scale against the LOAN amount; `title_settlement_percent` and `transfer_tax_percent` scale against the PURCHASE PRICE (`backend/proto/finance.proto:1043-1046`, `docs/CLOSING_COSTS_CLIENT_NOTICE.md:60-64`).
- Covariance matrix layout: In `PortfolioOptimizeRequest` and `RiskContributionRequest`, `covariance` is a flat 1D array of length $\text{size} \times \text{size}$ in row-major order. Ragged or mismatched arrays return `INVALID_ARGUMENT` (`backend/proto/finance.proto:997-999`).
- Bond and T-bill oneofs: `BondRequest` and `TreasuryBillRequest` require exactly one of yield/discount_rate or price. Supplying neither or both returns `INVALID_ARGUMENT` (`backend/proto/finance.proto:507-513, 527-531`).

---

## Service: `calculator.assistant.StrategyAssistant` (`backend/proto/assistant.proto`)

### Purpose
Parses unstructured natural language trading requests into structured parameters suitable for `calculator.OptionsCalculator/CalculateStrategy`.

### RPC inventory

| RPC name | Request message | Response message | Description |
| --- | --- | --- | --- |
| `ParseStrategy` | `ParseRequest` | `ParseResponse` | Parses an options/futures trade description into structured strategy parameters, clarification questions, or refusals (`backend/proto/assistant.proto:58`). |

### Message and field inventory

#### `ParseRequest` (`backend/proto/assistant.proto:61-79`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `utterance` | `string` | Free text | Required; max 1,000 chars (`backend/src/modules/assistant_service.cpp:176, 2840`). |
| `prior_clarification` | `string` | Prior answer | Optional; max 400 chars (`backend/src/modules/assistant_service.cpp:177, 2846`). |

#### `ParseResponse` (`backend/proto/assistant.proto:81-90`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `outcome` | `oneof` | Mutual exclusion | Exactly one of `params`, `clarification`, or `refusal` is populated (`backend/proto/assistant.proto:85-89`). |
| `params` | `StrategyParams` | Structured output | Set when the request resolves to a valid trade (`backend/proto/assistant.proto:86`). |
| `clarification` | `Clarification` | Clarifying question | Set when a single piece of missing context must be asked (`backend/proto/assistant.proto:87`). |
| `refusal` | `Refusal` | Refusal diagnostic | Set when the trade cannot be priced (`backend/proto/assistant.proto:88`). |

#### `StrategyParams` (`backend/proto/assistant.proto:101-156`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `symbol` | `string` | Resolved ticker (e.g. "NVDA") | Max 15 chars (`backend/src/modules/assistant_service.cpp:180`). |
| `asset_class` | `string` | "EQUITY", "FUTURES", "CRYPTO" | Resolved instrument class (`backend/proto/assistant.proto:103`). |
| `strategy` | `string` | Catalogued strategy name | One of 48 valid strategies (`backend/proto/assistant.proto:104`). |
| `expiration_days` | `int32` | Calendar days | Range: $[0, 3650]$ (`backend/src/modules/assistant_service.cpp:194-195`). |
| `quantity` | `int32` | Contracts/lots | Range: $[1, 100000]$ (`backend/src/modules/assistant_service.cpp:196-197`). |
| `far_expiration_days`| `int32` | Calendar days | Required for calendar spreads; must be $> \text{expiration\_days}$ (`backend/proto/assistant.proto:120-122`). |
| `exercise_type` | `sensen.finance.ExerciseType` | Enum | Keyword extracted; default `EUROPEAN=0` (`backend/proto/assistant.proto:144-149`). |
| `asian_type` | `sensen.finance.AsianType` | Enum | Keyword extracted; default `NOT_ASIAN=0` (`backend/proto/assistant.proto:151-155`). |

#### `Clarification` (`backend/proto/assistant.proto:163-165`)
Contains string `question` (capped at 400 chars, `backend/src/modules/assistant_service.cpp:144`).

#### `Refusal` (`backend/proto/assistant.proto:171-216`)
Contains `reason` (`Reason`: `REASON_UNSPECIFIED=0`, `UNSUPPORTED_STRATEGY=1`, `UNKNOWN_SYMBOL=2`, `OUT_OF_SCOPE=3`, `MODEL_UNAVAILABLE=4`, `DATA_UNAVAILABLE=5`) and human-readable string `message`.

### Invariants, refusals, and failure modes
- Clarification and Refusal return gRPC `OK`: Clarifications and refusals return gRPC `OK` with populated oneof branches (`backend/proto/assistant.proto:40-52`). They represent successful evaluations rather than RPC transport errors.
- Length bounds: Utterances $> 1,000$ chars or prior clarifications $> 400$ chars are refused with `INVALID_ARGUMENT` (`backend/src/modules/assistant_service.cpp:2840-2852`).
- Prompt injection & advice requests: Prompt injections or requests for market advice/recommendations return `OK` with `Refusal.reason = OUT_OF_SCOPE` (`backend/src/modules/assistant_service.cpp:2854-2870`).
- Inference worker failure: Unavailability of the model backend returns `OK` with `Refusal.reason = MODEL_UNAVAILABLE` (`backend/src/modules/assistant_service.cpp:2892-2895`).
- Market data check: If the symbol cannot be confirmed with upstream feeds, returns `OK` with `Refusal.reason = DATA_UNAVAILABLE` (`backend/proto/assistant.proto:201-211`).

### Gotchas
- Keyword extraction for exercise/Asian styles: The fine-tuned LLM emits only the core 5 fields; `exercise_type` and `asian_type` are populated by a deterministic keyword extractor over the utterance (`assistant_verification.cppm`), defaulting to `EUROPEAN` and `NOT_ASIAN` (`backend/proto/assistant.proto:124-155`).

---

## Service: `mortgage.assistant.MortgageAssistant` (`backend/proto/mortgage_assistant.proto`)

### Purpose
Parses unstructured natural language mortgage, loan, and financial calculation queries into structured parameters matching `sensen.finance.Finance` RPCs.

### RPC inventory

| RPC name | Request message | Response message | Description |
| --- | --- | --- | --- |
| `ParseOperation` | `ParseRequest` | `ParseResponse` | Translates a loan or cash flow query into structured arguments for a specific Finance RPC (`backend/proto/mortgage_assistant.proto:75`). |

### Message and field inventory

#### `ParseRequest` (`backend/proto/mortgage_assistant.proto:78-164`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `utterance` | `string` | Free text | Required; max 1,000 chars (`backend/src/modules/mortgage_assistant_service.cpp:225, 3096`). |
| `prior_clarification` | `string` | Prior answer | Optional; max 400 chars (`backend/src/modules/mortgage_assistant_service.cpp:226, 3102`). |
| `prior_question` | `string` | Question being answered | Optional; retained for backward compatibility (`backend/proto/mortgage_assistant.proto:104-163`). |

#### `ParseResponse` (`backend/proto/mortgage_assistant.proto:166-174`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `outcome` | `oneof` | Mutual exclusion | Exactly one of `params`, `clarification`, or `refusal` (`backend/proto/mortgage_assistant.proto:169-173`). |
| `params` | `FinanceParams` | Structured output | Set when the request resolves to an executable Finance RPC (`backend/proto/mortgage_assistant.proto:170`). |
| `clarification` | `Clarification` | Clarifying question | Set when a required input parameter is missing (`backend/proto/mortgage_assistant.proto:171`). |
| `refusal` | `Refusal` | Refusal diagnostic | Set when the query is out of scope or invalid (`backend/proto/mortgage_assistant.proto:172`). |

#### `FinanceParams` (`backend/proto/mortgage_assistant.proto:226-270`)
| Field name | Wire type | Units / Convention | Presence / Restriction |
| --- | --- | --- | --- |
| `operation` | `string` | Exact RPC name on `sensen.finance.Finance` | E.g. "ComputeAmortization", "ComputeRefinance" (`backend/proto/mortgage_assistant.proto:228-237`). |
| `params` | `map<string, string>` | Field name to string literal | Every field declared by the operation's request message is present (`backend/proto/mortgage_assistant.proto:242-269`). |

#### `Clarification` (`backend/proto/mortgage_assistant.proto:277-279`)
Contains string `question` (capped at 400 chars, `backend/src/modules/mortgage_assistant_service.cpp:211`).

#### `Refusal` (`backend/proto/mortgage_assistant.proto:284-322`)
Contains `reason` (`Reason`: `REASON_UNSPECIFIED=0`, `UNSUPPORTED_OPERATION=1`, `INVALID_PARAMETERS=2`, `OUT_OF_SCOPE=3`, `MODEL_UNAVAILABLE=4`) and human-readable string `message`.

### Invariants, refusals, and failure modes
- Complete field mapping: `params` must carry all fields declared by the request message; missing fields are refused with `INVALID_PARAMETERS` to prevent proto3 default zero-injection (`backend/proto/mortgage_assistant.proto:262-268`).
- String encoding of values: Values in `params` are formatted as: exact decimal string for BigDecimal, decimal digits for int32, shortest decimal text for double, `"true"`/`"false"` for bool, enum constant name for enums, and JSON arrays for repeated values (`backend/proto/mortgage_assistant.proto:245-260`).
- Text bounds: Utterances $> 1,000$ chars or prior clarifications $> 400$ chars return `INVALID_ARGUMENT` (`backend/src/modules/mortgage_assistant_service.cpp:3096-3108`). Decimal strings in values are capped at 48 chars, array lengths at 512, and absolute magnitudes at $10^{15}$ (`backend/src/modules/mortgage_assistant_service.cpp:240-242`).
- Five verification gates: Gated through `mortgage_verification.cppm`: G1 (closed operation vocabulary), G2 (declared field set & enum names), G3 (utterance grounding), G4 (presence check), and G5 (plausibility bounds). Only `Proven` outputs are served (`backend/src/modules/mortgage_verification.cppm:48-67`).

### Gotchas
- Model label-space vs. verification schema: The schema and verification code support 27 operations, but the active production model was trained on 26 operations (`docs/CLOSING_COSTS_CLIENT_NOTICE.md:156-158`). Utterances asking for closing costs yield a refusal in production rather than parameters.
- Opposite signs in annuity solvers: `ComputeRate` and `ComputePeriods` require cash flows with opposite signs (borrowing vs paying). Natural language queries describing positive loan and payment amounts are signed by the service layer before dispatch (`backend/src/modules/mortgage_verification.cppm:1360-1380`).

---

## HTTP edge mapping (JSON transcoder paths)

All 55 gRPC RPCs across the four services are accessible through Envoy's `grpc_json_transcoder` filter (`backend/envoy.yaml:154-170`) using HTTP `POST` at `https://api.optionsandfuturescalculator.com`.

| Package / Service | RPC Method | HTTP Method and Transcoder Path |
| --- | --- | --- |
| `calculator.OptionsCalculator` | `CalculateStrategy` | `POST /calculator.OptionsCalculator/CalculateStrategy` |
| `calculator.OptionsCalculator` | `GetMarketQuote` | `POST /calculator.OptionsCalculator/GetMarketQuote` |
| `calculator.OptionsCalculator` | `GetMarketChain` | `POST /calculator.OptionsCalculator/GetMarketChain` |
| `calculator.OptionsCalculator` | `GetRiskFreeRate` | `POST /calculator.OptionsCalculator/GetRiskFreeRate` |
| `calculator.OptionsCalculator` | `SaveStrategy` | `POST /calculator.OptionsCalculator/SaveStrategy` |
| `calculator.OptionsCalculator` | `ListStrategies` | `POST /calculator.OptionsCalculator/ListStrategies` |
| `calculator.OptionsCalculator` | `DeleteStrategy` | `POST /calculator.OptionsCalculator/DeleteStrategy` |
| `sensen.finance.Finance` | `ComputePayment` | `POST /sensen.finance.Finance/ComputePayment` |
| `sensen.finance.Finance` | `ComputePresentValue` | `POST /sensen.finance.Finance/ComputePresentValue` |
| `sensen.finance.Finance` | `ComputeFutureValue` | `POST /sensen.finance.Finance/ComputeFutureValue` |
| `sensen.finance.Finance` | `ComputeFutureValueDetailed` | `POST /sensen.finance.Finance/ComputeFutureValueDetailed` |
| `sensen.finance.Finance` | `ComputeInterestPayment` | `POST /sensen.finance.Finance/ComputeInterestPayment` |
| `sensen.finance.Finance` | `ComputePrincipalPayment` | `POST /sensen.finance.Finance/ComputePrincipalPayment` |
| `sensen.finance.Finance` | `ComputeRate` | `POST /sensen.finance.Finance/ComputeRate` |
| `sensen.finance.Finance` | `ComputePeriods` | `POST /sensen.finance.Finance/ComputePeriods` |
| `sensen.finance.Finance` | `ConvertInterestRate` | `POST /sensen.finance.Finance/ConvertInterestRate` |
| `sensen.finance.Finance` | `ComputeFisherRate` | `POST /sensen.finance.Finance/ComputeFisherRate` |
| `sensen.finance.Finance` | `ComputeAmortization` | `POST /sensen.finance.Finance/ComputeAmortization` |
| `sensen.finance.Finance` | `ComputeDetailedAmortization` | `POST /sensen.finance.Finance/ComputeDetailedAmortization` |
| `sensen.finance.Finance` | `ComputeAmortizationBatch` | `POST /sensen.finance.Finance/ComputeAmortizationBatch` |
| `sensen.finance.Finance` | `ComputeHeloc` | `POST /sensen.finance.Finance/ComputeHeloc` |
| `sensen.finance.Finance` | `ComputeRefinance` | `POST /sensen.finance.Finance/ComputeRefinance` |
| `sensen.finance.Finance` | `ComputePayoffTiming` | `POST /sensen.finance.Finance/ComputePayoffTiming` |
| `sensen.finance.Finance` | `ComputeMortgageRecast` | `POST /sensen.finance.Finance/ComputeMortgageRecast` |
| `sensen.finance.Finance` | `ComputeNpv` | `POST /sensen.finance.Finance/ComputeNpv` |
| `sensen.finance.Finance` | `ComputeIrr` | `POST /sensen.finance.Finance/ComputeIrr` |
| `sensen.finance.Finance` | `ComputeXnpv` | `POST /sensen.finance.Finance/ComputeXnpv` |
| `sensen.finance.Finance` | `ComputeXirr` | `POST /sensen.finance.Finance/ComputeXirr` |
| `sensen.finance.Finance` | `ComputePaybackPeriod` | `POST /sensen.finance.Finance/ComputePaybackPeriod` |
| `sensen.finance.Finance` | `ComputeCumulative` | `POST /sensen.finance.Finance/ComputeCumulative` |
| `sensen.finance.Finance` | `ComputeDepreciation` | `POST /sensen.finance.Finance/ComputeDepreciation` |
| `sensen.finance.Finance` | `AnalyzeBond` | `POST /sensen.finance.Finance/AnalyzeBond` |
| `sensen.finance.Finance` | `AnalyzeTreasuryBill` | `POST /sensen.finance.Finance/AnalyzeTreasuryBill` |
| `sensen.finance.Finance` | `PriceFutures` | `POST /sensen.finance.Finance/PriceFutures` |
| `sensen.finance.Finance` | `ValueFutures` | `POST /sensen.finance.Finance/ValueFutures` |
| `sensen.finance.Finance` | `SimulateMarginAccount` | `POST /sensen.finance.Finance/SimulateMarginAccount` |
| `sensen.finance.Finance` | `ComputeHedge` | `POST /sensen.finance.Finance/ComputeHedge` |
| `sensen.finance.Finance` | `ComputeCommoditySpread` | `POST /sensen.finance.Finance/ComputeCommoditySpread` |
| `sensen.finance.Finance` | `ComputeRentalRoi` | `POST /sensen.finance.Finance/ComputeRentalRoi` |
| `sensen.finance.Finance` | `ComputeHomeFutureValue` | `POST /sensen.finance.Finance/ComputeHomeFutureValue` |
| `sensen.finance.Finance` | `ComputeRentVsBuy` | `POST /sensen.finance.Finance/ComputeRentVsBuy` |
| `sensen.finance.Finance` | `ComputeRentVsBuyBatch` | `POST /sensen.finance.Finance/ComputeRentVsBuyBatch` |
| `sensen.finance.Finance` | `ComputeHomeNpv` | `POST /sensen.finance.Finance/ComputeHomeNpv` |
| `sensen.finance.Finance` | `ComputeClosingCosts` | `POST /sensen.finance.Finance/ComputeClosingCosts` |
| `sensen.finance.Finance` | `RefreshStateAssumptions` | `POST /sensen.finance.Finance/RefreshStateAssumptions` |
| `sensen.finance.Finance` | `GetStateAssumptions` | `POST /sensen.finance.Finance/GetStateAssumptions` |
| `sensen.finance.Finance` | `PriceOptionTree` | `POST /sensen.finance.Finance/PriceOptionTree` |
| `sensen.finance.Finance` | `PriceBlackScholes` | `POST /sensen.finance.Finance/PriceBlackScholes` |
| `sensen.finance.Finance` | `PriceOptionMonteCarlo` | `POST /sensen.finance.Finance/PriceOptionMonteCarlo` |
| `sensen.finance.Finance` | `ComputeProbabilityTree` | `POST /sensen.finance.Finance/ComputeProbabilityTree` |
| `sensen.finance.Finance` | `ComputePortfolioStats` | `POST /sensen.finance.Finance/ComputePortfolioStats` |
| `sensen.finance.Finance` | `OptimizePortfolio` | `POST /sensen.finance.Finance/OptimizePortfolio` |
| `sensen.finance.Finance` | `ComputeRiskContributions` | `POST /sensen.finance.Finance/ComputeRiskContributions` |
| `calculator.assistant.StrategyAssistant` | `ParseStrategy` | `POST /calculator.assistant.StrategyAssistant/ParseStrategy` |
| `mortgage.assistant.MortgageAssistant` | `ParseOperation` | `POST /mortgage.assistant.MortgageAssistant/ParseOperation` |

---

## Test coverage

Every service, verification module, and validation rule has corresponding automated regression gates:

| Test target / file | Primary exercised area |
| --- | --- |
| `backend/tests/test_calculator_service.cpp` | In-process gRPC tests for `calculator.OptionsCalculator`: closed-form bull call spread identity, iron condor breakevens/Greeks, P&L grid dimensions, action registration silent-halt postcondition, and `SaveStrategy`/`ListStrategies`/`DeleteStrategy` with real JWT authentication and storage limits. |
| `backend/tests/test_finance_service_validation.cpp` | Comprehensive input validation, boundary auditing, iteration limits, magnitude overflow checks, and NaN/Infinity bypass prevention across all `sensen.finance.Finance` methods. |
| `backend/tests/test_option_pricing_service.cpp` | `sensen.finance.Finance/PriceOptionTree` Bermudan date alignment, Asian averaging states ($2..200$), tree spacing parameter $\lambda$, spot and volatility validation. |
| `backend/tests/test_state_refresh.cpp` | Standalone validation bounds for US Census ACS rows ($50k-$3M price, $300-$8k rent, 0.05%-4% tax) consumed by `RefreshStateAssumptions`. |
| `backend/tests/test_state_assumptions_gate.cpp` | Tests write gate on `RefreshStateAssumptions`, proving `PERMISSION_DENIED` for anonymous and Pro callers, and admission for partner credentials. |
| `backend/tests/test_assistant_service.cpp` | In-process gRPC tests for `calculator.assistant.StrategyAssistant/ParseStrategy` over `StrategyAssistantWorkflow`: admission limits (1,000 char utterance, 400 char clarification), prompt injection rejection, model availability refusal, and did-compute postconditions. |
| `backend/tests/test_assistant_verification.cpp` | Standalone verification unit tests for `assistant_verification.cppm`: closed-vocabulary strategy validation (48 strategies), ticker validation, quantity and expiration limits, and keyword extraction. |
| `backend/tests/test_mortgage_assistant_service.cpp` | In-process gRPC tests for `mortgage.assistant.MortgageAssistant/ParseOperation` over `MortgageAssistantWorkflow`: length bounds, injection checks, model availability gating, and verdict routing. |
| `backend/tests/test_mortgage_grammar.cpp` | Proves grammar label-space matches `finance.proto` (27 operations, 184 fields in declaration order), verifies enum tables, and tests character-level automaton rejection and non-vacuous gold validation. |
| `backend/tests/test_mortgage_verification.cpp` | Standalone tests for the 5 mortgage verification gates: operation vocabulary, field totality, utterance value grounding, absent-params detection, and plausibility bounds. |
| `backend/tests/test_vendored_proto_drift.cpp` | Compares vendored client protos in `clients/mortgagefv/proto/` and `frontend/src/grpc/` against canonical backend protos in `backend/proto/`. |

---

## Open questions

Nothing UNVERIFIED. All RPCs, messages, field behaviors, invariants, transcoding routes, and test targets are documented and checkable directly against source code and configuration in the repository.
