# Options & Futures Calculator — OPC Parity, Contract Unification & Live Market Data

**Date:** 2026-07-26
**Author:** Olumuyiwa Oluwasanmi (design by Claude)
**Status:** Approved for planning

---

## 1. Problem

The deployed product is broken in four independent, compounding ways. The UI is not
"partly connected" to the backend — it has never been connected at all.

| # | Failure | Evidence |
|---|---|---|
| 1 | Railway build has never succeeded | `Build Failed: "/start.sh": not found`; API host returns 404 "Application not found" |
| 2 | Frontend and backend speak incompatible protos | Frontend calls `/calculator.OptionsCalculator/*`; backend serves `/options_calculator.CalculatorEngineService/*` |
| 3 | `calculator_service.cpp` cannot compile | Declares `GetMarketQuote`/`GetMarketChain` as `override` with no matching base virtual |
| 4 | `.railwayignore` excludes a required directory | Excludes `backend/external`, which `backend/CMakeLists.txt:76` needs for SGEE |

Beyond the outage, the data layer is fabricated. `calculator_service.cpp` synthesises
strikes from `spot`, invents `call_delta = 0.5 - (strike-spot)/spot*3.0`, and hardcodes
`volume=1200, open_interest=3400, iv=0.22`. `market_data.cppm:88` hardcodes
`impliedVolatility = 0.20`. Most seriously, quote-fetch failure is swallowed and returns
`price = 100.0` as success — so even a working deploy would have displayed fake prices
with no error surfaced.

## 2. Scope

Full parity with optionsprofitcalculator.com, plus this engine's differentiators:

- **OPC core** — ~12 named strategies, strike/expiry-driven leg builder, the signature
  colored P&L table (price rows × date columns), breakevens, max profit/loss, editable
  IV/entry/quantity
- **Engine extras** — aggregate and per-leg Greeks (Δ Γ Θ V ρ + vanna/volga/charm), POP,
  P(50% max profit), P(touch), expected value, VaR 95/99, CVaR, probability distribution overlay
- **Futures** — outright long/short, calendar spread, crack spread, cash & carry,
  covered futures call (FOP), term structure (basis, cost of carry, contango/backwardation)
- **3D WebGL P&L surface** — `react-three-fiber` is already a dependency

**Accepted limitation:** no vendor offers a real futures *option* chain here. Futures
term structure and FOP legs remain modelled, not live. This was explicitly accepted.

## 3. Market Data — Alpaca

### 3.1 Provider selection (empirically determined)

All candidates were probed live with the account's own credentials on 2026-07-26:

| Provider | Result |
|---|---|
| Polygon | **403** `NOT_AUTHORIZED` — no options entitlement |
| Finnhub | **403** — no access to `/stock/option-chain` |
| FMP | **404 / `[]`** — no entitlement |
| Yahoo Finance | **429 Too Many Requests** on every path, including the cookie+crumb auth flow |
| AlphaVantage | 200 — full chain with Greeks (viable alternate) |
| **Alpaca** | **200 — selected** |

