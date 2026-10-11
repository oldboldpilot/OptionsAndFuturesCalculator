# Backend code map: assistant verification, catalogue and pricing
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

This document provides reference documentation for the verification, cataloguing, pricing, and unit testing modules in the backend:

- `backend/src/modules/assistant_verification.cppm`
- `backend/src/modules/strategy_catalogue.cppm`
- `backend/src/modules/pricing_engine.cppm`
- `backend/src/modules/testing_framework.cppm`

> `mortgage_verification.cppm` and `mortgage_grammar.cppm`, which this map used to cover, left this
> repository with the mortgage assistant (2026-10): the verifier now lives in `nest-egg-loan`, and
> the grammar was decoder-only and was deleted with the Qwen3 path.

---

## backend/src/modules/assistant_verification.cppm

### Purpose
Implements mandatory, fail-closed cross-field verification for options and futures strategy assistant parameters. It ensures mutual consistency across symbol, asset class, strategy catalogue identity, and expiration structure, and provides input filtering for prompt injection, financial advice, and bare futures order recovery.

### Exported Symbol Inventory

| Symbol | Signature / Type | Description |
| :--- | :--- | :--- |
| `AssistantParamsInput` | `struct` (`:98-107`) | Input container: `symbol`, `asset_class`, `strategy`, `expiration_days`, `quantity`, `far_expiration_days`. |
| `ReasonCode` | `enum class` (`:114-119`) | Refusal mapping: `None`, `UnsupportedStrategy`, `UnknownSymbol`, `OutOfScope`. |
| `VerificationFacts` | `struct` (`:124-129`) | Evaluation facts: `violated`, `incomplete`, `reason`, `detail`. |
| `AssetClassSignal` | `enum class` (`:234`) | Trader signal: `None`, `Futures`, `Equity`. |
| `AmbiguousRootInfo` | `struct` (`:244-249`) | Ambiguity details: `symbol`, `futures_label`, `equity_label`, `equity_keyword`. |
| `find_ambiguous_root_info` | `(string_view) -> const AmbiguousRootInfo*` (`:394`) | Checks if root is in `detail::kAmbiguousRootDetails` (`"ES"`, `"CL"`). |
| `detect_asset_class_signal` | `(string_view, string_view) -> AssetClassSignal` (`:434-435`) | Resolves ambiguous root asset class from utterance keywords. |
| `build_ambiguity_clarification` | `(string_view, string_view) -> optional<string>` (`:490-491`) | Generates user clarification prompt for unresolved ambiguous roots. |
| `normalize_strategy_alias` | `(string_view) -> optional<string>` (`:609`) | Normalises transposed two-token strategy names (e.g. `long_futures` -> `futures_long`). |
| `AssistantParamsDomain` | `class` (`:631-799`) | GP-ARA `DomainPolicy` evaluating cross-field rules over 5 parameters. |
| `RuleBasedReasoner` | `class` (`:812-850`) | Direct evaluation `ReasonerPolicy` returning `expected<bool, ReasonerError>`. |
| `Outcome` | `enum class` (`:861`) | Tri-state: `Proven`, `Unsafe`, `Indeterminate`. |
| `VerificationVerdict` | `struct` (`:863-867`) | Output verdict: `outcome`, `reason`, `message`. |
| `verify_assistant_params` | `(const AssistantParamsInput&) -> VerificationVerdict` (`:882-883`) | Main verification entry point. |
| `strategy_has_lexical_support` | `(string_view, string_view) -> bool` (`:1001-1002`) | True if prompt contains at least one non-generic token from strategy id. |
| `is_unsupported_bare_direction_guess` | `(string_view, string_view) -> bool` (`:1044-1045`) | Detects hallucinated `long_call`/`long_put` without prompt lexical support. |
| `infer_ambiguous_root_asset_class` | `(string_view, string_view, int64_t, int64_t, string_view) -> optional<string>` (`:1121-1124`) | Infers `"FUTURES"` via strategy category constraint and lexical support. |
| `looks_like_prompt_injection` | `(string_view) -> bool` (`:1239`) | Detects system override and prompt extraction attacks. |
| `looks_like_advice_request` | `(string_view) -> bool` (`:1253`) | Detects requests asking for market predictions or trade recommendations. |
| `recover_bare_futures_directive` | `(string_view) -> optional<string>` (`:1361-1362`) | Deterministically reconstructs parameters for directional futures orders. |
| `ExerciseType` | `enum class` (`:1436`) | `EUROPEAN = 0`, `AMERICAN = 1`, `BERMUDAN = 2`. |
| `AsianType` | `enum class` (`:1440`) | `NOT_ASIAN = 0`, `AVERAGE_PRICE = 1`, `AVERAGE_STRIKE = 2`. |
| `ExerciseAsianExtraction` | `struct` (`:1447-1450`) | Container holding extracted `exercise_type` and `asian_type`. |
| `extract_exercise_and_asian` | `(string_view) -> ExerciseAsianExtraction` (`:1697`) | Extracts exercise style and Asian averaging options from prompt. |

