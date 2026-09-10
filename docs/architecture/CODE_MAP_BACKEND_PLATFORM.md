# Backend code map: platform services
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

This document provides a comprehensive code map and technical reference for the platform infrastructure modules underpinning the Options & Futures Calculator backend. These modules implement identity verification, quota enforcement, database abstraction, persistent user storage, automated demographic data ingestion, distributed inference job admission, market data caching, and cryptographic compliance:

- **API Key Authentication & Entitlements** (`backend/src/modules/api_key.cppm`, `backend/src/modules/api_key.cpp`): Identity extraction, API key hashing, Supabase JWT verification, licence tokens, and entitlement gating.
- **Quotas & Rate Limiting** (`backend/src/modules/quota.cppm`, `backend/src/modules/quota.cpp`): In-memory leaky-bucket/token-bucket rate limiter, compute cost estimation per RPC, caller attribution, and hourly allowance enforcement.
- **PostgreSQL Client Substrate** (`backend/src/modules/pg.cppm`): RAII-managed libpq wrapper, parameterized query execution, connection pooling, circuit breaking, and asynchronous `LISTEN/NOTIFY` pumping.
- **Strategy Persistence Store** (`backend/src/modules/strategy_store.cppm`, `backend/src/modules/strategy_store.cpp`): Scenario persistence abstraction isolating libpq dependencies and enforcing PostgreSQL Row-Level Security (RLS) contexts.
- **State Assumptions Refresh** (`backend/src/modules/state_refresh.cppm`, `backend/src/modules/state_refresh.cpp`): Scheduled ingestion and statistical validation of US Census American Community Survey (ACS) property tax and housing cost data.
- **Shared Inference Queue & Admission** (`backend/src/modules/inference_admission.cppm`, `backend/src/modules/inference_admission.cpp`, `backend/src/modules/inference_queue.cppm`, `backend/src/modules/inference_queue.cpp`, `backend/src/modules/sgee_queue_client.cppm`, `backend/src/modules/sgee_queue_client.cpp`): Unified admission abstraction, distributed queue brokers (Local, PostgreSQL, SGEE), lease/fencing tokens, and non-blocking background workers.
- **Market Data & Risk-Free Rates** (`backend/src/modules/market_data.cppm`): Resilient HTTP client for Alpaca and US Treasury data, multi-tier TTL caching, bounded stale fallback serving, and RFC3339 timestamps.
- **FIPS Cryptographic Mode** (`backend/src/modules/fips_mode.cppm`): OpenSSL 3.x FIPS provider initialization, startup enforcement, and container boundary constraints.

---

## API Key Authentication & Entitlements (api_key.cppm, api_key.cpp)

### Symbol Inventory

| Symbol | Kind | Location | Description |
| :--- | :--- | :--- | :--- |
| `KeyType` | `enum class` | `backend/src/modules/api_key.cppm:33` | Key classification: `Publishable`, `Secret`. |
| `Outcome` | `enum class` | `backend/src/modules/api_key.cppm:36-46` | Auth outcomes: `Allow`, `DeniedMissing`, `DeniedInvalid`, `DeniedRevoked`, `DeniedRateExceeded`, `DeniedOriginMismatch`, `DeniedSecretFromBrowser`, `DeniedInactive`, `InternalError`. |
| `Mode` | `enum class` | `backend/src/modules/api_key.cppm:57` | Service enforcement mode: `Observe`, `Warn`, `Enforce`. |
| `Identity` | `struct` | `backend/src/modules/api_key.cppm:60-134` | Authenticated principal metadata: `id`, `subject`, `tier`, `key_type`, `auth_mode`, `is_authenticated`, `origin`. |
| `AuthResult` | `struct` | `backend/src/modules/api_key.cppm:137-142` | Contains `Outcome outcome`, `Identity identity`, and `std::string message`. |
| `KeyRegistry` | `class` | `backend/src/modules/api_key.cppm:151-194` | In-memory key store indexed by 128-character SHA-512 hex digest. |
| `GateMode` | `enum class` | `backend/src/modules/api_key.cppm:301` | Feature gate enforcement mode: `Off`, `Warn`, `Enforce`. |
| `AssistantSurface` | `enum class` | `backend/src/modules/api_key.cppm:408-421` | Surface enumeration: `Strategy`, `Mortgage`. |
| `kStrategySurface` | `constexpr AssistantSurfaceConfig` | `backend/src/modules/api_key.cppm:430-450` | Entitlement configuration for the strategy surface. |
| `kMortgageSurface` | `constexpr AssistantSurfaceConfig` | `backend/src/modules/api_key.cppm:454-474` | Entitlement configuration for the mortgage surface. |
| `sha512_hex` | `function` | `backend/src/modules/api_key.cpp:116` | Computes 128-character lowercase hexadecimal SHA-512 digest. |
| `constant_time_equals` | `function` | `backend/src/modules/api_key.cpp:139` | Timing-attack resistant string comparison using `CRYPTO_memcmp`. |
| `generate_key` | `function` | `backend/src/modules/api_key.cpp:147` | Generates 51-character key with `pk_live_` or `sk_live_` prefix and 43 unpadded base64url characters. |
| `hmac_sha512_b64` | `function` | `backend/src/modules/api_key.cpp:239` | Produces truncated 256-bit HMAC-SHA-512 signature in base64url. |
| `verify_supabase_jwt` | `function` | `backend/src/modules/api_key.cpp:253` | Verifies HS256 HMAC signature and claims of Supabase bearer token. |
| `verify_licence` | `function` | `backend/src/modules/api_key.cpp:341` | Verifies HMAC-SHA-512 signature on `lk_live_` licence token. |
| `pro_gate_mode` | `function` | `backend/src/modules/api_key.cpp:401` | Evaluates the `PRO_GATE_MODE` environment variable. |
| `is_pro` | `function` | `backend/src/modules/api_key.cpp:408` | Returns true if tier is `"pro"` or `"partner"`. |
| `check_strategy_entitlement` | `function` | `backend/src/modules/api_key.cpp:457` | Validates multi-leg strategy entitlement against `PRO_GATE_MODE`. |
| `check_saved_scenarios_entitlement` | `function` | `backend/src/modules/api_key.cpp:480` | Enforces subject existence and Pro status for scenario persistence. |
| `check_assistant_entitlement` | `function` | `backend/src/modules/api_key.cpp:537` | Validates assistant surface access entitlements. |
| `authenticate` | `function` | `backend/src/modules/api_key.cpp:732` | Dispatches authentication across bearer tokens and API keys. |

### Key & Token Formats

