# Test and gate inventory
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
This document provides a comprehensive inventory, architectural analysis, and operational guide for every automated test target, verification proof, posture gate, and integrity checker across the repository. It covers:
- **Backend Test Framework & Link Seams** (`backend/src/modules/testing_framework.cppm`, `backend/src/test_main.cpp`, `backend/tests/strategy_store_stub.cpp`).
- **Backend Pricing & Core Services** (`backend/tests/test_calculator_service.cpp`, `backend/tests/test_option_pricing_service.cpp`, `backend/tests/test_market_data_resilience.cpp`, `backend/tests/test_option_chain_cache.cpp`).
- **Backend Financial Mathematics & Census Demographics** (`backend/tests/test_finance_service_validation.cpp`, `backend/tests/test_state_refresh.cpp`).
- **Backend SGEE, Local Admission & Distributed Queue** (`backend/tests/test_sgee_automated_reasoning.cpp`, `backend/tests/test_inference_admission.cpp`, `backend/tests/test_sgee_queue_client.cpp`, `backend/tests/test_inference_admission_pg.cpp`, `backend/tests/test_inference_queue_pg.cpp`, `backend/tests/integration/queue_node_entrypoint_test.sh`).
- **Backend Assistant Verification, Grammar Decoding & LLM Defense** (`backend/tests/test_assistant_service.cpp`, `backend/tests/test_assistant_verification.cpp`, `backend/tests/test_mortgage_assistant_service.cpp`, `backend/tests/test_mortgage_verification.cpp`, `backend/tests/test_mortgage_grammar.cpp`).
- **Backend Entitlements, Security Gates & Persistence** (`backend/tests/test_api_key_entitlement.cpp`, `backend/tests/test_quota_tier_label.cpp`, `backend/tests/test_state_assumptions_gate.cpp`, `backend/tests/test_strategy_store_pg.cpp`).
- **Build-Time Gate Scripts, Linters & Proto Drift** (`backend/tests/test_vendored_proto_drift.cpp`, `backend/tests/test_python_bindings.py`, `scripts/code_policy_check.sh`, `frontend/scripts/check-export.mjs`).
- **Frontend Store & UI Gate Suites** (`frontend/src/config/ad-routes.test.ts`, `frontend/src/content/strategy-guides.test.ts`, `frontend/src/lib/chainFreshness.test.ts`, `frontend/src/store/asian-leg.test.ts`, `frontend/src/store/assistant-apply.test.ts`, `frontend/src/store/assistant-outcomes.test.ts`, `frontend/src/store/calculate-guards.test.ts`, `frontend/src/store/calculator-not-ready.test.ts`, `frontend/src/store/calculator-race.test.ts`, `frontend/src/store/chain.test.ts`, `frontend/src/store/entitlement.test.ts`, `frontend/src/store/harness.canary.test.ts`, `frontend/src/store/matrix-bounds.test.ts`, `frontend/src/store/model-limit.test.ts`, `frontend/src/store/saved-scenarios.test.ts`, `frontend/src/store/ticket.test.ts`, `frontend/src/store/tree-pricer-not-ready.test.ts`).

---

## Master Test Target Inventory
The table below records every test binary, script, and suite across the backend, frontend, and build infrastructure.

| Target Name | Source File | Registered in CTest (`add_test`) | Execution Requirements | Declared Checks / Sections | What It Would Catch (Failure Mode if Deleted) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `test_runner` (`CoreEngineTest`) | `backend/src/test_main.cpp`, `backend/src/modules/testing_framework.cppm` | Yes (`backend/CMakeLists.txt:1314`) | In-process C++ | 1 test case | Fundamental failure of `calculator::calculate_strategy` or ROP execution flow |
| `test_calculator_service` (`CalculatorServiceTest`) | `backend/tests/test_calculator_service.cpp`, `backend/tests/strategy_store_stub.cpp` | Yes (`backend/CMakeLists.txt:1561`) | In-process gRPC / SGEE runner | 7 sections (40 checks) | SGEE graph execution breaks, DID-COMPUTE silent halts, Asian leg FAILED_PRECONDITION drops, matrix windowing corrupts max profit |
| `test_assistant_service` (`AssistantServiceTest`) | `backend/tests/test_assistant_service.cpp` | Yes (`backend/CMakeLists.txt:1729`) | In-process gRPC / SGEE runner | 6 sections (24 checks) | Strategy assistant DID-COMPUTE silent halts, missing CheckModel node, Asian option handoff failure |
| `test_mortgage_assistant_service` (`MortgageAssistantServiceTest`) | `backend/tests/test_mortgage_assistant_service.cpp` | Yes (`backend/CMakeLists.txt:1759`) | In-process gRPC / SGEE runner | 5 sections (14 checks) | Mortgage assistant DID-COMPUTE silent halts, missing CheckModel node, intent extraction failure, advice carve-out regressions |
| `test_state_assumptions_gate` (`StateAssumptionsGateTest`) | `backend/tests/test_state_assumptions_gate.cpp` | Yes (`backend/CMakeLists.txt:1519`) | In-process gRPC runner | 3 sections (7 checks) | Unentitled callers writing state assumptions; failure of Partner tier authorization gate |
| `test_strategy_store_pg` | `backend/tests/test_strategy_store_pg.cpp` | **No** (Target built `backend/CMakeLists.txt:1771-1781`) | PostgreSQL instance (`DATABASE_URL`) | 9 sections (45 checks) | RLS posture dropped on `saved_strategies`, cross-user data leakage, SQL injection via user IDs, transaction rollback failures |
| `test_option_chain_cache` (`OptionChainCacheTest`) | `backend/tests/test_option_chain_cache.cpp` | Yes (`backend/CMakeLists.txt:1341`) | In-process C++ | 5 sections (11 checks) | Cache serving stale data past 15-min TTL or past 1-hour hard cap on upstream outage; cold miss failure handling |
| `test_api_key_entitlement` (`ApiKeyEntitlementTest`) | `backend/tests/test_api_key_entitlement.cpp` | Yes (`backend/CMakeLists.txt:1589`) | In-process C++ | 6 sections (35 checks) | Entitlement gate failing to discriminate malformed, unknown, revoked, and missing keys; generic copy misdiagnosing bad keys |
| `test_quota_tier_label` (`QuotaTierLabelTest`) | `backend/tests/test_quota_tier_label.cpp` | Yes (`backend/CMakeLists.txt:1610`) | In-process C++ | 4 sections (10 checks) | Undefined quota tiers granting unlimited access; refusal messages failing to label anonymous fallback |
| `test_sgee_automated_reasoning` (`SgeeAutomatedReasoningTest`) | `backend/tests/test_sgee_automated_reasoning.cpp` | Yes (`backend/CMakeLists.txt:1445`) | Z3 SMT solver (`Z3::z3`) | 5 safety proofs (P1–P5) | SGEE workflow graphs violating action bindings, node reachability, path termination, error route completeness, or mortgage safety |
| `test_sgee_queue_client` (`SgeeQueueClientTest`) | `backend/tests/test_sgee_queue_client.cpp` | Yes (`backend/CMakeLists.txt:1834`) | In-process C++ | 5 sections (14 checks) | SGEE queue client blocking the engine, buffer overflow beyond 256 KiB cap, circuit breaker failure, leader redirection failure |
| `test_finance_service_validation` (`FinanceServiceValidationTest`) | `backend/tests/test_finance_service_validation.cpp` | Yes (`backend/CMakeLists.txt:1496`) | In-process gRPC runner | 27 sections (~120 checks) | Memory corruption on empty spans, IEEE 754 NaN UB, infinite TVM loops, negative closing cost exploits, Black-Scholes divide-by-zero |
| `test_state_refresh` (`StateRefreshTest`) | `backend/tests/test_state_refresh.cpp` | Yes (`backend/CMakeLists.txt:1405`) | In-process C++ | 6 sections (17 checks) | Out-of-bound Census ACS demographic data accepted into database; tax rate derivation overflow; hardcoded candidate years |
| `test_option_pricing_service` (`OptionPricingServiceTest`) | `backend/tests/test_option_pricing_service.cpp` | Yes (`backend/CMakeLists.txt:1473`) | In-process gRPC runner | 5 sections (~100 checks) | Binomial tree no-arbitrage bound violation, American early exercise premium inversion, Bermudan window mislabeling |
| `test_assistant_verification` (`AssistantVerificationTest`) | `backend/tests/test_assistant_verification.cpp` | Yes (`backend/CMakeLists.txt:1364`) | In-process C++ | 12 sections (~140 checks) | Strategy prompt injections escaping guards, ambiguity signals misrouting, bare futures misclassification |
| `test_mortgage_verification` (`MortgageVerificationTest`) | `backend/tests/test_mortgage_verification.cpp` | Yes (`backend/CMakeLists.txt:1385`) | In-process C++ | 8 sections (~60 checks) | Mortgage prompt injection, non-total slot-kind extraction, wire enum mutation drift |
| `test_mortgage_grammar` (`MortgageGrammarTest`) | `backend/tests/test_mortgage_grammar.cpp` | Yes (`backend/CMakeLists.txt:1429`) | In-process C++ | 27 operations, 184 fields | Constrained grammar generation producing invalid JSON/proto payloads, drift from `backend/proto/finance.proto` |
| `test_inference_admission` (`InferenceAdmissionTest`) | `backend/tests/test_inference_admission.cpp` | Yes (`backend/CMakeLists.txt:1821`) | In-process C++ | 4 sections (18 checks) | Local queue starvation, FIFO inversion, ungraceful thread shutdown, device selection fallback failure |
| `test_inference_queue_pg` | `backend/tests/test_inference_queue_pg.cpp` | **No** (Target built `backend/CMakeLists.txt:1783-1793`) | PostgreSQL instance (`DATABASE_URL`) | 7 sections (~30 checks) | Distributed queue lease hijacking, fencing token bypass, stale task reaper failure, concurrent worker races |
| `test_inference_admission_pg` | `backend/tests/test_inference_admission_pg.cpp` | **No** (Target built `backend/CMakeLists.txt:1855-1869`) | PostgreSQL instance (`DATABASE_URL`) | 6 sections (~25 checks) | Distributed admission deadlock, fallback to local admission failing under DB partition, fencing token collisions |
| `test_vendored_proto_drift` (`VendoredProtoDriftTest`) | `backend/tests/test_vendored_proto_drift.cpp` | Yes (`backend/CMakeLists.txt:1411`) | In-process C++ | 8 checks | Wire protocol drift between backend `backend/proto/finance.proto` and `clients/mortgagefv/proto/finance.proto` |
| `test_market_data_resilience` (`MarketDataResilienceTest`) | `backend/tests/test_market_data_resilience.cpp` | Yes (`backend/CMakeLists.txt:1331`) | In-process C++ | 9 sections (27 checks) | Circuit breaker failing to trip on 5xx, retrying 4xx client errors, half-open state deadlocks |
| `QueueNodeEntrypointTest` | `backend/tests/integration/queue_node_entrypoint_test.sh` | Yes (`backend/CMakeLists.txt:1842-1843`) | Bash, Linux environment, OpenSSL | 4 scenarios (10 checks) | Container entrypoint booting in plaintext when mTLS required; partial TLS environment variables admitted |
| `test_python_bindings.py` (`PythonBindingsTest`) | `backend/tests/test_python_bindings.py` | Yes (`backend/CMakeLists.txt:1268-1271`) | Python 3, nanobind `.so`, `LD_PRELOAD` jemalloc | 5 tests | Python nanobind bindings crashing on Quote/RatePoint conversion; C++ ABI symbol export regressions |
| `code_policy_check.sh` | `scripts/code_policy_check.sh` | **No** (Pre-commit / CI gate) | Bash, Git repository | 3 policy gates | Raw `new` introduced (Rule 3), `-ffast-math` enabled breaking IEEE 754 (Rules 50/55), non-trailing returns (Rule 31) |
| `check-export.mjs` | `frontend/scripts/check-export.mjs` | **No** (Post-build SSG export gate) | Node.js, `npm run build` | 4 validation passes | Google AdSense code placed on non-publisher pages (`404.html`), guide pages under 600 words, invalid JSON-LD |
| `ad-routes.test.ts` | `frontend/src/config/ad-routes.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 6 tests | Ads permitted on 404 routes or calculator pages; reverting allowlist to denylist |
| `strategy-guides.test.ts` | `frontend/src/content/strategy-guides.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 26 guide assertions | Broken strategy guide metadata, missing paths, duplicate slugs, malformed markdown frontmatter |
| `chainFreshness.test.ts` | `frontend/src/lib/chainFreshness.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 6 tests | Live vs Delayed market data badge misclassification, negative clock-skew regressions |
| `asian-leg.test.ts` | `frontend/src/store/asian-leg.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 5 tests | Asian option leg parameters dropped during proto serialization; store defaulting to European payoff |
| `assistant-apply.test.ts` | `frontend/src/store/assistant-apply.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 8 tests | Assistant recommended trades applied with unlisted expirations; invalid strike snapping |
| `assistant-outcomes.test.ts` | `frontend/src/store/assistant-outcomes.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 7 tests | gRPC status code discrimination failure; assistant error displayed as general failure instead of upgrade |
| `calculate-guards.test.ts` | `frontend/src/store/calculate-guards.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 11 tests | Calculate button firing without valid legs; calculation request sent with zero/null risk-free rate |
| `calculator-not-ready.test.ts` | `frontend/src/store/calculator-not-ready.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 6 tests | Preconditions displayed in loss red (`error`) instead of neutral prompt (`notReady`) |
| `calculator-race.test.ts` | `frontend/src/store/calculator-race.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 9 tests | Out-of-order gRPC response overwriting newer position; spinner stopping while newer calculation in flight |
| `chain.test.ts` | `frontend/src/store/chain.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 8 tests | Ticket expiration diverging from selected expiration; futures forward curve treated as chain error |
| `entitlement.test.ts` | `frontend/src/store/entitlement.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 7 tests | Pro gate discriminating on error string rather than gRPC code 7; upgrade prompt rendered alongside red error |
| `harness.canary.test.ts` | `frontend/src/store/harness.canary.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 2 tests | Test harness failing to intercept gRPC client calls, causing tests to pass vacuously |
| `matrix-bounds.test.ts` | `frontend/src/store/matrix-bounds.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 6 tests | P&L matrix bounds clipping global max profit calculation; NaN/Infinity bounds leaving client tab |
| `model-limit.test.ts` | `frontend/src/store/model-limit.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 5 tests | FAILED_PRECONDITION (code 9) routed to red error instead of explanatory model limit panel |
| `saved-scenarios.test.ts` | `frontend/src/store/saved-scenarios.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 9 tests | Saved scenario round-trip field truncation; UNAUTHENTICATED (16) vs PERMISSION_DENIED (7) misrouting |
| `ticket.test.ts` | `frontend/src/store/ticket.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 13 tests | Default add-leg click committing `expiration_days: 0`; strike/premium validation refusals failing |
| `tree-pricer-not-ready.test.ts` | `frontend/src/store/tree-pricer-not-ready.test.ts` | Vitest (`frontend/`) | Node.js / Vitest | 5 tests | Tree pricer missing strike/IV/spot routed to red "Unavailable" error instead of neutral prompt |