Yahoo, the original premise, is demoted out of the critical path: it 429s from this
network (and datacenter IPs such as Railway's are more likely blocked, not less), and it
returns neither implied volatility nor Greeks.

### 3.2 Verified Alpaca surface

Two hosts, joined on the OCC symbol (e.g. `SPY260918C00300000`). **Key/host pairing is
strict** — live keys authenticate only against `api.alpaca.markets`, paper keys only
against `paper-api.alpaca.markets`; cross-pairing returns `401 code 40110000`. This was
initially misdiagnosed as a missing entitlement.

**Chain quotes, Greeks, IV** — `data.alpaca.markets`:
```
GET /v1beta1/options/snapshots/{underlying}
    ?feed=opra&expiration_date=YYYY-MM-DD
    &strike_price_gte=&strike_price_lte=&limit=200
```
Per contract: `greeks{delta,gamma,theta,vega,rho}`, `impliedVolatility`,
`latestQuote{ap,as,bp,bs}`, `latestTrade`, `dailyBar{v}`, `minuteBar`, `prevDailyBar`.

**Open interest and contract metadata** — `api.alpaca.markets`:
```
GET /v2/options/contracts?underlying_symbols=&expiration_date_gte=&expiration_date_lte=
```
Returns `open_interest`, `open_interest_date`, `size` (contract multiplier, e.g. 100),
`style` (american), `expiration_date`, `strike_price`, `type`.

**Underlying spot** — `GET data.alpaca.markets/v2/stocks/{symbol}/snapshot`
(verified SPY = 738.93; the frontend's hardcoded `TICKER_DATABASE` claims 580.0).

Together these supply every column the OPC chain grid needs — strike, bid/ask, sizes, IV,
delta, volume, open interest — with Γ Θ V ρ available at no extra cost.

### 3.3 Design consequences

- **Server-side filtering is mandatory.** Unfiltered SPY is ~14,000 contracts at 100/page.
  The `expiration_date` + `strike_price_gte/lte` parameters keep responses to a single
  unpaginated page. A full chain must never reach the browser.
- **Absent fields are normal.** Illiquid strikes omit `greeks` entirely and carry stale
  `latestTrade` values (one sample: trade from 07-22 with `dailyBar.v = 2`). These map to
  `std::optional` (policy rule 9), not to defaulted zeros.
- **Cache key is `(symbol, expiration)`**, not per-contract, because the two-host join is
  performed per expiration.
- **Credentials come from Railway environment variables** (`ALPACA_API_KEY`,
  `ALPACA_API_SECRET`, `ALPACA_DATA_URL`, `ALPACA_TRADING_URL`). Never in source, never
  baked into the image. `config/config.yaml` is gitignored and stays out of the container.

### 3.4 Real data only — no fabrication (binding requirement)

**Every number displayed to a user must originate from a market data provider or from the
pricing engine operating on provider inputs. No synthetic constants, no placeholder
defaults, no invented series.** Where data is genuinely unavailable, the UI shows an
explicit unavailable/degraded state — never a plausible-looking substitute.

This is a hard acceptance criterion, not an aspiration. A feature is not complete if it
renders a fabricated value.

Complete inventory of current fabrication sites, all of which this work removes:

| Location | Fabrication | Replacement |
|---|---|---|
| `calculator_service.cpp:377` | `double spot = 100.0` — the entire generated chain is built around a fake spot | Alpaca stock snapshot |
| `calculator_service.cpp:387` | Strikes synthesised by `step = spot>=1000 ? 50 : spot>=100 ? 5 : 1` | Real strikes from Alpaca contracts |
| `calculator_service.cpp:396` | `call_delta = 0.5 - (strike-spot)/spot*3.0` — an invented formula, not a Greek | Alpaca `greeks.delta` |
| `calculator_service.cpp:397-399` | `call_volume=1200`, `call_open_interest=3400`, `call_iv=0.22` | Alpaca `dailyBar.v`, `open_interest`, `impliedVolatility` |
| `calculator_service.cpp:403-406` | `put_delta = call_delta - 1.0`, `put_volume=1100`, `put_open_interest=2900`, `put_iv=0.22` | Alpaca per-contract put values |
| `calculator_service.cpp:412-421` | Nine hardcoded futures months (`U26`…`F29`) with fixed day counts | Computed contract calendar |
| `calculator_service.cpp:428` | `f_price = spot * exp(0.045 * dte/365)` — a fixed 4.5% carry | Cost-of-carry from live spot + risk-free rate, labelled modelled |
| `market_data.cppm:145` | `quote.impliedVolatility = 0.20` | Per-contract IV from Alpaca |
| `calculator_service.cpp:351-357` | Quote failure returns `price = 100.0` as success | Typed `std::error_code`, propagated as gRPC status |
| `useCalculatorStore.ts:62-99` | `TICKER_DATABASE` — 31 static prices (SPY 580.0 vs actual 738.93) | Live quotes |
| `useCalculatorStore.ts:172-174` | `setUnderlyingSymbol('SPY')`, `setImpliedVolatility(0.20)` hardcoded | Selected symbol, per-leg chain IV |
| `useCalculatorStore.ts:182` | `setExpirationDays(30)` hardcoded | Selected expiration |
| `useCalculatorStore.ts:193,195` | `days_to_expiration: 30 // Mocked`, `probability_density: 0.5` | Real `MatrixCell` fields |
| `OptionChain.tsx:41-55` | `REAL_EXPIRATION_CHAINS` — 11 hardcoded dates, of which **`2027-06-18` and `2029-01-19` do not exist** | `ChainResponse.available_expirations` from Alpaca (35 real SPY expirations) |

**Modelled values are permitted only where no market data exists** — specifically the
futures term structure and FOP legs (§2). These must be visibly labelled as modelled in
the UI so a user cannot mistake them for quoted markets.

**Enforcement.** `scripts/code_policy_check.sh` gains a fabrication check that fails the
build on reintroduced synthetic market constants (hardcoded IV/volume/open-interest
literals in service code, and any quote-failure path returning a defaulted price). A test
asserts that a provider error yields an error status rather than a substituted value.

## 4. Architecture

```
Browser (Cloudflare Pages, static Next.js export)
   │  gRPC-Web
   ▼
Envoy :8080  (Railway container — grpc_web, cors, local_ratelimit filters)
   │  HTTP/2 gRPC
   ▼
calculator_engine :50051  (C++23)
   ├── market_data.cppm    → Alpaca (snapshots + contracts + stock snapshot)
   ├── pricing_engine.cppm → sensen SIMD pricing, TBB matrix
   └── calculator_service.cppm → SGEE graph pipeline
   │
   ▼
Railway PostgreSQL / Supabase (saved strategies, auth, RLS)
```

### 4.1 Contract unification

`backend/proto/calculator.proto` becomes the single source of truth. The root
`proto/calculator.proto` is **deleted**.

This is not arbitrary. The root proto's `PnLPoint {underlying_price, pnl}` has no date
axis and therefore *structurally cannot express* the OPC price×date grid. The backend
proto's `MatrixCell {price, days_to_expiration, date_str, pnl_dollars,
return_on_risk_percent, probability_density}` can, and it already defines
`ProbabilityBreakdown`, `GreekBreakdown` (with vanna/volga/charm) and a `leg_greeks` map —
exactly the selected feature set.

Additions to the canonical proto:

- `rpc GetMarketQuote (QuoteRequest) returns (QuoteResponse)`
- `rpc GetMarketChain (ChainRequest) returns (ChainResponse)`

These give the two orphaned `override` declarations in `calculator_service.cpp:344` and
`:370` real base virtuals, resolving failure #3.

- `QuoteRequest/QuoteResponse`, `ChainRequest/ChainResponse`, `ExpirationDate`,
  `FuturesContract` merged in from the root proto.
- `OptionStrike` extended to carry Alpaca's real payload per side:
  `bid, ask, bid_size, ask_size, last, volume, open_interest, iv, delta, gamma, theta,
  vega, rho`, plus `contract_multiplier`, `is_atm`, and a `has_greeks` presence flag.

`scripts/gen_proto.sh` generates both targets from the one file: C++ at build time via the
existing CMake `custom_command`, and grpc-web TypeScript into `frontend/src/grpc/`. The
generated TS is committed, because Cloudflare Pages has no `protoc` at build time. Drift
between the two sides becomes structurally impossible.

### 4.2 Market data module

`backend/src/modules/market_data.cppm` is rewritten around a `MarketDataProvider`
**concept** rather than a virtual base — compile-time dispatch, no vtable, no owning
pointers (rules 3, 19).

- `AlpacaProvider` implements: `fetch_quote`, `fetch_chain`, `fetch_expirations`.
- Every operation returns `std::expected<T, std::error_code>` (rule 32, ROP). The existing
  `MarketDataError` category is retained and extended.
- **The `price = 100.0` silent fallback is deleted.** Failures propagate as a gRPC status
  and the UI renders an explicit degraded state. Silent failure is precisely what the
  adversarial review gate exists to catch.
- Cache guarded by `std::shared_mutex` (rule 14, thread-safety), keyed `(symbol, expiration)`.
  TTL: 15 s for chains and quotes during market hours, 15 min outside them (open interest
  updates only once daily, per `open_interest_date`, so it is cached for 12 h regardless).
- `thread_local httplib::Client` per host, keep-alive, as today.
- JSON via `fastjson` — confirmed to be `fastestjsoninthewest`, vendored at
  `backend/sensen/external/fastestjsoninthewest`, satisfying rule 37.

### 4.3 Engine

`pricing_engine.cppm` is currently linked only into `test_runner`, not into
`calculator_engine` (`backend/CMakeLists.txt:78` vs `:112`). It gets linked into the
service binary.

- **Per-leg IV** sourced from the chain, replacing the single global `implied_volatility`.
  This matters mathematically, not just cosmetically: OPC prices each leg at its own IV,
  and a single global figure systematically misprices skewed strategies such as iron
  condors, where short wings sit at materially higher IV than the body.
- Matrix computed over price × date via `tbb::parallel_for` (rule 18) with the SIMD
  waterfall AVX-512 → AVX2 → SSE4.2 → scalar under dynamic dispatch (rule 29).
- Aggregate and per-leg Greeks including vanna, volga, charm.
- Probability: POP, P(target profit), P(touch), expected value, VaR 95/99, CVaR.
- Futures term structure computed from cost-of-carry; modelled, clearly labelled as such
  in the UI.

### 4.4 Frontend

- `TICKER_DATABASE`'s stale static prices removed in favour of live quotes.
- Chain-driven leg builder: click a strike in the chain to add a leg, rather than typing
  numbers blind.
- The signature OPC matrix — price rows × date columns, green→red heat, $/% toggle.
- Strategy presets for the ~12 named options strategies and the futures strategies.
- Greeks panel, probability panel, futures term-structure table, 3D WebGL surface.
- `frontend/AGENTS.md` mandates reading `node_modules/next/dist/docs/` before writing Next
  code — this Next 16 differs from training data. That is binding on implementation.

### 4.5 Deployment

- `railway.json` sets `dockerfilePath: backend/Dockerfile`, making the build context the
  repo root, while `backend/Dockerfile` is written for a `backend/` context. `COPY start.sh`
  and `COPY envoy.yaml` are corrected to `COPY backend/…`, and the CMake source dir to
  `/app/backend`.
- `.railwayignore` drops the `backend/external` exclusion.

## 5. Code Policy Compliance

The authoritative policy is `config/cpp_details.txt` (60 rules). Enforcement level for this
repo, as decided:

### 5.1 Enforced strictly (rules 1–49)

Applied to all code written under this spec: C++23; no raw pointers, `new`, or `delete`;
RAII; smart pointers where pointers are needed; `std::optional` for absent values;
mandatory trailing return types (`auto f() -> T`, rules 31/46); `[[nodiscard]]` and
attribute-based hints; namespaces; thread-safety with no data races; ranges and range-based
algorithms; STL and parallel algorithms; TBB for parallelism including lock-free structures;
concepts and constraints; structured bindings; move semantics and perfect forwarding;
`string_view`, `span`, `array`, `variant`; `std::filesystem`; `std::chrono`; SIMD
multi-register with dynamic dispatch and correct alignment; `std::expected`/`std::unexpected`
ROP; `nullptr` never `NULL`; coroutines for async; `std::size_t` / sized integer types;
no C-style arrays; `fastestjsoninthewest` for JSON; CMake + Ninja out-of-source builds;
**internal test framework only — rule 39 forbids Google Test and Catch2**.

Note on an internal contradiction in the policy document: rule 33 suggests "Google Test or
Catch2" while rule 39 forbids external testing libraries outright. Rule 39 is later and
more specific, and the repo already ships `testing_framework.cppm`. **Rule 39 governs.**

### 5.2 Build-side changes adopted now

- Adopt the canonical flags from `backend/external/SGEE/scripts/build_common.sh` (the
  canonical script the policy references — it exists, vendored inside SGEE).
- Add `-stdlib=libc++` to `CANONICAL_FLAGS`.
- Remove the seven `-Wno-error=` suppressions in `backend/CMakeLists.txt:17`, which are not
  sanctioned by the policy and undercut rule 34's zero-warnings requirement.
- Add `.clang-tidy` and `.clang-format` (neither exists today).
- Retain the already-compliant `-march=x86-64-v3 -mtune=generic` and the absence of
  `-ffast-math` (rules 50, 55).

### 5.3 Fixing the policy gate — it is currently a no-op

`scripts/code_policy_check.sh` audits `${REPO_ROOT}/src`. **That directory does not exist**;
the source lives in `backend/src`. Its raw-pointer and trailing-return-type checks have
therefore been passing against an empty set. Simultaneously, its `-ffast-math` check greps
documentation prose and fails on SGEE files that state "NO `-ffast-math`" — a false positive
and a false negative at the same time.

Remediation:
- Repoint the audit at `backend/src`.
- Exclude vendored trees (`backend/external`, `backend/sensen`) and `.md` prose from flag greps.
- Extend coverage to every mechanically-checkable rule: raw `new`/`delete`, trailing return
  types, `NULL`/`0` pointer literals, C-style arrays, forbidden test frameworks, `-ffast-math`,
  `-march=native`, hardcoded secrets, canonical-flag divergence.
- Rewrite `config/agents/code_policy_agent.yaml` to enumerate the full rule set; it currently
  encodes 10 of 60 and omits rules 5, 19, 22–25, 36, 37, 38, 39, 42, 44, 48, 56.

### 5.4 Deviation register (rules 50–55, deferred not forgotten)

These rules reference infrastructure absent from this repository. Each is recorded with its
gap so the deferral is explicit rather than silent:

| Rule | Mandate | Current state |
|---|---|---|
| 12, 41 | `import std;` / precompiled std module | **SATISFIED 2026-08-12.** `SENSEN_NO_IMPORT_STD` and `SGEE_NO_IMPORT_STD` are both OFF; every `.cppm` in `backend/src`, sensen and SGEE says `import std;` against one `std.pcm` built from `CANONICAL_FLAGS`. See `CLAUDE.md` §"`import std;` and the one std.pcm" |
| 50 | `clang++-22` only, `-nostdinc++ -isystem external/libcxx-v1/include` | Dockerfile installs whatever clang `llvm.sh` provides; flags absent |
| 51 | Vendored `modules/std.cppm` + `external/libcxx-v1/include` | Both missing from this repo |
| 50–54 | Build scripts source `scripts/build_common.sh` | Absent at repo root (exists only inside SGEE) |
| 53 | TBB sweeps pinned via `taskset -c 0-15` | Not applicable — this is a request-serving container, not a sweep host |
| 55 | Bit-identical cross-host FP parity verification | No second production host for this project |

Rule 18 (parallelism exclusively oneTBB) is **not** deferred. TBB is currently not linked
into `calculator_engine` at all, and `_GLIBCXX_USE_TBB_PAR_BACKEND=0` is defined; §4.3
closes that gap by linking TBB and using `tbb::parallel_for` for the matrix.

#### 5.4.1 Rule 3 (no raw pointers) — third-party API surface

Rule 3 is absolute and rule 4 permits pointers only where necessary, via smart pointers.
Three call sites cannot comply, and in each the compliant-looking alternative would be a
memory-safety bug rather than an improvement:

| Site | Why a smart pointer is impossible | Containment |
|---|---|---|
| `CalculateStrategy` / `GetMarketQuote` / `GetMarketChain` parameters | Signatures are fixed by the protoc-generated base class; changing a parameter type means the function no longer overrides the virtual. The pointers are non-owning views onto memory gRPC owns and reuses across calls, so wrapping them would double free. | Null-checked and bound to a reference on the first line of each body; no raw pointer appears below that. |
| `response.add_*()` / `mutable_*()` | Protobuf returns non-owning pointers into the message's own arena. A `unique_ptr` would free arena memory. | Bound immediately: `auto& cell = *response.add_matrix();` |
| `std::from_chars`, `std::error_category::name()`, `std::getenv` | Standard library signatures. | `getenv`'s result is copied into a `std::string` at the call site and never stored. |

Everything the project owns complies: no `new`, no `delete`, no owning raw pointer, and no
raw pointer in any interface we define. `RegisterCalculatorService` previously took a
`void*` and cast it back to `grpc::ServerBuilder*` — a genuine violation, since it was our
own signature — and now takes a reference.

#### 5.4.2 Toolchain deviations found during implementation

| Deviation | Reason |
|---|---|
| `gRPC_SSL_PROVIDER=package` (was `module`) | Vendored BoringSSL and system OpenSSL both export `SSL_CTX_new`; httplib's calls resolved into BoringSSL and segfaulted on every outbound HTTPS request. One TLS library per binary. |
| Generated `openssl/engine.h` shim + `OPENSSL_NO_ENGINE` | OpenSSL 3.5 removed the ENGINE API. gRPC includes the header unconditionally although every use is already guarded by that macro. Engages only on hosts without the header. |
| `CMAKE_POLICY_VERSION_MINIMUM=3.5` | CMake 4 removed compatibility with `cmake_minimum_required(VERSION < 3.5)`; gRPC 1.62 vendors a c-ares that still declares 3.0. |
| CMake 4.2 from Kitware in the builder image | `sensen/CMakeLists.txt` requires 4.1+; Ubuntu 24.04 ships 3.28.3. |

#### 5.4.3 SGEE usage constraints

Two properties of the vendored engine shape how the pipeline may be written. Both were
found by instrumenting a graph that silently computed nothing:

- `Builder::Execute(F&&)` **discards the callable**, registering only a generated name.
  Actions run only when an `ActionRegistry` is passed to the `Interpreter`. Binding must
  use `GraphBlueprint::GetActionId(name)`, because `ActionRegistry::Register` hashes the
  name (`hash % 10000`) while the builder assigns sequential IDs — registering by name
  compiles, runs, and never executes.
- The batch interpreter does not evaluate predicates on deterministic `Branch` nodes; it
  takes `branches[0]` unconditionally. `OnTrue`/`OnFalse` is therefore not a usable
  conditional, and validation lives inside the actions instead.

### 5.5 Review gate

Per `config/agents/code_review_agent.yaml` and `code_update_agent.yaml`: adversarial
red-team review with tri-agent consensus (≥2 of 3 from Claude / AGY / Cursor) before merge,
via `scripts/code_review_adversarial.sh`; zero hardcoded secrets; Supabase RLS validation;
gRPC contract alignment. GitHub operations via `gh` CLI.

## 6. Testing

| Layer | Approach |
|---|---|
| C++ unit | Internal `testing_framework.cppm` (rule 39). Pricing, matrix, Greeks, probability metrics. |
| Provider parsing | Against recorded Alpaca JSON fixtures captured 2026-07-26, including the illiquid-strike case with absent `greeks` and a stale `latestTrade`. |
| Error paths | 401 key/host mismatch, 429, malformed JSON, missing fields — each asserted to produce a typed `std::error_code`, **never a defaulted price**. |
| Memory / UB | AddressSanitizer, UBSan, LeakSanitizer (rules 36, 56). |
| Frontend unit | Vitest — no frontend test infrastructure exists today; it must be added. |
| End-to-end | Real browser gRPC-web call through Envoy to the engine, asserting `grpc-status: 0`. This exact hop is what has been broken, so it is the gate that matters. |

## 7. Sequencing and gates

Ordered so that the highest-risk unknown is resolved first, not last. Each numbered phase is
a self-contained unit of work with its own gate, and is intended to be planned and
implemented separately rather than as one monolithic plan.

1. **Backend restored.** Docker context, `.railwayignore`, proto unification, compile fix.
   **Gate: a real `grpc-status: 0` from a browser.** No UI work begins until this passes.
2. **Live Alpaca data.** Provider rewrite, silent-fallback removal, caching.
   Gate: real strikes, real IV, real open interest served over the wire.
3. **Engine wiring.** `pricing_engine` linked in, per-leg IV, TBB matrix, full Greeks and
   probability metrics.
4. **UI revamp.** Full parity bar including futures and the 3D surface.
5. **Policy and review.** Gate repaired, agent config completed, tri-agent review, deploy.

## 8. Risks

- **Docker build size/time.** `backend/Dockerfile:19` runs `./llvm.sh all` (every LLVM
  package) and CMake builds gRPC v1.62.0 from source via FetchContent, alongside C++23
  modules across sensen and SGEE. The project owner assesses this as acceptable — the
  dependency set is TBB, CMake, Ninja, sensen, fastjson and SGEE, none of which are large.
  Recorded as the owner's decision; validated empirically at first deploy rather than
  pre-optimised. If the build does exceed Railway's ceiling, mitigation is to pin
  `llvm.sh` to a single LLVM version and consider a prebuilt base image.
- **`-stdlib=libc++` and BMI compatibility.** Adding it may surface
  `module-file-config-mismatch` errors against the vendored sensen/SGEE module tree, since
  BMI validation requires exactly matching flags (rule 50). If this occurs it will be
  reported, not silently reverted.
- **Alpaca data staleness on weekends.** Probes on Sunday 2026-07-26 returned Friday
  07-24 closing data. Expected and correct; the UI must label quote freshness rather than
  imply live streaming.
- **No futures option chain.** Accepted. Futures term structure stays modelled and is
  labelled as such.

## 9. Out of scope

- Real futures option chains (no vendor provides them here).
- Migration to C++26 (rule 60 — forward-compatibility maintained, migration not attempted).
- Vendoring libc++ and converting the codebase to `import std;` (see §5.4).
- Streaming `StreamLiveMatrix`; the RPC stays declared but unimplemented this cycle.