1. **API Keys**:
   - Total length: 51 characters (`backend/src/modules/api_key.cpp:33-36`).
   - Prefix: `pk_live_` for publishable keys or `sk_live_` for secret keys (8 characters, `backend/src/modules/api_key.cpp:28-29`).
   - Entropy: 32 cryptographically secure pseudo-random bytes generated via `RAND_bytes` (`backend/src/modules/api_key.cpp:151-155`), encoded as 43 unpadded base64url characters (`backend/src/modules/api_key.cpp:166-173`).
   - Storage: Hashed via SHA-512 into 128 lowercase hexadecimal characters (`backend/src/modules/api_key.cpp:35, 609-618`). Plaintext keys are never stored in the registry.
   - Browser Leak Rule: If an API key is of type `KeyType::Secret` and the HTTP request contains a non-empty `Origin` header, authentication is rejected with `Outcome::DeniedSecretFromBrowser` (`backend/src/modules/api_key.cpp:781-786`). Secret keys may only be used server-to-server.
2. **Licence Tokens**:
   - Format: `lk_live_<payload>.<signature>` (`backend/src/modules/api_key.cppm:239`, `backend/src/modules/api_key.cpp:181, 341-380`).
   - The payload contains base64url-encoded JSON specifying `sub`, `tier`, `exp`, and `iat`.
   - The signature is calculated by computing HMAC-SHA-512 over the base64url payload with `LICENCE_SIGNING_KEY`, truncating the result to 256 bits (32 bytes), and base64url-encoding the digest (`backend/src/modules/api_key.cpp:239-251`).
3. **Supabase JWTs**:
   - Evaluated as standard HS256 JWTs using HMAC-SHA256 with `SUPABASE_JWT_SECRET` (`backend/src/modules/api_key.cpp:253-305`). Validates expiry (`exp`) against the system clock.

### Identity vs Subject Separation

- `Identity::id` (`backend/src/modules/api_key.cppm:61`, `backend/src/modules/api_key.cpp:335`) is a human-readable identifier intended strictly for logging, rate limiting, and caller labeling. For Supabase tokens, it defaults to `"supabase-user"`; for licence tokens, it defaults to `"licence"`; for API keys, it is configured in metadata (e.g. `"acme-risk"`). Multiple end users or clients can share an identical `id`.
- `Identity::subject` (`backend/src/modules/api_key.cppm:67-93`, `backend/src/modules/api_key.cpp:334`) represents the unique authenticated user identity. It is populated solely from the verified `sub` claim (UUID) of a Supabase JWT. For anonymous requests, API keys, and licence tokens, `subject` is explicitly empty (`backend/src/modules/api_key.cppm:77-83`). Any backend operation accessing or mutating user-isolated data must gate on `!subject.empty()`.

### Tier Vocabulary

Tiers are represented as lowercase strings matching the database constraint `profiles.tier IN ('free', 'pro')` (`backend/src/modules/api_key.cpp:409-415`):
- `"free"`: Unpaid tier.
- `"pro"`: Paid tier.
- `"partner"`: Administrative/partner tier recognized by `is_pro` (`backend/src/modules/api_key.cpp:414`).

### Gate Policy Enforcement: Honouring vs Overriding PRO_GATE_MODE

The `PRO_GATE_MODE` environment variable controls feature gating:
- Gates Honouring `PRO_GATE_MODE`:
  - Multi-leg strategy calculations in `check_strategy_entitlement` (`backend/src/modules/api_key.cpp:458-477`).
  - Pro status verification in `check_saved_scenarios_entitlement` (`backend/src/modules/api_key.cpp:495-515`).
  - Surface queries in `check_assistant_entitlement` (`backend/src/modules/api_key.cpp:539-560`).
  In `GateMode::Off`, these checks allow free users through. In `GateMode::Warn`, they log an audit warning and allow access. In `GateMode::Enforce`, they deny access with `grpc::StatusCode::PERMISSION_DENIED`.
- Security & Integrity Controls Deliberately NOT Honouring `PRO_GATE_MODE`:
  1. **Scenario Persistence Subject Gate** (`backend/src/modules/api_key.cpp:487-493`): `check_saved_scenarios_entitlement` unconditionally returns `grpc::StatusCode::UNAUTHENTICATED` if `identity.subject.empty()`. Honouring `PRO_GATE_MODE=off` here would cause unauthenticated requests to fall into a shared empty-subject bucket, exposing saved user data across callers (`backend/src/modules/api_key.cppm:334-340`).
  2. **State Assumptions Ingestion Gate** (`backend/src/modules/finance_service.cpp:3080-3109`): `RefreshStateAssumptions` unconditionally requires `_id.tier == "partner"` and rejects non-partner callers with `PERMISSION_DENIED`. Allowing billing bypasses here would permit unprivileged callers to overwrite authoritative 50-state macroeconomic data.

### Environment Variables

| Variable | Default | Code Location | Effect |
| :--- | :--- | :--- | :--- |
| `FINANCE_REQUIRE_KEY` | `Observe` (0) | `backend/src/modules/api_key.cpp:577` | `"2"` or `"enforce"` sets `Enforce`; `"1"` or `"warn"` sets `Warn`; otherwise defaults to `Observe`. |
| `FINANCE_API_KEYS` | Unset / Empty | `backend/src/modules/api_key.cpp:586` | JSON dictionary mapping 128-character SHA-512 hex digests to key metadata (`id`, `tier`, `origins`, `active`). When unset or empty, key authentication is disabled. |
| `SUPABASE_JWT_SECRET` | Unset / Empty | `backend/src/modules/api_key.cpp:254` | HMAC secret for verifying HS256 Supabase bearer tokens. If unset, JWT authentication fails closed. |
| `LICENCE_SIGNING_KEY` | Unset / Empty | `backend/src/modules/api_key.cpp:342` | Secret key for verifying `lk_live_` licence tokens. If unset, licence authentication fails closed. |
| `PRO_GATE_MODE` | `Off` (0) | `backend/src/modules/api_key.cpp:401` | `"2"` or `"enforce"` sets `Enforce`; `"1"` or `"warn"` sets `Warn`; otherwise defaults to `Off`. |

---

## Quotas & Rate Limiting (quota.cppm, quota.cpp)

### Symbol Inventory