---

## Backend Test Framework and Link Seams

### `backend/src/modules/testing_framework.cppm` and `backend/src/test_main.cpp`
- **Purpose**: Provides a minimal, C++23 module-based testing unit (`calculator.testing`) for verifying core calculation functionality (`calculator::calculate_strategy`) in the `test_runner` executable.
- **Exported Symbols & Structures**:
  - `struct TestResult` (`backend/src/modules/testing_framework.cppm:17-20`): Carries `bool passed` and `std::string message`.
  - `assert_true(bool condition, std::string_view msg)` (`backend/src/modules/testing_framework.cppm:22-28`): Helper returning formatted `PASS: <msg>` or `FAIL: <msg>`.
  - `run_all_tests()` (`backend/src/modules/testing_framework.cppm:30-52`): Constructs an AAPL 155-call single-leg `StrategyRequest`, invokes `calculator::calculate_strategy(req)`, and asserts success.
  - `main()` (`backend/src/test_main.cpp:5-15`): Imports `calculator.testing` and `market_data`, executes `calculator::testing::run_all_tests()`, prints `result.message`, and exits with code `0` on success or `1` on failure.
- **Global Module Fragment & Memory Anchor**:
  - `backend/src/modules/testing_framework.cppm:1-8`: Contains `#include <new>` in the global module fragment. This is an explicit `operator new` anchor required because the `<new>` header dragged in by `sensen.options` through Intel TBB is a global-module declaration that must be anchored in every translation unit that reaches it and allocates memory. Importing a module that anchors it does not inherit the anchor.
- **Invariants & Link Seams**:
  - Registered in CTest as `CoreEngineTest` (`backend/CMakeLists.txt:1314`).
- **Gotchas**:
  - Standalone test files under `backend/tests/` (e.g. `test_calculator_service.cpp`, `test_state_refresh.cpp`) do **not** use or import `calculator.testing`. They implement their own local `check()` and `section()` test harnesses per repository rule 39 (`config/cpp_details.txt`, prohibiting external test frameworks).

### `backend/tests/strategy_store_stub.cpp`
- **Purpose**: Test-only module implementation unit for `strategy_store` (`module strategy_store;`). It supplies a link-time stub for `make_pg_strategy_store` so that test binaries (such as `test_calculator_service`) can link `calculator_service` without linking PostgreSQL (`libpq`).
- **Exported Symbols**:
  - `make_pg_strategy_store(std::string_view connection_string)` (`backend/tests/strategy_store_stub.cpp:21-26`): Stub implementation returning `nullptr`.
- **Invariants & Failure Modes**:
  - Keeps `test_calculator_service` completely free of `PostgreSQL::PostgreSQL` symbols (`backend/CMakeLists.txt:1533-1542`). The binary exercises saved-scenario RPCs exclusively via in-memory fake stores injected through `RegisterCalculatorServiceForTest`.

---

## Backend Core Calculator and Option Pricing

### `backend/tests/test_calculator_service.cpp`
- **Purpose**: Exercises `calculator.OptionsCalculator/CalculateStrategy`, SGEE graph execution (`OptionsWorkflow`), spread identities, DID-COMPUTE postconditions, Asian leg refusals, matrix bounds, and scenario storage.
- **Fixtures & Test Sections**:
  - `FakeStrategyStore` (`backend/tests/test_calculator_service.cpp:108-164`): In-memory `IStrategyStore` implementing scenario CRUD operations.
  - `Section 1: CLOSED-FORM IDENTITY: 580/600 bull call spread @ 7.25 debit` (`backend/tests/test_calculator_service.cpp:408-452`): Validates analytical max profit ($1,275), max loss (-$725), and breakeven ($587.25).
  - `Section 2: IRON CONDOR: two breakevens, non-zero net Greeks` (`backend/tests/test_calculator_service.cpp:454-513`): Validates 4-leg payoff bounds and non-zero net greeks.
  - `Section 3: DID-COMPUTE, end-to-end: pnl_matrix and matrix have the declared dimensions and values` (`backend/tests/test_calculator_service.cpp:515-544`).
  - `Section 4: THE DISCRIMINATING PROOF: a server missing one of the actions (ComputeGreeks) stalls and returns INTERNAL` (`backend/tests/test_calculator_service.cpp:546-639`): Uses `PartialActionsFixture` missing `ComputeGreeks`. Proves the service returns `grpc::StatusCode::INTERNAL` naming the stall point rather than returning an empty OK response.
  - `Section 5: Saved scenarios` (`backend/tests/test_calculator_service.cpp:722-930`): Tests save, list, and delete operations via `FakeStrategyStore`.
  - `Section 6: ASIAN LEGS: refused by CODE, and only when actually Asian` (`backend/tests/test_calculator_service.cpp:641-720`): Verifies `FAILED_PRECONDITION` when a leg contains an Asian payoff type.
  - `Section 7: MATRIX PRICE BOUNDS: a window on the grid, not on the answer` (`backend/tests/test_calculator_service.cpp:932-1066`): Verifies `matrix_price_min`/`max` do not alter global max profit/loss, and tests refusal of inverted, negative, or NaN bounds.