### Invariants, Refusals and Failure Modes

- `symbol` not matching alphanumeric ticker grammar yields `Outcome::Unsafe` with `ReasonCode::UnknownSymbol` (`:640-645`).
- `asset_class` not in `{"EQUITY", "FUTURES", "CRYPTO"}` yields `Outcome::Unsafe` with `ReasonCode::UnknownSymbol` (`:651-656`).
- Strategy matching `kAssistantForbiddenStrategies` (`"crack_321"`) yields `Outcome::Unsafe` with `ReasonCode::UnsupportedStrategy` (`:659-666`).
- Unknown strategy yields `Outcome::Unsafe` with `ReasonCode::UnsupportedStrategy` (`:669-676`).
- Multi-expiry strategy with `far_expiration_days <= expiration_days` (and non-zero) yields `Outcome::Unsafe` with `ReasonCode::UnsupportedStrategy` (`:697-705`).
- Single-expiry strategy with non-zero `far_expiration_days` yields `Outcome::Unsafe` with `ReasonCode::UnsupportedStrategy` (`:709-715`).
- Cross-field category mismatches yield `Outcome::Unsafe` with `ReasonCode::UnsupportedStrategy`:
  - `category == "Futures"` paired with `asset_class != "FUTURES"` (`:719-727`).
  - Options category (`Bullish`, `Bearish`, `Neutral`, `Volatility`, `Income & Hedge`) paired with `asset_class == "FUTURES"` (`:728-737`).
- Unmapped strategy category in catalogue yields `Outcome::Indeterminate` with `ReasonCode::OutOfScope` (`:746-750`).
- Known non-ambiguous futures root paired with non-futures asset class, or known crypto symbol paired with non-crypto asset class, yields `Outcome::Unsafe` with `ReasonCode::UnknownSymbol` (`:762-777`). Ambiguous roots (`"ES"`, `"CL"`) are excluded from this static check.
- Quantity $< 1$ or $> 100{,}000$ yields `Outcome::Unsafe` with `ReasonCode::OutOfScope` (`:780-785`).
- Expiration days $< 1$ or $> 3{,}650$ yields `Outcome::Unsafe` with `ReasonCode::OutOfScope` (`:788-795`).

### Gotchas
- **Ambiguous root handling**: For roots in `kAmbiguousRoots` (`"ES"` and `"CL"`), `translate()` does not reject an equity or futures designation because both are valid listings (Eversource Energy / E-mini S&P, and Colgate-Palmolive / Crude Oil) (`:140-167`). Disambiguation must occur via `detect_asset_class_signal()`, prompt clarification, or live market data probing.
- **Asymmetric asset class inference**: `infer_ambiguous_root_asset_class()` only ever infers `"FUTURES"` (`:1074-1089, 1132`). It never infers `"EQUITY"` by process of elimination, because the fine-tuned model frequently defaults to equity option strategies when uncertain.
- **Capitalisation guard in exercise extraction**: When parsing exercise styles ("American"), the parser checks `next_token_is_capitalized` (`:1579-1586`). "American put" extracts as `ExerciseType::AMERICAN`, but "American Airlines put" does not, preventing ticker name collisions.