| Symbol | Kind | Location | Description |
| :--- | :--- | :--- | :--- |
| `TierLimits` | `struct` | `backend/src/modules/quota.cppm:35-38` | Defines `requests_per_minute`, `compute_budget_per_hour`, and `burst_allowance`. |
| `Decision` | `struct` | `backend/src/modules/quota.cppm:47-52` | Evaluated limit decision: `allowed`, `refusal_reason`, `retry_after_seconds`, `reset_in_seconds`, `burst_remaining`, `compute_used_this_hour`, `compute_budget_hourly`. |
| `QuotaEnforcer` | `class` | `backend/src/modules/quota.cppm:60-127` | Thread-safe quota manager tracking token buckets and hourly compute usage. |
| `cost_default` | `function` | `backend/src/modules/quota.cpp:482` | Returns baseline cost of 1 compute unit. |
| `cost_amortization` | `function` | `backend/src/modules/quota.cpp:484` | Returns `std::ceil(months / 12.0)`. |
| `cost_amortization_batch` | `function` | `backend/src/modules/quota.cpp:489` | Returns `cost_amortization(months) * std::max(1, items)`. |
| `cost_option_tree` | `function` | `backend/src/modules/quota.cpp:493` | Returns `std::max(1, steps / 100)`. |
| `cost_monte_carlo` | `function` | `backend/src/modules/quota.cpp:501` | Returns `std::max(1, simulations / 1000)`. |
| `cost_probability_tree` | `function` | `backend/src/modules/quota.cpp:508` | Returns `std::max(1, steps / 100)`. |
| `cost_portfolio_optimize` | `function` | `backend/src/modules/quota.cpp:513` | Returns `std::max(2, assets * 2)`. |
| `cost_portfolio_stats` | `function` | `backend/src/modules/quota.cpp:519` | Returns `std::max(1, assets)`. |
| `cost_margin_simulation` | `function` | `backend/src/modules/quota.cpp:525` | Returns `std::max(1, positions)`. |
| `cost_cash_flow` | `function` | `backend/src/modules/quota.cpp:529` | Returns `std::max(1, years)`. |
| `cost_strategy_grid` | `function` | `backend/src/modules/quota.cpp:557` | Returns `std::max(1, (spots * vols) / 250)`. |
| `cost_market_quote` | `function` | `backend/src/modules/quota.cpp:650` | Returns 1 compute unit. |
| `cost_risk_free_rate` | `function` | `backend/src/modules/quota.cpp:652` | Returns 1 compute unit. |
| `cost_market_chain` | `function` | `backend/src/modules/quota.cpp:654` | Returns 2 compute units. |
| `cost_llm_generate` | `function` | `backend/src/modules/quota.cpp:699` | Returns 5 compute units. |

### Bucket Key Attribution & State Management

- Bucket Key Determination:
  - In `admit`: If a caller presents an `api_key` that matches `key_to_tier_`, the plaintext key string is used as the bucket key (`backend/src/modules/quota.cpp:258-260`). If the key is unrecognized or absent, the bucket key defaults to `"~anonymous"`. This prevents unauthenticated clients from evading rate limits by randomizing headers.
  - In `charge`: If unauthenticated, the key collapses to `"~anonymous"` (`backend/src/modules/quota.cpp:318`).
- Mutex Boundaries:
  - `mu_` (`backend/src/modules/quota.cpp:82, 87`) synchronizes access to the `callers_` table (`std::unordered_map<std::string, CallerState>`).
  - `warn_mu_` (`backend/src/modules/quota.cpp:84-86, 130`) synchronizes access to `warned_tiers_` (`std::unordered_set<std::string>`), capped at 64 entries to prevent unbounded growth during tier warning logging.
- Memory Reclamation:
  - `sweep_idle` runs at most once every 5 minutes and removes caller states whose last activity occurred more than 1 hour ago (`backend/src/modules/quota.cpp:388-394`).

### Tier Configuration & Refusal Mechanics

- Policy Ingestion: Dynamic limits are parsed from the `QUOTA_POLICY` JSON string into `tiers_` (`std::unordered_map<std::string, TierLimits>`, `backend/src/modules/quota.cpp:78, 186-197`).
- Unknown Tier Fallback: When a request presents a tier that does not exist in `tiers_`, `limits_for_tier` falls back to the limits of `anonymous_tier_` (`backend/src/modules/quota.cpp:104-109`). The refusal label is explicitly annotated:
  `tier + " (undefined in QUOTA_POLICY; anonymous limits)"` (`backend/src/modules/quota.cpp:315`).
- Per-Key Overrides: Configured in `QUOTA_API_KEYS` and annotated as `tier + " (per-key)"` (`backend/src/modules/quota.cpp:313`).
- Oversized Request Rejection: If a single RPC request has an estimated compute cost exceeding the tier's total hourly budget (`estimated_compute > limits.compute_budget_per_hour`), the call is rejected immediately before admission with:
  `refusal_reason = "compute budget (this call alone exceeds the tier's hourly allowance)"` and `retry_after_seconds = 0` (`backend/src/modules/quota.cpp:358-364`).

### Environment Variables

| Variable | Default | Code Location | Effect |
| :--- | :--- | :--- | :--- |
| `QUOTA_POLICY` | Unset / Empty | `backend/src/modules/quota.cpp:159` | JSON policy defining `anonymous_tier` and individual tier limit blocks (`rpm`, `compute_per_hour`, `burst`). If unset or empty, quota enforcement is disabled. |
| `QUOTA_API_KEYS` | Unset / Empty | `backend/src/modules/quota.cpp:212` | JSON dictionary mapping plaintext API keys to tier names, establishing per-key tier overrides. |

---

## PostgreSQL Client Substrate (pg.cppm)

### Symbol Inventory

| Symbol | Kind | Location | Description |
| :--- | :--- | :--- | :--- |
| `ErrorCode` | `enum class` | `backend/src/modules/pg.cppm:60-70` | Substrate error codes: `ConnectionFailed`, `QueryFailed`, `PoolExhausted`, `CircuitOpen`, `TransactionAborted`. |
| `Error` | `struct` | `backend/src/modules/pg.cppm:81-89` | Pairs `ErrorCode code` with a diagnostic `std::string message`. |
| `ConnHandle` | `using` | `backend/src/modules/pg.cppm:106` | RAII alias: `std::unique_ptr<PGconn, void(*)(PGconn*)>` calling `PQfinish`. |
| `ResultHandle` | `using` | `backend/src/modules/pg.cppm:107` | RAII alias: `std::unique_ptr<PGresult, void(*)(PGresult*)>` calling `PQclear`. |
| `Result` | `class` | `backend/src/modules/pg.cppm:117-151` | Move-only wrapper around `ResultHandle` providing `ok()`, `rows()`, `get()`, and status checks. |
| `Connection` | `class` | `backend/src/modules/pg.cppm:157-243` | Move-only wrapper managing a `ConnHandle` and parameterized query execution. |
| `PoolConfig` | `struct` | `backend/src/modules/pg.cppm:249-260` | Config: `url`, `max_connections` (16), `connect_timeout` (5s), `statement_timeout` (10s), breaker settings. |
| `Lease` | `class` | `backend/src/modules/pg.cppm:269-297` | RAII guard returning a borrowed connection to the parent `Pool` on destruction. |
| `Pool` | `class` | `backend/src/modules/pg.cppm:305-374` | Thread-safe connection pool with health checks and circuit breaking. |
| `start_listen_pump` | `function` | `backend/src/modules/pg.cppm:592-624` | Background thread polling `LISTEN/NOTIFY` channels on a dedicated connection. |