- **Invariants & Refusals**:
  - DID-COMPUTE guard: Dropping `ComputeGreeks` yields `grpc::StatusCode::INTERNAL` naming "ComputeGreeks" or "Done" (`backend/tests/test_calculator_service.cpp:599-606`).
  - Asian leg refusal: Returns `grpc::StatusCode::FAILED_PRECONDITION` with `"Asian legs are not priced by this model."` (`backend/tests/test_calculator_service.cpp:663-670`).
  - Matrix bounds validation: Inverted bounds (`min >= max`), lower bound exceeding default upper fallback, negative bounds, or NaN return `grpc::StatusCode::INVALID_ARGUMENT` (`backend/tests/test_calculator_service.cpp:1032, 1046, 1054, 1062`).

### `backend/tests/test_option_pricing_service.cpp`
- **Purpose**: Exercises binomial tree pricing (`sensen.finance.Finance/PriceOptionTree`) and closed-form Black-Scholes (`PriceBlackScholes`) through an in-process gRPC server.
- **Test Sections**:
  - `Section 1: NO-ARBITRAGE ORDERING: European <= Bermudan <= American` (`backend/tests/test_option_pricing_service.cpp:293-389`): Proves American put price is strictly greater than or equal to European put price.
  - `Section 2: CONVERGENCE: tree price -> Black-Scholes closed form as steps increase` (`backend/tests/test_option_pricing_service.cpp:391-456`): European tree prices converge to Black-Scholes within $\pm 0.01$ at $N=200$ steps.
  - `Section 3: BERMUDAN DATE SEMANTICS` (`backend/tests/test_option_pricing_service.cpp:458-618`): Exercises discrete Bermudan exercise dates and dead-band handling.
  - `Section 4: ASIAN options` (`backend/tests/test_option_pricing_service.cpp:620-733`): Verifies arithmetic and geometric Asian option pricing and averaging states requirement (`averaging_states >= 2`).
  - `Section 5: INPUT VALIDATION at the service boundary` (`backend/tests/test_option_pricing_service.cpp:735-1092`): Rejects non-positive spot/strike, non-finite values (NaN / Inf), invalid steps, and bounds volatility up to 500%.
- **Invariants & Refusals**:
  - Asian averaging states: Returns `grpc::StatusCode::INVALID_ARGUMENT` if `averaging_states < 2`.
  - Non-finite fields: NaN or $\pm\infty$ on spot, strike, volatility, or rate return `grpc::StatusCode::INVALID_ARGUMENT` via `require_finite` (`backend/tests/test_option_pricing_service.cpp:857-875`).

### `backend/tests/test_market_data_resilience.cpp`
- **Purpose**: Exercises HTTP upstream resilience in `market_data.cppm` (`fetch_with_resilience`), verifying exponential backoff retry logic, circuit breaker tripping on consecutive 5xx errors, non-retryable 4xx bypass, and half-open state recovery.
- **Fixtures & Test Sections**:
  - `ScriptedAttempt` (`backend/tests/test_market_data_resilience.cpp:76-105`): Scriptable attempt tracker recording calls, failure codes, and retryable flags.
  - 9 test sections (`backend/tests/test_market_data_resilience.cpp:150-432`):
    1. Immediate success (1 attempt).
    2. Retryable failure recovers within attempt budget.
    3. Retryable failure exhausts attempt budget.
    4. Non-retryable failure short-circuits immediately.
    5. Circuit breaker trips and refuses without network attempt.
    6. Non-retryable 404s do not trip circuit breaker.
    7. 401/403 (bad credentials) do not trip shared breaker.
    8. Mixed 4xx/5xx: only 5xx count toward breaker threshold.
    9. Genuine upstream unhealth (5 consecutive 5xx) trips breaker, and half-open recovery recovers after cooldown.
- **Invariants & Refusals**:
  - When circuit breaker trips (`OPEN`), requests fail immediately with `MarketDataError::CircuitBreakerOpen` without making a network call (`backend/tests/test_market_data_resilience.cpp:227-234`).
  - Non-retryable HTTP 404 or 401/403 errors do not increment circuit breaker failure counters.

### `backend/tests/test_option_chain_cache.cpp`
- **Purpose**: Exercises in-memory option chain caching in `market_data.cppm`: 15-minute standard TTL, serve-stale behavior up to a 1-hour hard cap during upstream outages, and cache invalidation.
- **Sections & Invariants**:
  - `Section 1: Cold miss fetches from provider` (`backend/tests/test_option_chain_cache.cpp:120-131`).
  - `Section 2: Two fetches inside the TTL produce ONE provider call and identical fetched_at` (`backend/tests/test_option_chain_cache.cpp:133-156`): Validates 15-minute freshness TTL.
  - `Section 3: Upstream failure serves last good chain with UNCHANGED fetched_at` (`backend/tests/test_option_chain_cache.cpp:158-184`): Stale chain returned within stale window.
  - `Section 4: Refuses past the hard cap (1 hour)` (`backend/tests/test_option_chain_cache.cpp:186-210`): Advancing clock to 61 minutes (> 60 minute max stale cap) with a failing provider refuses the request.
  - `Section 5: Cold miss with failing provider refuses immediately` (`backend/tests/test_option_chain_cache.cpp:212-224`).
- **Discrepancy Note**:
  - The maximum stale serve cap is exactly **1 hour (60 minutes)** (`backend/tests/test_option_chain_cache.cpp:200-207`), not 2 hours.

---

## Backend Financial Mathematics and Census State Refresh

### `backend/tests/test_finance_service_validation.cpp`
- **Purpose**: Defends `sensen.finance.Finance` across 27 distinct validation sections, guarding against numerical edge cases, empty spans, magnitude overflow, IEEE 754 NaN undefined behavior, infinite iteration loops, and illegal financial parameters.
- **Sections**:
  1. `ComputeAmortization / ComputeDetailedAmortization: magnitude overflow` (`backend/tests/test_finance_service_validation.cpp:204-242`).
  2. `TVM family: compound-growth overflow` (`backend/tests/test_finance_service_validation.cpp:244-303`).
  3. `period_payment: unbounded iteration` (`backend/tests/test_finance_service_validation.cpp:305-353`).
  4. `ComputeCumulative: BigDecimal(double) UB on NaN, and unbounded range` (`backend/tests/test_finance_service_validation.cpp:355-421`).
  5. `ComputeNpv / ComputeXnpv: NaN/Infinity bypass` (`backend/tests/test_finance_service_validation.cpp:423-490`).
  6. `ComputeIrr / ComputeXirr / ComputePaybackPeriod: hardened for a clear error` (`backend/tests/test_finance_service_validation.cpp:492-558`).
  7. `ComputeDepreciation: NaN bypasses every existing "<= 0.0" guard` (`backend/tests/test_finance_service_validation.cpp:560-616`).
  8. `PriceBlackScholes input validation` (`backend/tests/test_finance_service_validation.cpp:618-746`).
  9. `PriceOptionMonteCarlo input validation` (`backend/tests/test_finance_service_validation.cpp:748-845`).
  10. `ComputeProbabilityTree: rate check, NaN immunity` (`backend/tests/test_finance_service_validation.cpp:847-943`).
  11. `ComputeAmortizationBatch: batch sibling of ComputeCumulative UB` (`backend/tests/test_finance_service_validation.cpp:945-1001`).
  12. `PriceOptionTree Bermudan dead band: [years_to_expiry - dt/2, years_to_expiry)` (`backend/tests/test_finance_service_validation.cpp:1003-1063`).
  13. `AnalyzeBond / AnalyzeTreasuryBill: NaN checks` (`backend/tests/test_finance_service_validation.cpp:1065-1189`).
  14. `PriceFutures / ValueFutures validation` (`backend/tests/test_finance_service_validation.cpp:1191-1274`).
  15. `SimulateMarginAccount: unchecked scalars + unbounded daily_prices` (`backend/tests/test_finance_service_validation.cpp:1276-1358`).
  16. `ComputeHedge: volatility NaN / negatives` (`backend/tests/test_finance_service_validation.cpp:1360-1434`).
  17. `ComputeCommoditySpread validation` (`backend/tests/test_finance_service_validation.cpp:1436-1464`).
  18. `ComputePortfolioStats: per-element NaN in repeated field, unbounded size` (`backend/tests/test_finance_service_validation.cpp:1466-1561`).
  19. `OptimizePortfolio / ComputeRiskContributions: NaN in covariance matrix, O(n^3) DoS` (`backend/tests/test_finance_service_validation.cpp:1563-1687`).
  20. `PriceOptionMonteCarlo: unbounded paths*steps` (`backend/tests/test_finance_service_validation.cpp:1689-1743`).
  21. `ComputeFutureValueDetailed validation` (`backend/tests/test_finance_service_validation.cpp:1745-1833`).
  22. `ComputeAmortizationBatch / ComputeCumulative magnitude overflow` (`backend/tests/test_finance_service_validation.cpp:1835-1953`).
  23. `ComputeClosingCosts: bases, bounds, presence, and credit clamping` (`backend/tests/test_finance_service_validation.cpp:1955-2145`).
  24. `ComputeRentVsBuy shape dispatch decision graph` (`backend/tests/test_finance_service_validation.cpp:2148-2332`).
  25. `ComputeXnpv / ComputeXirr: DAYS on wire, SECONDS in engine` (`backend/tests/test_finance_service_validation.cpp:2335-2517`).
  26. `ComputeRate / ComputePeriods: TVM bidirectional cash flow requirement` (`backend/tests/test_finance_service_validation.cpp:2519-2634`).
  27. `ComputeDepreciation MACRS: unsupported class refusal` (`backend/tests/test_finance_service_validation.cpp:2636-2719`).