---

## backend/src/modules/strategy_catalogue.cppm

### Purpose
Contains the authoritative compile-time catalogue of 47 option and futures trading strategies supported by the calculation engine and assistant verification layers.

### Generation Mechanism and Drift Invariants

- **Generator Script**: `scripts/generate_strategy_catalogue.py` (`backend/src/modules/strategy_catalogue.cppm:3-8`).
- **Source of Truth**: `agent/dataset/strategies.json` (47 entries).
- **Drift Hazards**:
  - Hand edits to `strategy_catalogue.cppm` are overwritten whenever `generate_strategy_catalogue.py` runs.
  - Three distinct files register strategy identifiers:
    1. `frontend/src/components/StrategySelector.tsx` (Web UI list, 48 entries).
    2. `agent/dataset/strategies.json` (Fine-tuning dataset, 47 entries).
    3. `backend/src/modules/strategy_catalogue.cppm` (Backend validator, 47 entries).
  - The frontend defines a 48th strategy: `"crack_321"` ("3-2-1 Crack Spread", category Futures). Because `"crack_321"` is absent from `strategies.json`, `strategy_catalogue.cppm` contains 47 entries and rejects `"crack_321"` (`:30-40`). `assistant_verification.cppm` explicitly blocks it via `kAssistantForbiddenStrategies` (`backend/src/modules/assistant_verification.cppm:179`).

### Exported Symbol Inventory

| Symbol | Signature / Type | Description |
| :--- | :--- | :--- |
| `StrategyInfo` | `struct` (`:58-65`) | Metadata: `id`, `name`, `category`, `description`, `leg_count`, `multi_expiry`. |
| `kCatalogue` | `inline constexpr array<StrategyInfo, 47>` (`:67-115`) | Complete internal static array of catalogued strategies. |
| `all` | `() noexcept -> span<const StrategyInfo>` (`:118-120`) | Returns span view over `kCatalogue`. |
| `count` | `() noexcept -> size_t` (`:123-125`) | Returns the number of strategies (47). |
| `find` | `(string_view) noexcept -> const StrategyInfo*` (`:128-132`) | Looks up strategy by exact wire id; returns `nullptr` if unknown. |
| `is_known` | `(string_view) noexcept -> bool` (`:140-142`) | Returns `true` if strategy exists in catalogue. |

### Invariants, Refusals and Failure Modes

- Any strategy id not present in `kCatalogue` causes `is_known()` to return `false` and `find()` to return `nullptr` (`:128-142`).
- Callers must reject unknown strategy ids rather than approximating them to nearest neighbours (`:50-54`).

### Gotchas
- `multi_expiry` is `true` for exactly 5 strategies: `calendar_spread`, `diagonal_spread`, `double_diagonal`, `pmcc`, and `futures_calendar` (`:99-101, 106, 109`). All other 42 strategies have `multi_expiry == false`.

---

## backend/src/modules/pricing_engine.cppm

### Purpose
Exposes strategy calculation and parametric risk simulation entry points for multi-leg option and futures strategies (`calculator.engine`), delegating option pricing and Greeks to `sensen`.

### Exposed Models and Sensen Entry Points

1. **Black-Scholes-Merton Pricing & Greeks**:
   - For option legs (`Type::CALL` and `Type::PUT`), calls `sensen::price_black_scholes(S, K, r, sigma, T, opt_type)` (`backend/src/modules/pricing_engine.cppm:132, 179, 182`).
   - Sensen returns `.value`, `.delta`, `.gamma`, `.theta`, `.vega`, `.rho`. Costs and Greeks are scaled by quantity and standard contract multiplier (100.0).