### Connection Establishment & SSLMode Overrides

- `Connection::connect` (`backend/src/modules/pg.cppm:398-433`) constructs database connections using `PQconnectdbParams`:
  - Keywords array: `{"dbname", "connect_timeout", "sslmode", nullptr}`.
  - Values array passes the connection string with `expand_dbname = 1`.
  - Connect timeout is calculated in whole seconds (`std::max(1L, config.connect_timeout.count() / 1000)`).
  - Default SSL mode is `"require"`. Because `sslmode` is specified explicitly in the keyword array *after* `dbname`, libpq precedence rules ensure this setting overrides any `sslmode` query parameter present inside the `DATABASE_URL` connection string.
  - The default can be overridden via the `PGSSLMODE_OVERRIDE` environment variable (`backend/src/modules/pg.cppm:409-412`).
  - Immediately following successful connection initialization, `Connection::connect` issues:
    `SET statement_timeout = <ms>` (`backend/src/modules/pg.cppm:427-432`) to enforce hard execution limits at the server level.

### Execution Invariants & Resilience

1. **Mandatory Parameterized Execution**: `Connection::exec` and `Connection::exec_params` exclusively invoke `PQexecParams` (`backend/src/modules/pg.cppm:38-53, 435-468`). `PQexec` is never invoked, preventing SQL injection by design.
2. **Integrated Circuit Breaker**: Connection establishment is protected by an `sgee::resilience::CircuitBreaker` instance (`backend/src/modules/pg.cppm:258-259, 320-325, 540-545`):
   - Failure threshold: 5 consecutive connection errors.
   - Cooldown period: 5,000 milliseconds.
   - Half-open trials: 1 probe attempt.
3. **Listen Pump Semantics**: `start_listen_pump` (`backend/src/modules/pg.cppm:592-624`) allocates a dedicated, unpooled connection and runs a poll loop on a `std::jthread` with a 200ms tick. Notifications received over this channel are treated strictly as low-latency wakeup hints and are never relied upon for transactional correctness.

### Environment Variables

| Variable | Default | Code Location | Effect |
| :--- | :--- | :--- | :--- |
| `PGSSLMODE_OVERRIDE` | `"require"` | `backend/src/modules/pg.cppm:409` | Overrides the libpq `sslmode` keyword value (e.g. set to `"disable"` in local testing). |

---

## Strategy Persistence Store (strategy_store.cppm, strategy_store.cpp)

### Symbol Inventory

| Symbol | Kind | Location | Description |
| :--- | :--- | :--- | :--- |
| `SavedRow` | `struct` | `backend/src/modules/strategy_store.cppm:51-57` | Storage record: `id`, `name`, `notes`, `surface`, `payload_json`, `created_at`, `updated_at`. |
| `SaveOutcome` | `struct` | `backend/src/modules/strategy_store.cppm:60-65` | Mutation outcome: `id`, `name`, `created_at`, `updated_at`. |
| `StoreError` | `enum class` | `backend/src/modules/strategy_store.cppm:73-89` | Store error taxonomy: `AtCapacity`, `Invalid`, `UnknownUser`, `Unavailable`, `Internal`. |
| `kMaxPerUser` | `constexpr size_t` | `backend/src/modules/strategy_store.cppm:113` | Hard capacity limit: 100 scenarios per user. |
| `kMaxNameBytes` | `constexpr size_t` | `backend/src/modules/strategy_store.cppm:117` | Maximum name byte length: 120 bytes. |
| `IStrategyStore` | `class` | `backend/src/modules/strategy_store.cppm:127-161` | Pure abstract persistence interface (`save`, `list`, `remove`). |
| `make_pg_strategy_store` | `function` | `backend/src/modules/strategy_store.cppm:177`, `backend/src/modules/strategy_store.cpp:350` | Factory instantiating the PostgreSQL-backed implementation. |

### Module Boundary Decoupling

`backend/src/modules/strategy_store.cppm` imports only standard library headers (`std`, `backend/src/modules/strategy_store.cppm:6-31`) and defines zero `pg` types or database symbols. The database substrate is imported exclusively inside the implementation unit `backend/src/modules/strategy_store.cpp` (`backend/src/modules/strategy_store.cpp:26`).

This physical module separation guarantees that consuming services (`calculator_service.cpp` and `finance_service.cpp`) do not link or transitively expose libpq symbols (`nm -uC calculator_service.cpp.o | grep -c 'pg::'` returns 0).

### Transaction Shape & Row-Level Security (RLS) Enforcement

All interactions with `saved_strategies` occur within a strictly structured transaction lifecycle in `Transaction::begin(subject)` (`backend/src/modules/strategy_store.cpp:134-164`):

1. **UUID Pre-Validation**: Validates that `subject` conforms to 36-character hyphenated UUID format (`is_uuid(subject)`, `backend/src/modules/strategy_store.cpp:142`).
2. **Transaction Start**: Emits `BEGIN` (`backend/src/modules/strategy_store.cpp:144`).
3. **Session Context Injection**: Executes `SELECT set_config('app.current_user_id', $1, true)` (`backend/src/modules/strategy_store.cpp:148`). The third argument `is_local => true` scopes the context variable strictly to the current transaction.
4. **Role Drop**: Issues `SET LOCAL ROLE ofc_app` (`backend/src/modules/strategy_store.cpp:156`). PostgreSQL superusers bypass Row-Level Security policies; dropping privileges to the unprivileged `ofc_app` role ensures PostgreSQL migration 04/05 RLS policies are actively enforced.
5. **Operation**: Executes the parameterized `save`, `list`, or `remove` query.
6. **Commit / Rollback**: Issues `COMMIT` (`backend/src/modules/strategy_store.cpp:168`). If the transaction scope exits without committing, the destructor issues `ROLLBACK` (`backend/src/modules/strategy_store.cpp:185-192`).

### StoreError to gRPC Status Mapping

| StoreError | gRPC Status Code | User-Facing Message | Citation |
| :--- | :--- | :--- | :--- |
| `AtCapacity` | `RESOURCE_EXHAUSTED` | `"You have reached the limit of 100 saved scenarios. Delete one to save another."` | `backend/src/modules/calculator_service.cpp:1836` |
| `Invalid` | `INVALID_ARGUMENT` | `"That scenario request was not valid."` | `backend/src/modules/calculator_service.cpp:1841` |
| `UnknownUser` | `UNAUTHENTICATED` | `"This account no longer exists. Sign in again."` | `backend/src/modules/calculator_service.cpp:1844`, detected via foreign key `saved_strategies_user_id_fkey` (`backend/src/modules/strategy_store.cpp:83`) |
| `Unavailable` | `UNAVAILABLE` | `"Saved scenarios are temporarily unavailable. Try again shortly."` | `backend/src/modules/calculator_service.cpp:1851` |
| `Internal` | `INTERNAL` | `"Saved scenarios failed unexpectedly."` | `backend/src/modules/calculator_service.cpp:1859` |