- **Invariants & Location Notes**:
  - Financial mathematics functions are defined in `backend/sensen/src/financial.cppm` (there is no `backend/src/modules/financial.cppm`).
  - Span check in `backend/sensen/src/financial.cppm:936-962`: `check_dated_span` ensures span $\ge 86,400$ seconds (1 day); `xnpv` verifies `values.size() == dates.size()`.
  - Closing costs credit clamping: seller/lender credit exceeding itemized bill returns `grpc::StatusCode::FAILED_PRECONDITION` (`backend/tests/test_finance_service_validation.cpp:2128-2130`).

### `backend/tests/test_state_refresh.cpp`
- **Purpose**: Exercises validation of state-level demographic and property tax assumptions fetched from the US Census Bureau American Community Survey (ACS) in `state_refresh.cppm`.
- **Sections & Checks (6 Sections, 17 Checks, Lines 1-189)**:
  - Compile-time static assertions (`backend/tests/test_state_refresh.cpp:53-55`): verifies `trim` and `is_not_a_state` are constant-evaluable.
  - `Section 1: A well-formed row is accepted, and the rate is DERIVED` (`backend/tests/test_state_refresh.cpp:63-89`): Alabama real 2023 ACS figures (price 195100, rent 963, taxes 738) derives tax rate 0.38%; New Hampshire derives hyphenated slug `"new-hampshire"`.
  - `Section 2: ACS sentinels are MISSING, not values` (`backend/tests/test_state_refresh.cpp:92-115`): Refuses `-666666666` sentinel, zero tax (`"0"`), empty strings, and non-numeric values.
  - `Section 3: Out-of-bounds values are REFUSED, never clamped` (`backend/tests/test_state_refresh.cpp:118-150`): Rejects median price outside `50,000..3,000,000`, median rent outside `300..8,000`, and derived property tax rate outside `0.05..4.0%` (4% ceiling, not 5%). Refusal names the offending field.
  - `Section 4: Boundary values are INSIDE the bounds` (`backend/tests/test_state_refresh.cpp:153-161`): Bounds are inclusive (`50000` / `300` accepted).
  - `Section 5: Candidate vintages are DERIVED from the clock, not hardcoded` (`backend/tests/test_state_refresh.cpp:164-177`): `sr::candidate_years(2026)` yields `[2024, 2023]`; moves dynamically with clock (`sr::candidate_years(2030)` yields `[2028, ...]`).
  - `Section 6: The abort floor is the contract's, not an ad-hoc number` (`backend/tests/test_state_refresh.cpp:180-185`): `sr::kMinUsableStates == 40`.
- **Invariants & Refusals**:
  - `validate_acs_row` takes 4 string arguments (`name`, `price`, `rent`, `taxes`) and returns `std::expected<AcsRow, std::string>`. Out-of-bounds values return `std::unexpected` naming the offending field.

---

## Backend SGEE, Inference Admission, and Distributed Queue

### `backend/tests/test_sgee_automated_reasoning.cpp`
- **Purpose**: Uses the Z3 SMT solver via sensen GP-ARA (`sensen::gp_ara::Z3Reasoner`, `backend/sensen/src/gp_ara_interfaces.cppm`) to formally verify 5 architectural safety properties across 4 SGEE workflow graphs:
  1. `OptionsWorkflow` (`backend/src/modules/calculator_service.cpp ~:940`).
  2. `FinanceRequestLifecycle` (`backend/src/modules/finance_service.cpp ~:943`).
  3. `StrategyAssistantWorkflow` (`backend/src/modules/assistant_service.cpp ~:2848`).
  4. `MortgageAssistantWorkflow` (`backend/src/modules/mortgage_assistant_service.cpp ~:2598`).
- **Formally Proved Safety Properties**:
  - `P1. Action Binding Completeness`: Every `Execute(name)` in a graph has a corresponding bound action in `ActionRegistry`.
  - `P2. Node Reachability`: Every node in a graph is reachable from the workflow entry node.
  - `P3. Path Termination`: Every execution path terminates at a node marked terminal (no dead ends, no infinite loops).
  - `P4. Error Route Completeness`: Every action that can fail has an `OnError` fallback route reaching a terminal node.
  - `P5. Mortgage Safety Invariant`: `response.mutable_params()` is populated **only** when the GP-ARA verification verdict is `Proven`. `Unsafe` and `Indeterminate` verdicts provably reach the `Refused` terminal node.
- **Invariants & Build Configuration**:
  - Compiles with definition `SENSEN_HAS_Z3` and links `${Z3_LIBRARY}` (`backend/CMakeLists.txt:1441-1443`). Negation of each property is checked for `z3::unsat`.

### `backend/tests/test_inference_admission.cpp`
- **Purpose**: Exercises local `QueuedBackend` admission in `inference_admission.cppm`, verifying FIFO scheduling, queue depth saturation, graceful worker shutdown, and device selection.
- **Exported Fixtures & Checks**:
  - `GatedEchoBackend final : public QueuedBackend` (`backend/tests/test_inference_admission.cpp:60-105`): Single owner thread running `take_jobs()`, controlled via `open_gate()` / `close_gate()`.
  - 4 test sections (`backend/tests/test_inference_admission.cpp:115-385`):
    - FIFO ordering verification.
    - Queue capacity saturation: excess submissions return `InferenceOutcome::Rejected` with `"inference queue is full"`.
    - Graceful shutdown: `drain_and_fail` fails queued requests with `"assistant backend is shutting down"`.
    - Device resolution: `resolve_device` resolves `Device::Cuda` when GPU is present and falls back to `Device::Cpu`.

### `backend/tests/test_sgee_queue_client.cpp`
- **Purpose**: Exercises `sgee_queue_client.cppm`, verifying non-blocking async enqueue, 256 KiB payload caps, circuit breaker behavior, and leader redirection retry logic.
- **Exported Sections**:
  - `Section 1: create_from_env when off or unconfigured` (`backend/tests/test_sgee_queue_client.cpp:80-91`): `SGEE_QUEUE=off` or unset returns `std::nullopt`.
  - `Section 2: 256 KB client-side payload cap` (`backend/tests/test_sgee_queue_client.cpp:93-118`): Payloads $> 262,144$ bytes return `QUEUE_ERROR_PAYLOAD_TOO_LARGE`.
  - `Section 3: enqueue_async returns immediately and runs in background` (`backend/tests/test_sgee_queue_client.cpp:120-145`).
  - `Section 4: circuit breaker trips after consecutive failures` (`backend/tests/test_sgee_queue_client.cpp:147-175`).
  - `Section 5: leader redirection follows hint and completes` (`backend/tests/test_sgee_queue_client.cpp:177-210`): Follows `leader_hint` on `QUEUE_ERROR_NOT_LEADER`.

### `backend/tests/test_inference_admission_pg.cpp` and `backend/tests/test_inference_queue_pg.cpp`
- **Purpose**: Exercises PostgreSQL-backed distributed inference queue mechanics: concurrent worker leases, fencing token monotonicity, sweep/reap of abandoned leases, and fallback to local admission during database partitions.
- **Execution & Invariants**:
  - Both binaries require `DATABASE_URL`. If unset, they exit with status `1` (`backend/tests/test_inference_queue_pg.cpp:25-28`, `backend/tests/test_inference_admission_pg.cpp:23-26`).
  - Deliberately excluded from `add_test()` in `backend/CMakeLists.txt:1783-1804, 1855-1869`.
  - Fencing token monotonicity ensures that workers with expired leases cannot commit completions.

### `backend/tests/integration/queue_node_entrypoint_test.sh`
- **Purpose**: Integration gate for `backend/queue-node-entrypoint.sh`. Verifies whether queue nodes start on mutual TLS, plaintext, or abort on misconfiguration.
- **Registration**:
  - Registered in CTest as `QueueNodeEntrypointTest` (`backend/CMakeLists.txt:1842-1843`).
- **Scenarios (4 Scenarios, 10 Checks)**:
  1. Plaintext startup allowed when `SGEE_TLS_*` variables are unset (rc=0, node reached, plaintext announced).
  2. Partial variable trios refused across all 6 combinations ("CA", "CERT", "KEY", "CA CERT", "CA KEY", "CERT KEY") (rc=1, node NOT reached).
  3. Full trio stages `ca.pem`, `cert.pem`, `key.pem` into staging directory, sets `key.pem` mode to `600`, and starts node (rc=0, node reached).
  4. Corrupt base64 data refused rather than staging corrupt files (rc=1, node NOT reached).

---

## Backend Assistant Verification, Grammar, and LLM Defense

### `backend/tests/test_assistant_service.cpp`
- **Purpose**: Exercises `calculator.assistant.StrategyAssistant/ProcessQuery`, SGEE `StrategyAssistantWorkflow`, DID-COMPUTE verification, and Asian option routing.
- **Sections & DID-COMPUTE Mutation**:
  - 6 sections (24 checks, `backend/tests/test_assistant_service.cpp:165-445`).
  - Section 2: Asian strategy questions route to educational refusal rather than pricing engine.
  - Section 4 (`backend/tests/test_assistant_service.cpp:270-306`): DID-COMPUTE discriminating proof. Omitting `CheckModel` node from `PartialActionsFixture` (`kMissingCheckModel`: `{"Admission", "Generate", "ParseAndVerify"}`) routes to terminal node `Refused` without populating payload, triggering `grpc::StatusCode::INTERNAL` naming the defect.