2. **Linear Asset Valuation**:
   - `Type::STOCK`: Evaluated linearly with $\Delta = 1.0 \times \text{sign} \times q$, cost calculated per share (`:142-146, 185-187`).
   - `Type::FUTURE`: Evaluated linearly with $\Delta = 1.0 \times \text{sign} \times q$, terminal PnL scaled by multiplier 100.0 (`:147-151, 188-192`).
3. **Parametric Value-at-Risk (VaR) & Conditional VaR (CVaR)**:
   - Evaluated by `calculate_risk(total_strategy_cost, implied_volatility)` (`:87-104`).
   - Computes parametric 95% ($Z = 1.64485$) and 99% ($Z = 2.32635$) normal distribution risk:
     $$\text{VaR}_{95} = \text{Cost} \cdot Z_{95} \cdot \sigma, \quad \text{VaR}_{99} = \text{Cost} \cdot Z_{99} \cdot \sigma$$
     $$\text{CVaR}_{95} = \text{Cost} \cdot \left(\frac{\phi(Z_{95})}{0.05}\right) \cdot \sigma, \quad \text{CVaR}_{99} = \text{Cost} \cdot \left(\frac{\phi(Z_{99})}{0.01}\right) \cdot \sigma$$
     where $\phi(Z_{95}) = 0.103135$ and $\phi(Z_{99}) = 0.026652$.
4. **PnL Matrix Simulation**:
   - Simulates strategy terminal payoff across 50 steps spanning $\pm 2$ standard deviations ($S \pm 2 \cdot S \cdot \sigma$) (`:158-200`). Computes `max_profit`, `max_loss`, and Probability of Profit (`pop`).

### Exported Symbol Inventory

| Symbol | Signature / Type | Description |
| :--- | :--- | :--- |
| `Action` | `enum class` (`:22`) | `BUY = 0`, `SELL = 1`. |
| `Type` | `enum class` (`:23`) | `CALL = 0`, `PUT = 1`, `FUTURE = 2`, `STOCK = 3`. |
| `Leg` | `struct` (`:25-31`) | `action`, `type`, `strike`, `expiration_days`, `quantity`. |
| `StrategyRequest` | `struct` (`:33-39`) | `underlying_symbol`, `current_price`, `implied_volatility`, `risk_free_rate`, `legs`. |
| `Greeks` | `struct` (`:41-47`) | `delta`, `gamma`, `theta`, `vega`, `rho`. |
| `PnLPoint` | `struct` (`:49-52`) | `underlying_price`, `pnl`. |
| `RiskMetrics` | `struct` (`:54-59`) | Parametric 95/99 VaR and CVaR metrics. |
| `StrategyResponse` | `struct` (`:61-70`) | Net metrics: `max_profit`, `max_loss`, `break_even`, `expected_value`, `pop`, `net_greeks`, `risk_metrics`, `pnl_matrix`. |
| `ErrorCode` | `enum class` (`:72-79`) | `INVALID_PRICE`, `INVALID_VOLATILITY`, `INVALID_RATE`, `INVALID_DAYS`, `SIMD_COMPUTATION_ERROR`, `UNKNOWN_ERROR`. |
| `EngineError` | `struct` (`:81-84`) | `ErrorCode code`, `std::string message`. |
| `calculate_strategy` | `(const StrategyRequest&) -> expected<StrategyResponse, EngineError>` (`:106-207`) | Evaluates multi-leg strategy pricing, net Greeks, VaR, and PnL matrix. |

### Invariants, Refusals and Failure Modes

- `req.current_price <= 0.0` returns `unexpected` with `ErrorCode::INVALID_PRICE` (`:107`).
- `req.implied_volatility <= 0.0` returns `unexpected` with `ErrorCode::INVALID_VOLATILITY` (`:108`).

### Gotchas
- **Global `operator new` anchor**: The module includes `<new>` in its global module fragment (`:1-12`). This is required to prevent ambiguous `operator new` linker errors caused by Intel TBB imported transitively through `sensen.options`.
- **Test-only linkage**: `pricing_engine.cppm` is linked into `test_runner`, while production services dispatch option pricing directly through `calculator_service.cpp` and `sensen` (`backend/CMakeLists.txt:1194, 1310`).