---

## State Assumptions Refresh (state_refresh.cppm, state_refresh.cpp)

### Symbol Inventory

| Symbol | Kind | Location | Description |
| :--- | :--- | :--- | :--- |
| `Abort` | `enum class` | `backend/src/modules/state_refresh.cppm:65-73` | Ingestion failure reasons: `MissingApiKey`, `NoUsableVintage`, `NoEligibleRows`, `WriteFailed`, `NetworkError`, `AlreadyRunning`, `PostgresUnavailable`. |
| `Outcome` | `struct` | `backend/src/modules/state_refresh.cppm:89-95` | Pipeline outcome: `ok`, `rows_written`, `vintage_year`, `abort_reason`, `error_message`. |
| `Refusal` | `enum class` | `backend/src/modules/state_refresh.cppm:98-101` | Record validation errors: missing values, bounds violations, negative sentinels. |
| `StateRow` | `struct` | `backend/src/modules/state_refresh.cppm:104-110` | Parsed census fields: `fips`, `name`, `median_home_value`, `median_gross_rent`, `taxes_paid`. |
| `JobStatus` | `struct` | `backend/src/modules/state_refresh.cppm:310-318` | Diagnostic state: `running`, `last_run`, `last_outcome`, `last_error`. |
| `StoredAssumption` | `struct` | `backend/src/modules/state_refresh.cppm:323-333` | Record in `state_assumptions`: state code, property tax rate, price, rent, updated timestamp. |
| `Scheduler` | `class` | `backend/src/modules/state_refresh.cppm:367-394` | Background thread scheduling periodic Census ACS checks. |
| `candidate_years` | `function` | `backend/src/modules/state_refresh.cppm:205-230` | Computes candidate Census vintage years based on calendar year. |
| `validate_acs_row` | `function` | `backend/src/modules/state_refresh.cppm:232-268` | Validates sanity bounds and computes tax rate for state records. |
| `run_refresh` | `function` | `backend/src/modules/state_refresh.cpp:217` | Full ingestion pipeline executing fetch, validate, and write steps. |
| `last_run` | `function` | `backend/src/modules/state_refresh.cpp:405` | Queries metadata timestamp from `state_assumptions_meta`. |
| `read_assumptions` | `function` | `backend/src/modules/state_refresh.cpp:436` | Reads cached state assumption record by state abbreviation code. |

### Clock Derivation of Census Vintage

Census ACS 1-year data releases experience an approximate 18-month publication lag. `candidate_years(year_now)` (`backend/src/modules/state_refresh.cppm:205-230`, `backend/src/modules/state_refresh.cpp:270`) derives candidate years dynamically using `std::chrono::year_month_day` on the system clock:
- Default candidate list: `{year_now - 2, year_now - 3, year_now - 4}` in descending order.
- The pipeline attempts each candidate year sequentially against `api.census.gov` until a valid dataset is found.
- The default candidate series can be overridden via `CENSUS_ACS_YEARS`.

### Statistical Validation Bounds

`validate_acs_row` (`backend/src/modules/state_refresh.cppm:232-268`) applies strict data validation to raw Census rows:
- Census negative sentinel values (`<= 0`) are rejected immediately.
- Effective property tax rate is derived as:
  `rate = round((taxes / price) * 10000) / 100` (`backend/src/modules/state_refresh.cppm:248`).
- Acceptance bounds:
  - `median_price`: `[50,000, 3,000,000]` USD (`backend/src/modules/state_refresh.cppm:252`).
  - `median_rent`: `[300, 8,000]` USD (`backend/src/modules/state_refresh.cppm:256`).
  - `property_tax_rate`: `[0.05, 4.0]` percent (`backend/src/modules/state_refresh.cppm:260`).
- Integrity Floor: The pipeline requires at least `kMinUsableStates = 40` valid state rows (`backend/src/modules/state_refresh.cppm:298`). If fewer than 40 states pass validation, the vintage is rejected with `Abort::NoEligibleRows`.

### Advisory Locking & Database Invariants

- Distributed Exclusion: `run_refresh` acquires a session advisory lock with key `kAdvisoryLockKey = 0x5354415445524653LL` (`"STATERFS"`, `backend/src/modules/state_refresh.cpp:45`) via `SELECT pg_try_advisory_lock(...)` (`backend/src/modules/state_refresh.cpp:251`). If the lock is held, the job terminates with `Abort::AlreadyRunning`.
- Role Drop: Mutation runs under `SET LOCAL ROLE ofc_refresh` (`backend/src/modules/state_refresh.cpp:330`).
- Zero-Write Rollback: If the update query affects 0 rows (`updated == 0`), the transaction issues `ROLLBACK` and terminates with `Abort::WriteFailed` (`backend/src/modules/state_refresh.cpp:382-389`), guarding against silent failure under misconfigured RLS rules.
- Scheduler Cadence: Checks occur every `kTickInterval = 6h` (`backend/src/modules/state_refresh.cppm:392`). If the last successful ingestion occurred more than `kStaleAfter = 8 days` ago (`backend/src/modules/state_refresh.cppm:388`), a refresh is dispatched. The scheduler thread sleeps using an interruptible `std::condition_variable_any::wait_for` bound to a `std::stop_token` (`backend/src/modules/state_refresh.cpp:583-587`).

### Environment Variables

| Variable | Default | Code Location | Effect |
| :--- | :--- | :--- | :--- |
| `CENSUS_API_KEY` | Unset / Empty | `backend/src/modules/state_refresh.cpp:220` | Authentication key for `api.census.gov`. If unset, ingestion aborts with `Abort::MissingApiKey`. |
| `CENSUS_ACS_YEARS` | Derived via clock | `backend/src/modules/state_refresh.cppm:206` | Comma-separated list of candidate years overriding clock derivation. |
| `DATABASE_URL` | Unset | `backend/src/modules/state_refresh.cpp:231` | PostgreSQL database connection string. |

---

## Shared Inference Queue & Admission (inference_admission, inference_queue, sgee_queue_client)

### Symbol Inventory