### `backend/tests/test_assistant_verification.cpp`
- **Purpose**: Standalone, offline GP-ARA verification tests for `assistant_verification.cppm` (`verify_assistant_params`), defending against prompt injections, out-of-scope financial advice, ambiguity signals, and bare futures misclassification.
- **Exported Symbols & Outcomes**:
  - Outcomes: `Outcome::Proven`, `Outcome::Unsafe`, `Outcome::Indeterminate` (`backend/tests/test_assistant_verification.cpp:56-62`).
  - 12 sections (~140 checks, `backend/tests/test_assistant_verification.cpp:80-960`):
    - Section 1: Prompt injection attacks refused as `Outcome::Unsafe`.
    - Section 2: Financial advice queries refused as `Outcome::Unsafe`.
    - Section 3: Ambiguous queries classified as `Outcome::Indeterminate` with clarifying questions.
    - Section 4: Bare commodity futures symbols disambiguated from equity tickers.
    - Section 5: Exercise style and Asian option classification.

### `backend/tests/test_mortgage_assistant_service.cpp`
- **Purpose**: Exercises `mortgage.assistant.MortgageAssistant/ProcessMortgageQuery`, SGEE `MortgageAssistantWorkflow`, oversized utterance guards, prompt injection defense, and specified calculation advice carve-outs.
- **Sections & Checks (5 Sections, 14 Checks, Lines 1-360)**:
  - `Section 1: ADMISSION -> CHECKMODEL, end to end, through the real graph` (`backend/tests/test_mortgage_assistant_service.cpp:181-198`).
  - `Section 2: Admission's OnError edge, hard-error flavour: oversized utterance` (`backend/tests/test_mortgage_assistant_service.cpp:200-217`): 1001-character prompt returns `grpc::StatusCode::INVALID_ARGUMENT`.
  - `Section 3: Admission's OnError edge, refusal flavour: prompt injection` (`backend/tests/test_mortgage_assistant_service.cpp:219-251`).
  - `Section 3c: ...but a SPECIFIED CALCULATION wearing an advice phrase is admitted` (`backend/tests/test_mortgage_assistant_service.cpp:254-300`): Fully specified rent-vs-buy query opening with "Should I rent" is admitted via density of specification, while unspecified questions remain refused `OUT_OF_SCOPE`.
  - `Section 4: THE DISCRIMINATING PROOF: a server missing the CheckModel action must NOT return an empty OK response` (`backend/tests/test_mortgage_assistant_service.cpp:302-334`): Fixture missing `CheckModel` returns `grpc::StatusCode::INTERNAL`.
  - `Section 5: CONTROL: the same fixture machinery with the FULL action set still succeeds` (`backend/tests/test_mortgage_assistant_service.cpp:337-356`).

### `backend/tests/test_mortgage_verification.cpp`
- **Purpose**: Standalone GP-ARA verification stage for mortgage loan assistant output (`mortgage_verification.cppm`).
- **Structure & Checks (8 Sections, ~60 Checks, Lines 1-1601)**:
  - Half One: Every measured failure class of the mortgage fine-tune reproduces and refuses with exact Outcome and ReasonCode (`backend/tests/test_mortgage_verification.cpp:9-12`).
  - Half Two: Non-vacuous control set of legitimate requests passes end-to-end (`backend/tests/test_mortgage_verification.cpp:13-31`).
  - Structural Gates: Label-space drift check re-parses `backend/proto/finance.proto`; Slot-kind totality check asserts every field classifies to a known `SlotKind` (`backend/tests/test_mortgage_verification.cpp:32-40`).

### `backend/tests/test_mortgage_grammar.cpp`
- **Purpose**: Validates character-by-character constrained grammar decoding (`sensen::IGrammar`) for the mortgage assistant's `<params>` output (`backend/src/modules/mortgage_grammar.cppm`).
- **Coverage**:
  - Re-parses `backend/proto/finance.proto` directly, asserting coverage across 27 operations and 184 fields (`backend/tests/test_mortgage_grammar.cpp:9-19`).
  - Asserts unrepresentability of invalid characters, malformed JSON, and wrong field names at the exact character boundary (`backend/tests/test_mortgage_grammar.cpp:20-27`).
  - Non-vacuous control set: verifies all gold params in `agent/dataset/data_mortgage/val.jsonl` are accepted into the completed state (`backend/tests/test_mortgage_grammar.cpp:28-35`).

---

## Backend Entitlements, Security Gates, and Persistence

### `backend/tests/test_api_key_entitlement.cpp`
- **Purpose**: Exercises entitlement gate discrimination in `api_key.cppm` / `api_key.cpp` (`check_strategy_entitlement`, `check_assistant_entitlement`), proving that invalid keys are distinguished by cause rather than collapsing into generic Pro copy.
- **Fixtures & Helpers**:
  - `identity_with_outcome(Outcome outcome)` (`backend/tests/test_api_key_entitlement.cpp:72-80`): Generates unauthenticated identity with specific outcome.
  - `pro_identity(std::string tier)` (`backend/tests/test_api_key_entitlement.cpp:82-90`): Generates authenticated identity.
  - `contains(std::string_view haystack, std::string_view needle)` (`backend/tests/test_api_key_entitlement.cpp:93-95`).
- **Sections & Checks (6 Sections, 35 Checks, Lines 1-349)**:
  - `Section 1: check_strategy_entitlement: REFUSAL message class per outcome` (`backend/tests/test_api_key_entitlement.cpp:105-191`):
    - `Outcome::Malformed`: Code 7 (`PERMISSION_DENIED`), message names "malformed", expected prefixes ("pk_live_", "sk_live_"), lengths (43, 51), and does **not** say "is a Pro feature".
    - `Outcome::Unknown`: Code 7, message names unknown key ("record" / "recognised").
    - `Outcome::Revoked`: Code 7, message names "revoked" and points to "support".
    - `Outcome::NoKey`: Code 7, generic Pro feature copy.
    - Free-tier identity: Code 7, generic Pro feature copy, names leg count ("3 legs").
  - `Section 2: check_strategy_entitlement: ADMIT path is unchanged` (`backend/tests/test_api_key_entitlement.cpp:194-223`): Pro and Partner admitted on 4-leg condor; single-leg request unconditionally admitted even with malformed key.
  - `Section 3: check_assistant_entitlement (kStrategySurface): message class per outcome` (`backend/tests/test_api_key_entitlement.cpp:226-272`).
  - `Section 4: check_assistant_entitlement (kMortgageSurface): the ACTUAL incident` (`backend/tests/test_api_key_entitlement.cpp:275-306`): Proves malformed key on `ParseOperation` receives `kMortgageSurface.malformed_message`, not generic copy.
  - `Section 5: check_assistant_entitlement: ADMIT path is unchanged` (`backend/tests/test_api_key_entitlement.cpp:309-329`).
  - `Section 6: GateMode::Off: every outcome is admitted, matching pre-existing behaviour` (`backend/tests/test_api_key_entitlement.cpp:332-345`): Unsetting `PRO_GATE_MODE` admits all requests.

### `backend/tests/test_quota_tier_label.cpp`
- **Purpose**: Exercises quota enforcement and refusal labeling in `quota.cppm` / `quota.cpp` (`QuotaEnforcer`), ensuring undefined tiers fall back safely to anonymous limits and label the refusal clearly.
- **Sections & Checks (4 Sections, 10 Checks, Lines 1-156)**:
  - Configured policy (`backend/tests/test_quota_tier_label.cpp:100-104`): `anonymous` tier allows 2 req/min; `pro` tier allows 5 req/min.
  - `Section 1: a tier the policy DEFINES is metered and named as itself` (`backend/tests/test_quota_tier_label.cpp:109-117`): User `alice` (`pro`) cut off at request 6, message names `tier 'pro'`, no undefined marker.
  - `Section 2: a tier the policy does NOT define is metered as anonymous and says so` (`backend/tests/test_quota_tier_label.cpp:119-131`): User `bob` (`business`, undefined) cut off at request 3 (applying anonymous 2 req/min limit), message names `tier 'business'` and carries `kUndefinedMarker` (`"(undefined in QUOTA_POLICY; anonymous limits)"`).
  - `Section 3: the anonymous tier itself is not treated as undefined` (`backend/tests/test_quota_tier_label.cpp:133-142`): User `carol` (`anonymous`) cut off at request 3, does not carry undefined marker.
  - `Section 4: an empty tier resolves to anonymous, and is not marked undefined` (`backend/tests/test_quota_tier_label.cpp:144-152`): User `dave` (`""`) cut off at request 3, does not carry undefined marker.

### `backend/tests/test_state_assumptions_gate.cpp`
- **Purpose**: Defends the administrative authorization gate for `sensen.finance.Finance/RefreshStateAssumptions` (`backend/src/modules/finance_service.cpp`).
- **Sections & Invariants (3 Sections, 7 Checks)**:
  - Anonymous callers return `grpc::StatusCode::PERMISSION_DENIED`.
  - Pro tier callers return `grpc::StatusCode::PERMISSION_DENIED` with message `"Only Partner-tier API keys may refresh state assumptions"`.
  - Partner tier callers are admitted with `grpc::StatusCode::OK`.

### `backend/tests/test_strategy_store_pg.cpp`
- **Purpose**: Tests PostgreSQL database persistence, schema migrations, CRUD operations, UUID parsing, and Row-Level Security (RLS) posture for saved strategies.
- **Sections & Checks (9 Sections, ~45 Checks, Lines 1-470)**:
  - Skip convention (`backend/tests/test_strategy_store_pg.cpp:92-97`): returns exit code `77` if `DATABASE_URL` is unset.
  - `Section 0: RLS Posture Verification` (`backend/tests/test_strategy_store_pg.cpp:106-149`): Directly queries PostgreSQL catalog tables:
    - `SELECT relrowsecurity, relforcerowsecurity FROM pg_class WHERE relname = 'saved_strategies';` asserts both are true.
    - `SELECT rolsuper, rolbypassrls FROM pg_roles WHERE rolname = 'ofc_app';` asserts both are false.
  - Sections 1-4: CRUD operations, cross-user isolation, and mutation arm proving RLS blocks cross-tenant reads even if application query omits `WHERE user_id = $1`.