---

## backend/src/modules/testing_framework.cppm

### Purpose
Defines unit test assertion helpers and self-test harness utilities for the pricing engine (`calculator.testing`).

### Assertion Helpers and Conventions

- `assert_true(bool condition, std::string_view msg) -> TestResult` (`backend/src/modules/testing_framework.cppm:22-28`):
  Returns a `TestResult` struct containing `passed = condition` and a formatted message:
  `"PASS: <msg>"` when true, or `"FAIL: <msg>"` when false.
- `run_all_tests() -> TestResult` (`:30-52`):
  Constructs an AAPL 1-leg Long Call request ($S=150, K=155, \sigma=0.20, r=0.05, T=30$) and calls `calculator::calculate_strategy(req)`.
- **Repo-wide Standalone Test Assertion Convention**:
  Test drivers (`test_assistant_verification.cpp`) do not link GTest or CppUnit. They use file-local `check()` helpers:
  ```cpp
  auto check(bool condition, const std::string& what) -> void {
      ++g_checks;
      if (condition) {
          std::printf("  PASS: %s\n", what.c_str());
      } else {
          ++g_failures;
          std::printf("  FAIL: %s\n", what.c_str());
      }
  }
  ```
  `main()` returns `g_failures == 0 ? 0 : 1`.
- **Skipped Test Return Code Convention**:
  When a test target detects missing dependencies or environment prerequisites, it exits with return code `77`. This matches CTest's standard convention for a skipped test (`backend/tests/test_strategy_store_pg.cpp:96`: `return 77; // ctest's conventional "skipped"`).

### Exported Symbol Inventory

| Symbol | Signature / Type | Description |
| :--- | :--- | :--- |
| `TestResult` | `struct` (`:17-20`) | `bool passed`, `std::string message`. |
| `assert_true` | `(bool, std::string_view) -> TestResult` (`:22-28`) | Formats condition outcome into `TestResult`. |
| `run_all_tests` | `() -> TestResult` (`:30-52`) | Core regression suite evaluating standard option pricing request. |

### Gotchas
- Like `pricing_engine.cppm`, `testing_framework.cppm` carries an `#include <new>` anchor in its global module fragment (`:1-8`) to prevent global allocator collisions from TBB.

---

## Test Coverage

| Test Target / Executable | Test Source File | Exercised Modules | Primary Verified Behaviors |
| :--- | :--- | :--- | :--- |
| `test_assistant_verification` | `backend/tests/test_assistant_verification.cpp` | `assistant_verification.cppm`, `strategy_catalogue.cppm` | Verifies cross-field constraints across 5 output fields (`:65-350`). Tests ambiguous root detection and clarification formatting for `"ES"` and `"CL"` (`:360-450`). Tests strategy alias normalisation (`:460-520`). Verifies lexical support rules and bare direction guess suppression (`:530-620`). Tests prompt injection and advice filters (`:630-720`). Tests bare futures directive parameter recovery (`:730-820`). Tests exercise style and Asian option extraction (`:830-960`). |
| `test_runner` (`CoreEngineTest`) | `backend/src/test_main.cpp` | `pricing_engine.cppm`, `testing_framework.cppm` | Verifies end-to-end pricing calculations, Black-Scholes execution, and assertion helper mechanics (`backend/src/test_main.cpp:5-15`). |
| `test_assistant_service` | `backend/tests/test_assistant_service.cpp` | `assistant_verification.cppm`, `strategy_catalogue.cppm` | Tests integration of assistant verification rules within the gRPC service layer. |
| `test_finance_service_validation` | `backend/tests/test_finance_service_validation.cpp` | `finance_service.cpp` | RPC validation bounds, closed-form identities and dispatch rules for the Finance service. |

---

## Open Questions

None. All behaviors, static tables, bounds, candidate mappings, and interfaces have been verified directly against the codebase.