| Symbol | Kind | Location | Description |
| :--- | :--- | :--- | :--- |
| `InferenceBackend` | `class` | `backend/src/modules/inference_admission.cppm:195-234` | Unified abstraction providing `submit(prompt)`, `name()`, and `device()`. |
| `QueuedBackend` | `class` | `backend/src/modules/inference_admission.cppm:341` | In-memory queue implementation of `InferenceBackend`. |
| `NoLocalBackend` | `class` | `backend/src/modules/inference_admission.cppm:256` | Null backend immediately returning `ok = false`. |
| `PostgresAdmission` | `class` | `backend/src/modules/inference_admission.cppm:809`, `backend/src/modules/inference_admission.cpp:396` | Admission controller proxying tasks through the PostgreSQL `inference_jobs` table. |
| `SgeeAdmission` | `class` | `backend/src/modules/inference_admission.cppm:932`, `backend/src/modules/inference_admission.cpp:665` | Admission controller proxying tasks through the external SGEE Raft/HTTP service. |
| `InferenceQueue` | `class` | `backend/src/modules/inference_queue.cppm:117-230` | PostgreSQL queue manager providing leasing, heartbeats, sweeps, and notifications. |
| `SgeeQueueClient` | `class` | `backend/src/modules/sgee_queue_client.cppm:106-122` | Resilient HTTP client interacting with the SGEE queue service. |
| `Surface` | `enum class` | `backend/src/modules/inference_queue.cppm:70` | Inference task surface: `Strategy`, `Mortgage`. |
| `JobState` | `enum class` | `backend/src/modules/inference_queue.cppm:74-80` | Queue state machine: `pending`, `leased`, `completed`, `failed`. |
| `FencingToken` | `struct` | `backend/src/modules/inference_queue.cppm:65`, `backend/src/modules/sgee_queue_client.cppm:45` | Monotonic token preventing split-brain lease completions. |

### Backend Selection Architecture

Inference admission routing is configured via the `INFERENCE_QUEUE` environment variable (`backend/src/modules/assistant_service.cpp:1700`, `backend/src/modules/mortgage_assistant_service.cpp:1340`):
- `"sgee"`: Executes `configure_sgee_queue()` (`backend/src/modules/assistant_service.cpp:1705`). Instantiates `SgeeQueueClient::create_for_admission()`, installs `SgeeLeaseSource` with a 90-second lease visibility timeout, and wraps the submission pipeline in `SgeeAdmission` (90-second deadline).
- `"postgres"`: Executes `configure_inference_queue()` (`backend/src/modules/assistant_service.cpp:1754`). Requires `DATABASE_URL`, initializes a dedicated 16-connection pool, installs `PostgresLeaseSource`, and wraps submission in `PostgresAdmission` (90-second deadline).
- `"local"` / Unset / Other: Bypasses distributed admission queues. The service interacts directly with the local engine (`backend_`), leaving `admission_` and `lease_source_` null.

### Surface Partitioning & Routing

- Queue Isolation: `inference_queue::Surface` differentiates `Strategy` and `Mortgage` workloads (`backend/src/modules/inference_queue.cppm:70`). Each surface maintains an independent pending queue bound (`pending_bound_per_surface = 200`, `backend/src/modules/inference_queue.cppm:225`).
- Envelope Injection: SGEE prompts are encoded via `encode_prompt_for_surface` to inject metadata: `{"surface": "strategy" | "mortgage"}` (`backend/src/modules/inference_admission.cppm:858`, `backend/src/modules/inference_admission.cpp:178`).
- Lease Filtering & Foreign Task Backoff: Workers pass a surface filter during lease acquisition (`backend/src/modules/inference_admission.cpp:192`). When a lease arrives, `decode_surface` validates that the job matches the worker's bound surface. If a mismatched surface task is received, the worker fails the lease once and applies backoff `kForeignLeaseBackoff = 2s` (`backend/src/modules/inference_admission.cpp:108, 527`).

### Leasing, Fencing, and Completion Lifecycle

1. **Lease Acquisition**: PostgreSQL leases assign a monotonic `fencing_token` from the `inference_fence` sequence (`backend/src/modules/inference_queue.cpp:290`). In SGEE, fencing is tracked via `(local_token, raft_term, raft_index)` (`backend/src/modules/sgee_queue_client.cppm:45`).
2. **Asynchronous Completion Helper**: When a worker calls `fill()`, the local decode engine runs on the decode thread. The admission engine launches a background thread via `std::jthread` (`backend/src/modules/inference_admission.cpp:300, 564`) to wait on `future.get()` and invoke `complete()` or `fail()` asynchronously without blocking subsequent decode executions.
3. **Optimistic Fencing Check**: Completion queries require matching criteria:
   `WHERE id = $1 AND fencing_token = $2 AND state = 'leased'`. If 0 rows are affected, the lease expired and was reassigned to another node; the worker discards the output (`backend/src/modules/inference_admission.cpp:342`, `backend/src/modules/inference_queue.cpp:25-41`). In PostgreSQL, `pg_notify('inference_jobs', ...)` is broadcast within the completion transaction (`backend/src/modules/inference_queue.cpp:363-377`).

### Degrade-Never-Hang Contract

Under database disconnection, queue saturation, network partitions, or SGEE cluster leader elections, `PostgresAdmission` and `SgeeAdmission` immediately fall back to local direct decode via `local_.submit(std::move(prompt))` (`backend/src/modules/inference_admission.cpp:419, 435, 680, 708, 728, 735`). If no local model is loaded (`NoLocalBackend`), it fails fast with an explicit error (`backend/src/modules/inference_admission.cppm:258-264`) rather than hanging indefinitely.

### Environment Variables

| Variable | Default | Code Location | Effect |
| :--- | :--- | :--- | :--- |
| `INFERENCE_QUEUE` | `"local"` | `backend/src/modules/assistant_service.cpp:1700` | Selects queue backend: `"sgee"`, `"postgres"`, or `"local"`. |
| `SGEE_QUEUE_URL` | `"http://127.0.0.1:7070"` | `backend/src/modules/sgee_queue_client.cppm:78` | Base endpoint URL for the SGEE queue service. |
| `SGEE_QUEUE_DRAIN_RATIO` | `0.5` | `backend/src/modules/inference_admission.cpp:50` | Fraction of local decode capacity dedicated to servicing queue leases vs direct local jobs. |
| `DATABASE_URL` | Unset | `backend/src/modules/assistant_service.cpp:1757` | PostgreSQL database connection string for `PostgresAdmission` and `InferenceQueue`. |

---

## Market Data & Risk-Free Rates (market_data.cppm)

### Symbol Inventory