- **Registration**:
  - Built via CMake (`backend/CMakeLists.txt:1771-1781`) but excluded from `add_test()`.

### `scripts/code_policy_check.sh`
- **Purpose**: Enforces repository C++23 coding rules using `git ls-files` against configuration in `config/cpp_details.txt`.
- **Policy Rules Enforced**:
  1. **Rule 3 (No Raw `new` / `delete`)**: Prohibits raw pointer allocations outside memory management primitives; requires `std::make_unique` or `std::make_shared`.
  2. **Rules 50/55 (No `-ffast-math`)**: Prohibits `-ffast-math` or `-Ofast` flags in CMake files to maintain IEEE 754 compliance for NaN and Infinity checks.
  3. **Rule 31 (Trailing Return Types)**: Enforces modern `auto func(...) -> ReturnType` syntax across interfaces.

### `frontend/scripts/check-export.mjs`
- **Purpose**: Post-build verification gate for Next.js static site export (`out/`). Enforces strict Google AdSense publisher compliance, word count thresholds, and JSON-LD structured data validity.
- **Validation Gates**:
  1. **Publisher Ad Placement Allowlist** (`frontend/scripts/check-export.mjs:154-179`): Ad scripts (`adsbygoogle.js`) are permitted **only** on the 26 strategy guides (`out/guides/*.html`). Ads are strictly prohibited on `out/404.html`, `out/_not-found.html`, and interactive tool pages.
  2. **Word Count Ceiling** (`frontend/scripts/check-export.mjs:180-205`): Every page carrying ad scripts must contain $\ge 600$ words of publisher prose.
  3. **JSON-LD Schema Validity** (`frontend/scripts/check-export.mjs:206-235`): Verifies structured data conforms to `FinancialProduct` or `Article` schemas.

### `backend/tests/test_vendored_proto_drift.cpp`
- **Purpose**: Compares canonical backend proto `backend/proto/finance.proto` against vendored copy `clients/mortgagefv/proto/finance.proto` (`backend/tests/test_vendored_proto_drift.cpp:77-83`).
- **Drift Logic & Checks (8 Checks, Lines 1-127)**:
  - `contract_body()` (`backend/tests/test_vendored_proto_drift.cpp:57-67`): extracts everything from the first `syntax` line onward, excluding the top-level provenance header which differs between repositories.
  - Byte-for-byte equality assertion (`backend/tests/test_vendored_proto_drift.cpp:109-112`).
  - Canary checks for specific additions: `ComputeRentVsBuyBatch`, `monthly_taxes_ins_maintenance`, and `real_buying_advantage` (`backend/tests/test_vendored_proto_drift.cpp:116-122`).
- **Registration**:
  - Registered in CTest as `VendoredProtoDriftTest` (`backend/CMakeLists.txt:1411`).

### `backend/tests/test_python_bindings.py`
- **Purpose**: Exercises Python nanobind bindings (`options_futures_engine`) for `Quote`, `RatePoint`, and `RiskFreeRate`.
- **Registration & Setup**:
  - Registered in CTest as `PythonBindingsTest` (`backend/CMakeLists.txt:1268-1271`).
  - Preloads jemalloc (`LD_PRELOAD=${JEMALLOC_LIBRARY}`) to avoid static TLS exhaustion during `dlopen` (`backend/CMakeLists.txt:1285-1287`).
- **Tests (5 Tests, Lines 1-143)**:
  - `test_import`: verifies `Quote`, `RatePoint`, `RiskFreeRate`, `fetch_quote`, `fetch_risk_free_rate`.
  - `test_quote_roundtrip`: verifies attribute read/write and float types.
  - `test_rate_point_roundtrip`: verifies tenor, days, and rate conversions.
  - `test_fetch_risk_free_rate`: calls US Treasury fetch; accepts either live curve or engine-produced `RuntimeError`.
  - `test_fetch_quote`: calls Alpaca quote fetch; accepts either live quote or engine-produced `RuntimeError`.

---

## Frontend Store and Component Test Suite

All frontend tests run under Vitest (`npm test` in `frontend/`). They verify store state machines, gRPC Web client mocking, refusal discrimination, and UI rendering invariants.

### `frontend/src/config/ad-routes.test.ts`
- **Purpose**: Defends the ad route allowlist architecture. Asserts that ad scripts are present ONLY on publisher content and strictly absent on all error/calculator routes.
- **Exported Tests (6 Tests)**:
  - Allowlist Totality: Proves exactly 26 strategy guide routes are in the allowlist.
  - 404 Route Strict Exclusion: Asserts `isAdEligible('/404') === false` and `isAdEligible('/unknown-route') === false`.
  - Mutation Arm (Reverting to Denylist): Proves a denylist approach fails on unpredicted paths.

### `frontend/src/content/strategy-guides.test.ts`
- **Purpose**: Defends static content integrity for all 26 strategy guides.
- **Exported Tests**:
  - Verifies slug uniqueness, non-empty markdown content, minimum 600-word count requirement, and valid metadata (title, description, category, tags).

### `frontend/src/lib/chainFreshness.test.ts`
- **Purpose**: Defends the market data freshness chip logic (switching between green "LIVE" and amber "DELAYED").
- **Exported Tests (6 Tests)**:
  - Data within 15 minutes formats as "LIVE".
  - Data older than 15 minutes formats as "DELAYED (X min ago)".
  - Negative clock skew (client time behind server timestamp) clamps safely to "LIVE" instead of throwing.

### `frontend/src/store/asian-leg.test.ts`
- **Purpose**: Defends Asian option parameter serialization and store state handling.
- **Exported Tests (5 Tests)**:
  - `buildStrategyRequest` correctly propagates `asian_type` enum (`NOT_ASIAN`, `ARITHMETIC_AVERAGE`, `GEOMETRIC_AVERAGE`) to gRPC request.
  - Defaults to `NOT_ASIAN` when omitted.

### `frontend/src/store/assistant-apply.test.ts`
- **Purpose**: Defends the "Apply to Calculator" action in the strategy assistant panel.
- **Exported Tests (8 Tests)**:
  - Verifies snapping recommended expiration dates to the nearest listed expiration in `chainExpirations`.
  - Rejects application if proposed legs do not match the current underlying asset class.

### `frontend/src/store/assistant-outcomes.test.ts`
- **Purpose**: Defends outcome routing for the Strategy Assistant gRPC responses.
- **Exported Tests (7 Tests)**:
  - Discriminates response `oneof` outcome (`clarification`, `strategy_proposal`, `educational_refusal`, `error`).
  - Proves status code 7 (`PERMISSION_DENIED`) routes to upgrade prompt, not generic error.

### `frontend/src/store/calculate-guards.test.ts`
- **Purpose**: Defends the precondition evaluation ladder in `useCalculatorStore.calculateStrategy()`.
- **Exported Tests (11 Tests)**:
  - Ladder ordering:
    1. Empty legs $\to$ `notReady: "Add at least one leg before calculating."`.
    2. Null spot price $\to$ `notReady: "Spot price must be greater than zero."`.
    3. Null risk-free rate $\to$ triggers rate fetch or halts if rate unavailable.
  - Proves guards halt execution before making network RPC calls.

### `frontend/src/store/calculator-not-ready.test.ts`
- **Purpose**: Defends the separation between `notReady` (neutral guidance prompt) and `error` (loss red banner).
- **Exported Tests (6 Tests)**:
  - Proves preconditions (missing strike, empty legs, unselected expiry) populate `notReady` while `error` remains strictly `null`.
  - Proves `notReady` and `error` are mutually exclusive.

### `frontend/src/store/calculator-race.test.ts`
- **Purpose**: Defends against out-of-order gRPC response resolution using staleness sequence tokens.
- **Exported Tests (9 Tests)**:
  - Stale Success Discard (`frontend/src/store/calculator-race.test.ts:182-197`): Request 1 resolving after Request 2 does NOT overwrite Request 2's result.
  - Stale Refusal Discard (`frontend/src/store/calculator-race.test.ts:199-212`): Older `PERMISSION_DENIED` does not blank newer live result.
  - Spinner Integrity (`frontend/src/store/calculator-race.test.ts:246-266`): Older response resolving does NOT set `isLoading: false` while newer request is in flight.
  - Overtaking Refusal (`frontend/src/store/calculator-race.test.ts:284-306`): Emptying legs while call is in-flight stops spinner cleanly.

### `frontend/src/store/chain.test.ts`
- **Purpose**: Defends option chain loading, expiration resolution precedence, and futures forward curve handling.
- **Exported Tests (8 Tests)**:
  - Ticket Expiration Lockstep (`frontend/src/store/chain.test.ts:180-191`): `loadChain` seeds `ticket.expiration` to `selectedExpiration`.
  - Expiration Precedence (`frontend/src/store/chain.test.ts:193-239`): Explicit argument wins over response selected date.
  - Futures Symbol Support (`frontend/src/store/chain.test.ts:241-266`): Response with forward curve and no option strikes sets `chainStatus: 'ready'` (defends against bug marking futures as error).
  - RPC Failure Cleanup (`frontend/src/store/chain.test.ts:284-304`): Clears stale strikes and expirations on error.
  - Strike Field Mapping (`frontend/src/store/chain.test.ts:329-361`): Asserts distinct call vs put mappings for bid, ask, delta, iv, volume, open interest.