| Symbol | Kind | Location | Description |
| :--- | :--- | :--- | :--- |
| `Quote` | `struct` | `backend/src/modules/market_data.cppm:592-598` | Real-time equity quote: `symbol`, `price`, `bid`, `ask`, `timestamp`. |
| `OptionContract` | `struct` | `backend/src/modules/market_data.cppm:601-630` | Option contract data: strike, expiry, call/put, greeks, implied volatility. |
| `Chain` | `struct` | `backend/src/modules/market_data.cppm:633-642` | Option chain container with `contracts` and `fetched_at` string. |
| `RiskFreeRate` | `struct` | `backend/src/modules/market_data.cppm:645-665` | Yield curve data container with `fetched_at` string. |
| `Client` | `class` | `backend/src/modules/market_data.cppm:668-800` | Resilient market data HTTP client for Alpaca and US Treasury data. |
| `rfc3339_now` | `function` | `backend/src/modules/market_data.cppm:486-499` | Formats current system clock time as RFC3339 UTC string. |
| `fetch_quote` | `function` | `backend/src/modules/market_data.cppm:1400` | Retrieves equity quote with caching and retry. |
| `fetch_chain` | `function` | `backend/src/modules/market_data.cppm:1508-1539` | Retrieves option chain with bounded stale fallback. |
| `fetch_risk_free_rate` | `function` | `backend/src/modules/market_data.cppm:1557-1570` | Retrieves Treasury yield curve with stale fallback. |

### Cache TTL Hierarchy & Overrides

| Cache Target | Default TTL | Override Environment Variable | Location |
| :--- | :--- | :--- | :--- |
| `quote_cache` | 60 seconds | `OPTION_QUOTE_TTL_SECONDS` | `backend/src/modules/market_data.cppm:725` |
| `expiration_cache` | 12 hours | None (Fixed) | `backend/src/modules/market_data.cppm:730` |
| `chain_cache` | 900 seconds (15m) | `OPTION_CHAIN_TTL_SECONDS` | `backend/src/modules/market_data.cppm:735` |
| `rate_cache` | 6 hours | None (Fixed) | `backend/src/modules/market_data.cppm:744` |
| Stale Bound | 3,600 seconds (1h) | `OPTION_CHAIN_MAX_STALE_SECONDS` | `backend/src/modules/market_data.cppm:721` |

### Bounded Stale Serving Mechanism

When an upstream API call to Alpaca or US Treasury fails, cached data is served conditionally based on age:
- In `fetch_chain` (`backend/src/modules/market_data.cppm:1508-1539`): If `now - last_good_chain_timestamp <= max_stale_seconds`, the stale cache is returned with an audit warning. If the data age exceeds `max_stale_seconds`, the request is refused and the upstream error is propagated.
- In `fetch_risk_free_rate` (`backend/src/modules/market_data.cppm:1557-1570`): On upstream failure, `last_good_rate_` is returned if available.

### RFC3339 Timestamp Formatting

Timestamps are formatted as `YYYY-MM-DDTHH:MM:SSZ` by `rfc3339_now()` (`backend/src/modules/market_data.cppm:486-499`) and embedded as `fetched_at` on `Chain` (`backend/src/modules/market_data.cppm:636`) and `RiskFreeRate` (`backend/src/modules/market_data.cppm:660`). Using standardized ISO/RFC strings eliminates floating-point precision loss and timezone discrepancies across client applications.

### Credential Hygiene in Error Diagnostics

Alpaca authentication credentials (`APCA-API-KEY-ID`, `APCA-API-SECRET-KEY`) are transmitted exclusively in HTTP headers, never within URL query parameters. In `get_text` (`backend/src/modules/market_data.cppm:398-404`), error logs record only `host + path` (omitting query strings and request headers) and truncate response bodies to 240 bytes, preventing credential leakage in server logs.

### Environment Variables

| Variable | Default | Code Location | Effect |
| :--- | :--- | :--- | :--- |
| `ALPACA_KEY_ID` | Unset | `backend/src/modules/market_data.cppm:680` | Alpaca Markets API key ID. |
| `ALPACA_SECRET_KEY` | Unset | `backend/src/modules/market_data.cppm:681` | Alpaca Markets API secret key. |
| `OPTION_QUOTE_TTL_SECONDS` | `60` | `backend/src/modules/market_data.cppm:725` | Cache TTL in seconds for equity quotes. |
| `OPTION_CHAIN_TTL_SECONDS` | `900` | `backend/src/modules/market_data.cppm:735` | Cache TTL in seconds for option chains. |
| `OPTION_CHAIN_MAX_STALE_SECONDS` | `3600` | `backend/src/modules/market_data.cppm:721` | Maximum allowable age in seconds for serving stale option chains on failure. |

---

## FIPS Cryptographic Mode (fips_mode.cppm)

### Symbol Inventory

| Symbol | Kind | Location | Description |
| :--- | :--- | :--- | :--- |
| `Mode` | `enum class` | `backend/src/modules/fips_mode.cppm:83-87` | Mode enumeration: `Off`, `Preferred`, `Required`. |
| `parse_mode` | `function` | `backend/src/modules/fips_mode.cppm:128-143` | Parses string values (`"off"`, `"preferred"`, `"required"`, `"1"`, `"on"`, `"true"`) into `Mode`. |
| `to_string` | `function` | `backend/src/modules/fips_mode.cppm:89-98` | Converts `Mode` enum to string representation. |
| `Status` | `struct` | `backend/src/modules/fips_mode.cppm:100-112` | Reports runtime state: `provider_loaded`, `fips_active`, `security_bits`, `error_message`. |
| `apply_from_environment` | `function` | `backend/src/modules/fips_mode.cppm:145-210` | Evaluates `FIPS_MODE` and loads the OpenSSL 3.x FIPS provider. |
| `check_status` | `function` | `backend/src/modules/fips_mode.cppm:212-245` | Probes OpenSSL runtime to confirm FIPS provider availability and properties. |

### Mode Semantics & Startup Refusal

The cryptographic mode is controlled via `FIPS_MODE` (`backend/src/modules/fips_mode.cppm:145-210`):
- `Mode::Off`: Retains standard OpenSSL default providers.
- `Mode::Preferred`: Attempts to load the OpenSSL 3.x FIPS provider; if absent, logs a warning and falls back to standard providers.
- `Mode::Required`: Enforces strict FIPS cryptographic operation:
  - Invoked during process initialization in `backend/src/main.cpp:88-100` before gRPC server binding (`builder.AddListeningPort`).
  - If loading the OpenSSL `fips` provider fails or setting default properties `fips=yes` fails, the process prints a fatal error to `std::cerr` and terminates via `std::exit(1)`.
  - **Timing Rationale**: Halting execution prior to opening listening sockets guarantees that the service will never serve requests with non-compliant cryptography, and prevents orchestrator healthchecks (e.g. Railway) from routing traffic to an improperly configured node.

### Boundary of Claims & Platform Constraints

1. **FIPS-Capable vs Certified**: The codebase implements FIPS capability by loading the OpenSSL 3.x `fips` provider and setting `default_properties = "fips=yes"` (`backend/src/modules/fips_mode.cppm:7-31`). It does *not* claim independent FIPS 140-2/3 validation or compliance certification, which requires formal CMVP audit of the binary and environment.
2. **Base Operating System Limits**: Stock `ubuntu:24.04` docker images provide only `legacy.so` in `/usr/lib/x86_64-linux-gnu/ossl-modules/` and omit `fips.so`. Deploying with `FIPS_MODE=required` on an unmodified Ubuntu image will cause startup exit by design (`backend/src/modules/fips_mode.cppm:35-39`).
3. **Ingress Boundary**: Public TLS termination is handled at the Railway ingress edge; internal gRPC routing and `envoy.yaml` do not terminate container TLS (`backend/src/modules/fips_mode.cppm:48-54`).
4. **Application Cryptographic Scope**: FIPS enforcement within the container applies strictly to SHA-512 hashing, HMAC-SHA-512/256 token verification, and `RAND_bytes` key generation in `api_key.cpp` (`backend/src/modules/fips_mode.cppm:55-61`).

### Environment Variables

| Variable | Default | Code Location | Effect |
| :--- | :--- | :--- | :--- |
| `FIPS_MODE` | `Off` | `backend/src/modules/fips_mode.cppm:146` | Cryptographic mode: `"off"`, `"preferred"`/`"prefer"`, or `"required"`/`"require"`/`"1"`/`"on"`/`"true"`. |

---

## Test Coverage

| Test Source File | Target Module Under Test | Validated Behaviors & Invariants |
| :--- | :--- | :--- |
| `backend/tests/test_api_key_entitlement.cpp` | `api_key.cppm`, `api_key.cpp` | API key and Supabase JWT authentication; entitlement outcome discrimination across strategy and assistant surfaces; rejection of secret keys bearing `Origin` headers (`DeniedSecretFromBrowser`); tier validation. |
| `backend/tests/test_quota_tier_label.cpp` | `quota.cppm`, `quota.cpp` | Dynamic fallback for undefined tiers to anonymous limits; tag formatting (`"(undefined in QUOTA_POLICY; anonymous limits)"`); per-key tier overrides (`"(per-key)"`); hourly compute allowance exhaustion and burst limits. |
| `backend/tests/test_strategy_store_pg.cpp` | `strategy_store.cppm`, `strategy_store.cpp` | PostgreSQL CRUD operations; RLS session isolation (`SET LOCAL ROLE ofc_app`); `app.current_user_id` scoping; user capacity limit enforcement (`kMaxPerUser = 100`); name byte limits (`kMaxNameBytes = 120`). |
| `backend/tests/test_calculator_service.cpp` | `strategy_store.cppm`, `api_key.cppm` | Service-level mapping of `StoreError` to gRPC status codes (`RESOURCE_EXHAUSTED`, `INVALID_ARGUMENT`, `UNAUTHENTICATED`, `UNAVAILABLE`, `INTERNAL`); multi-leg strategy entitlement enforcement under `PRO_GATE_MODE`. |
| `backend/tests/test_state_refresh.cpp` | `state_refresh.cppm`, `state_refresh.cpp` | ACS vintage derivation (`candidate_years`); statistical sanity checks (`validate_acs_row`); property tax rate calculation rounding; `kMinUsableStates = 40` floor rejection. |
| `backend/tests/test_state_assumptions_gate.cpp` | `state_refresh.cppm`, `finance_service.cpp` | Partner tier gate on `RefreshStateAssumptions` RPC; verifies unauthenticated and free/pro callers are rejected with `PERMISSION_DENIED`. |
| `backend/tests/test_inference_admission.cpp` | `inference_admission.cppm`, `inference_admission.cpp` | In-memory `QueuedBackend` admission; local concurrency throttling; pending queue bounds; execution device resolution (`resolve_device`). |
| `backend/tests/test_inference_admission_pg.cpp` | `inference_admission.cppm`, `inference_admission.cpp` | `PostgresAdmission` and `PostgresLeaseSource`; multi-replica task handoff; fencing token incrementation; degrade-never-hang fallback to local inference on database faults. |
| `backend/tests/test_inference_queue_pg.cpp` | `inference_queue.cppm`, `inference_queue.cpp` | PostgreSQL job lifecycle (`pending` -> `leased` -> `completed`/`failed`); transaction advisory locking; worker heartbeats; expired lease sweeping; `LISTEN/NOTIFY` dispatch. |
| `backend/tests/test_sgee_queue_client.cpp` | `sgee_queue_client.cppm`, `sgee_queue_client.cpp` | SGEE queue client protocol; surface metadata tagging; mirror queueing; exponential backoff; circuit breaker tripping; Raft leader redirect handling (HTTP 307). |
| `backend/tests/test_market_data_resilience.cpp` | `market_data.cppm` | Upstream retry with backoff; circuit breaker activation on repeated failures; HTTP 4xx (non-retryable) vs 5xx (retryable) error classification; header credential isolation in logging. |
| `backend/tests/test_option_chain_cache.cpp` | `market_data.cppm` | Quote, option chain, and Treasury rate cache TTL expiration; bounded stale cache serving within `OPTION_CHAIN_MAX_STALE_SECONDS`; RFC3339 timestamp generation. |
| `backend/src/main.cpp:88-100` | `fips_mode.cppm` | Startup verification exercising `fips::apply_from_environment()`, ensuring immediate process exit (`std::exit(1)`) prior to port binding when FIPS is required but unavailable. |

---

## Open Questions

1. **API Key Registry Plaintext vs Hashed Representation Alignment**:
   In `backend/src/modules/api_key.cpp:586`, `FINANCE_API_KEYS` parses a JSON map whose keys are 128-character SHA-512 hex digests. In `backend/src/modules/quota.cpp:212`, `QUOTA_API_KEYS` parses a JSON map whose keys are plaintext API keys. When configuring per-key quota overrides in production environments, administrators must maintain both the SHA-512 digest in `FINANCE_API_KEYS` and the plaintext key in `QUOTA_API_KEYS`. A unified configuration schema would prevent configuration divergence.
2. **SGEE Client Retry Behavior During Prolonged Raft Leader Elections**:
   In `backend/src/modules/sgee_queue_client.cpp`, leader redirection handles HTTP 307 redirects. However, during active Raft elections where no leader is reachable, client requests trip the circuit breaker and degrade immediately to local inference rather than buffering in-memory. While this satisfies the "degrade-never-hang" contract, it causes temporary node-local inference spikes during distributed queue failovers.
3. **Census ACS Variable Code Evolution**:
   In `backend/src/modules/state_refresh.cppm:205-230`, candidate years are derived dynamically based on calendar year. If the US Census Bureau alters variable identifiers (e.g. `B25077_001E` for home values) in future 1-year releases, `validate_acs_row` will reject the rows, causing the candidate vintage to abort under the `kMinUsableStates` threshold until variable mappings are updated.