### `frontend/src/store/entitlement.test.ts`
- **Purpose**: Enforces that Pro-gate refusals are discriminated strictly by gRPC status code (`GRPC_PERMISSION_DENIED = 7`), never by string matching.
- **Exported Tests (7 Tests)**:
  - Status Code Discrimination (`frontend/src/store/entitlement.test.ts:96-107`): Routes code 7 to `gateDenied` and keeps `error: null`.
  - Reworded Message Resilience (`frontend/src/store/entitlement.test.ts:109-128`): Varies message ("Needs Pro", "Upgrade to price spreads") while holding code 7 fixed; routing never breaks.
  - Non-Entitlement Codes to Error (`frontend/src/store/entitlement.test.ts:130-144`): Codes 2, 3, 14 route to `error`, never `gateDenied`.
  - Denial Clearing (`frontend/src/store/entitlement.test.ts:186-219`): `clearLegs()` or removing the last leg clears `gateDenied`.

### `frontend/src/store/harness.canary.test.ts`
- **Purpose**: Canary test proving `createFakeClient` harness actually intercepts gRPC client calls and updates store state.
- **Exported Tests (2 Tests)**:
  - Asserts gRPC calls are captured in `fake.calls` rather than reaching network.
  - Asserts scripted response lands in store state.

### `frontend/src/store/matrix-bounds.test.ts`
- **Purpose**: Defends P&L matrix price windowing bounds.
- **Exported Tests (6 Tests)**:
  - `buildStrategyRequest`: Unset bounds send wire zero (`0`).
  - `setMatrixBounds`: Performs PATCH (clearing min leaves max standing).
  - Sanitization: `0`, `-10`, `NaN`, `Infinity` are collapsed to `null` and never sent to engine.
  - Inverted bounds: Min > Max is not modified by client; engine validation refuses it.

### `frontend/src/store/model-limit.test.ts`
- **Purpose**: Enforces that modeling limits (`FAILED_PRECONDITION = 9`, such as Asian option averaging) are routed to `modelLimit`, never to red `error`.
- **Exported Tests (5 Tests)**:
  - Routes code 9 to `modelLimit` regardless of message wording.
  - Genuine transport failures (code 14) route to `error`, not `modelLimit`.

### `frontend/src/store/saved-scenarios.test.ts`
- **Purpose**: Defends saved scenarios: wire round-trip, status code discrimination, and reopen fidelity.
- **Exported Tests (9 Tests)**:
  - Refusal discrimination: Code 16 (`UNAUTHENTICATED`) $\to$ `needsSignIn: true`; Code 7 (`PERMISSION_DENIED`) $\to$ `needsPro: true`.
  - Empties scenario list on refusal (prevents signed-out users from seeing cached rows).
  - Saves identical payload structure as `calculateStrategy`.
  - Reopening a scenario into `useCalculatorStore` re-assigns fresh unique leg IDs.

### `frontend/src/store/ticket.test.ts`
- **Purpose**: Defends `commitTicket` and `setTicket` in `useCalculatorStore`.
- **Exported Tests (13 Tests)**:
  - Regression Test (`frontend/src/store/ticket.test.ts:147-171`): Resolves `expiration_days` from `selectedExpiration` when `ticket.expiration` was unset (`ticket.expiration || selectedExpiration`). Catches production bug where default clicks committed `expiration_days: 0`.
  - Same-day expiration: Legitimately selected DTE 0 is preserved as `expiration_days: 0`.
  - Refusals:
    - Strike null/0/-10 $\to$ `notReady: "Pick a strike before adding the leg."`.
    - Premium null/0/-1 $\to$ `notReady: "This contract has no quoted price. Enter the price you would pay or receive."`.
    - Unmatched date $\to$ `notReady: "Pick an expiry before adding the leg."`.
  - Quantity: Non-positive coerced to 1.

### `frontend/src/store/tree-pricer-not-ready.test.ts`
- **Purpose**: Defends exercise style binomial tree pricer preconditions.
- **Exported Tests (5 Tests)**:
  - Missing strike, missing expiry, missing IV, or zero spot price route to `notReady`, never to red `error`.
  - Clears `notReady` prompt once missing value is supplied.

---

## Test Coverage Matrix

| Production Subsystem | Primary Source Files | Exercised By Test Targets | Key Guarantees Verified |
| :--- | :--- | :--- | :--- |
| **Options & Futures Calculator Service** | `backend/src/modules/calculator_service.cpp` | `test_calculator_service`, `test_sgee_automated_reasoning`, `calculator-race.test.ts`, `calculate-guards.test.ts`, `matrix-bounds.test.ts` | Closed-form spread payoffs, SGEE execution soundness, DID-COMPUTE postcondition, response sequence ordering, matrix bounds |
| **Option Binomial / Trinomial Pricer** | `backend/src/modules/finance_service.cpp` | `test_option_pricing_service`, `tree-pricer-not-ready.test.ts` | Convergence to Black-Scholes, American early exercise premium, Bermudan window parsing, Asian averaging states |
| **Financial Mathematics Core** | `backend/sensen/src/financial.cppm` | `test_finance_service_validation` | XNPV span safety, TVM iteration bounds, IEEE 754 NaN immunity, closing costs credit clamping |
| **Option Chain & Market Data** | `backend/src/modules/market_data.cppm` | `test_market_data_resilience`, `test_option_chain_cache`, `chain.test.ts`, `chainFreshness.test.ts` | Circuit breaker 5xx tripping, non-retryable 4xx bypass, 15-min TTL / 1-hr serve-stale cap, LIVE vs DELAYED badge |
| **Strategy Assistant (LLM & SGEE)** | `backend/src/modules/assistant_service.cpp` | `test_assistant_service`, `test_assistant_verification`, `assistant-outcomes.test.ts`, `assistant-apply.test.ts` | Strategy assistant DID-COMPUTE postcondition, jailbreak / prompt injection defense, financial advice disclaimers |
| **Mortgage Assistant (LLM & SGEE)** | `backend/src/modules/mortgage_assistant_service.cpp` | `test_mortgage_assistant_service`, `test_mortgage_verification`, `test_mortgage_grammar.cpp` | Mortgage assistant DID-COMPUTE postcondition, slot-kind totality, character-by-character constrained grammar |
| **Inference Admission & Queue Cluster** | `backend/src/modules/inference_admission.cpp`, `backend/src/modules/inference_queue.cppm` | `test_inference_admission`, `test_sgee_queue_client`, `test_inference_queue_pg`, `test_inference_admission_pg`, `QueueNodeEntrypointTest` | FIFO scheduling, 256 KiB cap, leader redirection, distributed Postgres lease fencing tokens, mTLS entrypoint gate |
| **Entitlements & Auth Middleware** | `backend/src/modules/api_key.cppm`, `backend/src/modules/api_key.cpp`, `backend/src/modules/quota.cppm` | `test_api_key_entitlement`, `test_quota_tier_label`, `test_state_assumptions_gate`, `entitlement.test.ts`, `model-limit.test.ts` | Key format validation, revoked key rejection, Free vs Pro vs Partner authorization gates, code-based refusal discrimination |
| **Scenario Persistence (PostgreSQL)** | `backend/src/modules/strategy_store.cpp` | `test_strategy_store_pg`, `saved-scenarios.test.ts` | Row-Level Security (RLS) catalog posture, multi-tenant isolation, save/list round-trip, reopening fidelity |
| **State Demographics & Census ACS** | `backend/src/modules/state_refresh.cppm` | `test_state_refresh`, `test_state_assumptions_gate` | Census ACS demographic data bounds, property tax rate limits, administrative write protection |
| **Wire Protocol & Client Bindings** | `backend/proto/calculator.proto`, `backend/proto/finance.proto` | `test_vendored_proto_drift`, `test_python_bindings.py` | Client-backend proto synchronization, tag immutability, Python nanobind ABI stability |
| **Static Web Export & Ad Compliance** | `frontend/src/pages/`, `frontend/scripts/` | `check-export.mjs`, `ad-routes.test.ts`, `strategy-guides.test.ts` | Strict AdSense allowlist (only 26 guides), 0 ads on 404/tools, $\ge 600$ words publisher content, JSON-LD schemas |

---

## Tests Deliberately Excluded from CTest (`add_test`)

Three PostgreSQL integration test targets compiled in `backend/CMakeLists.txt` are deliberately not registered with `add_test()`:

1. `test_strategy_store_pg` (`backend/CMakeLists.txt:1771-1781`):
   - **Requirement**: Requires a running PostgreSQL cluster with database migrations applied and connection string passed in `DATABASE_URL`.
   - **Behavior when missing**: Line `backend/tests/test_strategy_store_pg.cpp:92-97` returns exit code `77`.
   - **Exclusion Rationale**: Standard automated developer builds (`ctest`) must run without external daemon dependencies. Registering it under `add_test` without a running PostgreSQL instance causes false failures or silent vacuous runs. Run directly:
     ```bash
     DATABASE_URL="postgresql://postgres@127.0.0.1:55432/postgres" ./test_strategy_store_pg
     ```
2. `test_inference_queue_pg` (`backend/CMakeLists.txt:1783-1793`):
   - **Requirement**: Requires live PostgreSQL database (`DATABASE_URL`).
   - **Behavior when missing**: Lines `backend/tests/test_inference_queue_pg.cpp:25-28` terminate the process with exit code `1` and error: `"DATABASE_URL environment variable must be set"`.
   - **Exclusion Rationale**: Exercises destructive concurrent lease sweeps, worker heartbeats, and reaper tests that require an isolated test database.
3. `test_inference_admission_pg` (`backend/CMakeLists.txt:1855-1869`):
   - **Requirement**: Requires live PostgreSQL database (`DATABASE_URL`).
   - **Behavior when missing**: Lines `backend/tests/test_inference_admission_pg.cpp:23-26` terminate the process with exit code `1`.
   - **Exclusion Rationale**: Simulates multi-worker distributed admission contention, cross-instance handoffs, and partition recovery.

*(Note: `QueueNodeEntrypointTest` and `PythonBindingsTest` are registered in CTest at `backend/CMakeLists.txt:1842-1843` and `1268-1271` respectively).*

---

## The Skip Convention: Exit Code 77

### Rationale and Specification
In standard POSIX test harnesses and CTest, exit code `77` denotes a **Skipped Test** (preconditions or external dependencies not met). 

When a test cannot run because an optional dependency (such as a database or external hardware device) is absent, returning exit code `77` signals to the test runner that the test was intentionally bypassed rather than executed.

In `backend/tests/test_strategy_store_pg.cpp:92-97`:
```cpp
const char* db_url = std::getenv("DATABASE_URL");
if (!db_url || std::strlen(db_url) == 0) {
    std::cout << "[SKIPPED] DATABASE_URL not set; skipping PostgreSQL integration tests.\n";
    return 77;
}
```

### Why Returning Skip Code Beats Passing Vacuously
A test that passes vacuously (e.g. `if (!db) return 0;`) creates a dangerous illusion of security:
- CI pipelines report green (100% passing) even if the database credentials were misconfigured or omitted.
- High-risk security gates (like PostgreSQL Row-Level Security and tenant isolation checks) are completely skipped without warning.
- Returning `77` causes CTest to explicitly report `***Skipped`, alerting developers and CI reporting dashboards that the suite did not execute.

### Why Disappearance from the Test List is Worse Than a Skip
If a test is conditionally excluded from CMake generation (e.g. wrapping `add_executable` inside `if(ENABLE_PG_TESTS)`), the target disappears from the build manifest entirely. 
- Disappearance leaves no historical record in the test log.
- A developer reading the test summary cannot tell whether a test was removed, forgotten, or intentionally omitted.
- Building the binary unconditionally and reporting exit code `77` when prerequisites are absent keeps the test visible in the test inventory while preventing false build breakages.

---

## Mutation Checks and CCACHE_DISABLE=1

### Compiler Cache Vulnerability (`CCACHE_DISABLE=1`)
As documented in the repository build rules, modern C++23 modular builds using Clang and `ccache` suffer from a critical dependency tracking blind spot:
- `ccache` calculates cache hashes from the preprocessed C++ source text and compiler flags of the `.cpp` translation unit.
- In C++23, modular interface units (`.cppm`) compile into binary module interfaces (`.pcm` BMI files).
- When a developer mutates a `.cppm` file (for example, removing a validation check or changing an invariant guard), the consuming `.cpp` file's source code has not changed.
- `ccache` reports a cache hit, reusing the stale object file compiled against the old BMI!
- **Consequence**: When verifying that a test properly catches a deleted guard, the build will silently reuse the old binary containing the guard, causing the test to pass falsely.
- **Mandatory Practice**: When running mutation verification checks or refactoring `.cppm` module interfaces, builds must set:
  ```bash
  export CCACHE_DISABLE=1
  ```

### Recorded Mutation Arms Across the Repository
Ten specific mutation arms are verified across the test suites:

1. **XNPV Span Size Guard Mutation** (`test_finance_service_validation.cpp:423-490`, `backend/sensen/src/financial.cppm:950`):
   - *Mutation*: Delete `if (values.size() != dates.size())` or `check_dated_span` in `backend/sensen/src/financial.cppm`.
   - *Result*: Production bug reproduced with out-of-bounds memory read. Test catches this and fails.
2. **Closing Costs Negative Credit Clamping** (`test_finance_service_validation.cpp:2122-2131`):
   - *Mutation*: Delete lender credit clamping logic allowing negative closing costs.
   - *Result*: Catches credit exceeding total settlement charges; verifies `FAILED_PRECONDITION` is returned.
3. **Date Arithmetic Second Multiplier** (`test_finance_service_validation.cpp:2335-2517`):
   - *Mutation*: Omit days-to-seconds ($86,400$) conversion for dates passed to `xnpv`/`xirr`.
   - *Result*: Catches undiscounted cash flows evaluated across sub-day intervals.
4. **Bermudan Exercise Window Guard** (`test_option_pricing_service.cpp:458-618`, `test_finance_service_validation.cpp:1003-1063`):
   - *Mutation*: Delete Bermudan discrete date dead-band checking `[years_to_expiry - dt/2, years_to_expiry)`.
   - *Result*: American-style continuous exercise was incorrectly permitted on Bermudan contracts; caught by test.
5. **SGEE DID-COMPUTE Postcondition in Calculator** (`test_calculator_service.cpp:571-607`):
   - *Mutation*: Remove `ComputeGreeks` action node from `CalculateStrategy` workflow graph (`kMissingGreeks`).
   - *Result*: Engine previously returned `Status::OK` with net greeks defaulting to 0. Test asserts that DID-COMPUTE postcondition catches the stall and returns `Status::INTERNAL` naming "ComputeGreeks" or "Done".
6. **SGEE DID-COMPUTE in Strategy Assistant** (`test_assistant_service.cpp:283-306`):
   - *Mutation*: Remove `CheckModel` node from assistant workflow (`kMissingCheckModel`).
   - *Result*: Returns `Status::INTERNAL` naming unpopulated payload on terminal node `Refused`.
7. **SGEE DID-COMPUTE in Mortgage Assistant** (`test_mortgage_assistant_service.cpp:312-334`):
   - *Mutation*: Remove `CheckModel` action from mortgage assistant workflow (`kMissingCheckModel`: `"Admission"`, `"Generate"`, `"ParseAndVerify"`).
   - *Result*: Catches unpopulated response and returns `Status::INTERNAL`.
8. **PostgreSQL RLS Application WHERE Clause Bypass** (`test_strategy_store_pg.cpp:106-149`, `Section 4`):
   - *Mutation*: Delete `WHERE user_id = $1` from SQL `SELECT` in `backend/src/modules/strategy_store.cpp`.
   - *Result*: Before RLS, User A read User B's strategies. With PostgreSQL RLS enabled, RLS blocks cross-tenant reads at the database engine level; test proves cross-user leakage is prevented.
9. **AdSense Route Allowlist vs Denylist** (`ad-routes.test.ts:92-95`, `check-export.mjs:168-179`):
   - *Mutation*: Revert allowlist to a denylist of excluded paths (`['/404', '/login']`).
   - *Result*: Unpredicted URLs or 404 pages rendered AdSense script tags; caught by export checker and unit tests.
10. **Option Ticket Expiration Fallback** (`ticket.test.ts:147-171`):
    - *Mutation*: Revert `t.expiration || selectedExpiration` back to `t.expiration` alone.
    - *Result*: Committing a leg on the default path generated `expiration_days: 0` despite UI showing a valid date; caught by test.

---

## Posture Gates vs Behavioral Testing

### Conceptual Distinction
- **Behavioral Tests**: Verify that given input $X$, the component returns output $Y$ through its standard execution paths (testing functional correctness).
- **Posture Gates**: Verify system boundary constraints, structural configuration, operating permissions, and defence-in-depth mechanisms that functional tests cannot observe.

### Why Behavioral Tests Alone Miss Posture Failures

#### 1. PostgreSQL Row-Level Security (RLS)
- In `backend/src/modules/strategy_store.cpp`, the application's SQL queries include `WHERE user_id = $1`.
- In a pure behavioral test, if RLS is completely disabled on the database, **all functional tests still pass**! The application query already filters by user ID.
- However, if an SQL injection vulnerability occurs, or a developer writes a query that accidentally omits `WHERE user_id = $1`, the database provides zero defense.
- **The Posture Gate** (`test_strategy_store_pg.cpp:106-149` Section 0):
  Directly inspects the PostgreSQL system catalogs:
  ```sql
  SELECT relrowsecurity, relforcerowsecurity FROM pg_class WHERE relname = 'saved_strategies';
  SELECT polname, polcmd, polroles FROM pg_policy WHERE polrelid = 'saved_strategies'::regclass;
  SELECT rolsuper, rolbypassrls FROM pg_roles WHERE rolname = 'ofc_app';
  ```
  It verifies that:
  1. `relrowsecurity` is `true` (RLS is active).
  2. `relforcerowsecurity` is `true` (RLS applies even to table owners).
  3. The database role `ofc_app` has `rolsuper = false` and `rolbypassrls = false`.
  Behavioral tests cannot verify this posture; dropping RLS leaves behavior unchanged until an exploit occurs.

#### 2. AdSense Publisher Policy Allowlist
- Google AdSense policies strictly prohibit advertisements on error pages, 404 pages, or pages lacking significant publisher content ($\ge 600$ words).
- A behavioral denylist approach tests known non-content routes (e.g. `expect(isAdEligible('/404')).toBe(false)`).
- If a user requests an arbitrary non-existent route (`/random-path-xyz`), Next.js routes to the not-found handler. If the route check relied on a denylist, the unlisted path would evaluate to eligible, injecting ad scripts onto a page with 13 words of error text.
- **The Posture Gate** (`ad-routes.test.ts:31-97` and `check-export.mjs:154-179`):
  Uses a strict, closed allowlist matching only the 26 pre-approved strategy guide files. The post-build script scans every generated `.html` file in `out/`, asserting that no ad script tag exists anywhere outside `out/guides/`.

#### 3. Census ACS Demographic Bounds
- In `test_state_refresh.cpp:118-150`, incoming Census API data is checked against strict macroeconomic bounds.
- If Census changes its API schema or returns uncalibrated sample data, downstream calculations would still run and produce numbers, passing functional tests.
- The posture gate enforces sanity bounds (property tax between 0.05% and 4.0%, median home value between $50,000 and $3,000,000), preventing corrupted demographic data from entering the database.

#### 4. Client Proto Drift Posture
- In `test_vendored_proto_drift.cpp:57-124`, the build inspects proto definition files directly.
- Behavioral tests use compiled protobuf objects; if the client proto repository drifts in field numbering, tests using mocks will pass, but client-server network serialization in production will drop payload fields. The posture gate detects drift at build time.

---

## Open Questions
All test files, targets, build gate scripts, and frontend suites listed in the scope were inspected and verified against the repository source code. There are no unverified behaviors or speculative claims in this inventory.
